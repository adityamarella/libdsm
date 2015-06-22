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
inline static 
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

inline static
dsm_request* get_request_object(dsm *d, const uint8_t *host, uint32_t port) {
  if (0 == is_same_node(d->master.host, d->master.port, host, port))
    return &d->master;

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

static int dsm_really_freechunk(dhandle chunk_id) { 
  uint32_t i;
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  if (mprotect(g_dsm->g_base_ptr[chunk_id], g_dsm->g_chunk_size[chunk_id], PROT_READ | PROT_WRITE) == -1) {
    print_err("mprotect failed for addr=%p, error=%s\n", g_dsm->g_base_ptr[chunk_id], strerror(errno));
    return -1;
  } 

  for (uint32_t i = 0; i < chunk_meta->count; i++)
    chunk_meta->pages[i].page_prot = PROT_WRITE;

  for (i = 0; i < chunk_meta->count; i++) {
    if (pthread_mutex_destroy(&chunk_meta->pages[i].lock) != 0) {
      print_err("mutex destroy failed for chunk %"PRIu64", page %"PRIu32"\n", chunk_id, i);
      return -1;
    }
  }
  
  free(g_dsm->g_base_ptr[chunk_id]);
  g_dsm->g_chunk_size[chunk_id] = 0;
  chunk_meta->count = 0;
  free(chunk_meta->pages);
  
  if (pthread_mutex_destroy(&chunk_meta->lock) != 0) {
    print_err("mutex destroy failed, error=%s\n", strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * This function will always execute on master
 * Fetch pages owned by requestor host. This is a synchronous call 
 * We wait here until all the pages owned by requestor are fetched. 
 */
static int fetch_remotely_owned_pages(dhandle chunk_id,
    const uint8_t *requestor_host, uint32_t requestor_port) {
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  dsm_request *requestor = get_request_object(g_dsm, requestor_host, requestor_port);
  dhandle page_offset = 0;
  if (requestor == &g_dsm->master)
    return 0;

  for (page_offset = 0; page_offset < chunk_meta->count; page_offset++) {
    dsm_page_meta *m = &chunk_meta->pages[page_offset];
    if (0 == is_same_node(requestor_host, requestor_port, m->owner_host, m->port)) {
      char *page_start_addr = g_dsm->g_base_ptr[chunk_id] + page_offset * PAGESIZE;
      if (mprotect(page_start_addr, PAGESIZE, PROT_READ | PROT_WRITE) == -1) {
        print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
        return -1;
      }
      dsm_request_getpage(requestor, chunk_id, page_offset, g_dsm->host, 
          g_dsm->port, (uint8_t**)&page_start_addr, FLAG_PAGE_READ);

      
      // finally update the page map
      dsm_page_meta *page_meta = &chunk_meta->pages[page_offset];
      page_meta->page_prot = PROT_WRITE;
      memcpy(page_meta->owner_host, g_dsm->host, strlen((char*)g_dsm->host) + 1);
      page_meta->port = g_dsm->port;
    }
  }

  // This will tell the node that it can safely release resources.
  // This request will reach 'MARK1' label on the requestor node
  dsm_request_freechunk(requestor, chunk_id, g_dsm->host, g_dsm->port);
  return 0;
}

int dsm_invalidatepage_internal(dhandle chunk_id, dhandle page_offset) {
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  dsm_page_meta *page_meta = &chunk_meta->pages[page_offset];
  char *base_ptr = g_dsm->g_base_ptr[chunk_id];
  char *page_start_addr = base_ptr + page_offset * PAGESIZE;

  // Change permissions to NONE
  // set the new owner for this page
  // TODO read-only pages can be kept
  if (mprotect(page_start_addr, PAGESIZE, PROT_NONE) == -1) {
    print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
    return -1;
  }
  log("Acquiring mutex lock, chunk_id: %"PRIu64", %"PRIu64"\n", chunk_id, page_offset);
  pthread_mutex_lock(&page_meta->lock);
  page_meta->page_prot = PROT_NONE;
  pthread_mutex_unlock(&page_meta->lock);
  log("Released lock, chunk_id: %"PRIu64", %"PRIu64"\n", chunk_id, page_offset);
  return 0;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 */
int dsm_locatepage_internal(dhandle chunk_id, 
    dhandle page_offset, uint8_t **host, uint32_t *port, uint32_t flags) {
  UNUSED(flags);
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  if (page_offset > chunk_meta->count) {
    print_err("Locate page error, wrong page offset\n");
    return -1;
  }
  log("chunk %p, %p, page owner: %"PRIu64", %"PRIu64"\n", chunk_meta, chunk_meta->pages, chunk_id, page_offset);
  dsm_page_meta *m = &chunk_meta->pages[page_offset];
  memcpy(*host, m->owner_host, strlen((char*)m->owner_host) + 1);
  *port = m->port;
  return 0;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 */
int dsm_getpage_internal(dhandle chunk_id, dhandle page_offset,
    uint8_t *requestor_host, uint32_t requestor_port, /*these are needed for updating the page map*/
    uint8_t **data, uint64_t *count, uint32_t flags) {
  UNUSED(chunk_id);
  UNUSED(flags);

  int error = 0;

  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  dsm_page_meta *page_meta = &chunk_meta->pages[page_offset];
  log("Acquiring mutex lock, chunk_id: %"PRIu64", %"PRIu64"\n", chunk_id, page_offset);
  pthread_mutex_lock(&page_meta->lock);

  char *base_ptr = g_dsm->g_base_ptr[chunk_id];

  if (!g_dsm->is_master) {
    log("I am not the master. Take the page I have.\n");
    char *page_start_addr = base_ptr + page_offset*PAGESIZE;

    memcpy(*data, page_start_addr, PAGESIZE);
    *count = PAGESIZE; 

    if (flags & FLAG_PAGE_WRITE ||
        ((flags & FLAG_PAGE_READ) && page_meta->page_prot == PROT_WRITE)) {
      // Change permissions to NONE
      // set the new owner for this page
      if ( (error = mprotect(page_start_addr, PAGESIZE, PROT_NONE)) == -1) {
        print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
        goto cleanup_unlock;
      }

      page_meta->page_prot = PROT_NONE;
      log("Client %s:%d requested write page chunk_id=%ld page_offset=%ld\n",
          requestor_host, requestor_port, chunk_id, page_offset);
    } else {
      log("Client %s:%d requested read page chunk_id=%ld page_offset=%ld\n",
          requestor_host, requestor_port, chunk_id, page_offset);
    }

    goto cleanup_unlock;
  }
  
  uint8_t *owner_host = (uint8_t*)calloc(HOST_NAME, sizeof(uint8_t)); // TODO free owner_host
  uint32_t owner_port = 0;

  if ((error=dsm_locatepage_internal(chunk_id, page_offset, 
                                     &owner_host, &owner_port, flags)) < 0) {
    print_err("Could not locate owner for %"PRIu64", %"PRIu64".\n", chunk_id, page_offset);
    free(owner_host);
    goto cleanup_unlock;
  }

  log("Located owner for the page %s:%d\n", owner_host, owner_port);
      
  // check if owner host is same as this machine -
  // if yes serve the page; else get the page from owner and serve it
  if (0 == is_same_node(owner_host, owner_port, g_dsm->host, g_dsm->port)) {
    debug("this node is the owner %s:%d\n", owner_host, owner_port);

    char *page_start_addr = base_ptr + page_offset*PAGESIZE;

    memcpy(*data, page_start_addr, PAGESIZE);
    *count = PAGESIZE; 

    // Change permissions to NONE
    // set the new owner for this page
    if ((error=mprotect(page_start_addr, PAGESIZE, PROT_NONE)) == -1) {
      print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
      goto cleanup_unlock;
    }
    page_meta->page_prot = PROT_NONE;
  } else {
    // this machine is not the owner of the page
    // get the page from the owner
    // owner host, port was fetched by dsm_locatepage_internal
    // find the dsm_request object corresponding to owner host, port

    dsm_request *owner = get_request_object(g_dsm, owner_host, owner_port);
    //TODO handle owner == NULL case
    
    *count = PAGESIZE; 

    if (1 == is_same_node(owner_host, owner_port, requestor_host, requestor_port)) {
      log("Sending getpage request to %s:%d for chunk=%ld page_offset=%ld\n",
          owner->host, owner->port, chunk_id, page_offset);
      dsm_request_getpage(owner, chunk_id, 
        page_offset, g_dsm->host, g_dsm->port, data, flags);
    }
  }

  if (flags & FLAG_PAGE_WRITE) {
    dsm_conf *c = &g_dsm->c;
    // Invalidate page for all hosts but the new owner
    for (int i = 0; i < c->num_nodes; i++) {

      // Continue for requester host and request port
      if (c->ports[i] == requestor_port)
        continue;

      // for master locally invalidate page
      if (i == c->master_idx) {
        char *page_start_addr = base_ptr + page_offset*PAGESIZE;

        if ((error=mprotect(page_start_addr, PAGESIZE, PROT_NONE)) == -1) {
          print_err("mprotect failed for addr=%p, error=%s\n", page_start_addr, strerror(errno));
          goto cleanup_unlock;
        }
        page_meta->page_prot = PROT_NONE;
        continue;
      }

      // send invalidate to only those clients which are using this chunk
      if (chunk_meta->clients_using[i]) {
        log("Sending invalidatepage for chunk_id=%"PRIu64", page_offset=%"PRIu64", host:port=%s:%d.\n", 
            chunk_id, page_offset, c->hosts[i], c->ports[i]);
        dsm_request_invalidatepage(&g_dsm->clients[i], chunk_id, 
                                 page_offset, c->hosts[i], c->ports[i], flags);
      }
    }
  }

  free(owner_host);

  // finally update the page map
  memcpy(page_meta->owner_host, requestor_host, strlen((char*)requestor_host) + 1);
  page_meta->port = requestor_port;

cleanup_unlock:
  pthread_mutex_unlock(&page_meta->lock);
  log("Released lock, chunk_id: %"PRIu64", %"PRIu64"\n", chunk_id, page_offset);
  return error;
}

/**
 * This function could be called from dsm_daemon thread and the main thread
 * @return 1 if the host, port is the owner
 *         0 if the host, port is not the owner
 *         -1 incase of error
 */
int dsm_allocchunk_internal(dhandle chunk_id, size_t size, 
    uint8_t *requestor_host, uint32_t requestor_port) {
  int ret = 0;
  uint32_t i;
  uint32_t num_pages = 1 + size/PAGESIZE;
  
  debug("Allocing chunk. Setting page map for chunk %"PRIu64", %zu, owner=%s, port=%d\n", 
      chunk_id, size, requestor_host, requestor_port);
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];


  log("Acquiring lock, chunk_id: %"PRIu64"\n", chunk_id);
  pthread_mutex_lock(&chunk_meta->lock);
  // fill the owner map with page entries corresponding to the chunk
  if (chunk_meta->count == 0) {
    // this is the first node to allocate
    ret = 1; // make this the owner

    // initialize chunk meta structure
    chunk_meta->count = num_pages;
    chunk_meta->ref_counter = 1;
    if (pthread_mutex_init(&chunk_meta->lock, NULL) != 0) {
      print_err("mutex init failed\n");
      ret = -1;
      goto cleanup;
    }
    
    // initialize page meta structure
    log("Allocing pages\n");
    chunk_meta->pages = (dsm_page_meta*)calloc(num_pages, sizeof(dsm_page_meta));
    for (i = 0; i < num_pages; i++) {
      if (pthread_mutex_init(&chunk_meta->pages[i].lock, NULL) != 0) {
        print_err("mutex init failed\n");
        ret = -1;
        goto cleanup;
      }
      dsm_page_meta *m = &chunk_meta->pages[i];
      m->chunk_id = chunk_id;
      m->port = requestor_port;
      strncpy((char*)m->owner_host, (char*)requestor_host, sizeof(m->owner_host));
      log("chunk id: %"PRIu64", page %d owner %s:%d, %p\n", chunk_id, i, m->owner_host, m->port, m);
    }
  } else {
    // this means some other node already created the chunk and is the owner now
    // just increment the reference count and bail out
    if (chunk_meta->count != num_pages) {
      print_err("Inconsistent allocation. Possibly, different nodes allocated different sizes for the same chunk.\n");
      ret = -1;
      goto cleanup;
    }
    // inc reference counter; new client requested chunk
    chunk_meta->ref_counter++;
    chunk_meta->clients_using[get_request_idx(g_dsm, requestor_host, requestor_port)] = 1;
  }

cleanup:
  pthread_mutex_unlock(&chunk_meta->lock);
  log("Released lock, chunk_id: %"PRIu64"\n", chunk_id);
  return ret;
}

int dsm_freechunk_internal(dhandle chunk_id,
    const uint8_t *requestor_host, uint32_t requestor_port) {
  log("Freeing chunk %"PRIu64", requestor=%s:%d\n", chunk_id, requestor_host, requestor_port);
  dsm_chunk_meta *chunk_meta = &g_dsm->g_dsm_page_map[chunk_id];
  if (g_dsm->is_master) {
    pthread_mutex_lock(&chunk_meta->lock);
    if (chunk_meta->count == 0) {
      print_err("Nothing to free. Chunk not allocated size is 0\n");
      pthread_mutex_unlock(&chunk_meta->lock);
      return -1;
    }
    chunk_meta->ref_counter--;
    chunk_meta->clients_using[get_request_idx(g_dsm, requestor_host, requestor_port)] = 0;
    log("ref counter %d\n", chunk_meta->ref_counter);
    if(chunk_meta->ref_counter != 0)
      fetch_remotely_owned_pages(chunk_id, requestor_host, requestor_port);
    pthread_mutex_unlock(&chunk_meta->lock);
  } 
  
  if (g_dsm->is_master == 0 || chunk_meta->ref_counter == 0)
    dsm_really_freechunk(chunk_id); // MARK1
  return 0;
}
