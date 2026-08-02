#ifndef SVRT_STATUS_SERVER_H
#define SVRT_STATUS_SERVER_H

#include <stdatomic.h>
#include <stdint.h>
#include <svrt/svrt.h>

enum svrt_receiver_state {
    SVRT_RECEIVER_STARTING = 0,
    SVRT_RECEIVER_READY = 1,
    SVRT_RECEIVER_STREAMING = 2,
    SVRT_RECEIVER_ERROR = 3
};

typedef struct svrt_status_server {
    uint16_t port;
    atomic_int stopping;
    atomic_int state;
    atomic_uint_fast64_t decoded;
    atomic_uint_fast64_t presented;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t bytes;
    void *thread;
} svrt_status_server;

int svrt_status_server_start(svrt_status_server *server, uint16_t port);
void svrt_status_server_update(svrt_status_server *server, int state,
                               const svrt_stats *stats);
void svrt_status_server_stop(svrt_status_server *server);

#endif
