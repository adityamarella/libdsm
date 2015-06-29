#ifndef __DSM_MAIN_H
#define __DSM_MAIN_H

#define MAX_URL_LENGTH 256

typedef struct test_options_struct {
  int verbose;
  int is_master;
  char host[256];
  int port;
  int node_id;
} test_options;

void test_ping_pong(const char *host, int port, int num_nodes, int is_master);
int test_matrix_mul(const char* host, int port, int node_id, int nnodes, int is_master);
#endif
