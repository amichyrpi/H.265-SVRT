#include <svrt/pipe.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
struct svrt_pipe { AVFormatContext *input; int video_stream; svrt_pipe_interrupt interrupt; void *opaque; };
static int interrupted(void *opaque){svrt_pipe *p=opaque;return p->interrupt?p->interrupt(p->opaque):0;}
int svrt_pipe_make_url(char *out,unsigned n,const svrt_pipe_config *c){if(!out||!n||!c)return -1;int used=snprintf(out,n,"tcp://%s:%u?listen=1&tcp_nodelay=1",c->bind_address?c->bind_address:"0.0.0.0",c->port?c->port:9944);return used<0||(unsigned)used>=n?-1:0;}
int svrt_pipe_listen(svrt_pipe **out,const svrt_pipe_config *c,char *error,unsigned error_size){if(!out||!c)return -1;*out=NULL;svrt_pipe *p=calloc(1,sizeof(*p));if(!p)return -1;p->video_stream=-1;p->interrupt=c->interrupt;p->opaque=c->opaque;p->input=avformat_alloc_context();if(!p->input)goto fail;p->input->interrupt_callback=(AVIOInterruptCB){interrupted,p};char url[256];if(svrt_pipe_make_url(url,sizeof(url),c))goto fail;int rc=avformat_open_input(&p->input,url,NULL,NULL);if(rc<0)goto fail;if(avformat_find_stream_info(p->input,NULL)<0)goto fail;for(unsigned i=0;i<p->input->nb_streams;i++)if(p->input->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){p->video_stream=(int)i;break;}if(p->video_stream<0)goto fail;*out=p;return 0;fail:if(error&&error_size)snprintf(error,error_size,"could not open MPEG-TS listener");svrt_pipe_close(&p);return -1;}
const AVCodecParameters *svrt_pipe_video_parameters(const svrt_pipe *p){return p&&p->video_stream>=0?p->input->streams[p->video_stream]->codecpar:NULL;}
AVRational svrt_pipe_time_base(const svrt_pipe *p){return p&&p->video_stream>=0?p->input->streams[p->video_stream]->time_base:(AVRational){1,1000000};}
int svrt_pipe_read(svrt_pipe *p,AVPacket *packet){if(!p||!packet)return AVERROR(EINVAL);int rc;while((rc=av_read_frame(p->input,packet))>=0){if(packet->stream_index==p->video_stream)return 0;av_packet_unref(packet);}return rc;}
void svrt_pipe_close(svrt_pipe **ptr){if(!ptr||!*ptr)return;svrt_pipe *p=*ptr;*ptr=NULL;avformat_close_input(&p->input);free(p);}
