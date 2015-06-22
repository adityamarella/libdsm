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
static void sigterm_handler(int sig) {
  UNUSED(sig);
  log("Got sigterm. Terminating server.\n");
  g_dsm->s.terminated = 1;
}

static 
dhandle get_chunk_id_for_addr(char *saddr) {
  dhandle i;
  char *base_ptr;
  size_t chunk_size;

  for (i=0; i<NUM_CHUNKS; i++) {
    dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[i];
    base_ptr = chunk_meta->g_base_ptr;
    chunk_size = chunk_meta->g_chunk_size;
    //log("base:%p sddar:%p end:%p\n", base_ptr, saddr, base_ptr+chunk_size);
    if (saddr >= base_ptr && saddr < base_ptr + chunk_size)
      return i; 
  }
  return -1;
}

static void dsm_sigsegv_handler(int sig, siginfo_t *si, void *unused) {
  UNUSED(sig);
  UNUSED(unused);

  int write_fault = 0;
  uint32_t flags = 0;

  // TODO remove this hack
  // get chunk_id from si->si_addr
  // may be just iterate through the list of g_dsm->g_base_ptr and using g_dsm->chunk_sizes
  // determine where si->si_addr falls into 
  int chunk_id = (dhandle)get_chunk_id_for_addr((char*)si->si_addr);
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  char *base_ptr = chunk_meta->g_base_ptr;
  size_t chunk_size = chunk_meta->g_chunk_size;
  log("\nhost:port=%s:%d Got SIGSEGV at address: 0x%lx\n",
      g_dsm->host, g_dsm->port, (long) si->si_addr);

  // safety check; probably not necessary
  if ((char*)si->si_addr < base_ptr || 
      (char*)si->si_addr >= base_ptr + chunk_size) {
    log("Fault out of chunk. address: 0x%lx chunk_id: %d base_ptr: %p base_end: %p\n",
        (long) si->si_addr, chunk_id, base_ptr, base_ptr + chunk_size);
    return;
  }
  // Build page offset
  dhandle page_offset = (dhandle)get_page_offset((char *)(si->si_addr), base_ptr);
  char *page_start_addr = base_ptr + PAGESIZE*page_offset;

  // NONE to READ (read-only) to WRITE (read write) transition
  if (chunk_meta->pages[page_offset].page_prot == PROT_NONE) {
    log("read fault..................\n");
    write_fault = 0;
    flags |= FLAG_PAGE_READ;
  } else if (chunk_meta->pages[page_offset].page_prot == PROT_READ) {
    log("write fault..................\n");
    write_fault = 1;
    flags |= FLAG_PAGE_WRITE | FLAG_PAGE_NOUPDATE;
  }
 
  // Request page from master
  if (g_dsm->is_master) {
    uint64_t count;
    if (dsm_getpage_internal(chunk_id, page_offset, 
          g_dsm->host, g_dsm->port, &g_dsm->page_buffer, &count, flags) < 0) {
      //TODO: we have not yet decided on what to do if page is not found;
    }
  } else {
    dsm_request *r = &g_dsm->master;
    if (dsm_request_getpage(r, chunk_id, page_offset, 
          g_dsm->host, g_dsm->port, &g_dsm->page_buffer, flags) < 0) {
      //TODO: we have not yet decided on what to do if page is not found;
    }
  }

  // set the protection to READ/WRITE so that 
  // the page can be written
  if (mprotect(page_start_addr, PAGESIZE, PROT_READ | PROT_WRITE) == -1)
    print_err("mprotect\n");
  // No chunk_page_prot update needed for above mprotect as it's temp

  // copy the page
  if (!(flags & FLAG_PAGE_NOUPDATE)) {
    memcpy(page_start_addr, g_dsm->page_buffer, PAGESIZE);
    log("writing page\n");
  } else {
    log("not writing page\n");
  }

  // None to read (read-only) to write (read write) transition
  if (write_fault) {
    debug("write fault\n");
    if (mprotect(page_start_addr, PAGESIZE, PROT_READ | PROT_WRITE) == -1)
      print_err("mprotect\n");
    chunk_meta->pages[page_offset].page_prot = PROT_WRITE;
  } else {
    debug("read fault\n");
    if (mprotect(page_start_addr, PAGESIZE, PROT_READ) == -1)
      print_err("mprotect\n");
    chunk_meta->pages[page_offset].page_prot = PROT_READ;
  }
}


/**
 * Alloc function allocates a new shared memory chunk and return the pointer to that chunk.
 *
 * @param d is the dsm object structure; to maintain context
 * @param chunk_id is maintained by the user of libdsm;
 *        if multiple chunks are used then the user 
 *        has to ensure that chunk id are distinct
 * @param size is the size of the chunk 
 * @param @deprecated creator flag indicates whether this node is 
 *        the creator of this chunk; first one to call alloc is the creator
 *
 * @return pointer to the shared memory chunk; NULL in case of error
 */
void *dsm_alloc(dsm *d, dhandle chunk_id, ssize_t size) {
  uint32_t i;

  // register signal handler for the chunk
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = dsm_sigsegv_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    handle_error("sigaction");
  }

  dsm_chunk_meta *chunk_meta = &d->g_dsm_page_map[chunk_id];
  
  // get page size
  PAGESIZE = sysconf(_SC_PAGE_SIZE);
  if (PAGESIZE == -1)
    handle_error("sysconf");
  debug("---------- PAGESIZE: %d\n", PAGESIZE);

  // allocate chunk memory
  void *buffer = chunk_meta->g_base_ptr = memalign(PAGESIZE, size);
  if (buffer == NULL)
    handle_error("memalign\n");
  memset(buffer, 0, size);

  // initialize chunk related data structures
  uint32_t num_pages = 1 + size/PAGESIZE;
  chunk_meta->g_chunk_size = size;
  
  if (!d->is_master) {
    // for master this is allocated in the internal function
    if (pthread_mutex_init(&chunk_meta->lock, NULL) != 0) {
      handle_error("mutex init failed\n");
    }

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
  if ( (is_owner = dsm_request_allocchunk(&d->master, chunk_id, size, d->host, d->port)) < 0) {
    handle_error("allocchunk failed\n");
  }
  log("Allocchunk success. I am the owner?  %s.\n", 
      is_owner==1?"Yes.":"No."); 

  if(is_owner) {
    if (mprotect(buffer, size, PROT_READ | PROT_WRITE) == -1)
      handle_error("mprotect\n");
    for (int i = 0; i < size / PAGESIZE; i++)
      chunk_meta->pages[i].page_prot = PROT_WRITE;

  } else { 
    if (mprotect(buffer, size, PROT_NONE) == -1)
      handle_error("mprotect\n");
    for (int i = 0; i < size / PAGESIZE; i++)
      chunk_meta->pages[i].page_prot = PROT_NONE;
  }
  return buffer;
}

void dsm_free(dsm *d, dhandle chunk_id) {
  UNUSED(d);
  dsm_request_freechunk(&d->master, chunk_id, d->host, d->port);
  
}

static void *dsm_daemon_start(void *ptr) {
  dsm *d = (dsm*)ptr;
  log("Starting server on port %d\n", d->port);
  
  dsm_server *s = &d->s;
  dsm_server_init(s, "localhost", d->port);
  dsm_server_start(s);
#if 0
  dsm_server_close(s);
#endif
  return 0;
}

int dsm_init(dsm *d) {
  // storing the dsm structure in the global ctxt
  // to be accessible in the signal handler function
  g_dsm = d;
  
  // reading configuration
  dsm_conf *c = &d->c;
  if (dsm_conf_init(c, "dsm.conf") < 0) {
    print_err("Error parsing conf file\n");
    return -1;
  }

  d->page_buffer = (uint8_t*) calloc(PAGESIZE, sizeof(uint8_t));
  
  // catch SIGTERM to clean up
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &act, NULL);


  // initialize the server if this is a master
  // in future we might need bidirectional communication
  // for now this is req resp 
  if (pthread_create(&d->dsm_daemon, NULL, &dsm_daemon_start, (void *)d) != 0) {
    print_err("Thread not created! %d\n", -errno);
    return -1;
  }

  if (d->is_master) {
    d->clients = (dsm_request*)calloc(c->num_nodes, sizeof(dsm_request));
    for (int i = 0; i < c->num_nodes; i++) {
      debug("dsm_request_init for host:port=%s:%d\n",
            c->hosts[i], c->ports[i]);
      dsm_request_init(&d->clients[i], c->hosts[i], c->ports[i]);
    }
  }
  
  // initialize the requestor
  // establish connection to the master node 
  dsm_request *r = &d->master;
  memset(r, 0, sizeof(dsm_request));
  debug("dsm_request_init for host:port=%s:%d\n",
        c->hosts[c->master_idx], c->ports[c->master_idx]);
  dsm_request_init(r, c->hosts[c->master_idx], c->ports[c->master_idx]);
  
  return 0;
}
    
int dsm_close(dsm *d) {
  log("Closing node %s:%d\n", d->host, d->port);
  // wait for master to give green signal
  // master has to complete page copying
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
  d->s.terminated = 1;
  free(d->page_buffer);
  pthread_kill(d->dsm_daemon, SIGTERM);
  pthread_join(d->dsm_daemon, NULL); /* Wait until thread is finished */
  if(d->is_master) {
    for (int i = 0; i < c->num_nodes; i++)
      dsm_request_close(&d->clients[i]);
    free(d->clients);
  } else {
    dsm_request_close(&d->master);
  }
  dsm_conf_close(c);
  return 0;
}
