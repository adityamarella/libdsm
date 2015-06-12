#ifndef SNFS_COMMON_STRINGS_H
#define SNFS_COMMON_STRINGS_H

#include <stdlib.h>

#include "dsmtypes.h"

const char *strmsgtype(dsm_msg_type);
const char *strdsmerror(dsm_error);
void printbuf(void *, size_t n);

#endif
