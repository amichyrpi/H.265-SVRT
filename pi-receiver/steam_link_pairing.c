#include "steam_link_pairing.h"

#include <ihslib/client.h>
#include <ihslib/net.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define IDENTITY_PATH "/var/lib/svrt-receiver/steam-link-identity"
#define PAIRED_PATH "/var/lib/svrt-receiver/steam-link-host"

typedef struct pairing_worker {
    svrt_steam_link_pairing *pairing;
    IHS_Client *client;
    IHS_HostInfo host;
    atomic_int host_found;
    atomic_int authorization_done;
    atomic_int authorization_ok;
    uint64_t steam_id;
} pairing_worker;

static uint64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int random_bytes(void *output, size_t size) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t used = 0;
    while (used < size) {
        ssize_t count = read(fd, (char *)output + used, size - used);
        if (count > 0) used += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else { close(fd); return -1; }
    }
    close(fd);
    return 0;
}

static void set_state(svrt_steam_link_pairing *pairing,
                      svrt_steam_link_state state, const char *host,
                      const char *error) {
    pthread_mutex_lock(&pairing->lock);
    pairing->state = state;
    if (host) snprintf(pairing->hostname, sizeof(pairing->hostname), "%s", host);
    if (error) snprintf(pairing->error, sizeof(pairing->error), "%s", error);
    pthread_mutex_unlock(&pairing->lock);
}

static FILE *open_private_temp(const char *temporary) {
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                  S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    if (fchmod(fd, S_IRUSR | S_IWUSR)) {
        close(fd);
        unlink(temporary);
        return NULL;
    }
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(temporary);
    }
    return file;
}

static int save_identity(uint64_t id, const uint8_t key[32]) {
    char temporary[256];
    snprintf(temporary, sizeof(temporary), "%s.tmp", IDENTITY_PATH);
    FILE *file = open_private_temp(temporary);
    if (!file) return -1;
    int ok = fprintf(file, "%016llx\n", (unsigned long long)id) > 0;
    for (unsigned i = 0; ok && i < 32; ++i) ok = fprintf(file, "%02x", key[i]) == 2;
    ok = ok && fputc('\n', file) != EOF && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file)) ok = 0;
    if (!ok || rename(temporary, IDENTITY_PATH)) { unlink(temporary); return -1; }
    return 0;
}

static int load_or_create_identity(uint64_t *id, uint8_t key[32]) {
    FILE *file = fopen(IDENTITY_PATH, "r");
    char id_text[32] = {0}, key_text[80] = {0};
    if (file) {
        int valid = fgets(id_text, sizeof(id_text), file) &&
                    fgets(key_text, sizeof(key_text), file);
        fclose(file);
        char *end = NULL;
        unsigned long long parsed = valid ? strtoull(id_text, &end, 16) : 0;
        valid = valid && parsed && end && (*end == '\n' || *end == '\0') &&
                strlen(key_text) >= 64;
        for (unsigned i = 0; valid && i < 32; ++i) {
            char byte[3] = {key_text[i * 2], key_text[i * 2 + 1], 0};
            char *byte_end = NULL;
            unsigned long value = strtoul(byte, &byte_end, 16);
            if (!byte_end || *byte_end || value > 255) valid = 0;
            key[i] = (uint8_t)value;
        }
        if (valid) { *id = (uint64_t)parsed; return 0; }
    }
    if (random_bytes(id, sizeof(*id)) || random_bytes(key, 32)) return -1;
    if (!*id) *id = 1;
    return save_identity(*id, key);
}

static int load_paired_host(char hostname[64]) {
    FILE *file = fopen(PAIRED_PATH, "r");
    if (!file) return 0;
    int ok = fgets(hostname, 64, file) != NULL;
    fclose(file);
    if (!ok) return 0;
    hostname[strcspn(hostname, "\r\n")] = 0;
    return hostname[0] != 0;
}

static int save_paired_host(const pairing_worker *worker) {
    char temporary[256];
    snprintf(temporary, sizeof(temporary), "%s.tmp", PAIRED_PATH);
    FILE *file = open_private_temp(temporary);
    if (!file) return -1;
    char *address = IHS_IPAddressToString(&worker->host.address.ip);
    int ok = fprintf(file, "%s\n%llu\n%llu\n%s\n%llu\n",
                     worker->host.hostname,
                     (unsigned long long)worker->host.clientId,
                     (unsigned long long)worker->host.instanceId,
                     address ? address : "",
                     (unsigned long long)worker->steam_id) > 0;
    free(address);
    ok = ok && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file)) ok = 0;
    if (!ok || rename(temporary, PAIRED_PATH)) { unlink(temporary); return -1; }
    return 0;
}

static void discovered(IHS_Client *client, const IHS_HostInfo *host, void *context) {
    (void)client;
    pairing_worker *worker = context;
    if (atomic_load(&worker->host_found)) return;
    worker->host = *host;
    atomic_store(&worker->host_found, 1);
    set_state(worker->pairing, SVRT_STEAM_LINK_SEARCHING, host->hostname, NULL);
}

static void authorization_progress(IHS_Client *client, const IHS_HostInfo *host,
                                   void *context) {
    (void)client; (void)host; (void)context;
}

static void authorization_success(IHS_Client *client, const IHS_HostInfo *host,
                                  uint64_t steam_id, void *context) {
    (void)client; (void)host;
    pairing_worker *worker = context;
    worker->steam_id = steam_id;
    atomic_store(&worker->authorization_ok, 1);
    atomic_store(&worker->authorization_done, 1);
}

static void authorization_failed(IHS_Client *client, const IHS_HostInfo *host,
                                 IHS_AuthorizationResult result, void *context) {
    (void)client; (void)host;
    pairing_worker *worker = context;
    atomic_store(&worker->authorization_ok, 0);
    atomic_store(&worker->authorization_done, 1);
    char message[96];
    snprintf(message, sizeof(message), "Steam authorization failed (%d)", result);
    fprintf(stderr, "SVRT Steam Link: %s\n", message);
    set_state(worker->pairing, SVRT_STEAM_LINK_FAILED, NULL, message);
}

static void ihs_log(IHS_LogLevel level, const char *tag, const char *message) {
    if (level <= IHS_LogLevelWarn)
        fprintf(stderr, "Steam Link [%s]: %s\n", tag, message);
}

static void *pairing_thread(void *opaque) {
    svrt_steam_link_pairing *pairing = opaque;
    char paired_host[64] = {0};
    const int had_paired_host = load_paired_host(paired_host);
    if (had_paired_host)
        set_state(pairing, SVRT_STEAM_LINK_SEARCHING, paired_host, NULL);
    uint64_t device_id = 0;
    uint8_t secret[32];
    if (load_or_create_identity(&device_id, secret)) {
        set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                  "Cannot create Steam Link identity");
        return NULL;
    }
    pthread_mutex_lock(&pairing->lock);
    pairing->device_id = device_id;
    pthread_mutex_unlock(&pairing->lock);
    char generated_pin[5] = {0};
    for (;;) {
        uint16_t pin_value = 0;
        if (random_bytes(&pin_value, sizeof(pin_value))) {
            set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                      "Cannot generate pairing PIN");
            return NULL;
        }
        if (pin_value < 60000u) {
            snprintf(generated_pin, sizeof(generated_pin), "%04u",
                     (unsigned)(pin_value % 10000u));
            break;
        }
    }
    pthread_mutex_lock(&pairing->lock);
    memcpy(pairing->pin, generated_pin, sizeof(pairing->pin));
    const char *log_pin = getenv("SVRT_LOG_STEAM_LINK_PIN");
    if (log_pin && log_pin[0] && strcmp(log_pin, "0"))
        fprintf(stderr, "SVRT Steam Link test PIN: %s\n", pairing->pin);
    pthread_mutex_unlock(&pairing->lock);

    IHS_Init();
    IHS_ClientConfig config = {device_id, secret, "Stearlight HMD"};
    pairing_worker worker = {.pairing = pairing};
    worker.client = IHS_ClientCreate(&config);
    if (!worker.client) {
        IHS_Quit();
        set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                  "Cannot start IHSlib client");
        return NULL;
    }
    IHS_ClientDiscoveryCallbacks discovery_callbacks = {.discovered = discovered};
    IHS_ClientAuthorizationCallbacks authorization_callbacks = {
        .progress = authorization_progress,
        .success = authorization_success,
        .failed = authorization_failed};
    IHS_ClientSetLogFunction(worker.client, ihs_log);
    IHS_ClientSetDiscoveryCallbacks(worker.client, &discovery_callbacks, &worker);
    IHS_ClientSetAuthorizationCallbacks(worker.client, &authorization_callbacks, &worker);
    IHS_ClientStartDiscovery(worker.client, 500);

    const uint64_t search_deadline = monotonic_ms() + 30000;
    while (!atomic_load(&pairing->stopping) &&
           !atomic_load(&worker.host_found) && monotonic_ms() < search_deadline)
        usleep(20000);
    if (!atomic_load(&worker.host_found)) {
        set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                  "No Steam Remote Play PC found");
    } else if (!atomic_load(&pairing->stopping)) {
        IHS_ClientStopDiscovery(worker.client);
        char *selected_address = IHS_IPAddressToString(&worker.host.address.ip);
        fprintf(stderr, "SVRT Steam Link: selected first responding host %s (%s)\n",
                worker.host.hostname,
                selected_address ? selected_address : "unknown address");
        free(selected_address);
        set_state(pairing, SVRT_STEAM_LINK_AUTHORIZING, worker.host.hostname, NULL);
        char pin[5];
        pthread_mutex_lock(&pairing->lock);
        memcpy(pin, pairing->pin, sizeof(pin));
        pthread_mutex_unlock(&pairing->lock);
        if (!IHS_ClientAuthorizationRequest(worker.client, &worker.host, pin)) {
            set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                      "Cannot request Steam authorization");
        } else {
            const uint64_t auth_deadline = monotonic_ms() + 120000;
            while (!atomic_load(&pairing->stopping) &&
                   !atomic_load(&worker.authorization_done) &&
                   monotonic_ms() < auth_deadline) usleep(20000);
            if (atomic_load(&worker.authorization_ok) && !save_paired_host(&worker))
                set_state(pairing, SVRT_STEAM_LINK_PAIRED,
                          worker.host.hostname, NULL);
            else if (!atomic_load(&worker.authorization_done)) {
                set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                          "Steam authorization timed out");
            } else if (atomic_load(&worker.authorization_ok)) {
                set_state(pairing, SVRT_STEAM_LINK_FAILED, NULL,
                          "Cannot save Steam authorization");
            } else {
                /* Steam has explicitly rejected or revoked this device.  A
                   local marker must never outlive the host-side grant. */
                unlink(PAIRED_PATH);
            }
        }
    }
    IHS_ClientStop(worker.client);
    IHS_ClientThreadedJoin(worker.client);
    IHS_ClientDestroy(worker.client);
    IHS_Quit();
    return NULL;
}

int svrt_steam_link_pairing_start(svrt_steam_link_pairing *pairing) {
    if (!pairing) return -1;
    memset(pairing, 0, sizeof(*pairing));
    pthread_mutex_init(&pairing->lock, NULL);
    pairing->state = SVRT_STEAM_LINK_SEARCHING;
    if (pthread_create(&pairing->worker, NULL, pairing_thread, pairing)) {
        pthread_mutex_destroy(&pairing->lock);
        return -1;
    }
    pairing->worker_started = 1;
    return 0;
}

void svrt_steam_link_pairing_snapshot(svrt_steam_link_pairing *pairing,
                                      svrt_steam_link_state *state,
                                      char pin[5], char hostname[64],
                                      char error[128], uint64_t *device_id) {
    pthread_mutex_lock(&pairing->lock);
    if (state) *state = pairing->state;
    if (pin) memcpy(pin, pairing->pin, sizeof(pairing->pin));
    if (hostname) memcpy(hostname, pairing->hostname, sizeof(pairing->hostname));
    if (error) memcpy(error, pairing->error, sizeof(pairing->error));
    if (device_id) *device_id = pairing->device_id;
    pthread_mutex_unlock(&pairing->lock);
}

int svrt_steam_link_pairing_is_paired(svrt_steam_link_pairing *pairing) {
    svrt_steam_link_state state;
    svrt_steam_link_pairing_snapshot(pairing, &state, NULL, NULL, NULL, NULL);
    return state == SVRT_STEAM_LINK_PAIRED;
}

void svrt_steam_link_pairing_forget_host(void) { unlink(PAIRED_PATH); }

int svrt_steam_link_pairing_host_address(char *address, size_t size) {
    if (!address || size < 2) return 0;
    FILE *file = fopen(PAIRED_PATH, "r");
    if (!file) return 0;
    char hostname[64] = {0}, client[64] = {0}, instance[64] = {0},
         stored[64] = {0};
    int ok = fgets(hostname, sizeof(hostname), file) &&
             fgets(client, sizeof(client), file) &&
             fgets(instance, sizeof(instance), file) &&
             fgets(stored, sizeof(stored), file);
    fclose(file);
    if (!ok) return 0;
    stored[strcspn(stored, "\r\n")] = 0;
    if (!stored[0]) return 0;
    snprintf(address, size, "%s", stored);
    return 1;
}

void svrt_steam_link_pairing_stop(svrt_steam_link_pairing *pairing) {
    if (!pairing) return;
    atomic_store(&pairing->stopping, 1);
    if (pairing->worker_started) pthread_join(pairing->worker, NULL);
    pthread_mutex_destroy(&pairing->lock);
    memset(pairing, 0, sizeof(*pairing));
}
