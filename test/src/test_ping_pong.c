#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "main.h"
#include "utils.h"
#include "dsm.h"
#include "request.h"
#include "reply_handler.h"
#include "dsm_server.h"

int test_dsm_master(const char *host, int port, int num_nodes, int is_master) {
  int host_len = strlen(host);
  dsm *d = (dsm*)malloc(sizeof(dsm));

  memset(d, 0, sizeof(dsm)); // Needed for initial state to be 0
  d->port = port;
  d->is_master = is_master;
  memcpy(d->host, host, 1+host_len);

  if (dsm_init(d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "page: 0 host: %s port: %d", d->host, d->port);
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "page: 1 host: %s port: %d", d->host, d->port);
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "page: 2 host: %s port: %d", d->host, d->port);
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "page: 3 host: %s port: %d", d->host, d->port);

  log("Done writing. Listening for requests from other nodes.\n");

  dsm_barrier_all(d);

  dsm_free(d, g_chunk_id);

cleanup:
  dsm_close(d);
  free(d); 
  return 0;
}

int test_dsm_client_n(const char *host, int port, int num_nodes, int is_master) {
  int host_len = strlen(host);
  dsm *d = (dsm*)malloc(sizeof(dsm));

  memset(d, 0, sizeof(dsm)); // Needed for initial state to be 0
  d->port = port;
  d->is_master = is_master;
  memcpy(d->host, host, 1+host_len);
  
  if (dsm_init(d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  srand ( time(NULL) );

  for (int i = 0; i < 60; i++) {
    int page = random()%4;
    snprintf(buffer + PAGESIZE * page, PAGESIZE, "host: %s port: %d", d->host, d->port);
    usleep(1000000+random()%100000);
  }

  dsm_barrier_all(d);
  dsm_free(d, g_chunk_id);

cleanup:
  dsm_close(d);
  free(d); 
  
  return 0;
}

void test_ping_pong() {

}

#endif

