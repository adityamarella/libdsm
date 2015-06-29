#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "dsm.h"

#define ARR(A,cols,i,j) *((double*)(A + i*cols) + j)

void print_matrix(double *m, int row, int col)
{
  int i, j;
  printf("row:%d col:%d\n\n", row, col);
  printf("generate read fault: %lf\n\n", (double)*m);
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      printf("%lf ", *((double*)(m + i*col) + j));
    }
    printf("\n");
  }
  printf("\n");
}

void generate_matrix(double *m, double *lm, int row, int col)
{
  int i, j;
  srandom(time(NULL));
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
     *((double*)(lm + i*col) + j) = *((double*)(m + i*col) + j) = random() % 10;
    }
  }
}

double* multiply_locally(double *lA, double *lB, int m, int n, int p) {
  int i, j, k;
  double *lC;
  
  lC = (double*)malloc(m*p*sizeof(double));
  for (i = 0; i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        ARR(lC, p, i, j) += ARR(lA, n, i, k) * ARR(lB, p, k, j);
      }
    }
  }
  return lC;
}

/**
 * This method multiplies on partition of A(mxn) and B(nxp) matrices and stores the 
 * result in C(pszxp).
 *
 * psz - stands for partition size
 * pb - stands for partition begin idx
 *
 * Partitioning is done on A's row dimension. Each node takes one partition of A(psz x n) 
 * and multiplies it with B(nxp). Note that the result of this computation directly goes into the 
 * corresponding partition of the final output C
 *
 * Assumption: 
 */
double** multiply_partition(double *A, double *B, int m, int n, int p, int pb, int psz) {
  int i, j, k;
  double **C;

  C = (double**)calloc(psz, sizeof(double*));
  for (i = 0; i < psz; i++) {
    C[i] = (double*)calloc(p, sizeof(double));
  }

  for (i = pb; i < pb + psz && i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        C[i - pb][j] += ARR(A, n, i, k) * ARR(B, p, k, j);
      }
      printf("\n");
    }
    printf("completed C[%d] \n\n", i);
  }
  return C;
}

int test_matrix_mul(const char* host, int port, int node_id, int nnodes, int is_master) {
  double *A, *B, *C;
  double *lA, *lB; // local memory for profiling
  int i, j, m, n, p, psz, pb;
  int cid = 0;
  dsm *d;

  m = n = p = 3;

  d = (dsm*)malloc(sizeof(dsm));
  memset(d, 0, sizeof(dsm));
  dsm_init(d, host, port, is_master);

  // allocate shared memory 
  A = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
  B = (double*)dsm_alloc(d, cid++, n*p*sizeof(double));
  C = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
 
  // allocate local memory 
  lA = (double*)malloc(m*n*sizeof(double));
  lB = (double*)malloc(n*p*sizeof(double));

  // initialize input matrices
  if (node_id == 0) {
    generate_matrix(A, lA, m, n);
    generate_matrix(B, lB, n, p);
  }
  
  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);
 
  // do this to generate read fault 
  print_matrix(A, m, n);
  print_matrix(B, n, p);
  
  // multiply the assigned partition
  psz = 1 + (m - 1) / nnodes;
  pb = node_id * psz;
  double **result = multiply_partition(A, B, m, n, p, pb, psz);

  // finally copy the result into shared memory
  for (i = pb; i < pb + psz && i < m; i++) {
    for (j = 0; j < p; j++) {
      *((double*)(C + i*p) + j) = result[i - pb][j];
    }
  }
  
  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);
  print_matrix(C, m, p);

  // free local memory
  for (i = 0; i < m; i++)
    free(result[i]);
  free(result);


  // free global shared memory
  for (i = 0; i < cid; i++)
    dsm_free(d, i);
  dsm_close(d);
  free(d);
  
  // multiply locally
  if (node_id == 0) {
    double *lC = multiply_locally(lA, lB, m, n, p);
    print_matrix(lC, m, p);
    free(lC);
  }
  free(lA); free(lB); 
  return 0;
}
