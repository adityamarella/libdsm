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
/*static
void print_matrix(double *m, int row, int col)
{
  printf("row:%d col:%d\n\n", row, col);
  printf("generate read fault: %lf\n\n", (double)*m);
  
  int i, j;
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      printf("%lf ", *((double*)(m + i*col) + j));
    }
    printf("\n");
  }
  printf("\n");
}*/

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

/**
 * Multipy the matrics locally. To cross check the output and also to compare the time taken
 * for the computation.
 *
 * @param lA matrix A
 * @param lB matrix B
 * @param m number of rows in A
 * @param n number of cols in A OR rows in B
 * @param p number of cols in B
 */
double* multiply_locally(double *lA, double *lB, int m, int n, int p) {
  int i, j, k;
  double *lC;
  
  lC = (double*)calloc(m*p, sizeof(double));
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
double* multiply_partition(double *A, double *B, int m, int n, int p, int pb, int psz) {
  int i, j, k;
  volatile double *C;
  volatile double a, b;

  C = (volatile double*)calloc(psz*p, sizeof(double));

  for (i = pb; i < pb + psz && i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        a = ARR(A, n, i, k);
        b = ARR(B, p, k, j);
        C[i*p -pb*p + j] += a * b; 
      }
    }
  }
  return (double*)C;
}

int test_matrix_mul(const char* host, int port, int node_id, int nnodes, int is_master) {
  UNUSED(nnodes);
  double *A, *B, *C;
  double *lA, *lB; // local memory for profiling
  int i, j, m, n, p, psz, pb;
  int cid = 0;

#ifdef _MUL_STATS
  long long talloc=0, tbarrier=0, tprintA=0;
  long long tprintB=0, tcompute=0, tfree=0;
  long long tclose=0, ttotal=0, tlcompute=0;
#endif

  dsm *d;

  m = n = p = 1024;

  START_TIMING(ttotal);
  d = (dsm*)malloc(sizeof(dsm));
  memset(d, 0, sizeof(dsm));
  dsm_init(d, host, port, is_master);

  START_TIMING(talloc);
  // allocate shared memory 
  A = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
  B = (double*)dsm_alloc(d, cid++, n*p*sizeof(double));
  C = (double*)dsm_alloc(d, cid++, m*p*sizeof(double));
  END_TIMING(talloc);
 
  // allocate local memory 
  lA = (double*)malloc(m*n*sizeof(double));
  lB = (double*)malloc(n*p*sizeof(double));

  // initialize input matrices
  if (node_id == 0) {
    generate_matrix(A, lA, m, n);
    generate_matrix(B, lB, n, p);
  }
  
  // all nodes should reach this point before proceeding
  START_TIMING(tbarrier);
  dsm_barrier_all(d);
  END_TIMING(tbarrier);
 
  // do this to generate read fault 
  START_TIMING(tprintA);
  access_matrix(A, m, n);
  END_TIMING(tprintA);
  START_TIMING(tprintB);
  access_matrix(B, n, p);
  END_TIMING(tprintB);

  // multiply the assigned partition
  psz = 1 + (m - 1) / nnodes;
  pb = node_id * psz;
  
  START_TIMING(tcompute);
  double *result = multiply_partition(A, B, m, n, p, pb, psz);

  // finally copy the result into shared memory
  for (i = pb*p; i < pb*p + psz*p && i < m*p; i++) {
    *(C + i) = result[i-pb*p];
  }
  
  // free local memory
  free(result);
  
  // all nodes should reach this point before proceeding
  dsm_barrier_all(d);
  if (node_id == 0)
    access_matrix(C, m, p);
  END_TIMING(tcompute);

  // multiply locally to verify computation
  if (node_id == 0) {
    int flag = 1;
    printf("Computing locally.\n");
    START_TIMING(tlcompute);
    double *lC = multiply_locally(lA, lB, m, n, p);
    END_TIMING(tlcompute);
    for (i = 0; i < m*p; i++) {
      if (*(C + i) != *(lC + i)) {
        flag = 0;
        goto OUT_FALSE;
      }
    }
OUT_FALSE:
    if (flag) printf("Success.\n");
    else printf("Failed.\n");
    free(lC);
  }
  free(lA); free(lB);
  
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
  printf("printA %lldus.\n", tprintA);
  printf("printB %lldus.\n", tprintB);
  printf("compute %lldus.\n", tcompute);
  printf("local compute %lldus.\n", tlcompute);
  printf("free %lldus.\n", tfree);
  printf("close %lldus.\n", tclose);
  printf("total %lldus.\n", ttotal);
  printf("----------_MUL_STATS--------\n");
#endif

  return 0;
}
