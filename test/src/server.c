#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#define URL "tcp://*:9000"

int main ()
{
  char *buf = NULL;
  int sock = nn_socket (AF_SP, NN_REP), bytes, endpoint;
  assert (sock >= 0);
  assert ((endpoint = nn_bind (sock, URL)) >= 0);
  while (1) {
    buf = NULL; // let nanomsg allocate the buffer;
    bytes = nn_recv (sock, &buf, NN_MSG, 0);
    assert (bytes >= 0);
    printf ("Server: RECEIVED \"%s\"\n", buf);
    
    if(buf[0] == 'x') break;

    nn_send(sock, buf, bytes, 0);
    nn_freemsg(buf);
  }
  nn_freemsg (buf);
  nn_shutdown (sock, endpoint);
  nn_close(sock);
}

