#ifndef DSM_REPLYS_H
#define DSM_REPLYS_H

#include "dsmtypes.h"
#include "comm.h"
#include "request.h"

typedef struct packed dsm_error_rep_struct {
  dsm_error error; // The error type.
} dsm_error_rep;

typedef struct packed dsm_allocchunk_rep_struct {
  uint8_t is_owner;
} dsm_allocchunk_rep;

typedef struct packed dsm_freechunk_rep_struct {
  dhandle chunk_id;
} dsm_freechunk_rep;

typedef struct packed dsm_invalidatepage_rep_struct {
  dhandle chunk_id;
  dhandle page_offset;
} dsm_invalidatepage_rep;

typedef struct packed dsm_getpage_rep_struct {
  uint64_t count; // Number of bytes in data.
  uint8_t data[]; // The data itself.
} dsm_getpage_rep;

typedef struct packed dsm_locatepage_rep_struct {
  uint32_t port;
  uint8_t host[];
} dsm_locatepage_rep;

typedef struct packed dsm_rep_struct {
  dsm_msg_type type;
  union {
    dsm_error_rep error_rep;
    dsm_getpage_rep getpage_rep;
    dsm_locatepage_rep locatepage_rep;
    dsm_invalidatepage_rep invalidatepage_rep;
    dsm_freechunk_rep freechunk_rep;
    dsm_allocchunk_rep allocchunk_rep;
  } content;
} dsm_rep;

void handle_noop(comm *c);
void handle_error(comm *c, dsm_error error);
void handle_unimplemented(comm *c, dsm_msg_type msg_type);
void handle_allocchunk(comm *c, dsm_allocchunk_args *args);
void handle_freechunk(comm *c, dsm_freechunk_args *args);
void handle_getpage(comm *c, dsm_getpage_args *args);
void handle_invalidatepage(comm *c, dsm_invalidatepage_args *args);
void handle_locatepage(comm *c, dsm_locatepage_args *args);

/*
 * A convenience macro to generate a dsm_rep structure. The first parameter is
 * the message type, i.e. GETATTR, ERROR, etc., and the second parameter is the
 * corresponding reply structure.
 *
 * The following invocation creates an error response:
 * dsm_rep reply = make_reply(ERROR, .error_rep = {
 *   .error = ENOTIMPL
 * });
 */
#define make_reply(reply_type, ...) \
  { \
    .type = reply_type, \
    .content = { \
      __VA_ARGS__ \
    } \
  }

/*
 * A convenience macro to generate the size of a given message.
 */
#define dsm_rep_size(msg_type) \
  (sizeof(dsm_msg_type) + sizeof(dsm_##msg_type##_rep))

#endif
