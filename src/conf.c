#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "conf.h"

int dsm_conf_init(dsm_conf *c, const char *conf_file_path, const char *host, uint32_t port) {

  char buffer[256];
  char is_master;
  int i;
  size_t this_host_len = strlen(host);
  FILE *fp = fopen(conf_file_path, "r");

  if (fscanf(fp, "%d", &c->num_nodes) == EOF)
    return -1;

  c->hosts = (uint8_t**)calloc(c->num_nodes, sizeof(uint8_t*));
  c->ports = (uint32_t*)calloc(c->num_nodes, sizeof(uint32_t));


  for (i = 0; i < c->num_nodes; i++) {
    size_t host_len = 0;
    
    if ( fscanf(fp, "%c", &is_master) == EOF)
      return -1;
    
    if ( fscanf(fp, "%c %s %d", &is_master, buffer, &c->ports[i]) == EOF) {
      return -1;
    }

    host_len = strlen(buffer);
    c->hosts[i] = (uint8_t*)calloc(1 + host_len, sizeof(uint8_t));
    memcpy(c->hosts[i], buffer, host_len);

    if (is_master == '*') 
      c->master_idx = i;

    if (c->ports[i] == port &&
        strncmp(host, (char*)c->hosts[i], 
          this_host_len < host_len?this_host_len:host_len) == 0 )
      c->this_node_idx = i;
  }

  fclose(fp);
  return 0;
}

int dsm_conf_close(dsm_conf *c) {
  int i;
  free(c->ports);
  for (i = 0; i < c->num_nodes; i++) {
    free(c->hosts[i]);
  }
  free(c->hosts);
  return 0;
}

int test_conf_main() {

  dsm_conf c;

  dsm_conf_init(&c, "dsm.conf", "locahost", 8000);

   
  for (int i = 0; i < c.num_nodes; i++) {
    printf("%d %s %d\n", i==c.master_idx, c.hosts[i], c.ports[i]);
  }

  dsm_conf_close(&c);
  return 0;
}
