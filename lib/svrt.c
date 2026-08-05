#include <svrt.h>
#include <pipe.h>
#include "drm_presenter.h"
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>

struct svrt_context {
    svrt_config cfg; atomic_int stopping;
    svrt_pipe *pipe;
    AVCodecContext *decoder; AVBufferRef *hw_device; AVFrame *frame; AVPacket *packet;
    SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *texture;
    svrt_drm *drm;
    struct {
        atomic_uint_fast64_t access_units, decoded_frames, presented_frames;
        atomic_uint_fast64_t dropped_frames, bytes_received, last_pts_us;
    } stats;
    uint64_t first_frame_us, last_report_us, last_report_frame;
    char error[256];
};
static void set_error(svrt_context *c,const char *fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(c->error,sizeof(c->error),fmt,ap);va_end(ap);}
static uint64_t monotonic_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void packet_event(svrt_context *c,svrt_packet_event event,uint64_t pts_us){if(c->cfg.packet_event)c->cfg.packet_event(c->cfg.packet_event_opaque,event,pts_us,monotonic_us());}
static enum AVPixelFormat choose_format(AVCodecContext *unused,const enum AVPixelFormat *fmts){(void)unused;for(const enum AVPixelFormat *p=fmts;*p!=AV_PIX_FMT_NONE;p++)if(*p==AV_PIX_FMT_DRM_PRIME)return *p;return fmts[0];}
static const AVCodec *find_decoder(int require_hw){(void)require_hw;return avcodec_find_decoder(AV_CODEC_ID_HEVC);}
static int open_video(svrt_context *c,const AVCodecParameters *parameters){
    const AVCodec *codec=find_decoder(c->cfg.require_hardware);if(!codec){set_error(c,"HEVC decoder not found");return -1;}
    if(parameters)fprintf(stderr,"SVRT: stream=%dx%d codec=%s\n",parameters->width,parameters->height,avcodec_get_name(parameters->codec_id));
    c->decoder=avcodec_alloc_context3(codec);if(!c->decoder){set_error(c,"avcodec_alloc_context3 failed");return -1;}if(parameters&&avcodec_parameters_to_context(c->decoder,parameters)<0){set_error(c,"invalid HEVC stream parameters");return -1;}if(c->cfg.require_hardware){int hw=av_hwdevice_ctx_create(&c->hw_device,AV_HWDEVICE_TYPE_DRM,NULL,NULL,0);if(hw<0){set_error(c,"could not open FFmpeg DRM hardware device: %d",hw);return -1;}c->decoder->hw_device_ctx=av_buffer_ref(c->hw_device);}c->decoder->flags|=AV_CODEC_FLAG_LOW_DELAY;c->decoder->thread_count=1;c->decoder->get_format=choose_format;
    AVDictionary *opts=NULL;av_dict_set(&opts,"num_capture_buffers","6",0);int rc=avcodec_open2(c->decoder,codec,&opts);av_dict_free(&opts);if(rc<0){char e[128];av_strerror(rc,e,sizeof(e));set_error(c,"opening decoder %s failed: %s",codec->name,e);return -1;}
    c->frame=av_frame_alloc();c->packet=av_packet_alloc();if(!c->frame||!c->packet){set_error(c,"FFmpeg allocation failed");return -1;}fprintf(stderr,"SVRT: decoder=%s\n",codec->name);return 0;
}
static int open_display(svrt_context *c){
    SDL_setenv("SDL_VIDEODRIVER","kmsdrm",0);SDL_SetHint(SDL_HINT_RENDER_VSYNC,"0");if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)){set_error(c,"SDL_Init: %s",SDL_GetError());return -1;}
    fprintf(stderr,"SVRT: SDL video=%s displays=%d\n",SDL_GetCurrentVideoDriver(),SDL_GetNumVideoDisplays());
    uint32_t flags=SDL_WINDOW_SHOWN|SDL_WINDOW_BORDERLESS;if(c->cfg.fullscreen)flags|=SDL_WINDOW_FULLSCREEN_DESKTOP;c->window=SDL_CreateWindow("SVRT HEVC",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,640,480,flags);if(!c->window){set_error(c,"SDL_CreateWindow: %s",SDL_GetError());return -1;}
    int drm_rc=svrt_drm_open(&c->drm,c->window,c->error,sizeof(c->error));if(drm_rc&&c->cfg.require_zero_copy)return -1;
    if(!c->drm){c->renderer=SDL_CreateRenderer(c->window,-1,SDL_RENDERER_ACCELERATED);if(!c->renderer){set_error(c,"SDL_CreateRenderer: %s",SDL_GetError());return -1;}c->error[0]='\0';}
    return 0;
}
static int interrupted(void *opaque){return atomic_load(&((svrt_context*)opaque)->stopping);}
int svrt_open(svrt_context **out,const svrt_config *cfg){if(!out)return -1;*out=NULL;svrt_context *c=calloc(1,sizeof(*c));if(!c)return -1;if(cfg)c->cfg=*cfg;else c->cfg=(svrt_config){.port=9944,.require_hardware=1,.require_zero_copy=1,.fullscreen=1};if(!c->cfg.port)c->cfg.port=9944;if(!c->cfg.headless&&open_display(c)){fprintf(stderr,"SVRT: %s\n",c->error);svrt_close(&c);return -1;}*out=c;return 0;}
static int software_present(svrt_context *c,AVFrame *f){uint32_t fmt;if(f->format==AV_PIX_FMT_YUV420P)fmt=SDL_PIXELFORMAT_IYUV;else if(f->format==AV_PIX_FMT_NV12)fmt=SDL_PIXELFORMAT_NV12;else{set_error(c,"unsupported software pixel format %s",av_get_pix_fmt_name(f->format));return -1;}if(c->texture){Uint32 texture_fmt=0;int texture_w=0,texture_h=0;if(SDL_QueryTexture(c->texture,&texture_fmt,NULL,&texture_w,&texture_h)){set_error(c,"SDL_QueryTexture: %s",SDL_GetError());return -1;}if(texture_fmt!=fmt||texture_w!=f->width||texture_h!=f->height){SDL_DestroyTexture(c->texture);c->texture=NULL;}}if(!c->texture)c->texture=SDL_CreateTexture(c->renderer,fmt,SDL_TEXTUREACCESS_STREAMING,f->width,f->height);if(!c->texture){set_error(c,"SDL_CreateTexture: %s",SDL_GetError());return -1;}int rc=f->format==AV_PIX_FMT_YUV420P?SDL_UpdateYUVTexture(c->texture,NULL,f->data[0],f->linesize[0],f->data[1],f->linesize[1],f->data[2],f->linesize[2]):SDL_UpdateNVTexture(c->texture,NULL,f->data[0],f->linesize[0],f->data[1],f->linesize[1]);if(rc||SDL_RenderClear(c->renderer)||SDL_RenderCopy(c->renderer,c->texture,NULL,NULL)){set_error(c,"SDL render: %s",SDL_GetError());return -1;}SDL_RenderPresent(c->renderer);return 0;}
static int decode(svrt_context *c,AVRational time_base,uint64_t packet_pts_us,int flush){int rc=avcodec_send_packet(c->decoder,flush?NULL:c->packet);if(rc<0&&rc!=AVERROR(EAGAIN)){set_error(c,"avcodec_send_packet failed: %d",rc);return -1;}while((rc=avcodec_receive_frame(c->decoder,c->frame))>=0){uint64_t decoded=atomic_fetch_add(&c->stats.decoded_frames,1)+1;int shown;if(c->frame->format!=AV_PIX_FMT_DRM_PRIME&&(c->cfg.require_hardware||c->cfg.require_zero_copy))shown=-1;else if(c->cfg.headless)shown=0;else if(c->frame->format==AV_PIX_FMT_DRM_PRIME&&c->drm)shown=svrt_drm_present(c->drm,c->frame,c->error,sizeof(c->error));else if(!c->cfg.require_hardware&&!c->cfg.require_zero_copy&&c->renderer)shown=software_present(c,c->frame);else shown=-1;if(shown&&!c->error[0])set_error(c,"decoder returned %s instead of DRM PRIME",av_get_pix_fmt_name(c->frame->format));if(shown)atomic_fetch_add(&c->stats.dropped_frames,1);else{atomic_fetch_add(&c->stats.presented_frames,1);int64_t pts=c->frame->best_effort_timestamp;uint64_t pts_us=pts==AV_NOPTS_VALUE?packet_pts_us:(uint64_t)av_rescale_q(pts,time_base,AV_TIME_BASE_Q);packet_event(c,SVRT_PACKET_PROCESSED,pts_us);}uint64_t now=monotonic_us();if(decoded==1){c->first_frame_us=c->last_report_us=now;c->last_report_frame=decoded;}if(decoded==1||decoded%60==0){double fps=0.0,average=0.0;if(now>c->last_report_us)fps=(double)(decoded-c->last_report_frame)*1000000.0/(double)(now-c->last_report_us);if(now>c->first_frame_us)average=(double)(decoded-1)*1000000.0/(double)(now-c->first_frame_us);fprintf(stderr,"SVRT: frame=%llu format=%s shown=%llu dropped=%llu fps=%.2f average_fps=%.2f\n",(unsigned long long)decoded,av_get_pix_fmt_name(c->frame->format),(unsigned long long)atomic_load(&c->stats.presented_frames),(unsigned long long)atomic_load(&c->stats.dropped_frames),fps,average);c->last_report_us=now;c->last_report_frame=decoded;}av_frame_unref(c->frame);if(shown)return -1;}return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:-1;}
int svrt_run(svrt_context *c){if(!c)return -1;svrt_pipe_config pc={.bind_address=c->cfg.bind_address,.port=c->cfg.port,.interrupt=interrupted,.opaque=c};fprintf(stderr,"SVRT: listening for MPEG-TS on TCP %u\n",c->cfg.port);if(svrt_pipe_listen(&c->pipe,&pc,c->error,sizeof(c->error)))return -1;if(open_video(c,svrt_pipe_video_parameters(c->pipe)))return -1;AVRational time_base=svrt_pipe_time_base(c->pipe);c->decoder->pkt_timebase=time_base;int rc=0;while(!atomic_load(&c->stopping)){SDL_Event e;while(!c->cfg.headless&&SDL_PollEvent(&e))if(e.type==SDL_QUIT||e.type==SDL_KEYDOWN)atomic_store(&c->stopping,1);rc=svrt_pipe_read(c->pipe,c->packet);if(rc<0)break;atomic_fetch_add(&c->stats.access_units,1);atomic_fetch_add(&c->stats.bytes_received,(uint64_t)c->packet->size);uint64_t pts_us=c->packet->pts==AV_NOPTS_VALUE?UINT64_MAX:(uint64_t)av_rescale_q(c->packet->pts,time_base,AV_TIME_BASE_Q);if(pts_us!=UINT64_MAX){atomic_store(&c->stats.last_pts_us,pts_us);packet_event(c,SVRT_PACKET_RECEIVED,pts_us);}if(decode(c,time_base,pts_us,0)){av_packet_unref(c->packet);return -1;}av_packet_unref(c->packet);}if(!atomic_load(&c->stopping)&&decode(c,time_base,UINT64_MAX,1))return -1;if(rc<0&&!atomic_load(&c->stopping)&&!atomic_load(&c->stats.decoded_frames))set_error(c,"video stream ended: %d",rc);return c->error[0]?-1:0;}
void svrt_stop(svrt_context *c){if(c)atomic_store(&c->stopping,1);}
void svrt_get_stats(const svrt_context *c,svrt_stats *out){if(c&&out)*out=(svrt_stats){.access_units=atomic_load(&c->stats.access_units),.decoded_frames=atomic_load(&c->stats.decoded_frames),.presented_frames=atomic_load(&c->stats.presented_frames),.dropped_frames=atomic_load(&c->stats.dropped_frames),.bytes_received=atomic_load(&c->stats.bytes_received),.last_pts_us=atomic_load(&c->stats.last_pts_us)};}
const char *svrt_last_error(const svrt_context *c){return c?c->error:"invalid context";}
void svrt_close(svrt_context **ptr){if(!ptr||!*ptr)return;svrt_context *c=*ptr;*ptr=NULL;svrt_pipe_close(&c->pipe);svrt_drm_close(&c->drm);if(c->texture)SDL_DestroyTexture(c->texture);if(c->renderer)SDL_DestroyRenderer(c->renderer);if(c->window)SDL_DestroyWindow(c->window);SDL_Quit();av_packet_free(&c->packet);av_frame_free(&c->frame);avcodec_free_context(&c->decoder);av_buffer_unref(&c->hw_device);free(c);}
