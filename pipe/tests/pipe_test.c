#include <pipe.h>
#include <assert.h>
#include <string.h>
int main(void){char url[128];svrt_pipe_config c={.bind_address="127.0.0.1",.port=1234};assert(svrt_pipe_make_url(url,sizeof(url),&c)==0);assert(strcmp(url,"tcp://127.0.0.1:1234?listen=1&tcp_nodelay=1&recv_buffer_size=65536")==0);char tiny[4];assert(svrt_pipe_make_url(tiny,sizeof(tiny),&c)<0);return 0;}
