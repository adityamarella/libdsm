#include <stdio.h>
#include <stdlib.h>
//#include "utils.h"
//#include "dsm.h"

typedef int dsm;
#define dsm_alloc(a, b, c) malloc(c)
#define dsm_free(a, b) do {int __a=0;}while(0)
#define dsm_init(a) do {int __a=0;}while(0)
#define dsm_close(a) do {int __a=0;}while(0)

void print_matrix(double **m, int row, int col)
{
  int i, j;

  printf("row:%d col:%d\n\n", row, col);
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      printf("%f ", m[i][j]);
    }
    printf("\n");
  }
  printf("\n");
}

void generate_matrix(double **m, int row, int col)
{
  int i, j;
  srandom(time(NULL));
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      m[i][j] = random() % 10;
    }
  }
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
double** multiply_partition(double **A, double **B, int m, int n, int p, int pb, int psz) {
  int i, j, k;
  double **C;

  C = (double**)calloc(m, sizeof(double*));
  for (i = 0; i < psz; i++) {
    C[i] = (double*)calloc(p, sizeof(double));
  }

  for (i = pb; i < pb + psz && i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        C[i - pb][j] += A[i][k] * B[k][j];
      }
    }
  }
  return C;
}

int test_matrix_mul(int nnodes) {
  double **A, **B, **C;
  int i, j, m, n, p, psz, pb;
  int *g_node_id, node_id;
  int cid = 0;
  dsm *d;

  m = n = p = 3;

  d = (dsm*)malloc(sizeof(dsm));
  dsm_init(d);

  g_node_id = (int*)dsm_alloc(d, cid++, sizeof(int));

  // dsm_alloc always returns zeroed out memory
  // this innocuous looking statement will ensure that each node
  // has a different node id;
  *g_node_id = *g_node_id + 1;
  node_id = *g_node_id - 1;

  printf("node id : %d\n", node_id);

  // allocate shared memory 
  A = (double**)dsm_alloc(d, cid++, m*sizeof(double*));
  B = (double**)dsm_alloc(d, cid++, n*sizeof(double*));
  C = (double**)dsm_alloc(d, cid++, m*sizeof(double*));
  for (i = 0; i < m; i++) {
    A[i] = (double*)dsm_alloc(d, cid++, n*sizeof(double));
    C[i] = (double*)dsm_alloc(d, cid++, p*sizeof(double));
  }
  for (i = 0; i < n; i++)
    B[i] = (double*)dsm_alloc(d, cid++, p*sizeof(double));

  // initialize input matrices
  if (node_id == 0) {
    generate_matrix(A, m, n);
    generate_matrix(B, n, p);
  }

  // multiply the assigned partition
  psz = 1 + (m - 1) / nnodes;
  pb = node_id * psz;
  double **result = multiply_partition(A, B, m, n, p, pb, psz);

  // finally copy the result into shared memory
  for (i = pb; i < pb + psz && i < m; i++)
    for (j = 0; j < p; j++)
      C[i][j] = result[i-pb][j];

  print_matrix(A, m, n);
  print_matrix(B, n, p);
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
}

int main() {
  test_matrix_mul(1, 1);
}

