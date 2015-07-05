#include<sys/time.h>
#include<time.h>

/**
 * Get seconds from epoch
 */
long long current_us() {
  struct timeval te;
  gettimeofday(&te, NULL);
  return (long long) te.tv_sec * 1000000 + te.tv_usec;
}
