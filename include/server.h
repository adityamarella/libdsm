#ifndef SNFS_COMMON_COMM_H
#define SNFS_COMMON_COMM_H

#include <stdlib.h>
#include <signal.h>

typedef struct dsm_server_struct {
  uint32_t port;
  int sock;
  // Set to 1 when SIGTERM was received
  volatile sig_atomic_t terminated;
} dsm_server;

int dsm_server_init(dsm_server *c, const char *host, uint32_t port);
int dsm_server_close(dsm_server *c);
int dsm_server_start(dsm_server *c);

#endif
