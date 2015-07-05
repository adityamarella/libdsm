/*#define DEBUG*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "utils.h"
#include "reply_handler.h"
#include "request.h"
#include "strings.h"

int dsm_request_init(dsm_request *r, uint8_t *host, uint32_t port) {
  if (r->initialized)
    return 0;

  int err = 0;
  debug("Initializing request with host:port = %s:%d\n", host, port);
  memcpy(r->host, host, 1+strlen((char*)host));
  r->port = port;

  comm *c = &r->c;
  if ((err = comm_init(c, 1)) < 0)
    return err;
  
  if ((err = comm_connect(c, (char*)host, port)) < 0)
    return err;

  r->initialized = 1;
  return 0;
}

int dsm_request_close(dsm_request *r) {
  if (r->initialized) {
    comm_shutdown(&r->c); 
    comm_close(&r->c);
  }
  r->initialized = 0;
  return 0;
}

/**
 * Sends a request and waits for a reply. If flags is zero, we wait forever for
 * a reply. If flags == NN_DONTWAIT, we wait about a second and then return
 * NULL. Returns the reply if there was one and it wasn't an ERROR.
 *
 * @param sock the socket to send the request to
 * @param url the url to send the request to
 * @param request the request to send
 * @param size the size of the request
 * @param @deprecated flags flags == NN_DONTWAIT, we wait about a second for a response,
 *              otherwise, the wait could be forever
 *
 * @return reply is successful, NULL otherwise
 */
dsm_rep *dsm_request_req_rep_f(dsm_request *r, dsm_req *request,
    size_t size) {
  assert(r);
  assert(r->c.sock >= 0);
  assert(request);

  debug("Sending request '%s' to '%s:%d'\n", strmsgtype(request->type), r->host, r->port);

  // Send the request. If it fails, close the connection and return NULL.
  if (comm_send_data(&r->c, request, size) < 0) {
    return NULL;
  }

  // Wait for a reply.
  dsm_rep *reply = (dsm_rep*)comm_receive_data(&r->c, NULL);

  // No reply? Well, okay. Return NULL.
  if (!reply) {
    debug("No reply or reply is NULL\n");
    return NULL;
  }

  if (reply->type != request->type) {
    debug("Bad reply type: %s (%d).\n", strmsgtype(reply->type), reply->type);
    comm_free(&r->c, reply);
    return NULL;
  }

  // Check if it's an error and return NULL if it is
  if (reply->type == ERROR) {
    dsm_error error = reply->content.error_rep.error;
    debug("Server Returned Error: %s\n", strdsmerror(error));
    comm_free(&r->c, reply);
    return NULL;
  }

  return reply;
}

/**
 * The same as dsm_req_rep, but always waits forever for a response.
 *
 * @param sock the socket to send the request to
 * @param url the url to send the request to
 * @param req the request to send
 * @param size the size of the request
 * @param flags flags == NN_DONTWAIT, we wait about a second for a response,
 *              otherwise, the wait could be forever
 *
 * @return reply is successful, NULL otherwise
 */
dsm_rep *dsm_request_req_rep(dsm_request *r, dsm_req *req, size_t size) {
  if (!r->initialized)
    return NULL;
  return dsm_request_req_rep_f(r, req, size);
}

int dsm_request_allocchunk(dsm_request *r, dhandle chunk_id, size_t size, uint8_t *host, uint32_t port) {
  dsm_req req = make_request(ALLOCCHUNK, .allocchunk_args = {
    .chunk_id = chunk_id,
    .size = size,
    .requestor_port = port,
  });

  log("Sending allocchunk %"PRIu64", %zu, %s:%d\n", chunk_id, size, host, port);
  
  memcpy(req.content.allocchunk_args.requestor_host, host, strlen((char*)host) + 1);

  dsm_rep *rep = dsm_request_req_rep(r, &req, dsm_req_size(allocchunk));
  if (rep == NULL) {
    comm_free(&r->c, rep);
    return -1;
  }

  log("Received allocchunk response.\n\n");

  return rep->content.allocchunk_rep.is_owner;
}

int dsm_request_freechunk(dsm_request *r, dhandle chunk_id, 
    uint8_t *requestor_host, uint32_t requestor_port) {
  dsm_req req = make_request(FREECHUNK, .freechunk_args = {
      .chunk_id = chunk_id,
      .requestor_port = requestor_port
      });
  memcpy(req.content.freechunk_args.requestor_host, 
      requestor_host, strlen((char*)requestor_host) + 1);

  log("\n");
  log("Sending freechunk %"PRIu64"\n", chunk_id);
  
  dsm_rep *rep = dsm_request_req_rep(r, &req, dsm_req_size(freechunk));
  if (rep == NULL) {
    return -1;
  }

  log("Received freechunk reponse.\n"); 

  comm_free(&r->c, rep);
  return 0;
}
  
/**
 * The LOCATEPAGE request.
 *
 * @return 0 on success, < 0 (a -errno) on error
 */
int dsm_request_locatepage(dsm_request *r, dhandle chunk_id, 
    dhandle page_offset, uint8_t **host, int *port) {
  dsm_req req = make_request(LOCATEPAGE, .locatepage_args = {
    .chunk_id = chunk_id,
    .page_offset = page_offset, 
  });

  dsm_rep *rep = dsm_request_req_rep(r, &req, dsm_req_size(locatepage));
  if (rep == NULL) {
    return -1;
  }

  debug("Received locatepage response. Page is in %s:%d\n", rep->content.locatepage_rep.host,
      rep->content.locatepage_rep.port); 

  memcpy(*host, rep->content.locatepage_rep.host, 1+strlen((char*)rep->content.locatepage_rep.host));
  *port = rep->content.locatepage_rep.port;

  comm_free(&r->c, rep);
  return 0;
}

/**
 * The GETPAGE request.
 *
 * @return 0 on success, < 0 (a -errno) on error
 */
int dsm_request_getpage(dsm_request *r, dhandle chunk_id, 
    dhandle page_offset, uint8_t *host, uint32_t port, 
    uint8_t **page_start_addr, uint32_t flags) {
  log("Sending getpage %"PRIu64", %"PRIu64", %s:%d\n", chunk_id, page_offset, host, port);
  size_t host_len = strlen((char*)host) + 1;
  size_t req_size = dsm_req_size(getpage) + host_len*sizeof(uint8_t); 
  dsm_req *req = (dsm_req*)malloc(req_size);
  memset(req, 0, req_size);  
  req->type = GETPAGE;
  
  dsm_getpage_args *args = &req->content.getpage_args;
  args->chunk_id = chunk_id,
  args->page_offset = page_offset,
  args->flags = flags,
  args->requestor_port = port,
  memcpy(args->requestor_host, host, host_len);

  dsm_rep *rep = dsm_request_req_rep(r, req, req_size);
  free(req);
  
  if (rep == NULL) {
    log("Received NULL reply for getpage %"PRIu64", %"PRIu64", %s:%d\n",
        chunk_id, page_offset, host, port);
    return -1;
  }

  if (flags & FLAG_PAGE_NOUPDATE) {
    log("Received getpage for owned page\n");
    return 0;
  }

  log("Received getpage data=\"%p\", bytes=%"PRIu64"\n", 
         rep->content.getpage_rep.data,
         rep->content.getpage_rep.count); 
  
  memcpy(*page_start_addr, rep->content.getpage_rep.data,
         PAGESIZE);

  comm_free(&r->c, rep);
  return 0;
}

/**
 * The INVALIDATEPAGE request.
 *
 * @return 0 on success, < 0 (a -errno) on error
 */
int dsm_request_invalidatepage(dsm_request *r, dhandle chunk_id,
    dhandle page_offset, uint8_t *host, uint32_t port, uint32_t flags) {
  UNUSED(host);
  UNUSED(port);

  size_t host_len = strlen((char*)host) + 1;
  size_t req_size = dsm_req_size(invalidatepage) + host_len*sizeof(uint8_t); 
  dsm_req *req = (dsm_req*)malloc(req_size);
  memset(req, 0, req_size);  
  req->type = INVALIDATEPAGE;
  
  dsm_invalidatepage_args *args = &req->content.invalidatepage_args;
  args->chunk_id = chunk_id,
  args->page_offset = page_offset,
  args->flags = flags,
  args->requestor_port = port,
  memcpy(args->requestor_host, host, host_len);
 

  log("Sending invalidatepage %"PRIu64", %"PRIu64", %s:%d\n", chunk_id, page_offset, host, port);

  dsm_rep *rep = dsm_request_req_rep(r, req, req_size);
  free(req);
  
  if (rep == NULL) {
    return -1;
  }
  comm_free(&r->c, rep);
  return 0;
}

int dsm_request_barrier(dsm_request *r) {
  dsm_req req = make_request(BARRIER, .barrier_args = {.tmp=1});
  dsm_rep *rep = dsm_request_req_rep(r, &req, dsm_req_size(barrier));
  if (rep == NULL) {
    return -1;
  }
  comm_free(&r->c, rep);
  return 0;
}

int dsm_request_terminate(dsm_request *r, uint8_t *requestor_host, uint32_t requestor_port) {
  dsm_req req = make_request(TERMINATE, .terminate_args = {
      .requestor_port = requestor_port,
      });
  memcpy(req.content.terminate_args.requestor_host, requestor_host, 1+strlen((char*)requestor_host));
  dsm_rep *rep = dsm_request_req_rep(r, &req, dsm_req_size(terminate));
  if (rep == NULL) {
    return -1;
  }
  comm_free(&r->c, rep);
  return 0;
}
