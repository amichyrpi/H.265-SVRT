#ifndef SVRT_H
#define SVRT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct svrt_context svrt_context;

typedef enum svrt_packet_event {
    SVRT_PACKET_RECEIVED = 1,
    SVRT_PACKET_PROCESSED = 2,
} svrt_packet_event;

typedef void (*svrt_packet_event_cb)(void *opaque, svrt_packet_event event,
                                     uint64_t pts_us, uint64_t receiver_time_us);

typedef struct svrt_config {
    uint16_t port;                 /* TCP port; 9944 when zero */
    const char *bind_address;      /* NULL means all interfaces */
    int require_hardware;          /* fail instead of software fallback */
    int require_zero_copy;         /* reject frames that are not DRM PRIME */
    int fullscreen;                /* create a KMSDRM fullscreen window */
    int headless;                  /* decode/measure without opening a display */
    svrt_packet_event_cb packet_event; /* optional latency instrumentation */
    void *packet_event_opaque;
} svrt_config;

typedef struct svrt_stats {
    uint64_t access_units;
    uint64_t decoded_frames;
    uint64_t presented_frames;
    uint64_t dropped_frames;
    uint64_t bytes_received;
    uint64_t last_pts_us;
} svrt_stats;

/* Open decoder, SDL KMSDRM display and listening socket. */
int svrt_open(svrt_context **out, const svrt_config *config);
/* Accept one sender and run until disconnect, quit event, or error. */
int svrt_run(svrt_context *ctx);
void svrt_stop(svrt_context *ctx);
void svrt_get_stats(const svrt_context *ctx, svrt_stats *out);
const char *svrt_last_error(const svrt_context *ctx);
void svrt_close(svrt_context **ctx);

#ifdef __cplusplus
}
#endif
#endif
