/**
 * The functions in this file will be called from multiple threads. The main thread 
 * calls them to send requests to other nodes while the dsm daemon thread calls them 
 * to service any requests from other nodes. 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <malloc.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>

#include "dsm.h"
#include "utils.h"

extern dsm *g_dsm;

/**
 * Compares the host and ports of two different nodes
 * 
 * @return 0 if the nodes are same; 1 if not same
 */
int is_same_node(const uint8_t *host1, uint32_t port1, 
    const uint8_t *host2, uint32_t port2) {
  if (port1 != port2)
    return 1; 

  //TODO just being safe here; computing lengths could be unnecessary/inefficient
  size_t host1_len = strlen((char*)host1);
  size_t host2_len = strlen((char*)host2);
  if (host1_len == host2_len && strncmp((char*)host1, (char*)host2, host1_len) == 0)
    return 0;

  return 1;
}

dsm_request* get_request_object(dsm *d, const uint8_t *host, uint32_t port) {
  dsm_request *clients = d->clients;
  for (int i = 0; i < d->c.num_nodes; i++) {
    if (0 == is_same_node(clients[i].host, clients[i].port, host, port))
      return &clients[i];
  }
  return NULL;
}

inline static
int get_request_idx(dsm *d, const uint8_t *host, uint32_t port) {
  dsm_request *clients = d->clients;
  //TODO assume the first index is always master
  for (int i = 1; i < d->c.num_nodes; i++) {
    if (0 == is_same_node(clients[i].host, clients[i].port, host, port))
      return i;
  }
  return 0;
}

int dsm_invalidatepage_internal(dhandle chunk_id, dhandle page_offset) {
  char *base_ptr = g_dsm->g_base_ptr[chunk_id];
  char *page_start_addr = base_ptr + page_offset * PAGESIZE;

  // Change permissions to NONE
  // set the new owner for this page
  // TODO read-only pages can be kept
  if (mprotect(page_start_addr, PAGESIZE, PROT_NONE) == -1) {
    print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
    return -1;
  }
  if (g_dsm->is_master) {
    log("Acquiring mutex lock\n");
    pthread_mutex_lock(&g_dsm->lock);
    g_dsm->chunk_page_prot[chunk_id][page_offset] = PROT_NONE;
    pthread_mutex_unlock(&g_dsm->lock);
    log("Released mutex lock\n");
  }
  return 0;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 * And, this will always be executed by the master; no other node 
 * should execute this function
 */
int dsm_locatepage_internal(dhandle chunk_id, dhandle page_offset, 
    uint8_t *requestor_host, uint32_t requestor_port, /*these are needed for updating the page map*/
    uint32_t *owner_idx, uint64_t *nodes_accessing, 
    uint32_t flags) {
  UNUSED(flags);

  assert(g_dsm->is_master == 1);

  int error = 0;
  log("Acquiring mutex lock\n");
  pthread_mutex_lock(&g_dsm->lock);
  
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  if (page_offset >= chunk_meta->count) {
    error = -1;
    goto cleanup_unlock;
  } 

  dsm_page_meta *m = &chunk_meta->pages[page_offset];
  *owner_idx = get_request_idx(g_dsm, m->owner_host, m->port); 
  *nodes_accessing = 0;
  for (int i = 0; i < g_dsm->c.num_nodes; i++) {
    if (chunk_meta->clients_using[i]) 
      *nodes_accessing |= ((uint64_t)1)<<i;
  }
  
  if (flags & FLAG_PAGE_WRITE) {
    // finally update the page map
    // this should be done only if the requestor is trying to write the page
    dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
    dsm_page_meta *page_meta = &chunk_meta->pages[page_offset];
    memcpy(page_meta->owner_host, requestor_host, strlen((char*)requestor_host) + 1);
    page_meta->port = requestor_port;
  }

cleanup_unlock:
  pthread_mutex_unlock(&g_dsm->lock);
  log("Released mutex lock\n");
  return error;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 */
int dsm_getpage_internal(dhandle chunk_id, dhandle page_offset,
    uint8_t *requestor_host, uint32_t requestor_port, /*these are needed for updating the page map*/
    uint8_t **data, uint32_t *count, uint32_t flags) {
  UNUSED(chunk_id);
  UNUSED(flags);
  UNUSED(requestor_host);
  UNUSED(requestor_port);

  log("Acquiring mutex lock\n");
  pthread_mutex_lock(&g_dsm->lock);

  char *page_start_addr = g_dsm->g_base_ptr[chunk_id] + page_offset*PAGESIZE;
  memcpy(*data, page_start_addr, PAGESIZE);
  *count = PAGESIZE; 

  pthread_mutex_unlock(&g_dsm->lock);
  log("Released mutex lock\n");
  return 0;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 * @return 1 if the host, port is the owner
 *         0 if the host, port is not the owner
 *         -1 incase of error
 */
int dsm_allocchunk_internal(dhandle chunk_id, size_t sz, 
    uint8_t *requestor_host, uint32_t requestor_port) {
  debug("Allocing chunk. Setting page map for chunk %"PRIu64", %zu, owner=%s, port=%d\n", chunk_id, sz, requestor_host, requestor_port);

  log("Acquiring mutex lock\n");
  pthread_mutex_lock(&g_dsm->lock);

  uint32_t num_pages = 0;
  // calculate number of pages used for the chunk
  num_pages = 1 + sz/PAGESIZE;

  // fill the owner map with page entries corresponding to the chunk
  // TODO this should be done atomically or not?
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  if (chunk_meta->count) {
    // this means some other node already created the chunk and is the owner now
    // just increment the reference count and bail out
    if (chunk_meta->count != num_pages) {
      // inconsistent allocation
      pthread_mutex_unlock(&g_dsm->lock);
      log("Released mutex lock\n");
      return -1;
    } else {
      // inc reference counter; new client requested chunk
      chunk_meta->ref_counter++;
      chunk_meta->clients_using[get_request_idx(g_dsm, requestor_host, requestor_port)] = 1;
    }
    pthread_mutex_unlock(&g_dsm->lock);
    log("Released mutex lock\n");
    return 0;
  }
 
  // this means no other host has created the chunk until now
  // we are the owner of the chunk 
  uint32_t i;
  // set the number of pages for this chunk
  chunk_meta->count = num_pages;

  // set reference counter to 1; because this is the first client
  chunk_meta->ref_counter = 1;

  // allocate page meta structure for each page
  chunk_meta->pages = (dsm_page_meta*)calloc(num_pages, sizeof(dsm_page_meta));
  for (i = 0; i < num_pages; i++) {
    dsm_page_meta *m = &chunk_meta->pages[i];
    m->chunk_id = chunk_id;
    m->port = requestor_port;
    strncpy((char*)m->owner_host, (char*)requestor_host, sizeof(m->owner_host));
    log("chunk id: %"PRIu64", page %d owner %s:%d\n", chunk_id, i, m->owner_host, m->port);
  }
  pthread_mutex_unlock(&g_dsm->lock);
  log("Released mutex lock\n");
  return 1;
}

int dsm_freechunk_internal(dhandle chunk_id,
    const uint8_t *requestor_host, uint32_t requestor_port) {
  UNUSED(requestor_host);
  UNUSED(requestor_port);
  log("Freeing chunk %"PRIu64", requestor=%s:%d\n", chunk_id, requestor_host, requestor_port);
  dhandle page_offset = 0;

  if (g_dsm->is_master) {
    log("Acquiring mutex lock\n");
    pthread_mutex_lock(&g_dsm->lock);
    
    dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
    if (chunk_meta->count) {
      // decrement ref counter
      chunk_meta->ref_counter--;
      chunk_meta->clients_using[get_request_idx(g_dsm, requestor_host, requestor_port)] = 0;
      log("ref counter %d\n", chunk_meta->ref_counter);
      if(chunk_meta->ref_counter == 0) {
        if (mprotect(g_dsm->g_base_ptr[chunk_id], g_dsm->g_chunk_size[chunk_id], PROT_READ | PROT_WRITE) == -1) {
          print_err("mprotect failed for addr=%p, error=%s\n", g_dsm->g_base_ptr[chunk_id], strerror(errno));
        } else {
          for (uint32_t i = 0; i < chunk_meta->count; i++)
            g_dsm->chunk_page_prot[chunk_id][i] = PROT_WRITE;
          free(chunk_meta->pages);
          free(g_dsm->g_base_ptr[chunk_id]);
          g_dsm->g_chunk_size[chunk_id] = 0;
          memset(&g_dsm->chunk_page_prot[chunk_id], 0, MAP_SIZE);
          chunk_meta->count = 0;
        }
      }
    }

    dsm_request *requestor = get_request_object(g_dsm, requestor_host, requestor_port);
    //TODO handle requestor == NULL case
    
    // if the requestor is not master node only then do something.
    if (requestor != &g_dsm->clients[g_dsm->c.master_idx]) {
      // getpages owned by requestor host
      // this is a synchronous call 
      // we wait here until all the pages owned by requestor
      // are fetched.
      for (page_offset = 0; page_offset < chunk_meta->count; page_offset++) {
        dsm_page_meta *m = &chunk_meta->pages[page_offset];
        if (0 == is_same_node(requestor_host, requestor_port, m->owner_host, m->port)) {
          char *page_start_addr = g_dsm->g_base_ptr[chunk_id] + page_offset * PAGESIZE;
          if (mprotect(page_start_addr, PAGESIZE, PROT_READ | PROT_WRITE) == -1) {
            print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
          } else {
            dsm_request_getpage(requestor, chunk_id, page_offset, g_dsm->host, 
                g_dsm->port, (uint8_t**)&page_start_addr, FLAG_PAGE_READ);

            g_dsm->chunk_page_prot[chunk_id][page_offset] = PROT_WRITE;
            // finally update the page map
            dsm_page_meta *page_meta = &chunk_meta->pages[page_offset];
            memcpy(page_meta->owner_host, g_dsm->host, strlen((char*)g_dsm->host) + 1);
            page_meta->port = g_dsm->port;
          }
        }
      }
      // this will tell the node that it can safely shutdown 
      dsm_request_freechunk(requestor, chunk_id, g_dsm->host, g_dsm->port);
    }

    pthread_mutex_unlock(&g_dsm->lock);
    log("Released mutex lock\n");
    
  } else {
    free(g_dsm->g_base_ptr[chunk_id]);
    g_dsm->g_chunk_size[chunk_id] = 0;
  } 
  return 0;
}

int dsm_terminate_internal() {
  g_dsm->s->terminated = 1;
  return 0;
}
