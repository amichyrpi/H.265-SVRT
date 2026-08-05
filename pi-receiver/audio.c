#include "audio.h"

#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <glob.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#pragma pack(push, 1)
typedef struct audio_header {
    char magic[4];
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t format;
} audio_header;
#pragma pack(pop)

static int receive_all(int fd, void *data, size_t size) {
    unsigned char *cursor = data;
    while (size) {
        ssize_t count = recv(fd, cursor, size, 0);
        if (count <= 0) return -1;
        cursor += count;
        size -= (size_t)count;
    }
    return 0;
}

static int open_output(snd_pcm_t **pcm, char *name, size_t name_size) {
    glob_t paths;
    if (!glob("/sys/class/drm/card*-HDMI-A-*/status", 0, NULL, &paths)) {
        for (size_t i = 0; i < paths.gl_pathc; ++i) {
            FILE *status = fopen(paths.gl_pathv[i], "r");
            char state[32] = {0};
            if (status) {
                fgets(state, sizeof(state), status);
                fclose(status);
            }
            const char *port = strstr(paths.gl_pathv[i], "HDMI-A-");
            int number = port ? atoi(port + 7) : 0;
            if (!strncmp(state, "connected", 9) && number > 0) {
                snprintf(name, name_size, "plughw:CARD=vc4hdmi%d,DEV=0",
                         number - 1);
                if (snd_pcm_open(pcm, name, SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
                    globfree(&paths);
                    return 0;
                }
            }
        }
        globfree(&paths);
    }
    snprintf(name, name_size, "plughw:CARD=Headphones,DEV=0");
    return snd_pcm_open(pcm, name, SND_PCM_STREAM_PLAYBACK, 0);
}

static void play_client(int fd, svrt_audio_receiver *receiver) {
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    audio_header header;
    if (receive_all(fd, &header, sizeof(header)) ||
        memcmp(header.magic, "SVRA", 4)) return;
    unsigned rate = ntohl(header.rate);
    unsigned channels = ntohs(header.channels);
    unsigned bits = ntohs(header.bits);
    unsigned format_id = ntohl(header.format);
    snd_pcm_format_t format = format_id == 3 && bits == 32
                                  ? SND_PCM_FORMAT_FLOAT_LE
                                  : format_id == 1 && bits == 16
                                        ? SND_PCM_FORMAT_S16_LE
                                        : SND_PCM_FORMAT_UNKNOWN;
    if (!rate || !channels || format == SND_PCM_FORMAT_UNKNOWN) {
        fprintf(stderr, "SVRT audio: unsupported stream %u Hz/%u ch/%u-bit format %u\n",
                rate, channels, bits, format_id);
        return;
    }
    snd_pcm_t *pcm = NULL;
    char output[96];
    int rc = open_output(&pcm, output, sizeof(output));
    if (rc < 0) {
        fprintf(stderr, "SVRT audio: cannot open HDMI or analogue output: %s\n",
                snd_strerror(rc));
        return;
    }
    rc = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED,
                            channels, rate, 1, 40000);
    if (rc < 0) {
        fprintf(stderr, "SVRT audio: cannot configure ALSA: %s\n",
                snd_strerror(rc));
        snd_pcm_close(pcm);
        return;
    }
    fprintf(stderr, "SVRT audio: playing %u Hz, %u channels through %s\n",
            rate, channels, output);
    const size_t frame_size = channels * (bits / 8);
    unsigned char buffer[32768];
    while (!atomic_load(&receiver->stopping)) {
        ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (bytes <= 0) break;
        snd_pcm_sframes_t frames = (snd_pcm_sframes_t)((size_t)bytes / frame_size);
        unsigned char *cursor = buffer;
        while (frames > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(pcm, cursor, frames);
            if (written == -EPIPE) {
                snd_pcm_prepare(pcm);
                continue;
            }
            if (written < 0) {
                written = snd_pcm_recover(pcm, (int)written, 1);
                if (written < 0) break;
                continue;
            }
            cursor += (size_t)written * frame_size;
            frames -= written;
        }
    }
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    fprintf(stderr, "SVRT audio: stream disconnected\n");
}

static void *audio_thread(void *opaque) {
    svrt_audio_receiver *receiver = opaque;
    int listener = socket(AF_INET6, SOCK_STREAM, 0);
    if (listener < 0) return NULL;
    int enabled = 1;
    int disabled = 0;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &disabled, sizeof(disabled));
    struct sockaddr_in6 address = {.sin6_family = AF_INET6,
                                   .sin6_addr = IN6ADDR_ANY_INIT,
                                   .sin6_port = htons(receiver->port)};
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        listen(listener, 1)) {
        fprintf(stderr, "SVRT audio: cannot listen on TCP %u: %s\n",
                receiver->port, strerror(errno));
        close(listener);
        return NULL;
    }
    fprintf(stderr, "SVRT audio: listening on TCP %u (connected HDMI, then analogue fallback)\n",
            receiver->port);
    while (!atomic_load(&receiver->stopping)) {
        struct pollfd wait = {.fd = listener, .events = POLLIN};
        if (poll(&wait, 1, 250) <= 0) continue;
        int client = accept(listener, NULL, NULL);
        if (client >= 0) {
            play_client(client, receiver);
            close(client);
        }
    }
    close(listener);
    return NULL;
}

int svrt_audio_receiver_start(svrt_audio_receiver *receiver, uint16_t port) {
    memset(receiver, 0, sizeof(*receiver));
    receiver->port = port;
    pthread_t thread;
    if (pthread_create(&thread, NULL, audio_thread, receiver)) return -1;
    receiver->thread = thread;
    receiver->started = 1;
    return 0;
}

void svrt_audio_receiver_stop(svrt_audio_receiver *receiver) {
    if (!receiver || !receiver->started) return;
    atomic_store(&receiver->stopping, 1);
    pthread_join(receiver->thread, NULL);
    receiver->started = 0;
}
