#include "status_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return -1;
        data += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static void answer_client(svrt_status_server *server, int fd) {
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char request[128] = {0};
    ssize_t length = recv(fd, request, sizeof(request) - 1, 0);
    unsigned long long nonce = 0;
    if (length <= 0 || sscanf(request, "SVRT/1 PING %llu", &nonce) != 1) return;
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
            answer_client(server, client);
            close(client);
        }
    }
    close(listener);
    return NULL;
}

int svrt_status_server_start(svrt_status_server *server, uint16_t port) {
    if (!server) return -1;
    memset(server, 0, sizeof(*server));
    server->port = port ? port : 9945;
    atomic_store(&server->state, SVRT_RECEIVER_STARTING);
    pthread_t *thread = malloc(sizeof(*thread));
    if (!thread || pthread_create(thread, NULL, status_thread, server)) {
        free(thread);
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

void svrt_status_server_stop(svrt_status_server *server) {
    if (!server || !server->thread) return;
    atomic_store(&server->stopping, 1);
    pthread_t *thread = server->thread;
    pthread_join(*thread, NULL);
    free(thread);
    server->thread = NULL;
}
