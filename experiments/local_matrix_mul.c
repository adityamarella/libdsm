#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>


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

/**
 * Get seconds from epoch
 */
static
long long current_us() {
  struct timeval te;
  gettimeofday(&te, NULL);
  return (long long) te.tv_sec * 1000000 + te.tv_usec;
}

/**
 * Utility to print matrix
 */
static
void print_matrix(double *m, int row, int col)
{
  printf("row:%d col:%d\n\n", row, col);
  int i, j;
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      printf("%lf ", *((double*)(m + i*col) + j));
    }
    printf("\n");
  }
  printf("\n");
}

static
void generate_matrix(double **lm, int row, int col)
{
  int i, j;
  srandom(time(NULL));
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
     lm[i][j] = (double)(random()%1000000) / (double)(random()%1000);
    }
  }
}

void multiply_locally(double **lA, double **lB, double **lC, int m, int n, int p) {
  int i, j, k;
  #pragma omp parallel for private(i) 
  for (i = 0; i < m; i++) {
    for (j = 0; j < p; j++) {
      for(k = 0; k < n; k++) {
        lC[i][j] += lA[i][k] * lB[k][j];
      }
    }
  }
}

int main(int argc, char **argv) {
  int i, m, n, p;

  double **lA, **lB, **lC;
#ifdef _MUL_STATS
  long long tcompute=0, ttotal=0;
#endif

  m = n = p = atoi(argv[1]);

  printf("%d, %d, %d\n", m, n, p);

  START_TIMING(ttotal);

  // allocate local memory 
  lA = (double**)calloc(m, sizeof(double*));
  lB = (double**)calloc(n, sizeof(double*));
  lC = (double**)calloc(m, sizeof(double*));

  for (i = 0; i < m; i++) {
    lA[i] = (double*)calloc(n, sizeof(double));
    lC[i] = (double*)calloc(p, sizeof(double));
  }
  
  for (i = 0; i < m; i++)
    lB[i] = (double*)calloc(p, sizeof(double));

  // initialize input matrices
  generate_matrix(lA, m, n);
  generate_matrix(lB, n, p);
  
  START_TIMING(tcompute);
  multiply_locally(lA, lB, lC, m, n, p);
  END_TIMING(tcompute);
  
  free(lC);
  free(lA); free(lB);

  END_TIMING(ttotal);
#ifdef _MUL_STATS
  printf("----------_MUL_STATS--------\n");
  printf("compute %lldus.\n", tcompute);
  printf("total %lldus.\n", ttotal);
  printf("----------_MUL_STATS--------\n");
#endif
  return 0;
}
