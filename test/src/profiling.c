#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "utils.h"
#include "dsm.h"

#define _MUL_STATS 1
#define ARR(A,cols,i,j) *((double*)(A + i*cols) + j)

#ifdef _MUL_STATS
#define START_TIMING(a) a=current_us()
#else
#define START_TIMING(a)
#endif

#ifdef _MUL_STATS
#define END_TIMING(a) a=current_us()-a
#else
#define END_TIMING(a)
#endif

extern volatile double tmp;

/**
 * Utility to print matrix
 */
void write_matrix(double *m, int row, int col)
{
  int i, j;
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      *((double*)(m + i*col) + j) = random() % 100;
    }
  }
}

/**
 * Utility to access matrix
 */
static
void access_matrix(double *m, int row, int col)
{
  for (int i = 0; i < row*col; i++) {
      tmp = *(m + i);
  }
  printf("row %f,%d,%d\n", tmp, row, col);
  printf("returning from access matrix\n");
}

/**
 * Utility to generate matrix
 */
static
void generate_matrix(double *m, int row, int col)
{
  int i, j;
  srandom(time(NULL));
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      *(m + i*col + j) = random() % 10;
    }
  }
}

int profile(const char* host, int port, int node_id, int nnodes, int is_master) {
  UNUSED(nnodes);
  double *A;
  int i, j, m, n;
  int cid = 0;
  dsm *d;
#ifdef _MUL_STATS
  long long talloc=0, tbarrier=0, treadfault=0, twritefault=0, tfree=0, tclose=0, ttotal=0;
#endif

  m = n = 1024;

  START_TIMING(ttotal);
  d = (dsm*)malloc(sizeof(dsm));
  memset(d, 0, sizeof(dsm));
  dsm_init(d, host, port, is_master);

  START_TIMING(talloc);
  // allocate shared memory 
  A = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
  END_TIMING(talloc);
 
  // initialize input matrices
  if (node_id == 0) {
    generate_matrix(A, m, n);
  }
  
  // all nodes should reach this point before proceeding
  START_TIMING(tbarrier);
  dsm_barrier_all(d);
  END_TIMING(tbarrier);
 
  // generate read fault
  START_TIMING(treadfault);
  access_matrix(A, m, n);
  END_TIMING(treadfault);

  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);

  // generate write fault
  //START_TIMING(twritefault);
  //write_matrix(A, m, n);
  //END_TIMING(twritefault);

  // free global shared memory
  START_TIMING(tfree);
  for (i = 0; i < cid; i++)
    dsm_free(d, i);
  END_TIMING(tfree);

  START_TIMING(tclose);
  dsm_close(d);
  END_TIMING(tclose);

  free((void*)d);
  END_TIMING(ttotal);
  
#ifdef _MUL_STATS
  printf("----------_MUL_STATS--------\n");
  printf("alloc %lldus.\n", talloc);
  printf("barrier %lldus.\n", tbarrier);
  printf("readfault %lldus.\n", treadfault);
  printf("writefault %lldus.\n", twritefault);
  printf("free %lldus.\n", tfree);
  printf("close %lldus.\n", tclose);
  printf("total %lldus.\n", ttotal);
  printf("----------_MUL_STATS--------\n");
#endif

  return 0;
}
