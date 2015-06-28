/* This code is taken from mprotect man page */
#define _GNU_SOURCE

#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>


//#define GEN_WRITE_FAULT

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

char *buffer;

static void
handler(int sig, siginfo_t *si, ucontext_t *ctxt)
{
    printf("Got SIGSEGV at address: 0x%lx\n",
            (long) si->si_addr);

    if (ctxt->uc_mcontext.gregs[REG_ERR] & 0x2) {
      printf("Write fault\n");
    } else {
      printf("Read Fault\n");
    }

    exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    char *p;
    int pagesize;
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1)
        handle_error("sigaction");

    pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1)
        handle_error("sysconf");

    /* Allocate a buffer aligned on a page boundary;
       initial protection is PROT_READ | PROT_WRITE */

    buffer = memalign(pagesize, 4 * pagesize);
    if (buffer == NULL)
        handle_error("memalign");

    printf("Start of region:        0x%lx\n", (long) buffer);

    if (mprotect(buffer, pagesize,
                PROT_NONE) == -1)
        handle_error("mprotect");

#ifdef GEN_WRITE_FAULT
    *buffer = 'a';
#else
    printf("%s\n", (char*)buffer);
#endif

    printf("Loop completed\n");     /* Should never happen */
    exit(EXIT_SUCCESS);
}

