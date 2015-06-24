#include <stdio.h>
#include <stdlib.h>
//#include "utils.h"
//#include "dsm.h"

typedef int dsm;
#define dsm_alloc(a,b,c) malloc(c)
#define dsm_free(a,b) free(b)
#define dsm_init(a) do {int __a=0;}while(0)
#define dsm_close(a) do {int __a=0;}while(0)

int read_matrices(char *filepath, double **A, double **B, int *m, int *n, int *p) {

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
        C[i - pb][j] += A[i][k] + B[k][j];
      }
    }
  }
  return C;
}

int test_matrix_mul(int nnodes, int is_master) {
  double **A, **B, **C;
  int m, n, p;
  int *g_node_id, node_id;
  int cid = 0;
  dsm *d;
 
  d = (dsm*)malloc(sizeof(dsm));
  dsm_init(d);

  *g_node_id = (int*)dsm_alloc(d, cid++, sizeof(int));

  // dsm_alloc always returns zeroed out memory
  // this innocuous looking line of code will ensure that each node
  // has a different node id;
  *g_node_id = *g_node_id + 1;
  node_id = *g_node_id;

  printf("node id : %d\n", node_id);
 
  A = (double**)dsm_alloc(d, cid++, m*sizeof(double*));
  B = (double**)dsm_alloc(d, cid++, n*sizeof(double*));
  C = (double**)dsm_alloc(d, cid++, m*sizeof(double*));

  for (i = 0; i < m; i++) {
    A[i] = (double*)dsm_alloc(d, cid++, n*sizeof(double));
    C[i] = (double*)dsm_alloc(d, cid++, p*sizeof(double));
  }
  
  for (i = 0; i < n; i++)
    B[i] = (double*)dsm_alloc(d, cid++, p*sizeof(double));

  if (node_id == 0) {
    FILE *fp = fopen(filepath, "r");
    fscanf(fp, "%d%d%d", &m, &n, &p);
    fclose(fp);
  }

  psz = (m + nnodes - 1) / nnodes;
  pb = node_id * psz;
  double **result = multiply_partition(A, B, m, n, p, pb, psz);

  // finally copy the result into shared memory
  for (i = pb; i < pb + psz && i < m; i++)
    for (j = 0; j < p; j++)
      C[i][j] = result[i-pb][j];

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

