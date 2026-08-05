#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exercise the same HEVC encode/decode workload that the wireless path uses.
 * Set SVRT_LOAD_SECONDS to raise the duration when profiling on the Pi. */
int main(void) {
    const char *seconds = getenv("SVRT_LOAD_SECONDS");
    unsigned long duration = 5;
    if (seconds) {
        char *end = NULL;
        for (const char *digit = seconds; *digit; ++digit) {
            if (*digit < '0' || *digit > '9') {
                fprintf(stderr, "SVRT_LOAD_SECONDS must be an integer from 1 to 3600\n");
                return 1;
            }
        }
        errno = 0;
        duration = strtoul(seconds, &end, 10);
        if (errno || end == seconds || *end || duration == 0 || duration > 3600) {
            fprintf(stderr, "SVRT_LOAD_SECONDS must be an integer from 1 to 3600\n");
            return 1;
        }
    }
    char command[1024];
    int command_size = snprintf(command, sizeof(command),
        "ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=30 "
        "-t %lu -pix_fmt yuv420p -c:v libx265 -preset ultrafast -f hevc - | "
        "ffmpeg -hide_banner -loglevel error -f hevc -i - -f null -", duration);
    if (command_size < 0 || (size_t)command_size >= sizeof(command)) {
        fprintf(stderr, "SVRT GUI load test command is too long\n");
        return 1;
    }
    int result = system(command);
    if (result == -1 || !WIFEXITED(result) || WEXITSTATUS(result)) {
        fprintf(stderr, "SVRT GUI load test failed: encoder/decoder pipeline unavailable\n");
        return 1;
    }
    puts("SVRT GUI load test passed");
    return 0;
}
