/* #define DEBUG */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#ifdef __linux__
#include <bsd/stdlib.h>
#endif

#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#include "utils.h"
#include "strings.h"
#include "request.h"
#include "reply_handler.h"
#include "dsm_server.h"

// The URL to serve at - the port should probably be a command line argument
// static const char *TCP_URL = "tcp://*:2048";

/**
 * Init the communication structure.
 *
 * @param host 
 * @param port
 * @returns 0 on success; -1 on failure 
 */
int dsm_server_init(dsm_server *c, const char *host, uint32_t port) {
  UNUSED(host);
  c->port = port;
  c->terminated = 0;
  return 0;
}

int dsm_server_close(dsm_server *c) {
  c->terminated = 1;
  return 0;
}

/**
 * The main server loop.
 *
 * Waits for connections from clients, determines the type of request the client
 * is making, and dispatches the request to respective handlers, passing it the
 * request's parameters.
 *
 * @param url the nanomsg formatted URL the server should listen at
 */
int dsm_server_start(dsm_server *s) {
  comm c;
  int error;

  // passing 0 as second argument because we will be receiving and replying to requests.
  if ((error = comm_init(&c, 0)) < 0)
    return error;

  if ((error = comm_bind(&c, s->port)) < 0)
    return error;

  // okay, it all checks out. Let's loop, waiting for a message.
  debug( "DSM listening on %s...\n", c.url);

  dsm_req *req = NULL;
  while (!s->terminated) {
    ssize_t bytes = 0;
    req = _comm_receive_data(&c, &bytes, 0);
    if (req == NULL) {
      debug("Received NULL\n");
      continue;
    }

    dsm_msg_type msg_type = ERROR;
    if ((size_t) bytes >= sizeof(dsm_msg_type)) {
      msg_type = req->type;
    }

    log("\n\nDSMServer=%s Received '%s' request.\n", c.url, strmsgtype(msg_type));
    switch (req->type) {
      case ALLOCCHUNK:
        handle_allocchunk(&c, &req->content.allocchunk_args);
        break;
      case FREECHUNK:
        handle_freechunk(&c, &req->content.freechunk_args);
        break;
      case GETPAGE:
        handle_getpage(&c, &req->content.getpage_args);
        break;
      case LOCATEPAGE:
        handle_locatepage(&c, &req->content.locatepage_args);
        break;
      case INVALIDATEPAGE:
        handle_invalidatepage(&c, &req->content.invalidatepage_args);
        break;
      default:
        handle_unimplemented(&c, msg_type);
        break;
    }
    log("Sent response\n");
    log("\n");

    comm_free(&c, req);
  }

  // Check if we were terminated or simply failed
  if (s->terminated) {
    debug("Terminating...\n");
    debug("Recevied SIGTERM. Terminating.\n");
    fflush(stdout);
  } else {
    print_err("Server failed to recv()!\n");
    fflush(stderr);
  }

  // Cleanup
  comm_shutdown(&c);
  comm_close(&c);
  return 0;
}
