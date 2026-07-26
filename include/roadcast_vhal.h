#ifndef ROADCAST_VHAL_H
#define ROADCAST_VHAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "roadcast_frames.h"
#include "roadcast_protocol.h"

typedef struct {
    pid_t pid;
    uint64_t addresses[ROADCAST_FRAME_COUNT];
    struct iovec *local_iov;
    struct iovec *remote_iov;
    size_t resolved_count;
} roadcast_vhal_source_t;

int roadcast_vhal_open(roadcast_vhal_source_t *source,
                       const char *process_name);
int roadcast_vhal_read(roadcast_vhal_source_t *source,
                       roadcast_frame_t frames[ROADCAST_FRAME_COUNT]);
void roadcast_vhal_close(roadcast_vhal_source_t *source);

#endif
