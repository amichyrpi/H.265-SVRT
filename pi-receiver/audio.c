#include "audio.h"
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <glob.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#pragma pack(push,1)
typedef struct audio_header {char magic[4];uint8_t version,type;uint16_t header_size;uint32_t sequence,rate;uint16_t channels,bits,format,payload_size;} audio_header;
#pragma pack(pop)

static int open_output(snd_pcm_t **pcm,char *name,size_t size){glob_t paths;if(!glob("/sys/class/drm/card*-HDMI-A-*/status",0,NULL,&paths)){for(size_t i=0;i<paths.gl_pathc;i++){FILE *f=fopen(paths.gl_pathv[i],"r");char state[32]={0};if(f){fgets(state,sizeof(state),f);fclose(f);}const char *p=strstr(paths.gl_pathv[i],"HDMI-A-");int n=p?atoi(p+7):0;if(!strncmp(state,"connected",9)&&n>0){snprintf(name,size,"plughw:CARD=vc4hdmi%d,DEV=0",n-1);if(snd_pcm_open(pcm,name,SND_PCM_STREAM_PLAYBACK,0)>=0){globfree(&paths);return 0;}}}globfree(&paths);}snprintf(name,size,"plughw:CARD=Headphones,DEV=0");return snd_pcm_open(pcm,name,SND_PCM_STREAM_PLAYBACK,0);}

static void *audio_thread(void *opaque){svrt_audio_receiver *receiver=opaque;int fd=socket(AF_INET6,SOCK_DGRAM,0);if(fd<0)return NULL;int one=1,off=0,buffer=512*1024;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&buffer,sizeof(buffer));setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&off,sizeof(off));struct sockaddr_in6 address={.sin6_family=AF_INET6,.sin6_addr=IN6ADDR_ANY_INIT,.sin6_port=htons(receiver->port)};if(bind(fd,(struct sockaddr*)&address,sizeof(address))){fprintf(stderr,"SVRT audio: cannot listen on UDP %u: %s\n",receiver->port,strerror(errno));close(fd);return NULL;}fprintf(stderr,"SVRT audio: listening on UDP %u\n",receiver->port);
  snd_pcm_t *pcm=NULL;unsigned current_rate=0,current_channels=0,current_bits=0,current_format=0;uint8_t datagram[1200];uint32_t last_sequence=0;time_t retry_output_after=0;
  while(!atomic_load(&receiver->stopping)){struct pollfd wait={.fd=fd,.events=POLLIN};if(poll(&wait,1,250)<=0)continue;ssize_t size=recv(fd,datagram,sizeof(datagram),0);if(size<(ssize_t)sizeof(audio_header))continue;audio_header header;memcpy(&header,datagram,sizeof(header));unsigned payload=ntohs(header.payload_size),rate=ntohl(header.rate),channels=ntohs(header.channels),bits=ntohs(header.bits),format_id=ntohs(header.format);uint32_t sequence=ntohl(header.sequence);if(memcmp(header.magic,"SVRA",4)||header.version!=2||header.type!=4||ntohs(header.header_size)!=sizeof(header)||sizeof(header)+payload!=(size_t)size||!payload)continue;snd_pcm_format_t format=format_id==3&&bits==32?SND_PCM_FORMAT_FLOAT_LE:format_id==1&&bits==16?SND_PCM_FORMAT_S16_LE:SND_PCM_FORMAT_UNKNOWN;if(!rate||!channels||format==SND_PCM_FORMAT_UNKNOWN)continue;
    if(!pcm||rate!=current_rate||channels!=current_channels||bits!=current_bits||format_id!=current_format){if(pcm){snd_pcm_drop(pcm);snd_pcm_close(pcm);pcm=NULL;}if(time(NULL)<retry_output_after)continue;char output[96];int rc=open_output(&pcm,output,sizeof(output));if(rc<0){fprintf(stderr,"SVRT audio: cannot open output: %s; retrying in 2 seconds\n",snd_strerror(rc));retry_output_after=time(NULL)+2;continue;}rc=snd_pcm_set_params(pcm,format,SND_PCM_ACCESS_RW_INTERLEAVED,channels,rate,1,40000);if(rc<0){snd_pcm_close(pcm);pcm=NULL;retry_output_after=time(NULL)+2;continue;}current_rate=rate;current_channels=channels;current_bits=bits;current_format=format_id;fprintf(stderr,"SVRT audio: playing %u Hz/%u ch through %s\n",rate,channels,output);}
    const size_t frame_size=channels*(bits/8);snd_pcm_sframes_t frames=(snd_pcm_sframes_t)(payload/frame_size);if(!frames)continue;if(last_sequence&&sequence!=last_sequence+1)fprintf(stderr,"SVRT audio: packet gap %u -> %u\n",last_sequence,sequence);last_sequence=sequence;const uint8_t *cursor=datagram+sizeof(header);while(frames>0){snd_pcm_sframes_t written=snd_pcm_writei(pcm,cursor,frames);if(written==-EPIPE){snd_pcm_prepare(pcm);continue;}if(written<0){written=snd_pcm_recover(pcm,(int)written,1);if(written<0)break;continue;}cursor+=(size_t)written*frame_size;frames-=written;}
  }
  if(pcm){snd_pcm_drop(pcm);snd_pcm_close(pcm);}close(fd);return NULL;
}
int svrt_audio_receiver_start(svrt_audio_receiver *receiver,uint16_t port){memset(receiver,0,sizeof(*receiver));receiver->port=port;if(pthread_create(&receiver->thread,NULL,audio_thread,receiver))return -1;receiver->started=1;return 0;}
void svrt_audio_receiver_stop(svrt_audio_receiver *receiver){if(!receiver||!receiver->started)return;atomic_store(&receiver->stopping,1);pthread_join(receiver->thread,NULL);receiver->started=0;}
