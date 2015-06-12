/* #define DEBUG */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <utime.h>

#include "reply_handler.h"
#include "request.h"
#include "strings.h"
#include "dsm.h"
#include "dsm_internal.h"
#include "utils.h"

extern struct dsm_map g_dsm_map[];
extern int PAGESIZE;

/**
 * Not a traditional handler: should be called when there's an error.
 *
 * Attempts to send an ERROR reply `error` to the client connected to `sock`.
 *
 * @param sock the endpoint connected to the client
 * @param error the dsm_error being sent to the client
 */
void handle_error(comm *c, dsm_error error) {
  debug("Sending error message '%s' to client.\n", strdsmerror(error));
  dsm_rep reply = make_reply(ERROR, .error_rep = {
    .error = error
  });

  if (comm_send_data(c, &reply, dsm_rep_size(error)) < 0) {
    print_err("Failed to send error message to client.\n");
  }
}

/**
 * The NOOP handler. Handles the NOOP message by sending a NOOP reply.
 *
 * @param sock the endpoint connected to the client
 */
void handle_noop(comm *c) {
  dsm_msg_type msg = NOOP;
  if (comm_send_data(c, (dsm_rep *)&msg, sizeof(dsm_msg_type)) < 0) {
    print_err("Failed to send NOOP reply.\n");
  }
}

void handle_allocchunk(comm *c, dsm_allocchunk_args *args) {
  UNUSED(args);
  log("Handling allocchunk for chunk %"PRIu64" from %s:%d\n", 
      args->chunk_id, args->requestor_host, args->requestor_port);

  int is_owner = 0;
  if ( (is_owner = dsm_allocchunk_internal(args->chunk_id, args->size, 
      args->requestor_host, args->requestor_port)) < 0) {
      handle_error(c, DSM_EBADALLOC);
      return;
  }

  dsm_rep reply = make_reply(ALLOCCHUNK, .allocchunk_rep = {
      .is_owner = is_owner
  });

  // Send reply
  if(comm_send_data(c, &reply, dsm_rep_size(allocchunk)) < 0) {
    print_err("Failed to send allocchunk reply.\n");
  }
}

void handle_freechunk(comm *c, dsm_freechunk_args *args) {
  UNUSED(args);

  log("Handling freechunk for chunk %"PRIu64" from %s:%d\n", 
      args->chunk_id, args->requestor_host, args->requestor_port);

  dsm_rep reply = make_reply(FREECHUNK, .freechunk_rep = {
      .chunk_id = args->chunk_id
  });
  
  // Send reply
  if(comm_send_data(c, &reply, dsm_rep_size(freechunk)) < 0) {
    print_err("Failed to send FREECHUNK reply.\n");
  }

  // this happens after the response is sent so that the node does not blocking
  if (dsm_freechunk_internal(args->chunk_id, args->requestor_host, args->requestor_port) < 0) {
      handle_error(c, DSM_EBADALLOC);
      return;
  }
}

/**
 * The LOCATEPAGE handler.
 *
 *
 * @param sock the endpoint connected to the client
 * @param args the client's arguments
 */
void handle_locatepage(comm *c, dsm_locatepage_args *args) {
  UNUSED(args);
  debug("Handling locatepage for chunk %"PRIu64".\n", args->chunk_id);

  uint8_t host[HOST_NAME];
  uint32_t port = 0;

  if (dsm_locatepage_internal(args->chunk_id, 
        args->page_offset, (uint8_t**)&host, &port) < 0) {
    handle_error(c, DSM_ENOPAGE);
    return;
  }
  
  size_t host_name_len = strlen((char*)host) + 1;
  size_t reply_size = dsm_rep_size(locatepage) + host_name_len;
  dsm_rep *reply = (dsm_rep*)malloc(reply_size);
  reply->type = LOCATEPAGE;
  reply->content.locatepage_rep.port = port;
  memcpy(reply->content.locatepage_rep.host, host, host_name_len);

#if 0
  // Update new new owner of page
  strncpy(g_dsm_map[i].host, args->host, HOST_NAME);
  g_dsm_map[i].port = args->port;
#endif

  // Send reply
  if(comm_send_data(c, reply, reply_size) < 0) {
    print_err("Failed to send LOCATEPAGE reply.\n");
  }
}


/**
 * The GETPAGE handler.
 *
 *
 * @param sock the endpoint connected to the client
 * @param args the client's arguments
 */
void handle_getpage(comm *c, dsm_getpage_args *args) {
  UNUSED(args);
  log("Handling getpage for chunk_id=%"PRIu64", page_offset=%"PRIu64", host:port=%s:%d.\n", 
      args->chunk_id, args->page_offset, args->client_host, args->client_port);

  uint64_t count = PAGESIZE;
  size_t reply_size = dsm_rep_size(getpage) + PAGESIZE;
  dsm_rep *reply = (dsm_rep*)malloc(reply_size);
  memset(reply, 0, reply_size);

  uint8_t *data = reply->content.getpage_rep.data;
  if (dsm_getpage_internal(args->chunk_id, args->page_offset, 
    args->client_host, args->client_port, &data, &count, args->flags) < 0) {
    handle_error(c, DSM_ENOPAGE);
    goto cleanup_reply;
  }

  reply->type = GETPAGE;
  reply->content.getpage_rep.count = count;
  
  if(comm_send_data(c, reply, reply_size) < 0) {
    print_err("Failed to send GETPAGE reply.\n");
  }

cleanup_reply:
  free(reply);
}

/**
 * The INVALIDATEPAGE handler.
 *
 *
 * @param sock the endpoint connected to the client
 * @param args the client's arguments
 */
void handle_invalidatepage(comm *c, dsm_invalidatepage_args *args) {
  log("Handling invalidatepage for chunk_id=%"PRIu64", page_offset=%"PRIu64", host:port=%s:%d.\n",
      args->chunk_id, args->page_offset, args->client_host, args->client_port);

  if (dsm_invalidatepage_internal(args->chunk_id, args->page_offset) < 0) {
    handle_error(c, DSM_EINTERNAL);
    return;
  }

  dsm_rep reply = make_reply(INVALIDATEPAGE, .invalidatepage_rep = {
      .chunk_id = args->chunk_id,
      .page_offset = args->page_offset,
  });

  // Send reply
  if(comm_send_data(c, &reply, dsm_rep_size(invalidatepage)) < 0) {
    print_err("Failed to send INVALIDATEPAGE reply.\n");
  }
}

/**
 * Not a traditional handler: should be called when a message doesn't have an
 * implementation. Simply prints a note and sends an DSM_ENOTIMPL error reply
 * to the client.
 *
 * @param sock the endpoint connected to the client
 * @param error the msg_type that hasn't been implemented
 */
void handle_unimplemented(comm *c, dsm_msg_type msg_type) {
  printf("NOTE: Handler for '%s' is unimplemented.\n", strmsgtype(msg_type));
  handle_error(c, DSM_ENOTIMPL);
}
