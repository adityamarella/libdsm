#ifndef __DSM_H_
#define __DSM_H_

#include "dsmtypes.h"
#include "conf.h"
#include "request.h"
#include "server.h"

typedef struct dsm_page_meta_struct {
  dhandle chunk_id;                // chunk_id; this is maintained by the user
  pthread_mutex_t lock;
  int page_prot;
  uint32_t port;                    // port on the owner node is listening 
  uint8_t owner_host[HOST_NAME];    // node which owns this memory
} dsm_page_meta;

typedef struct dsm_chunk_meta_struct {
  uint32_t count;               // count of pages in this chunk
  uint32_t ref_counter;         // used to maintain how many clients are using the shared memory
  uint32_t clients_using[64];    // used to maintain the list of clients using this chunk
  char *g_base_ptr;
  size_t g_chunk_size;
  dsm_page_meta *pages;
} dsm_chunk_meta;

typedef struct dsm_struct {

  // indicates whether this node is master or not
  uint8_t is_master;
  
  // host name of this node
  uint8_t host[HOST_NAME]; 
  
  // port on which this node is listening
  uint32_t port; 
  
  //private variables; user will not initialize these variables
  dsm_conf c;
 
  // background thread to receive requests from other nodes
  pthread_t dsm_daemon;

  // cond variable for barrier
  pthread_cond_t barrier_cond;
  pthread_mutex_t barrier_lock;
  volatile uint64_t barrier_counter;

  // this points to the client at master_idx
  dsm_request *master;

  // all clients open connections to all other clients
  dsm_request *clients;

  // handle to listener server on this node
  dsm_server s;

  // this is maintained by the master
  // for client this structure null
  // TODO make this a hash later; key:value -> chunk_id:list of page meta objects
  dsm_chunk_meta g_dsm_page_map[NUM_CHUNKS];   

  // TODO remove this
  // copy getpage contents into this buffer instead of 
  // directly copying to the fault address
  uint8_t *page_buffer;

} dsm;

/**
 * Initializes the system. Reads the system configuration from dsm.conf file. 
 * Creates a background (dsm_daemon) thread which listens for requests from other nodes. 
 *
 * @param d dsm object
 * @param host name of this node
 * @param port on which this node listens
 * @param is_master whether this node is the master
 * @return 0 if init succeeds; negative value incase of error
 */
int dsm_init(dsm *d, const char *host, uint32_t port, int is_master);

/**
 * Closes the system. Frees any memory on the heap. 
 * Closes connections. Terminates the background 
 * (dsm_daemon) thread.
 *
 * @param d dsm object
 * @return 0 if init succeeds; negative value incase of error
 */
int dsm_close(dsm *d);

/**
 * Alloc gives a pointer to the shared memory chunk. 
 * User is expected to pass a chunk_id, which 
 * will be used to identify shared memory across nodes.
 *
 * @param d dsm object
 * @param chunk_id integer identifying the shared memory chunk
 * @size size of chunk; chunk size could be different
 *       on different nodes for the same chunk id.
 *
 * @return pointer to the shared memory chunk
 */
void* dsm_alloc(dsm *d, dhandle chunk_id, ssize_t size);

/**
 * Frees the shared memory chunk.
 *
 * @param d dsm object
 * @param chunk id integer identifying the shared memory chunk
 */
void dsm_free(dsm *d, dhandle chunk_id);

/**
 * Barrier could be used by application to synchronize control flow.
 *
 * @param d dsm object
 */
int dsm_barrier_all(dsm *d);

#define UNUSED(var) (void)(var)

#endif /* __DSM_H_ */
