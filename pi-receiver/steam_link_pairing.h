#ifndef SVRT_STEAM_LINK_PAIRING_H
#define SVRT_STEAM_LINK_PAIRING_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

typedef enum svrt_steam_link_state {
    SVRT_STEAM_LINK_SEARCHING,
    SVRT_STEAM_LINK_AUTHORIZING,
    SVRT_STEAM_LINK_PAIRED,
    SVRT_STEAM_LINK_FAILED
} svrt_steam_link_state;

typedef struct svrt_steam_link_pairing {
    pthread_mutex_t lock;
    pthread_t worker;
    int worker_started;
    atomic_int stopping;
    svrt_steam_link_state state;
    uint64_t device_id;
    char pin[5];
    char hostname[64];
    char error[128];
} svrt_steam_link_pairing;

int svrt_steam_link_pairing_start(svrt_steam_link_pairing *pairing);
void svrt_steam_link_pairing_snapshot(svrt_steam_link_pairing *pairing,
                                      svrt_steam_link_state *state,
                                      char pin[5], char hostname[64],
                                      char error[128], uint64_t *device_id);
int svrt_steam_link_pairing_is_paired(svrt_steam_link_pairing *pairing);
void svrt_steam_link_pairing_stop(svrt_steam_link_pairing *pairing);
void svrt_steam_link_pairing_forget_host(void);
int svrt_steam_link_pairing_host_address(char *address, size_t size);

#endif
