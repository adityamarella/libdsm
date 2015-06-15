#ifndef DSM_COMM_H
#define DSM_COMM_H

#include <stdlib.h>

typedef struct comm_struct {
  int sock;
  int endpoint;
} comm;

int comm_init(comm *c, int is_req);
int comm_close(comm *c);

int comm_connect(comm *c, const char *host, uint32_t port);
int comm_bind(comm *c, int port);
int comm_shutdown(comm *c);

int comm_send_data(comm *c, void *data, size_t size);
void* comm_receive_data(comm *c, ssize_t *size);
void* _comm_receive_data(comm *c, ssize_t *size, int flags);

void comm_free(comm *c, void *p);

#endif
