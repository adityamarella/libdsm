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
#include <signal.h>

#ifdef __linux__
#include <bsd/stdlib.h>
#endif

#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#include "utils.h"
#include "strings.h"
#include "comm.h"

/**
 * Returns the current time in milliseconds.
 *
 * @return the current time in milliseconds
 */
long long current_ms() {
  struct timeval te;
  gettimeofday(&te, NULL);
  return (long long) te.tv_sec * 1000 + te.tv_usec / 1000;
}

/**
 * Sends `size` bytes from the buffer of `data` to the machine referred to by
 * `sock`. If `flags` == NN_DONTWAIT, this call blocks for at most about 500ms.
 *
 * @param sock the socket endpoint
 * @param data the data to send
 * @param size the number of bytes in `data`
 * @param flags if NN_DONTWAIT, blocks for at most ~500ms, else blocks until
 *              message is sent.
 *
 * @return number of bytes sent on success, < 0 on error
 */
static int _comm_send_data(comm *c, void *data, size_t size, int flags) {
  debug("Sending (flags: %d) %zu bytes of data (%p):\n", flags, size, data);
  if_debug { printbuf(data, size); }

  // Try for half a second to send the data.
  int bytes = 0;

  bytes = nn_send(c->sock, data, size, flags);
    
    if (errno < 0) {
      debug("Send failed: '%s'\n", strerror(errno));
      return bytes;
    }

  // Check that the second was successful
  if (bytes != (int) size) {
    debug("Send failed: incorrect byte count.\n");
    return -1;
  }

  return bytes;
}

/**
 * Receives data from the machine referred to by `sock`. If `flags` ==
 * NN_DONTWAIT, this call blocks for at most about 1.25s. Returns a malloc()d
 * reply if it was received. It is the callers responsibility to free it.
 *
 * @param[out] size size in bytes of the received data
 * @param flags if NN_DONTWAIT, blocks for at most ~1.25s, else blocks until
 *              message is received
 *
 * @return malloc()d reply is it was received, NULL otherwise
 */
void* _comm_receive_data(comm *c, ssize_t *size, int flags) {
  debug("Attempting to receive data (flags: %d)\n", flags);

  // Try for a little over a second to receive data
  int bytes;
  void *data = NULL;

  if (flags == 0) {
    int timeout = 60000;
    nn_setsockopt (c->sock, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
  }

  bytes = nn_recv(c->sock, &data, NN_MSG, flags);

  if (errno == EBADF || errno == ENOTSUP || errno == ETERM) {
    debug("Receive failed: '%s'\n", strerror(errno));
    return NULL;
  }

  // If response is NULL, we didn't get a successful receive
  if (!data) {
    debug("Receive failed: timed out (%d bytes).\n", bytes);
    return NULL;
  }

  // All is well. Set `size` if it was passed in.
  if (size) *size = bytes;
  debug("Received %d bytes of data:\n", bytes);
  if_debug { printbuf(data, bytes); }

  return data;
}

int comm_send_data(comm *c, void *data, size_t size) {
  return _comm_send_data(c, data, size, 0/*NN_DONTWAIT*/);
}

void* comm_receive_data(comm *c, ssize_t *size) {
  return _comm_receive_data(c, size, 0/*NN_DONTWAIT*/);
}

void comm_free(comm *c, void *p) {
  UNUSED(c);
  nn_freemsg(p);
}

int comm_init(comm *c, int is_req) {
  memset(c, 0, sizeof(comm));
  if (is_req)
    c->sock = nn_socket(AF_SP, NN_REQ);
  else
    c->sock = nn_socket(AF_SP, NN_REP);

  if(c->sock < 0) {
    print_err("Failed to open socket: %s\n", strerror(errno));
    return -errno;
  }
  return 0;
}

/**
 * Init the communication structure.
 *
 * @param host 
 * @param port
 * @returns 0 on success; -1 on failure 
 */
int comm_connect(comm *c, const char *host, uint32_t port) {
  size_t host_len = strlen(host);
  char *url;

  url = (char*)calloc(sizeof(char), host_len + 32); 
  snprintf(url, host_len + 32, "tcp://%s:%d", host, port);
  
  // Connect the socket
  c->endpoint = nn_connect(c->sock, url);
  if (c->endpoint < 0) {
    print_err("Socket connection to '%s' failed: %s\n", url, strerror(errno));
    free(url);
    return -1;
  }
  free(url);
  return 0;
}

int comm_bind(comm *c, int port) {
  char *url;
  url = (char*)calloc(32, sizeof(char)); 
  snprintf(url, 32, "tcp://*:%d", port);
  if((c->endpoint = nn_bind(c->sock, url)) < 0) {
    print_err("Failed to bind with url '%s': %s\n", url, strerror(errno));
    free(url);
    return -1;
  }
  free(url);
  return 0;
}

int comm_shutdown(comm *c) {
  nn_shutdown(c->sock, c->endpoint);
  return 0;
}

int comm_close(comm *c) {
  nn_close(c->sock);
  return 0;
}
