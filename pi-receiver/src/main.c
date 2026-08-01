#include <svrt/svrt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static svrt_context *running;
static void stop(int sig){(void)sig;svrt_stop(running);}
int main(int argc,char **argv){
    int headless=0;uint16_t port=9944;
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){printf("usage: svrt-receiver [--headless] [port]\n");return 0;}else if(!strcmp(argv[i],"--headless"))headless=1;else port=(uint16_t)atoi(argv[i]);}
    svrt_config cfg={.port=port,.require_hardware=1,.require_zero_copy=1,.fullscreen=1,.headless=headless};
    signal(SIGINT,stop);signal(SIGTERM,stop);if(svrt_open(&running,&cfg))return 1;int rc=svrt_run(running);if(rc)fprintf(stderr,"SVRT: %s\n",svrt_last_error(running));
    svrt_stats s;svrt_get_stats(running,&s);fprintf(stderr,"SVRT: %llu decoded, %llu shown, %llu dropped\n",(unsigned long long)s.decoded_frames,(unsigned long long)s.presented_frames,(unsigned long long)s.dropped_frames);svrt_close(&running);return rc?1:0;
}
