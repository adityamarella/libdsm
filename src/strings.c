#include <stdio.h>

#include "strings.h"

/**
 * Returns a string representation of the `msg_type`. The returned string is
 * statically allocated and should not be free()d.
 *
 * @param msg_type the message type
 *
 * @return a statically allocated string respresenting the `msg_type`
 */
const char *strmsgtype(dsm_msg_type msg_type) {
  switch (msg_type) {
    case NOOP:
      return "NOOP";
    case ALLOCCHUNK:
      return "ALLOCCHUNK";
    case FREECHUNK:
      return "FREECHUNK";
    case GETPAGE:
      return "GETPAGE";
    case LOCATEPAGE:
      return "LOCATEPAGE";
    case INVALIDATEPAGE:
      return "INVALIDATEPAGE";
    case BARRIER:
      return "BARRIER";
    case TERMINATE:
      return "TERMINATE";
    case ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

/**
 * Returns a string representation of the `error`. The returned string is
 * statically allocated and should not be free()d.
 *
 * @param error the error
 *
 * @return a statically allocated string respresenting the `error`
 */
const char *strdsmerror(dsm_error error) {
  switch (error) {
    case DSM_ENOTIMPL:
      return "The requested operation has not been implemented.";
    case DSM_EBADOP:
      return "The requested operation is invalid.";
    case DSM_ENOENT:
      return "The entry specified does not exist or is invalid.";
    case DSM_EINTERNAL:
      return "There was an internal server error.";
    case DSM_EBADALLOC:
      return "Bad allocation request. Check if the size of chunk allocation request is same of all nodes.";
    case DSM_ENOPAGE:
      return "Page not found.";
    default:
      return "UNKNOWN";
  }
}

/**
 * Pretty prints `n` bytes of `buf` in hex with 32 hex numbers per line.
 *
 * @param buf the buffer to print from
 * @param n number of bytes in `buf` to print
 */
void printbuf(void *buf, size_t n) {
  size_t i;
  uchar *uchar_buf = (uchar *)buf;
  for (i = 0; i < n; i++) {
    printf("%02X", uchar_buf[i]);
    if ((i + 1) % 32 == 0) printf("\n");
  }

  if (i % 32 != 0) printf("\n");
}
