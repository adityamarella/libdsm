#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "utils.h"
#include "dsm.h"

#define STATS 1
#define ARR(A,cols,i,j) *((double*)(A + i*cols) + j)

#ifdef STATS
#define START_TIMING(a) a=current_us()
#else
#define START_TIMING(a)
#endif

#ifdef STATS
#define END_TIMING(a) a=current_us()-a
#else
#define END_TIMING(a)
#endif

/**
 * Get seconds from epoch
 */
long long current_us() {
  struct timeval te;
  gettimeofday(&te, NULL);
  return (long long) te.tv_sec * 1000000 + te.tv_usec;
}

/**
 * Utility to print matrix
 */
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

/**
 * Utility to generate matrix
 */
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

#ifdef STATS
  long long talloc=0, tbarrier=0, tprintA=0, tprintB=0, tcompute=0, tfree=0, tclose=0, ttotal=0;
#endif

  dsm *d;

  m = n = p = 100;


  START_TIMING(ttotal);
  d = (dsm*)malloc(sizeof(dsm));
  memset(d, 0, sizeof(dsm));
  dsm_init(d, host, port, is_master);

  START_TIMING(talloc);
  // allocate shared memory 
  A = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
  B = (double*)dsm_alloc(d, cid++, n*p*sizeof(double));
  C = (double*)dsm_alloc(d, cid++, m*n*sizeof(double));
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
  print_matrix(A, m, n);
  END_TIMING(tprintA);
  START_TIMING(tprintB);
  print_matrix(B, n, p);
  END_TIMING(tprintB);
  
  // multiply the assigned partition
  psz = 1 + (m - 1) / nnodes;
  pb = node_id * psz;
  
  START_TIMING(tcompute);
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
  END_TIMING(tcompute);

  // free local memory
  for (i = 0; i < m; i++)
    free(result[i]);
  free(result);

  // free global shared memory
  START_TIMING(tfree);
  for (i = 0; i < cid; i++)
    dsm_free(d, i);
  END_TIMING(tfree);

  START_TIMING(tclose);
  dsm_close(d);
  END_TIMING(tclose);

  free(d);
  END_TIMING(ttotal);
  
  // multiply locally
  if (node_id == 0) {
    int flag = 1;
    double *lC = multiply_locally(lA, lB, m, n, p);
    for (i = 0; i < m; i++) {
      for (j = 0; j < p; j++) {
        if (*((double*)(C + i*p) + j) != *((double*)(lC + i*p) + j)) {
          flag = 0;
          goto OUT_FALSE;
        }
      }
    }
OUT_FALSE:
    if (flag) printf("Success.\n");
    else printf("Failed.\n");
    free(lC);
  }
  free(lA); free(lB);

#ifdef STATS
  printf("----------STATS--------\n");
  printf("alloc %lldus.\n", talloc);
  printf("barrier %lldus.\n", tbarrier);
  printf("printA %lldus.\n", tprintA);
  printf("printB %lldus.\n", tprintB);
  printf("compute %lldus.\n", tcompute);
  printf("free %lldus.\n", tfree);
  printf("close %lldus.\n", tclose);
  printf("total %lldus.\n", ttotal);
  printf("----------STATS--------\n");
#endif

  return 0;
}
