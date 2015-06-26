#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#define URL "tcp://192.168.29.156:9000"

int send (int sock, const char *data, size_t size, int flags) {

  int bytes = 0;
  int retry = 5;
  do {
    if (flags == 0) {
      int timeout = 2000;
      nn_setsockopt (sock, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    }

    if ((bytes = nn_send(sock, data, size, flags)) >= 0) break;
    
    switch (errno) {
      case EFAULT:
      case EBADF:
      case ENOTSUP:
      case ETERM:
        printf("Send failed: '%s'\n", strerror(errno));
        return bytes;
      default:
        usleep(300);
    }
  } while (--retry > 0);

  // Check that the second was successful
  if (bytes != (int) size) {
    printf("Send failed: incorrect byte count.\n");
    return -1;
  }
  return bytes;
}

int main ()
{
  int i, N = 100000;
  char * msg = "hello world";
  int sz_msg = strlen (msg) + 1; // '\0' too
  int sock = nn_socket (AF_SP, NN_REQ);
  assert (sock >= 0);
  int endpoint = nn_connect (sock, URL);
  printf ("Client: SENDING \"%s\"\n", msg);


  for (i = 0; i < N; i++) {
   
    int bytes;
    if (i == N-1)
      msg = "x";
   
    bytes = nn_send(sock, msg, sz_msg, 0);
    assert (bytes == sz_msg);

    int timeout = 2000;
    nn_setsockopt (sock, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));

    char *data = NULL;
    nn_recv(sock, &data, NN_MSG, 0);

    if (data)
      nn_freemsg(data);
  }
  nn_shutdown (sock, endpoint);

  return 0;
}
