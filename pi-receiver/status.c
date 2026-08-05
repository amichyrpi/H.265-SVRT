#include "status.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

static const char *pairing_file(void) {
    const char *path = getenv("SVRT_PAIRING_FILE");
    return path && path[0] ? path : "/var/lib/svrt-receiver/pairing";
}

static void refresh_pairing_code_locked(svrt_status_server *server) {
    time_t now = time(NULL);
    if (server->paired_client[0] || now - server->pairing_code_started < 30)
        return;
    uint32_t random_value = 0;
    size_t received = 0;
    int random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random_fd >= 0) {
        while (received < sizeof(random_value)) {
            ssize_t count = read(random_fd, (char *)&random_value + received,
                                 sizeof(random_value) - received);
            if (count > 0) received += (size_t)count;
            else if (count < 0 && errno == EINTR) continue;
            else break;
        }
        close(random_fd);
    }
    if (received != sizeof(random_value)) {
        server->pairing_code[0] = '\0';
        return;
    }
    unsigned value = random_value % 10000;
    snprintf(server->pairing_code, sizeof(server->pairing_code), "%04u", value);
    server->pairing_code_started = now;
    server->pairing_failures = 0;
}

static int save_pairing_locked(const svrt_status_server *server) {
    char temporary[PATH_MAX];
    int used = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX",
                        pairing_file());
    if (used < 0 || (size_t)used >= sizeof(temporary)) return -1;
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(temporary);
        return -1;
    }
    int ok = fputs(server->paired_client, file) >= 0 && fputc('\n', file) != EOF;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok || rename(temporary, pairing_file()) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static void load_pairing_locked(svrt_status_server *server) {
    FILE *file = fopen(pairing_file(), "r");
    if (!file) return;
    if (!fgets(server->paired_client, sizeof(server->paired_client), file))
        server->paired_client[0] = '\0';
    server->paired_client[strcspn(server->paired_client, "\r\n")] = '\0';
    fclose(file);
}

static int valid_client_id(const char *id) {
    if (!id || !id[0]) return 0;
    for (; *id; ++id)
        if (!isalnum((unsigned char)*id) && *id != '-' && *id != '_') return 0;
    return 1;
}

static int send_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return -1;
        data += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int send_ack(int fd, unsigned long long nonce, const char *stage,
                    uint64_t pts_us, uint64_t receiver_time_us) {
    char response[192];
    int used = snprintf(response, sizeof(response),
                        "SVRT/1 ACK %llu %s %llu %llu\n", nonce, stage,
                        (unsigned long long)pts_us,
                        (unsigned long long)receiver_time_us);
    return used > 0 && (size_t)used < sizeof(response)
               ? send_all(fd, response, (size_t)used)
               : -1;
}

static void answer_trace(svrt_status_server *server, int fd,
                         unsigned long long nonce, uint64_t target_pts_us) {
    /* Never monopolize the single status listener for a stale trace request.
     * PING is the driver's connection/liveness path, so it must remain
     * responsive while a stream is reconnecting or has stopped. */
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int received_sent = 0;
    for (int waited_ms = 0; waited_ms < 1000; ++waited_ms) {
        if (atomic_load(&server->stopping)) return;
        uint64_t received_pts = atomic_load(&server->received_pts_us);
        uint64_t processed_pts = atomic_load(&server->processed_pts_us);
        if (!received_sent && received_pts == target_pts_us) {
            if (send_ack(fd, nonce, "RECEIVED", received_pts,
                         atomic_load(&server->received_time_us)))
                return;
            received_sent = 1;
        }
        if (processed_pts == target_pts_us) {
            if (!received_sent &&
                send_ack(fd, nonce, "RECEIVED", target_pts_us,
                         atomic_load(&server->received_time_us)))
                return;
            send_ack(fd, nonce, "PROCESSED", processed_pts,
                     atomic_load(&server->processed_time_us));
            return;
        }
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }
    send_ack(fd, nonce, "TIMEOUT", target_pts_us, 0);
}

static void answer_client(svrt_status_server *server, int fd) {
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char request[128] = {0};
    size_t length = 0;
    int complete = 0;
    while (length < sizeof(request) - 1) {
        ssize_t received =
            recv(fd, request + length, sizeof(request) - 1 - length, 0);
        if (received <= 0) return;
        char *newline = memchr(request + length, '\n', (size_t)received);
        length += (size_t)received;
        if (newline) {
            length = (size_t)(newline - request) + 1;
            request[length] = '\0';
            complete = 1;
            break;
        }
    }
    if (!complete) return;
    unsigned long long nonce = 0, target_pts = 0;
    char code[8] = {0}, client[65] = {0};
    if (!strncmp(request, "SVRT/1 PAIRING", 14)) {
        char current[5];
        svrt_status_server_pairing_code(server, current);
        char response[96];
        int used = snprintf(response, sizeof(response), "SVRT/1 PAIRING %s\n",
                            current[0] ? current : "PAIRED");
        if (used > 0 && (size_t)used < sizeof(response)) send_all(fd, response, (size_t)used);
        return;
    }
    if (sscanf(request, "SVRT/1 PAIR %7s %64s", code, client) == 2) {
        int accepted = 0;
        pthread_mutex_lock(&server->pairing_lock);
        server->pairing_started = time(NULL);
        refresh_pairing_code_locked(server);
        if (!server->paired_client[0] && valid_client_id(client) && !strcmp(code, server->pairing_code)) {
            snprintf(server->paired_client, sizeof(server->paired_client), "%s", client);
            if (save_pairing_locked(server) == 0) accepted = 1;
            else server->paired_client[0] = '\0';
        } else if (!server->paired_client[0] && server->pairing_code[0]) {
            if (++server->pairing_failures >= 5) {
                server->pairing_code_started = 0;
                server->pairing_code[0] = '\0';
                refresh_pairing_code_locked(server);
            }
        }
        pthread_mutex_unlock(&server->pairing_lock);
        send_all(fd, accepted ? "SVRT/1 PAIRED\n" : "SVRT/1 PAIR_FAILED\n",
                 accepted ? 14 : 19);
        return;
    }
    if (sscanf(request, "SVRT/1 UNPAIR %64s", client) == 1) {
        int accepted = 0;
        pthread_mutex_lock(&server->pairing_lock);
        if (client[0] && !strcmp(client, server->paired_client)) {
            server->paired_client[0] = '\0'; save_pairing_locked(server);
            server->pairing_code_started = 0; server->pairing_started = 0; refresh_pairing_code_locked(server); accepted = 1;
        }
        pthread_mutex_unlock(&server->pairing_lock);
        send_all(fd, accepted ? "SVRT/1 UNPAIRED\n" : "SVRT/1 UNPAIR_FAILED\n",
                 accepted ? 16 : 21);
        return;
    }
    if (sscanf(request, "SVRT/1 TRACE %llu %llu", &nonce, &target_pts) == 2) {
        answer_trace(server, fd, nonce, (uint64_t)target_pts);
        return;
    }
    if (sscanf(request, "SVRT/1 PING %llu", &nonce) != 1) return;
    char response[256];
    int used = snprintf(response, sizeof(response),
                        "SVRT/1 STATUS %llu %d %llu %llu %llu %llu\n",
                        nonce, atomic_load(&server->state),
                        (unsigned long long)atomic_load(&server->decoded),
                        (unsigned long long)atomic_load(&server->presented),
                        (unsigned long long)atomic_load(&server->dropped),
                        (unsigned long long)atomic_load(&server->bytes));
    if (used > 0 && (size_t)used < sizeof(response))
        send_all(fd, response, (size_t)used);
}

typedef struct status_client {
    svrt_status_server *server;
    int fd;
} status_client;

static void *status_client_thread(void *opaque) {
    status_client *client = opaque;
    svrt_status_server *server = client->server;
    answer_client(server, client->fd);
    close(client->fd);
    pthread_mutex_lock(&server->clients_lock);
    --server->active_clients;
    pthread_cond_broadcast(&server->clients_done);
    pthread_mutex_unlock(&server->clients_lock);
    free(client);
    return NULL;
}

static void *status_thread(void *opaque) {
    svrt_status_server *server = opaque;
    int listener = socket(AF_INET6, SOCK_STREAM, 0);
    if (listener < 0) return NULL;
    int one = 1, off = 0;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    struct sockaddr_in6 address = {.sin6_family = AF_INET6,
                                   .sin6_addr = IN6ADDR_ANY_INIT,
                                   .sin6_port = htons(server->port)};
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        listen(listener, 4)) {
        close(listener);
        return NULL;
    }
    while (!atomic_load(&server->stopping)) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(listener, &reads);
        struct timeval wait = {.tv_sec = 1};
        int ready = select(listener + 1, &reads, NULL, NULL, &wait);
        if (ready <= 0) continue;
        int client = accept(listener, NULL, NULL);
        if (client >= 0) {
            status_client *connection = malloc(sizeof(*connection));
            if (!connection) {
                close(client);
                continue;
            }
            connection->server = server;
            connection->fd = client;
            pthread_mutex_lock(&server->clients_lock);
            ++server->active_clients;
            pthread_mutex_unlock(&server->clients_lock);
            pthread_t thread;
            if (pthread_create(&thread, NULL, status_client_thread, connection)) {
                pthread_mutex_lock(&server->clients_lock);
                --server->active_clients;
                pthread_cond_broadcast(&server->clients_done);
                pthread_mutex_unlock(&server->clients_lock);
                close(client);
                free(connection);
            } else {
                pthread_detach(thread);
            }
        }
    }
    close(listener);
    return NULL;
}

int svrt_status_server_start(svrt_status_server *server, uint16_t port) {
    if (!server) return -1;
    memset(server, 0, sizeof(*server));
    server->port = port ? port : 9945;
    pthread_mutex_init(&server->pairing_lock, NULL);
    pthread_mutex_init(&server->clients_lock, NULL);
    pthread_cond_init(&server->clients_done, NULL);
    pthread_mutex_lock(&server->pairing_lock);
    load_pairing_locked(server);
    refresh_pairing_code_locked(server);
    pthread_mutex_unlock(&server->pairing_lock);
    atomic_store(&server->state, SVRT_RECEIVER_STARTING);
    pthread_t *thread = malloc(sizeof(*thread));
    if (!thread || pthread_create(thread, NULL, status_thread, server)) {
        free(thread);
        pthread_cond_destroy(&server->clients_done);
        pthread_mutex_destroy(&server->clients_lock);
        pthread_mutex_destroy(&server->pairing_lock);
        return -1;
    }
    server->thread = thread;
    return 0;
}

void svrt_status_server_update(svrt_status_server *server, int state,
                               const svrt_stats *stats) {
    if (!server) return;
    if (stats) {
        atomic_store(&server->decoded, stats->decoded_frames);
        atomic_store(&server->presented, stats->presented_frames);
        atomic_store(&server->dropped, stats->dropped_frames);
        atomic_store(&server->bytes, stats->bytes_received);
    }
    atomic_store(&server->state, state);
}

void svrt_status_server_packet_event(void *opaque, svrt_packet_event event,
                                     uint64_t pts_us, uint64_t receiver_time_us) {
    svrt_status_server *server = opaque;
    if (!server || pts_us == UINT64_MAX) return;
    if (event == SVRT_PACKET_RECEIVED) {
        atomic_store(&server->received_time_us, receiver_time_us);
        atomic_store(&server->received_pts_us, pts_us);
    } else if (event == SVRT_PACKET_PROCESSED) {
        atomic_store(&server->processed_time_us, receiver_time_us);
        atomic_store(&server->processed_pts_us, pts_us);
    }
}

void svrt_status_server_reset_trace(svrt_status_server *server) {
    if (!server) return;
    atomic_store(&server->received_pts_us, UINT64_MAX);
    atomic_store(&server->received_time_us, 0);
    atomic_store(&server->processed_pts_us, UINT64_MAX);
    atomic_store(&server->processed_time_us, 0);
}

void svrt_status_server_pairing_code(svrt_status_server *server, char out[5]) {
    if (!out) return;
    out[0] = '\0';
    if (!server) return;
    pthread_mutex_lock(&server->pairing_lock);
    refresh_pairing_code_locked(server);
    if (!server->paired_client[0]) memcpy(out, server->pairing_code, sizeof(server->pairing_code));
    pthread_mutex_unlock(&server->pairing_lock);
}

int svrt_status_server_is_paired(svrt_status_server *server) {
    if (!server) return 0;
    pthread_mutex_lock(&server->pairing_lock);
    int paired = server->paired_client[0] != '\0';
    pthread_mutex_unlock(&server->pairing_lock);
    return paired;
}

int svrt_status_server_pairing_in_progress(svrt_status_server *server) {
    if (!server) return 0;
    const time_t now = time(NULL);
    pthread_mutex_lock(&server->pairing_lock);
    const int pairing = server->pairing_started && now - server->pairing_started < 2;
    pthread_mutex_unlock(&server->pairing_lock);
    return pairing;
}

void svrt_status_server_stop(svrt_status_server *server) {
    if (!server || !server->thread) return;
    atomic_store(&server->stopping, 1);
    pthread_t *thread = server->thread;
    pthread_join(*thread, NULL);
    free(thread);
    server->thread = NULL;
    pthread_mutex_lock(&server->clients_lock);
    while (server->active_clients) pthread_cond_wait(&server->clients_done,
                                                       &server->clients_lock);
    pthread_mutex_unlock(&server->clients_lock);
    pthread_cond_destroy(&server->clients_done);
    pthread_mutex_destroy(&server->clients_lock);
    pthread_mutex_destroy(&server->pairing_lock);
}
