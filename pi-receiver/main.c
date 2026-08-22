#include <svrt.h>

#include "status.h"
#include "audio.h"
#include "pairing.h"
#include "steam_link_pairing.h"

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
}

typedef struct monitor_args {
    svrt_context *context;
    svrt_status_server *server;
    atomic_int stopping;
} monitor_args;

typedef struct run_args {
    svrt_context *context;
    atomic_int done;
    int result;
} run_args;

static void *run_receiver(void *opaque) {
    run_args *args = opaque;
    args->result = svrt_run(args->context);
    atomic_store(&args->done, 1);
    return NULL;
}

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

static int authorize_steam_link(int headless, svrt_status_server *status) {
    svrt_steam_link_pairing pairing;
    if (svrt_steam_link_pairing_start(&pairing)) {
        fprintf(stderr, "SVRT: cannot start Steam Link discovery\n");
        return -1;
    }
    if (headless) {
        svrt_steam_link_state state = SVRT_STEAM_LINK_SEARCHING;
        char error[128] = {0};
        while (!quitting && state != SVRT_STEAM_LINK_PAIRED &&
               state != SVRT_STEAM_LINK_FAILED) {
            svrt_steam_link_pairing_snapshot(&pairing, &state, NULL, NULL,
                                             error, NULL);
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000};
            nanosleep(&delay, NULL);
        }
        if (state == SVRT_STEAM_LINK_FAILED)
            fprintf(stderr, "SVRT Steam Link: %s\n", error);
    } else {
        svrt_pairing_gui_show(&pairing, &quitting);
    }
    const int paired = svrt_steam_link_pairing_is_paired(&pairing);
    uint64_t device_id = 0;
    svrt_steam_link_pairing_snapshot(&pairing, NULL, NULL, NULL, NULL,
                                     &device_id);
    svrt_steam_link_pairing_stop(&pairing);
    svrt_status_server_set_steam_device_id(status, device_id);
    return paired;
}

int main(int argc, char **argv) {
    int headless = 0;
    uint16_t port = 9944;
    uint16_t status_port = 0;
    uint16_t audio_port = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("usage: svrt-receiver [--headless] [--status-port PORT] [--audio-port PORT] [video-port]");
            return 0;
        }
        if (!strcmp(argv[i], "--headless")) {
            headless = 1;
        } else if (!strcmp(argv[i], "--status-port") && i + 1 < argc) {
            status_port = (uint16_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--audio-port") && i + 1 < argc) {
            audio_port = (uint16_t)atoi(argv[++i]);
        } else {
            port = (uint16_t)atoi(argv[i]);
        }
    }
    if (!status_port) status_port = (uint16_t)(port + 1);
    if (!audio_port) audio_port = (uint16_t)(port + 2);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    int exit_code = 0;
    svrt_status_server status;
    if (svrt_status_server_start(&status, status_port)) {
        fprintf(stderr, "SVRT: cannot listen for driver health checks on port %u\n",
                status_port);
        return 1;
    }
    while (!quitting) {
        svrt_status_server_update(&status, SVRT_RECEIVER_UNAUTHORIZED, NULL);
        const int authorized = authorize_steam_link(headless, &status);
        if (quitting) break;
        if (authorized <= 0) {
            exit_code = 1;
            struct timespec retry = {.tv_sec = 2};
            nanosleep(&retry, NULL);
            continue;
        }
        svrt_status_server_reset_authorization(&status);
        svrt_status_server_update(&status, SVRT_RECEIVER_STARTING, NULL);
        svrt_audio_receiver audio;
        int audio_started = svrt_audio_receiver_start(&audio, audio_port) == 0;
        if (!audio_started)
            fprintf(stderr, "SVRT audio: failed to start receiver\n");

        while (!quitting &&
               !svrt_status_server_authorization_revoked(&status)) {
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
            int monitoring = pthread_create(&monitor_thread, NULL,
                                             monitor_receiver, &monitor) == 0;
            run_args runner = {.context = running, .result = 0};
            pthread_t run_thread;
            int running_in_thread = pthread_create(&run_thread, NULL,
                                                   run_receiver, &runner) == 0;
            int rc;
            if (running_in_thread) {
                while (!quitting && !atomic_load(&runner.done) &&
                       !svrt_status_server_authorization_revoked(&status)) {
                    struct timespec delay = {.tv_sec = 0,
                                             .tv_nsec = 50000000};
                    nanosleep(&delay, NULL);
                }
                if (quitting ||
                    svrt_status_server_authorization_revoked(&status))
                    svrt_stop(running);
                pthread_join(run_thread, NULL);
                rc = runner.result;
            } else {
                fprintf(stderr, "SVRT: failed to start video worker thread\n");
                rc = -1;
                exit_code = 1;
            }
            if (monitoring) {
                atomic_store(&monitor.stopping, 1);
                pthread_join(monitor_thread, NULL);
            }

            svrt_stats stats = {0};
            svrt_get_stats(running, &stats);
            const int revoked =
                svrt_status_server_authorization_revoked(&status);
            if (revoked || quitting) {
                svrt_status_server_update(&status,
                                          SVRT_RECEIVER_UNAUTHORIZED, &stats);
            } else if (rc) {
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
            running = NULL;
            if (!quitting && !revoked) {
                struct timespec retry = {.tv_sec = rc ? 2 : 0,
                                         .tv_nsec = rc ? 0 : 250000000};
                nanosleep(&retry, NULL);
            }
        }
        if (audio_started) svrt_audio_receiver_stop(&audio);
        if (svrt_status_server_authorization_revoked(&status)) {
            fprintf(stderr, "SVRT Steam Link: host revoked this headset; returning to pairing\n");
            svrt_steam_link_pairing_forget_host();
            svrt_status_server_update(&status, SVRT_RECEIVER_UNAUTHORIZED, NULL);
        }
    }
    svrt_status_server_stop(&status);
    return exit_code;
}
