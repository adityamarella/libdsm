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

// The name the program was invoked with.
static const char *PROG_NAME;

// Server options structure
static test_options OPTIONS;

static const int g_chunk_id = 10;

volatile double tmp = 0;

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
    "  -u     provide host name with this option\n",
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
  opterr = 0;

  // Parse the command line.
  int opt = '\0';
  while ((opt = getopt(argc, argv, "hvmi:p:u:")) != -1) {
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
      case 'i':
        opts->node_id = atoi(optarg);
        break;
      case '?':
      default:
        usage_msg_exit("%s: Unknown option '%c'\n", PROG_NAME, opt);
    }
  }
  return optind;
}

int main(int argc, char *argv[]) {
  PROG_NAME = argv[0];

  // Parse the command line and check that a string lives in argv
  int index = parse_command_line(argc, argv, &OPTIONS);
  if (argc < index) {
      usage_msg_exit("%s: wrong arguments\n", PROG_NAME);
  }

  dsm_conf c;
  if (dsm_conf_init(&c, "dsm.conf", OPTIONS.host, OPTIONS.port) < 0) {
    print_err("Error parsing conf file\n");
    return -1;
  }

  //test_ping_pong(OPTIONS.host, OPTIONS.port, c.num_nodes, OPTIONS.is_master);
  //test_matrix_mul(OPTIONS.host, OPTIONS.port, OPTIONS.node_id, c.num_nodes, OPTIONS.is_master);
  profile(OPTIONS.host, OPTIONS.port, OPTIONS.node_id, c.num_nodes, OPTIONS.is_master);

  dsm_conf_close(&c);
  return 0; 
}
