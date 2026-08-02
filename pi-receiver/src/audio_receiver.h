#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>

typedef struct svrt_audio_receiver {
    uint16_t port;
    atomic_int stopping;
    pthread_t thread;
    int started;
} svrt_audio_receiver;

int svrt_audio_receiver_start(svrt_audio_receiver *receiver, uint16_t port);
void svrt_audio_receiver_stop(svrt_audio_receiver *receiver);
