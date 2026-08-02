#include <svrt/svrt.h>

#include "status_server.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static svrt_context *running;
static volatile sig_atomic_t quitting;

static void stop(int sig) {
    (void)sig;
    quitting = 1;
    if (running) svrt_stop(running);
}

typedef struct monitor_args {
    svrt_context *context;
    svrt_status_server *server;
    atomic_int stopping;
} monitor_args;

static void *monitor_receiver(void *opaque) {
    monitor_args *args = opaque;
    uint64_t previous_decoded = 0;
    while (!atomic_load(&args->stopping)) {
        svrt_stats stats = {0};
        svrt_get_stats(args->context, &stats);
        int state = stats.decoded_frames > previous_decoded
                        ? SVRT_RECEIVER_STREAMING
                        : SVRT_RECEIVER_READY;
        previous_decoded = stats.decoded_frames;
        svrt_status_server_update(args->server, state, &stats);
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 250000000};
        nanosleep(&delay, NULL);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int headless = 0;
    uint16_t port = 9944;
    uint16_t status_port = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("usage: svrt-receiver [--headless] [--status-port PORT] [video-port]");
            return 0;
        }
        if (!strcmp(argv[i], "--headless")) {
            headless = 1;
        } else if (!strcmp(argv[i], "--status-port") && i + 1 < argc) {
            status_port = (uint16_t)atoi(argv[++i]);
        } else {
            port = (uint16_t)atoi(argv[i]);
        }
    }
    if (!status_port) status_port = (uint16_t)(port + 1);

    svrt_status_server status;
    if (svrt_status_server_start(&status, status_port)) {
        fprintf(stderr, "SVRT: cannot listen for driver health checks on port %u\n",
                status_port);
        return 1;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    int exit_code = 0;
    while (!quitting) {
        svrt_config cfg = {.port = port,
                           .require_hardware = 1,
                           .require_zero_copy = 1,
                           .fullscreen = 1,
                           .headless = headless,
                           .packet_event = svrt_status_server_packet_event,
                           .packet_event_opaque = &status};
        svrt_status_server_reset_trace(&status);
        if (svrt_open(&running, &cfg)) {
            svrt_status_server_update(&status, SVRT_RECEIVER_ERROR, NULL);
            exit_code = 1;
            struct timespec retry = {.tv_sec = 2};
            nanosleep(&retry, NULL);
            continue;
        }

        svrt_status_server_update(&status, SVRT_RECEIVER_READY, NULL);
        monitor_args monitor = {.context = running, .server = &status};
        pthread_t monitor_thread;
        int monitoring = pthread_create(&monitor_thread, NULL, monitor_receiver,
                                        &monitor) == 0;
        int rc = svrt_run(running);
        if (monitoring) {
            atomic_store(&monitor.stopping, 1);
            pthread_join(monitor_thread, NULL);
        }

        svrt_stats stats = {0};
        svrt_get_stats(running, &stats);
        if (rc && !quitting) {
            fprintf(stderr, "SVRT: %s\n", svrt_last_error(running));
            svrt_status_server_update(&status, SVRT_RECEIVER_ERROR, &stats);
            exit_code = 1;
        } else {
            svrt_status_server_update(&status, SVRT_RECEIVER_READY, &stats);
        }
        fprintf(stderr, "SVRT: %llu decoded, %llu shown, %llu dropped\n",
                (unsigned long long)stats.decoded_frames,
                (unsigned long long)stats.presented_frames,
                (unsigned long long)stats.dropped_frames);
        svrt_close(&running);
        if (!quitting) {
            struct timespec retry = {.tv_sec = rc ? 2 : 0,
                                     .tv_nsec = rc ? 0 : 250000000};
            nanosleep(&retry, NULL);
        }
    }
    svrt_status_server_stop(&status);
    return exit_code;
}
