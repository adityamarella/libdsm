//#define DEBUG
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


// The name the program was invoked with.
static const char *PROG_NAME;

// Server options structure
static test_options OPTIONS;

static const int g_chunk_id = 10;

/**
 * Prints the usage information for this program.
 */
static void usage() {
  fprintf(stderr,
    "Usage: %s [OPTION]... <dir>\n"
    "  <dir>  path to directory to serve via simple NFS\n"
    "\nOptions:\n"
    "  -h     give this help message\n"
    "  -v     print verbose output\n"
    "  -m     make this node master\n"
    "  -u     provide master url with this option\n",
    PROG_NAME);
}

/**
 * Parses the command line, setting options in `opts` as necessary.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line arguments.
 * @param[out] opts The options as set by the user via the command line.
 *
 * @return index in argv of the first argv-element that is not an option
 */
static int parse_command_line(int argc, char *argv[], test_options *opts) {
  assert(argc);
  assert(argv);
  assert(opts);

  // Zero out the options structure.
  memset(opts, 0, sizeof(test_options));

  /*
   * Don't have getopt print an error message when it finds an unknown option.
   * We'll take care of that ourselves by looking for '?'.
   */
  opterr = 0;

  // Parse the command line.
  int opt = '\0';
  while ((opt = getopt(argc, argv, "hvmp:u:")) != -1) {
    switch (opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case 'v':
        opts->verbose = 1;
        break;
      case 'u':
        strncpy(opts->host, optarg, sizeof(opts->host));
        break;
      case 'm':
        opts->is_master = 1; 
        break;
      case 'p':
        opts->port = atoi(optarg);
        break;
      case '?':
      default:
        usage_msg_exit("%s: Unknown option '%c'\n", PROG_NAME, opt);
    }
  }

  return optind;
}

#if 1

int test_dsm_master(void) {
  int host_len = strlen(OPTIONS.host);
  dsm *d = (dsm*)malloc(sizeof(dsm));

  memset(d, 0, sizeof(dsm)); // Needed for initial state to be 0
  d->port = OPTIONS.port;
  d->is_master = OPTIONS.is_master;
  memcpy(d->host, OPTIONS.host, 1+host_len);

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

int test_dsm_client_n() {
  int host_len = strlen(OPTIONS.host);
  dsm *d = (dsm*)malloc(sizeof(dsm));

  memset(d, 0, sizeof(dsm)); // Needed for initial state to be 0
  d->port = OPTIONS.port;
  d->is_master = OPTIONS.is_master;
  memcpy(d->host, OPTIONS.host, 1+host_len);
  
  if (dsm_init(d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  srand ( time(NULL) );

  for (int i = 0; i < 1000; i++) {
    int page = random()%4;
    snprintf(buffer + PAGESIZE * page, PAGESIZE, "host: %s port: %d", d->host, d->port);
    usleep(100000+random()%100000);
  }

  dsm_free(d, g_chunk_id);

  dsm_barrier_all(d);
cleanup:
  dsm_close(d);
  free(d); 
  
  return 0;
}

#elif 0
int test_dsm_master(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "page: 0 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "page: 1 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "page: 2 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "page: 3 host: %s port: %d", d.host, d.port);

  log("Done writing. Listening for requests from other nodes.\n");

  sleep(10);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client1(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);
  log("client waiting for some time...\n");
  sleep(1);

  log("client 1 writing page 2\n");
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "host: %s port: %d", d.host, d.port);
  log("client 1 writing page 3\n");
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("%s: client waiting for some time...\n", __func__);
  sleep(5);

  log("client 1 writing page 0\n");
  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "host: %s port: %d", d.host, d.port);
  log("client 1 writing page 1\n");
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("Done. Sleeping for while before exiting.\n");
  sleep(5);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client2(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port; // Client needs port to identify self
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE);
  if (buffer == NULL)
   goto cleanup; 

  log("%s: dsm_alloc buffer %p, %d\n", __func__, buffer, PAGESIZE);
  log("%s: client waiting for some time...\n", __func__);
  sleep(2);

  log("client 2 writing page 0\n");
  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "host: %s port: %d", d.host, d.port);
  log("client 2 writing page 1\n");
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("%s: client waiting for some time...\n", __func__);
  sleep(2);

  log("client 2 writing page 2\n");
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "host: %s port: %d", d.host, d.port);
  log("client 2 writing page 3\n");
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("Done. Sleeping for a while before exiting\n");
  sleep(5);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

#elif 0

int test_dsm_master(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "page: 0 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "page: 1 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "page: 2 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "page: 3 host: %s port: %d", d.host, d.port);

  log("Done writing. Listening for requests from other nodes.\n");

  sleep(20);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client1(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  char tmp[PAGESIZE];
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);
  log("client waiting for some time...\n");
  sleep(1);

  log("client 1 read page 0\n");
  memcpy(tmp, buffer + PAGESIZE * 0, PAGESIZE);
  log("page 0 read is: \"%s\"\n", tmp);

  log("client 1 writing page 1\n");
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("client 1 writing page 4\n");
  snprintf(buffer + PAGESIZE * 4, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("%s: client waiting for some time...\n", __func__);
  sleep(5);

  log("Done. Sleeping for while before exiting.\n");
  sleep(5);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client2(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  char tmp[PAGESIZE];
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port; // Client needs port to identify self
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 4*PAGESIZE);
  if (buffer == NULL)
   goto cleanup; 

  log("%s: dsm_alloc buffer %p, %d\n", __func__, buffer, PAGESIZE);
  log("%s: client waiting for some time...\n", __func__);
  sleep(2);

  log("client 1 read page 0\n");
  memcpy(tmp, buffer + PAGESIZE * 0, PAGESIZE);
  printf("page 0 read is: \"%s\"\n", tmp);

  log("client 1 writing page 2\n");
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("client 1 writing page 4\n");
  snprintf(buffer + PAGESIZE * 4, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("Done. Sleeping for a while before exiting\n");
  sleep(5);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

#elif 0

int test_dsm_master(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);

  snprintf(buffer + PAGESIZE * 0, PAGESIZE, "page: 0 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 1, PAGESIZE, "page: 1 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "page: 2 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "page: 3 host: %s port: %d", d.host, d.port);
  snprintf(buffer + PAGESIZE * 4, PAGESIZE, "page: 4 host: %s port: %d", d.host, d.port);

  log("Done writing. Listening for requests from other nodes.\n");

  sleep(30);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client1(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  char tmp[PAGESIZE];
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  log("dsm_alloc buffer %p\n", buffer);
  log("client waiting for some time...\n");
  sleep(1);

  log("client 1 read page 0\n");
  memcpy(tmp, buffer + PAGESIZE * 0, PAGESIZE);
  log("page 0 read is: \"%s\"\n", tmp);

  log("client 1 read page 1\n");
  memcpy(tmp, buffer + PAGESIZE * 1, PAGESIZE);
  log("page 0 read is: \"%s\"\n", tmp);

  log("client 1 writing page 2\n");
  snprintf(buffer + PAGESIZE * 2, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("client 1 writing page 4\n");
  snprintf(buffer + PAGESIZE * 4, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("%s: client waiting for some time...\n", __func__);
  sleep(5);

  log("Done. Sleeping for while before exiting.\n");
  sleep(50);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client2(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  char tmp[PAGESIZE];
  dsm d;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port; // Client needs port to identify self
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE);
  if (buffer == NULL)
   goto cleanup; 

  log("%s: dsm_alloc buffer %p, %d\n", __func__, buffer, PAGESIZE);
  log("%s: client waiting for some time...\n", __func__);
  sleep(2);

  log("client 1 read page 0\n");
  memcpy(tmp, buffer + PAGESIZE * 0, PAGESIZE);
  log("page 0 read is: \"%s\"\n", tmp);

  log("client 1 read page 1\n");
  memcpy(tmp, buffer + PAGESIZE * 1, PAGESIZE);
  log("page 0 read is: \"%s\"\n", tmp);

  log("client 1 writing page 3\n");
  snprintf(buffer + PAGESIZE * 3, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("client 1 writing page 4\n");
  snprintf(buffer + PAGESIZE * 4, PAGESIZE, "host: %s port: %d", d.host, d.port);

  log("Done. Sleeping for a while before exiting\n");
  sleep(30);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}
#else

#define MAX 100
#define MAX_CLIENTS 2

typedef struct matrix_info {
  int dummy;
  int start;
  int clients_done[MAX_CLIENTS];
} matrix_info_t;

typedef unsigned long matrix_type_t;

unsigned long matrix_mult[MAX][MAX];

int read_matrix(const char *mfile, unsigned long *buf)
{
  FILE *stream;
  unsigned long *m;
  unsigned long r, c;
  unsigned long i, j;
  int ret;

  stream = fopen(mfile, "r");
  if (!stream) {
    perror("fopen");
    return -errno;
  }

  ret = fscanf(stream, "%ld %ld", &r, &c);
  if (ret < 2) {
    perror("fscanf: r & c\n");
    fclose(stream);
    return -errno;
  }

  buf[0] = r;
  buf[1] = c;
  m = buf + 2;

  for (i = 0; i < r; i++) {
    for (j = 0; j < c; j++) {
      ret = fscanf(stream, "%ld", m + c * i + j);
      if (ret < 1) {
        perror("fscanf: element\n");
        fclose(stream);
        return -errno;
      }
    }
  }

  printf("%ld %ld\n", r, c);
  for (i = 0; i < r; i++) {
    for (j = 0; j < c; j++) {
      printf("%ld ", *(m + c * i + j));
    }
    printf("\n");
  }
  printf("\n");

  fclose(stream);
  return 0;
}

/**
 * page 0: matrix info
 * page 1: matrix1
 * page 2: matrix2
 * page 3: matrix part 1
 * page 4: matrix part 2
 */

int test_dsm_master(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;
  matrix_info_t *minfo;
  int ret;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE); 
  if (buffer == NULL)
    goto cleanup; 

  printf("dsm_alloc buffer %p\n", buffer);

  minfo = (matrix_info_t *)buffer;
  memset(minfo, 0, sizeof(*minfo));

  printf("Matrix 1\n");
  ret = read_matrix("matrix1.txt", (unsigned long*)(buffer + PAGESIZE * 1));
  if (ret)
    goto cleanup;

  printf("Matrix 2\n");
  ret = read_matrix("matrix2.txt", (unsigned long*)(buffer + PAGESIZE * 2));
  if (ret)
    goto cleanup;

  minfo->start = 1;
  printf("Start matrix multtiplication(%d)\n", minfo->start);

  while (1) {
    int i;
    int flag = 1;

    for (i = 0; i < MAX_CLIENTS; i++) {
      flag &= minfo->clients_done[i];
      printf("client=%d done=%d\n", i, minfo->clients_done[i]);
    }
    if (flag)
      break;

    printf("Waiting for clients to finish matrix multiplication\n");
    sleep(random() % 10);
  }

  unsigned long *m;
  unsigned long *__buf1 = (unsigned long*)(buffer + PAGESIZE * 3);
  unsigned long m_row = __buf1[0];
  unsigned long *__buf2 = (unsigned long*)(buffer + PAGESIZE * 4);
  unsigned long m_col = __buf2[1];
  unsigned long i, j;

  printf("%ld %ld\n", m_row, m_col);
  m = __buf1 + 2;
  for (i = 0; i < m_row; i++) {
    if (i == m_row / 2)
      m = __buf2 + 2;
    for (j = 0; j < m_col; j++) {
      printf("%ld ", *(m + m_col * i + j));
    }
    printf("\n");
  }
  printf("\n");
  
  log("Done writing. Listening for requests from other nodes.\n");
  fflush(stdout);
  sleep(20);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client1(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;
  matrix_info_t *minfo;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port;
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE); 
  if (buffer == NULL)
   goto cleanup; 

  printf("%s: dsm_alloc buffer %p, %d\n", __func__, buffer, PAGESIZE);

  minfo = (matrix_info_t *)buffer;

  while (1) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
      printf("%s: client=%d done=%d\n", __func__, i, minfo->clients_done[i]);
    }
    
    printf("%s: Waiting for master to start matrix multiplication (start=%d)\n",
           __func__, minfo->start);

    if (minfo->start)
      break;

    sleep(random() % 10);
  }

  unsigned long *__buf1 = (unsigned long*)(buffer + PAGESIZE * 1);
  unsigned long m1_row = __buf1[0];
  unsigned long m1_col = __buf1[1];
  unsigned long *m1 = __buf1 + 2;

  unsigned long *__buf2 = (unsigned long*)(buffer + PAGESIZE * 2);
  unsigned long m2_row = __buf2[0];
  unsigned long m2_col = __buf2[1];
  unsigned long *m2 = __buf2 + 2;

  unsigned long *__buf = (unsigned long*)(buffer + PAGESIZE * 3);
  unsigned long m_row = __buf[0] = m1_row;
  unsigned long m_col = __buf[1] = m2_col;
  unsigned long *m = __buf + 2;

  unsigned long i, j, k, tmp;

  printf("%s: Starting matrix multiplication\n", __func__);

  for (i = 0; i < m1_row / 2; i++) {
    for (j = 0; j < m2_col; j++) {
      tmp = 0;
      for (k = 0; k < m2_row; k++) {
        tmp += *(m1 + i * m1_col + k) * *(m2  + k * m2_col + j);
      }
      *(m + i * m2_col + j) = tmp;
    }
  }

  printf("%s: Starting matrix multiplication done.\n", __func__);

  printf("%ld %ld\n", m_row, m_col);
  for (i = 0; i < m_row / 2; i++) {
    for (j = 0; j < m_col; j++) {
      printf("%ld ", *(m + m_col * i + j));
    }
    printf("\n");
  }
  printf("\n");

  minfo->clients_done[0] = 1;
  printf("%s: Setting matrix multiplication done.\n", __func__);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    printf("%s: client=%d done=%d\n", __func__, i, minfo->clients_done[i]);
  }
    
  printf("%s: Waiting for master to start matrix multiplication (start=%d)\n",
         __func__, minfo->start);

  printf("%s: Done. Sleeping for while before exiting.\n", __func__);
  fflush(stdout);
  sleep(10);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

int test_dsm_client2(void) {
  const char *host = "localhost";
  int host_len = strlen(host);
  dsm d;
  matrix_info_t *minfo;

  memset(&d, 0, sizeof(d)); // Needed for initial state to be 0
  d.port = OPTIONS.port; // Client needs port to identify self
  d.is_master = OPTIONS.is_master;
  memcpy(d.host, host, 1+host_len);
  if (dsm_init(&d) < 0)
    return 0;

  char *buffer = (char*)dsm_alloc(&d, g_chunk_id, 5*PAGESIZE);
  if (buffer == NULL)
   goto cleanup; 

  printf("%s: dsm_alloc buffer %p, %d\n", __func__, buffer, PAGESIZE);

  minfo = (matrix_info_t *)buffer;

  while (1) {
    if (minfo->start)
      break;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      printf("%s: client=%d done=%d\n", __func__, i, minfo->clients_done[i]);
    }
    
    printf("%s: Waiting for master to start matrix multiplication (start=%d)\n",
           __func__, minfo->start);
    sleep(random() % 10);
  }

  unsigned long *__buf1 = (unsigned long*)(buffer + PAGESIZE * 1);
  unsigned long m1_row = __buf1[0];
  unsigned long m1_col = __buf1[1];
  unsigned long *m1 = __buf1 + 2;

  unsigned long *__buf2 = (unsigned long*)(buffer + PAGESIZE * 2);
  unsigned long m2_row = __buf2[0];
  unsigned long m2_col = __buf2[1];
  unsigned long *m2 = __buf2 + 2;

  unsigned long *__buf = (unsigned long*)(buffer + PAGESIZE * 4);
  unsigned long m_row = __buf[0] = m1_row;
  unsigned long m_col = __buf[1] = m2_col;
  unsigned long *m = __buf + 2;

  unsigned long i, j, k, tmp;

  printf("%s: Starting matrix multiplication\n", __func__);

  for (i = m1_row / 2; i < m1_row; i++) {
    for (j = 0; j < m2_col; j++) {
      tmp = 0;
      for (k = 0; k < m2_row; k++) {
        tmp += *(m1 + i * m1_col + k) * *(m2  + k * m2_col + j);
      }
      *(m + i * m2_col + j) = tmp;
    }
  }

  printf("%s: Starting matrix multiplication done.\n", __func__);

  printf("%ld %ld\n", m_row, m_col);
  for (i = m_row / 2; i < m_row; i++) {
    for (j = 0; j < m_col; j++) {
      printf("%ld ", *(m + m_col * i + j));
    }
    printf("\n");
  }
  printf("\n");

  minfo->clients_done[1] = 1;
  printf("%s: Setting matrix multiplication done.\n", __func__);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    printf("%s: client=%d done=%d\n", __func__, i, minfo->clients_done[i]);
  }
    
  printf("%s: Waiting for master to start matrix multiplication (start=%d)\n",
         __func__, minfo->start);

  printf("%s: Done. Sleeping for a while before exiting\n", __func__);
  fflush(stdout);
  sleep(10);

  dsm_free(&d, g_chunk_id);

cleanup:
  dsm_close(&d);
  
  return 0;
}

#endif

int main(int argc, char *argv[]) {
  int i;
  PROG_NAME = argv[0];

  // Parse the command line and check that a string lives in argv
  int index = parse_command_line(argc, argv, &OPTIONS);
  if (argc < index) {
      usage_msg_exit("%s: wrong arguments\n", PROG_NAME);
  }

  // configuration file is should be in the 
  // current directory before execution
  FILE *fp = fopen("dsm.conf", "r");
  int n = 0;
  if (fscanf(fp, "%d", &n) == EOF) {
    log("Error parsing conf file\n");
    return 0;
  }

  int ports[n];
  for (i = 0; i < n; i++) {
    char host[256];
    char is_master;
    if (fscanf(fp, "%c", &is_master) == EOF)
      return 0;

    if (fscanf(fp, "%c %s %d", &is_master, host, &ports[i]) == EOF) {
      log("Error parsing conf file\n");
      return 0;
    }
  }

  fclose(fp);

  if (OPTIONS.is_master)
    test_dsm_master();
  else
    test_dsm_client_n();

  /*
  if (OPTIONS.is_master)
    test_dsm_master();
  else if (OPTIONS.port == ports[1])
    test_dsm_client1();
  else if (OPTIONS.port == ports[2])
    test_dsm_client2();
  */

  return 0; 
}
