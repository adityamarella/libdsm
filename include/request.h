#ifndef DSM_REQUESTS_H
#define DSM_REQUESTS_H

#include "dsmtypes.h"
#include "comm.h"

typedef struct dsm_request_struct {
  comm c;
  int initialized;
  // redundant fields useful 
  // for searching for owner host during getpage
  uint32_t port;
  uint8_t host[HOST_NAME];
} dsm_request;


typedef struct packed dsm_getpage_args_struct {
  dhandle chunk_id;
  dhandle page_offset;
  uint32_t flags;
  uint32_t requestor_port;
  uint8_t requestor_host[];
} dsm_getpage_args;

typedef struct packed dsm_invalidatepage_args_struct {
  dhandle chunk_id;
  dhandle page_offset;
  uint32_t flags;
  uint32_t requestor_port;
  uint8_t requestor_host[];
} dsm_invalidatepage_args;

typedef struct packed dsm_allocchunk_args_struct {
  dhandle chunk_id;
  size_t size;
  uint32_t requestor_port;
  uint8_t requestor_host[HOST_NAME];     // TODO: passing unnecessary data
} dsm_allocchunk_args;

typedef struct packed dsm_freechunk_args_struct {
  dhandle chunk_id;
  uint32_t requestor_port;
  uint8_t requestor_host[HOST_NAME];     // TODO: passing unnecessary data
} dsm_freechunk_args;

typedef struct packed dsm_locatepage_args_struct {
  dhandle chunk_id;
  dhandle page_offset;
} dsm_locatepage_args;

typedef struct packed dsm_barrier_args_struct {
  uint8_t tmp;     // TODO: passing unnecessary data
} dsm_barrier_args;

typedef struct packed dsm_terminate_args_struct {
  uint32_t requestor_port;
  uint8_t requestor_host[HOST_NAME];     // TODO: passing unnecessary data
} dsm_terminate_args;

typedef struct packed dsm_req_struct {
  dsm_msg_type type;
  union {
    dsm_getpage_args   getpage_args;
    dsm_invalidatepage_args invalidatepage_args;
    dsm_locatepage_args locatepage_args;
    dsm_allocchunk_args allocchunk_args;
    dsm_freechunk_args freechunk_args;
    dsm_barrier_args barrier_args;
    dsm_terminate_args terminate_args;
  } content;
} dsm_req;

/*
 * A convenience macro to generate a dsm_req structure. The first parameter is
 * the message type, i.e. GETPAGE, etc., and the second parameter is the
 * corresponding request structure.
 *
 * The following invocation creates a GETPAGE request:
 * dsm_req request = make_request(GETPAGE, .getpage_args = {
 *   .ph = 0
 * });
 */
#define make_request(request_type, ...) \
  { \
    .type = request_type, \
    .content = { \
      __VA_ARGS__ \
    } \
  }

/*
 * A convenience macro to generate the size of a given message.
 */
#define dsm_req_size(msg_type) \
  (sizeof(dsm_msg_type) + sizeof(dsm_##msg_type##_args))


int dsm_request_init(dsm_request *r, uint8_t *host, uint32_t port);
int dsm_request_close(dsm_request *c);
int dsm_request_allocchunk(dsm_request *r, dhandle chunk_id, size_t size, uint8_t *host, uint32_t port);
int dsm_request_freechunk(dsm_request *r, dhandle chunk_id, uint8_t *requestor_host, uint32_t requestor_port);
int dsm_request_getpage(dsm_request *r, dhandle chunk_id, dhandle page_offset, uint8_t *host, uint32_t port, uint8_t **page_start_addr, uint32_t flags);
int dsm_request_locatepage(dsm_request *r, dhandle chunk_id, dhandle page_offset, uint8_t **host, int *port);
int dsm_request_invalidatepage(dsm_request *r, dhandle chunk_id, dhandle page_offset, uint8_t *host, uint32_t port, uint32_t flags);

int dsm_request_barrier(dsm_request *r);

int dsm_request_terminate(dsm_request *r, uint8_t *requestor_host, uint32_t requestor_port);

#endif
