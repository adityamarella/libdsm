#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/time.h>

#include "main.h"
#include "utils.h"
#include "dsm.h"
#include "request.h"
#include "reply_handler.h"
#include "dsm_server.h"

#define PROG_NAME "matrix_gen"

static void usage() {
  fprintf(stderr,
          "Usage: %s <m1_row> <m1_col> <m2_row> <m2_col> <1>\n"
          "  m1_row : # of m1 rows\n"
          "  m1_col : # of m1 columns\n"
          "  m2_row : # of m2 rows\n"
          "  m2_col : # of m2 columns\n"
          "  1      : print matrices\n",
          PROG_NAME);
}

#define NUM_LIMIT 10000

unsigned long *generate_matrix(unsigned long row, unsigned long col)
{
  unsigned long *m;
  unsigned long i, j;

  m = malloc(row * col * sizeof(unsigned long));
  if (!m) {
    printf("Failed to allocate memory for matrix\n");
    return NULL;
  }

  sleep(1); // to let the time change

  srandom(time(NULL));
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      *(m + i * col + j)= random() % NUM_LIMIT;
    }
  }

  return m;
}

void print_matrix(unsigned long *m, unsigned long row, unsigned long col)
{
  unsigned long i, j;

  printf("row:%ld col:%ld\n\n", row, col);
  for (i = 0; i < row; i++) {
    for (j = 0; j < col; j++) {
      printf("%ld ", *(m + col * i + j));
    }
    printf("\n");
  }
  printf("\n");
}

int write_matrix(const char *mfile, unsigned long *m, unsigned long r, unsigned long c)
{
  FILE *stream;
  unsigned long i, j;
  int ret;

  stream = fopen(mfile, "w");
  if (!stream) {
    perror("fopen");
    return -errno;
  }

  ret = fprintf(stream, "%lu %lu\n", r, c);
  if (ret < 0) {
    perror("fprintf: r & c\n");
    fclose(stream);
    return -errno;
  }

  for (i = 0; i < r; i++) {
    for (j = 0; j < c; j++) {
      ret = fprintf(stream, "%lu ", *(m + c * i + j));
      if (ret < 0) {
        perror("fprintf: element\n");
        fclose(stream);
        return -errno;
      }
    }
    ret = fprintf(stream, "\n");
    if (ret < 0)
      perror("fprintf: newline\n");
  }

  ret = fprintf(stream, "\n");
  if (ret < 0)
    perror("fprintf: newline\n");

  fclose(stream);
  return 0;
}

int main(int argc, char *argv[]) {

  unsigned long m1_row, m1_col, m2_row, m2_col;
  unsigned long *m, *m1, *m2;

  if (argc < 5) {
    usage();
    exit(1);
  }

  m1_row = atol(argv[1]);
  m1_col = atol(argv[2]);
  m2_row = atol(argv[3]);
  m2_col = atol(argv[4]);

  if (m1_col != m2_row) {
    printf("Invalid args for matrix multiplication. "
           "m1_col(%lu) != m2_row(%lu)\n", m1_col, m2_row);
    exit(1);
  }

  m1 = generate_matrix(m1_row, m1_col);
  if (!m1) {
    printf("Failed to generate m1\n");
    exit(1);
  }

  m2 = generate_matrix(m2_row, m2_col);
  if (!m2) {
    printf("Failed to generate m2\n");
    free(m1);
    exit(1);
  }

  m = malloc(m1_row * m2_col * sizeof(unsigned long));
  if (!m) {
    printf("Failed to allocate memory for mul matrix\n");
    free(m1);
    free(m2);
  }

  unsigned long i, j, k;
  unsigned long tmp, m_row, m_col;

  struct timeval tv1, tv2;

  if (gettimeofday(&tv1, NULL))
    goto exit;

  printf("\nStarting time: %lu sec and %lu usec\n", tv1.tv_sec, tv1.tv_usec);

  for (i = 0; i < m1_row ; i++) {
    for (j = 0; j < m2_col; j++) {
      tmp = 0;
      for (k = 0; k < m2_row; k++) {
        tmp += *(m1 + i * m1_col + k) * *(m2  + k * m2_col + j);
      }
      *(m + i * m2_col + j) = tmp;
    }
  }

  if (gettimeofday(&tv2, NULL))
    goto exit;

  printf("Ending   time: %lu sec and %lu usec\n", tv2.tv_sec, tv2.tv_usec);
  printf("Time required: %lu sec and %lu usec\n",
         tv2.tv_sec - tv1.tv_sec, labs(tv2.tv_usec - tv1.tv_usec));

  m_row = m1_row;
  m_col = m2_col;

  if (argc == 6) {
    printf("\nMatrix1\n");
    print_matrix(m1, m1_row, m1_col);
    printf("Matrix2\n");
    print_matrix(m2, m2_row, m2_col);
    printf("Matrix Multiplication\n");
    print_matrix(m, m_row, m_col);
  }

  write_matrix("matrix1.txt", m1, m1_row, m1_col);
  write_matrix("matrix2.txt", m2, m2_row, m2_col);
  write_matrix("matrix_ans.txt", m, m_row, m_col);

exit:
  free(m1);
  free(m2);
  free(m);
  return 0;
}
