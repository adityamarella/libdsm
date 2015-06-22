#ifndef __DSM_INTERNAL_H_
#define __DSM_INTERNAL_H_

#include "dsmtypes.h"
#include "dsm.h"
#include "utils.h"

int dsm_allocchunk_internal(dhandle chunk_id, size_t sz, 
    const uint8_t *requestor_host, uint32_t requestor_port);

int dsm_freechunk_internal(dhandle chunk_id, 
    const uint8_t *requestor_host, uint32_t requestor_port);

int dsm_locatepage_internal(dhandle chunk_id, 
    dhandle page_offset, uint8_t **host, uint32_t *port);

int dsm_getpage_internal(dhandle chunk_id, dhandle page_offset,
    uint8_t *host, uint32_t port, uint8_t **data, uint64_t *count, uint32_t flags);

int dsm_invalidatepage_internal(dhandle chunk_id, dhandle page_offset);

int dsm_barrier_internal();
int dsm_terminate_internal();
#endif
