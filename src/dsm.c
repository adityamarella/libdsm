#define _GNU_SOURCE

#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <inttypes.h>

#include "utils.h"
#include "dsm.h"
#include "dsm_internal.h"

#define handle_error(msg) \
  do { print_err(msg); return(NULL); } while (0)

#define get_page_offset(ptr, base_ptr) \
  (ptr - base_ptr)/PAGESIZE

static struct sigaction sa;
int PAGESIZE = 4096;
dsm *g_dsm;

/**
 * The SIGTERM signal handler. Simply sets the 'terminated' variable to 1 to
 * inform the serve loop it should exit.
 *
 * @param sig the signal received
 */
static 
void sigterm_handler(int sig) {
  UNUSED(sig);
  log("Got sigterm. Terminating server.\n");
  g_dsm->s.terminated = 1;
}

/**
 * Utility function which returns the chunk to which the addr belongs
 *
 * @param saddr addr from the signal
 */
static 
dhandle get_chunk_id_for_addr(char *saddr) {
  dhandle i;
  char *base_ptr;
  size_t chunk_size;

  for (i=0; i<NUM_CHUNKS; i++) {
    dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[i];
    base_ptr = chunk_meta->g_base_ptr;
    chunk_size = chunk_meta->g_chunk_size;
    if (saddr >= base_ptr && saddr < base_ptr + chunk_size)
      return i; 
  }
  return NUM_CHUNKS;
}

static 
void *dsm_daemon_start(void *ptr) {
  dsm *d = (dsm*)ptr;
  log("Starting server on port %d\n", d->port);
  dsm_server *s = &d->s;
  dsm_server_init(s, "localhost", d->port);
  dsm_server_start(s);
  return 0;
}

/**
 * The main signal handler for this application. Handles SIGSEGV on registered addresses.
 * Everything in this function should be re-entrant and asynchronous. Curiously, 
 * even printfs are not allowed; so do not add logs in this function. 
 * 
 * TODO remove log statements from this function
 *
 * TODO remove synchronous getpage calls
 *
 * Refer to the signal man page to get the list of functions allowed in this function. 
 *
 * @param sig refer to the signal man page
 * @param si refer to the signal man page
 * @param *unused refer to the signal man page
 */
static 
void dsm_sigsegv_handler(int sig, siginfo_t *si, void *ctxt) {
  UNUSED(sig);
  UNUSED(si);
  UNUSED(ctxt);
  int write_fault = 0;
  uint32_t flags = 0;

  // get the chunk meta for this addr
  dhandle chunk_id; 
  if ((chunk_id = get_chunk_id_for_addr((char*)si->si_addr)) == NUM_CHUNKS) {
    log("Wrong chunk id for addr: 0x%lx chunk_id: %"PRIu64"\n",
        (long) si->si_addr, chunk_id);
    return;
  }

  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  char *base_ptr = chunk_meta->g_base_ptr;
  size_t chunk_size = chunk_meta->g_chunk_size;

  // safety check; probably not necessary
  // TODO remove this later
  if ((char*)si->si_addr < base_ptr || 
      (char*)si->si_addr >= base_ptr + chunk_size) {
    log("Fault out of chunk. address: 0x%lx chunk_id: %"PRIu64" base_ptr: %p base_end: %p\n",
        (long) si->si_addr, chunk_id, base_ptr, base_ptr + chunk_size);
    return;
  }

  // this works on x86_64 GNU/Linux 
  if (((ucontext_t*)ctxt)->uc_mcontext.gregs[REG_ERR] & 0x2) {
    log("write fault..................\n");
    write_fault = 1;
  } else {
    log("read fault..................\n");
    write_fault = 0;
  }

  // Build page offset
  dhandle page_offset = (dhandle)get_page_offset((char *)(si->si_addr), base_ptr);
  char *page_start_addr = base_ptr + PAGESIZE*page_offset;

  // Use a state transition table for this later?
  if (chunk_meta->pages[page_offset].page_prot == PROT_NONE) {
    if (write_fault) {
      flags |= FLAG_PAGE_WRITE;
      chunk_meta->pages[page_offset].page_prot = PROT_WRITE;
    } else {
      flags |= FLAG_PAGE_READ;
      chunk_meta->pages[page_offset].page_prot = PROT_READ;
    }
  } else if (chunk_meta->pages[page_offset].page_prot == PROT_READ) {
    flags |= FLAG_PAGE_WRITE;
    chunk_meta->pages[page_offset].page_prot = PROT_WRITE;
  }
 
  // Request page from master
  dsm_request *r = g_dsm->master;
  if (dsm_request_getpage(r, chunk_id, page_offset, 
        g_dsm->host, g_dsm->port, &g_dsm->page_buffer, flags) < 0) {
    //TODO: we have not yet decided on what to do if page is not found;
  }
  
  // temporarily set the protection to READ/WRITE to update the page
  if (mprotect(page_start_addr, PAGESIZE, PROT_READ | PROT_WRITE) == -1)
    print_err("mprotect\n");
  
  // copy the page
  if (!(flags & FLAG_PAGE_NOUPDATE)) {
    memcpy(page_start_addr, g_dsm->page_buffer, PAGESIZE);
    log("writing page\n");
  }

  // reset protection back to read if it is just read fault
  if (!write_fault) {
    debug("read fault\n");
    if (mprotect(page_start_addr, PAGESIZE, PROT_READ) == -1)
      print_err("mprotect\n");
  }
}


/**
 * Alloc function allocates a new shared memory chunk and return the pointer to that chunk.
 *
 * @param d is the dsm object structure; to maintain context
 * @param chunk_id is maintained by the user of libdsm;
 *        if multiple chunks are used then the user 
 *        has to ensure that chunk id are distinct
 * @param chunk_size is the size of the chunk 
 * @param @deprecated creator flag indicates whether this node is 
 *        the creator of this chunk; first one to call alloc is the creator
 *
 * @return pointer to the shared memory chunk; NULL in case of error
 */
void *dsm_alloc(dsm *d, dhandle chunk_id, ssize_t chunk_size) {
  uint32_t i;

  // register signal handler for the chunk
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = dsm_sigsegv_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    handle_error("sigaction");
  }

  dsm_chunk_meta *chunk_meta = &d->g_dsm_page_map[chunk_id];
  memset(chunk_meta, 0, sizeof(dsm_chunk_meta));
  
  // get page size
  PAGESIZE = sysconf(_SC_PAGE_SIZE);
  if (PAGESIZE == -1)
    handle_error("sysconf");
  debug("---------- PAGESIZE: %d\n", PAGESIZE);
  
  // initialize chunk related data structures
  uint32_t num_pages = 1 + (chunk_size-1)/PAGESIZE;
  chunk_meta->g_chunk_size = chunk_size;

  // allocate chunk memory
  void *base_ptr = chunk_meta->g_base_ptr = memalign(PAGESIZE, num_pages*PAGESIZE);
  if (base_ptr == NULL)
    handle_error("memalign\n");
  memset(base_ptr, 0, chunk_size);

  log("Num pages alloc'ed for chunk %"PRIu64": %d\n", chunk_id, num_pages);
  
  if (!d->is_master) {
    // for master this is allocated in the internal function
    log("Allocing pages\n");
    chunk_meta->pages = (dsm_page_meta*)calloc(num_pages, sizeof(dsm_page_meta));
    for (i = 0; i < num_pages; i++) {
      if (pthread_mutex_init(&chunk_meta->pages[i].lock, NULL) != 0) {
        handle_error("mutex init failed\n");
      }
    }
  }
 
  int is_owner = 0;
  // synchronously inform the master about the memory allocation
  // When a request for a page comes from other nodes;
  //   master will get the chunk_meta, page_meta for the request and
  //   return the page
  if ( (is_owner = dsm_request_allocchunk(d->master, chunk_id, chunk_size, d->host, d->port)) < 0) {
    handle_error("allocchunk failed\n");
  }
  log("Allocchunk success. I am the owner?  %s.\n", 
      is_owner==1?"Yes.":"No."); 

  if(is_owner) {
    if (mprotect(base_ptr, chunk_size, PROT_READ | PROT_WRITE) == -1)
      handle_error("mprotect\n");
    for (i = 0; i < num_pages; i++)
      chunk_meta->pages[i].page_prot = PROT_WRITE;

  } else {
    log("prot none %p, %lx\n", base_ptr, chunk_size); 
    if (mprotect(base_ptr, chunk_size, PROT_NONE) == -1)
      handle_error("mprotect\n");
    for (i = 0; i < num_pages; i++)
      chunk_meta->pages[i].page_prot = PROT_NONE;
  }
  return base_ptr;
}

void dsm_free(dsm *d, dhandle chunk_id) {
  dsm_request_freechunk(d->master, chunk_id, d->host, d->port);
}

int dsm_barrier_all(dsm *d) {
  int i;
  dsm_conf *c = &d->c;
  
  // send barrier request to all other nodes
  for (i = 0; i < c->num_nodes; i++) {
    if (i == c->this_node_idx) continue;
    dsm_request_barrier(&d->clients[i]);
  }

  // wait until all nodes hit the barrier
  pthread_mutex_lock(&d->barrier_lock);
  while (d->barrier_counter < (uint64_t)c->num_nodes) {
    pthread_cond_wait(&d->barrier_cond, &d->barrier_lock);
  }
  pthread_mutex_unlock(&d->barrier_lock);
  
  // set it back to 1 and not 0
  d->barrier_counter = 1;
  return 0;
}

int dsm_init(dsm *d, const char* host, uint32_t port, int is_master) {
  // initialize dsm structure
  strncpy((char*)d->host, host, sizeof(d->host));
  d->port = port;
  d->is_master = is_master;

  // catch SIGTERM to clean up
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &act, NULL);

  // storing the dsm structure in the global ctxt
  // to be accessible in the signal handler function
  g_dsm = d;
  
  // reading configuration
  dsm_conf *c = &d->c;
  if (dsm_conf_init(c, "dsm.conf", (const char*)d->host, d->port) < 0) {
    print_err("Error parsing conf file\n");
    return -1;
  }

  // allocate page buffer
  // this will be used to store getpage responses
  d->page_buffer = (uint8_t*)calloc(PAGESIZE, sizeof(uint8_t));

  // initialize barrier variables 
  d->barrier_counter = 1;
  if (pthread_mutex_init(&d->barrier_lock, NULL) != 0) {
    print_err("barrier mutex init failed\n");
    return -1;
  }
  if (pthread_cond_init(&d->barrier_cond, NULL) != 0) {
    print_err("barrier cond init failed\n");
    return -1;
  }

  // initialize background thread
  if (pthread_create(&d->dsm_daemon, NULL, &dsm_daemon_start, (void *)d) != 0) {
    print_err("Thread not created! %d\n", -errno);
    return -1;
  }

  // open connections to other nodes
  d->clients = (dsm_request*)calloc(c->num_nodes, sizeof(dsm_request));
  for (int i = 0; i < c->num_nodes; i++) {
    dsm_request_init(&d->clients[i], c->hosts[i], c->ports[i]);
  }
  d->master = &d->clients[c->master_idx];
  return 0;
}
    
int dsm_close(dsm *d) {
  log("Closing node %s:%d\n", d->host, d->port);
  // wait for master to give green signal
  // master has to complete page copying
  // TODO use conditional wait rather than doing this stupidity
  int loop = 1;
  do {
    sleep(2);
    loop = 0;
    for (int i = 0; i < NUM_CHUNKS; i++) {
      dsm_chunk_meta *chunk_meta = &d->g_dsm_page_map[i];
      if (chunk_meta->g_chunk_size != 0) {
        loop = 1;
        break;
      }
    } 
    log("Waiting for master's approval.\n");
  } while (loop);

  log("Master approved! Shutting down.\n");
  dsm_conf *c = &d->c;
  free(d->page_buffer);
  
  dsm_request_terminate(&d->clients[c->this_node_idx], d->host, d->port);

  pthread_join(d->dsm_daemon, NULL); /* Wait until thread is finished */
  pthread_cond_destroy(&d->barrier_cond);
  pthread_mutex_destroy(&d->barrier_lock);
  
  for (int i = 0; i < c->num_nodes; i++)
    dsm_request_close(&d->clients[i]);
  free(d->clients);
  dsm_conf_close(c);
  return 0;
}
