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

extern 
int is_same_node(const uint8_t *host1, uint32_t port1, 
    const uint8_t *host2, uint32_t port2);

extern  
dsm_request* get_request_object(dsm *d, const uint8_t *host, uint32_t port);

/**
 * The SIGTERM signal handler. Simply sets the 'terminated' variable to 1 to
 * inform the serve loop it should exit.
 *
 * @param sig the signal received
 */
static void sigterm_handler(int sig) {
  UNUSED(sig);
  log("Got sigterm. Terminating server.\n");
  g_dsm->s->terminated = 1;
}

static 
dhandle get_chunk_id_for_addr(char *saddr) {
  dhandle i;
  char *base_ptr;
  size_t chunk_size;

  for (i=0; i<NUM_CHUNKS; i++) {
    base_ptr = g_dsm->g_base_ptr[i];
    chunk_size = g_dsm->g_chunk_size[i];
    if (saddr >= base_ptr && saddr < base_ptr + chunk_size)
      return i; 
  }
  return -1;
}

/**
 * Send locatepage to the master
 * Send getpage to the owner
 * Send invalidatepage to all other nodes accessing the page. If this is write access
 */
static void dsm_locate_and_getpage(dhandle chunk_id, dhandle page_offset, 
    uint8_t **page_buffer, int flags) {

  uint32_t owner_idx = 0;
  uint64_t nodes_accessing = 0;

  // locatepage from master
  if (g_dsm->is_master) {
    if (dsm_locatepage_internal(chunk_id, page_offset, 
          g_dsm->host, g_dsm->port,
          &owner_idx, &nodes_accessing, flags) < 0) {
      print_err("locatepage_internal failed: \n");
      assert(0); // for now this should not happen
    }
  } else {
    if (dsm_request_locatepage(&g_dsm->clients[g_dsm->c.master_idx], 
          chunk_id, page_offset, 
          g_dsm->host, g_dsm->port,
          &owner_idx, &nodes_accessing, flags) < 0) {
      print_err("locatepage failed: \n");
      assert(0); // for now this should not happen
    } 
  }

  //getpage
  dsm_request *r = &g_dsm->clients[owner_idx];
  if (dsm_request_getpage(r, chunk_id, page_offset, 
        g_dsm->host, g_dsm->port, 
        page_buffer, flags) < 0) {
    print_err("getpage failed: \n");
    assert(0); 
  }

  if (flags & FLAG_PAGE_WRITE) {
    dsm_conf *c = &g_dsm->c;
    // Invalidate page for all hosts but the new owner
    for (int i = 0; i < c->num_nodes; i++) {
      if (!(nodes_accessing & ((uint64_t)1)<<i))
        continue;

      // Continue for requester host and request port
      if (c->ports[i] == g_dsm->port)
        continue;

      log("Sending invalidatepage for chunk_id=%"PRIu64", page_offset=%"PRIu64", host:port=%s:%d.\n", 
          chunk_id, page_offset, c->hosts[i], c->ports[i]);
      dsm_request_invalidatepage(&g_dsm->clients[i], 
          chunk_id, page_offset, 
          g_dsm->host, g_dsm->port, flags);
    }
  }
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
  char *base_ptr = g_dsm->g_base_ptr[chunk_id];
  size_t chunk_size = g_dsm->g_chunk_size[chunk_id];
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
  if (g_dsm->chunk_page_prot[chunk_id][page_offset] == PROT_NONE) {
    log("read fault..................\n");
    write_fault = 0;
    flags |= FLAG_PAGE_READ;
  } else if (g_dsm->chunk_page_prot[chunk_id][page_offset] == PROT_READ) {
    log("write fault..................\n");
    write_fault = 1;
    flags |= FLAG_PAGE_WRITE | FLAG_PAGE_NOUPDATE;
  }

  dsm_locate_and_getpage(chunk_id, page_offset, &g_dsm->page_buffer, flags);

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
    g_dsm->chunk_page_prot[chunk_id][page_offset] = PROT_WRITE;
  } else {
    debug("read fault\n");
    if (mprotect(page_start_addr, PAGESIZE, PROT_READ) == -1)
      print_err("mprotect\n");
    g_dsm->chunk_page_prot[chunk_id][page_offset] = PROT_READ;
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
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = dsm_sigsegv_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    // TODO set errno correctly
    handle_error("sigaction");
  }

  PAGESIZE = sysconf(_SC_PAGE_SIZE);
  if (PAGESIZE == -1)
    handle_error("sysconf");

  debug("---------- PAGESIZE: %d\n", PAGESIZE);

  void *buffer = d->g_base_ptr[chunk_id] = memalign(PAGESIZE, size);
  if (buffer == NULL)
    handle_error("memalign\n");

  memset(buffer, 0, size);

  d->g_chunk_size[chunk_id] = size;
 
  int is_owner = 0;
  // after informing CM register signal handlers
  if(d->is_master) {
    if ( (is_owner = dsm_allocchunk_internal(chunk_id, size, d->host, d->port)) < 0) 
      handle_error("allocchunk failed\n");
  } else {
    // synchronously inform the Central Manager about the memory allocation
    // CM will maintain, ptr -> node, size mapping
    // When a request for a page comes from other nodes;
    //   - CM will search the entries to return the correct page
    if ( (is_owner = dsm_request_allocchunk(&d->clients[d->c.master_idx], chunk_id, size, d->host, d->port)) < 0) {
      handle_error("allocchunk failed\n");
    }
  }

  if(is_owner) {
    log("setting buffer=%p size=%ld PROT_READ | PROT_WRITE\n", buffer, size);
    if (mprotect(buffer, size, PROT_READ | PROT_WRITE) == -1)
      handle_error("mprotect\n");
    for (int i = 0; i < size / PAGESIZE; i++)
      g_dsm->chunk_page_prot[chunk_id][i] = PROT_WRITE;

  } else { 
    log("setting buffer=%p size=%ld PROT_NONE\n", buffer, size);
    if (mprotect(buffer, size, PROT_NONE) == -1)
      handle_error("mprotect\n");
    for (int i = 0; i < size / PAGESIZE; i++)
      g_dsm->chunk_page_prot[chunk_id][i] = PROT_NONE;
  }
  log("Allocchunk success. I am the owner?  %s.\n", 
      is_owner==1?"Yes.":"No."); 

  return buffer;
}

void dsm_free(dsm *d, dhandle chunk_id) {
  UNUSED(d);
  // synchronous send free request to central manager
  if (d->is_master)
    dsm_freechunk_internal(chunk_id, d->host, d->port);
  else
    dsm_request_freechunk(&d->clients[d->c.master_idx], chunk_id, d->host, d->port);
}

static void *dsm_daemon_start(void *ptr) {
  dsm *d = (dsm*)ptr;
  log("Starting server on port %d\n", d->port);
  
  dsm_server *s = (dsm_server*)malloc(sizeof(dsm_server));
  memset(s, 0, sizeof(dsm_server));
  d->s = s;
  dsm_server_init(s, "localhost", d->port);
  dsm_server_start(s);
  return 0;
}

int dsm_barrier_all(dsm *d) {
  int i;
  dsm_conf *c = &d->c;
  
  for (i = 0; i < c->num_nodes; i++) {
    if (i == c->this_node_idx) continue;
    dsm_request_barrier(&d->clients[i]);
  }

  pthread_mutex_lock(&d->barrier_lock);
  while (d->barrier_counter < (uint64_t)c->num_nodes) {
    pthread_cond_wait(&d->barrier_cond, &d->barrier_lock);
  }
  pthread_mutex_unlock(&d->barrier_lock);
  // set it back to 1 and not 0
  d->barrier_counter = 1;
  return 0;
}

int dsm_init(dsm *d) {
  // storing the dsm structure in the global ctxt
  // to be accessible in the signal handler function
  g_dsm = d;
  
  // reading configuration
  dsm_conf *c = &d->c;
  if (dsm_conf_init(c, "dsm.conf", (const char*)d->host, d->port) < 0) {
    print_err("Error parsing conf file\n");
    return -1;
  }

  d->page_buffer = (uint8_t*) calloc(PAGESIZE, sizeof(uint8_t));
  
  // catch SIGTERM to clean up
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &act, NULL);

  if (pthread_mutex_init(&d->lock, NULL) != 0) {
    print_err("mutex init failed\n");
    return -1;
  }

  if (pthread_mutex_init(&d->barrier_lock, NULL) != 0) {
    print_err("barrier mutex init failed\n");
    return -1;
  }

  if (pthread_cond_init(&d->barrier_cond, NULL) != 0) {
    print_err("barrier cond init failed\n");
    return -1;
  }
  
  d->barrier_counter = 1;

  // initialize the server if this is a master
  // in future we might need bidirectional communication
  // for now this is req resp 
  if (pthread_create(&d->dsm_daemon, NULL, &dsm_daemon_start, (void *)d) != 0) {
    print_err("Thread not created! %d\n", -errno);
    return -1;
  }

  d->clients = (dsm_request*)calloc(c->num_nodes, sizeof(dsm_request));
  for (int i = 0; i < c->num_nodes; i++) {
    debug("dsm_request_init for host:port=%s:%d\n",
          c->hosts[i], c->ports[i]);
    dsm_request_init(&d->clients[i], c->hosts[i], c->ports[i]);
  }
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
    pthread_mutex_lock(&g_dsm->lock);
    for (int i = 0; i < NUM_CHUNKS; i++) {
      if (g_dsm->g_chunk_size[i] != 0) {
        loop = 1;
        break;
      }
    } 
    pthread_mutex_unlock(&g_dsm->lock);
    log("Waiting for master's approval.\n");
  } while (loop);

  log("Master approved! Shutting down.\n");
  dsm_conf *c = &d->c;
  free(d->page_buffer);

  // send terminate request to background thread
  dsm_request_terminate(&d->clients[c->this_node_idx], d->host, d->port);

  /* Wait until thread is finished */
  pthread_join(d->dsm_daemon, NULL);

  pthread_cond_destroy(&d->barrier_cond);
  pthread_mutex_destroy(&d->barrier_lock);
  pthread_mutex_destroy(&d->lock);

  for (int i = 0; i < c->num_nodes; i++)
    dsm_request_close(&d->clients[i]);
  free(d->clients);
  free(d->s);
  dsm_conf_close(c);
  return 0;
}
