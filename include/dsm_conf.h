#ifndef __DSM_CONF_H_
#define __DSM_CONF_H_

typedef struct dsm_conf_struct {
  int num_nodes;
  int master_idx;
  uint8_t **hosts;
  uint32_t *ports;
} dsm_conf;


int dsm_conf_init(dsm_conf *c, const char *conf_file_path);
int dsm_conf_close(dsm_conf *c);

#endif
