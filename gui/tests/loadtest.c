#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exercise the same HEVC encode/decode workload that the wireless path uses.
 * Set SVRT_LOAD_SECONDS to raise the duration when profiling on the Pi. */
int main(void) {
    const char *seconds = getenv("SVRT_LOAD_SECONDS");
    if (!seconds) seconds = "5";
    char command[1024];
    snprintf(command, sizeof(command),
        "ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=30 "
        "-t %s -pix_fmt yuv420p -c:v libx265 -preset ultrafast -f hevc - | "
        "ffmpeg -hide_banner -loglevel error -f hevc -i - -f null -", seconds);
    int result = system(command);
    if (result == -1 || !WIFEXITED(result) || WEXITSTATUS(result)) {
        fprintf(stderr, "SVRT GUI load test failed: encoder/decoder pipeline unavailable\n");
        return 1;
    }
    puts("SVRT GUI load test passed");
    return 0;
}
