#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "utils.h"
#include "dsm.h"

extern volatile double tmp;

/**
 * Utility to access matrix
 */
static
void access_matrix(double *m, int r, int c)
{
  int i, j;
  for (i = 0; i < r*c; i++) {
      tmp = *(m + i);
  }
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
      m[i*col + j] = (double)(random() % 31) / (double)(random() % 17);
    }
  }
}

/**
 * This method multiplies a partition of A(mxn) and B(nxp) matrices
 */
static 
double* multiply_partition(double *A, double *B, int m, int n, int p, int pstart, int pend) {
  int i, j, k;
  double *result;
  double a, b;
  
  printf("Partition pstart=%d, pend=%d\n", pstart, pend);
  result = (double*)calloc(m*p, sizeof(double));
  for (i = pstart; i < pend && i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        a = A[n*i + k];
        b = B[p*k + j];
        result[i*p + j] += a * b; 
      }
    }
  }
  printf("Multiply partition done\n");
  return (double*)result;
}

int demo_matrix_mul(const char* host, int port, int node_id, int nnodes, int is_master) {
  UNUSED(nnodes);
  double *A, *B, *C;
  int i, j, m, n, p, pstart, pend;
  int cid = 0;
  
  m = n = p = 1024;

  dsm *d = (dsm*)calloc(1, sizeof(dsm));
 
  // initialize 
  dsm_init(d, host, port, is_master);

  // allocate shared memory 
  A = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
  B = (double*)dsm_alloc(d, cid++, n*p*sizeof(double));
  C = (double*)dsm_alloc(d, cid++, m*p*sizeof(double));
 
  // initialize input matrices
  if (node_id == 0) {
    generate_matrix(A, m, n);
    generate_matrix(B, n, p);
  }
  
  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);
 
  // do this to generate read fault 
  access_matrix(A, m, n);
  access_matrix(B, n, p);

  dsm_barrier_all(d);
  
  // multiply the assigned partition
  pstart = (    node_id ) * m / nnodes;
  pend =   (1 + node_id ) * m / nnodes;
  
  double *result = multiply_partition(A, B, m, n, p, pstart, pend);

  // finally copy the result into shared memory
  for (i = pstart*p; i < pend*p && i < m*p; i++)
    C[i] = result[i];
  
  // free local memory
  free(result);
  
  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);

  // free global shared memory
  for (i = 0; i < cid; i++)
    dsm_free(d, i);

  // close dsm
  dsm_close(d);

  // free dsm memory
  free((void*)d);
  return 0;
}
