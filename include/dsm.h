#ifndef __DSM_H_
#define __DSM_H_

#include "dsmtypes.h"
#include "dsm_conf.h"
#include "request.h"
#include "dsm_server.h"

typedef struct dsm_page_meta_struct {
  dhandle chunk_id;                // chunk_id; this is maintained by the user
  pthread_mutex_t lock;
  uint32_t port;                    // port on the owner node is listening 
  uint8_t owner_host[HOST_NAME];    // node which owns this memory
} dsm_page_meta;

typedef struct dsm_chunk_meta_struct {
  uint32_t count;               // count of pages in this chunk
  uint32_t ref_counter;         // used to maintain how many clients are using the shared memory
  pthread_mutex_t lock;
  uint32_t clients_using[32];    // used to maintain the list of clients using this chunk
  dsm_page_meta *pages;
} dsm_chunk_meta;

typedef struct dsm_struct {
  uint8_t is_master;
  uint8_t host[HOST_NAME];                   // master host name
  uint32_t port;                             // port on which master is listening
  
  
  //private variables; user will not initialize these variables
  dsm_conf c;
  
  pthread_t dsm_daemon;

  // TODO Make this cleaner. 
  // num_nodes can be accessed from dsm_conf object;
  // dsm_request *clients is initialized here but
  // number of clients is taken from dsm_conf.
  //
  // clients will open connection to master 
  // this structure will be null for master
  dsm_request master;

  // master will open connections to clients
  // this structure will be null for clients
  dsm_request *clients;

  // handle to listener server on this node
  dsm_server s;

  // this is maintained by the master
  // for client this structure null
  dsm_chunk_meta g_dsm_page_map[NUM_CHUNKS];   // make this a hash later; key:value -> chunk_id:list of page meta objects

  // initialize another structure to maintain the base_ptrs
  // TODO: combine these two
  char *g_base_ptr[NUM_CHUNKS];
  size_t g_chunk_size[NUM_CHUNKS];
  int chunk_page_prot[NUM_CHUNKS][MAP_SIZE];

  // copy getpage contents into this buffer instead of 
  // directly copying to the fault address
  uint8_t *page_buffer;

} dsm;

/**
 * Initializes the system. Reads the system configuration from dsm.conf file. 
 * Creates a background (dsm_daemon) thread which listens for requests from other nodes. 
 *
 * @param d dsm object
 * @return 0 if init succeeds; negative value incase of error
 */
int dsm_init(dsm *d);

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
 * @param chunk id integer identifying the shared memory chunk
 */
void dsm_free(dsm *d, dhandle chunk_id);

#define UNUSED(var) (void)(var)

#endif /* __DSM_H_ */
