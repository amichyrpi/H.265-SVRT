/* SPDX-License-Identifier: GPL-2.0-only
 * Adapted from Vanilla's ui_sdl_drm.c; scoped to DRM PRIME scanout.
 */
#include "drm_presenter.h"
#include "drm_geometry.h"
#include <errno.h>
#include <libavutil/hwcontext_drm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <SDL_syswm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

#define SVRT_MAX_PLANES 4
struct svrt_drm {
    int fd, crtc_index;
    uint32_t crtc, crtc_w, crtc_h, plane, fb;
    uint32_t handles[SVRT_MAX_PLANES];
    int handle_count, has_fb, has_plane;
    uint32_t hud_plane, hud_fb, hud_handle, hud_pitch, hud_height;
    uint8_t *hud_pixels;
    size_t hud_size;
    uint64_t fps_started_ns, fps_frames;
    int hud_state;
};

/* A dependency-free 5x7 font. Bits 4..0 are the pixels in each row. */
static const uint8_t glyphs[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {0,0,0,0,0,12,12}, /* . */
    {31,16,16,30,16,16,16}, /* F */
    {30,17,17,30,16,16,16}, /* P */
    {15,16,16,14,1,1,30}, /* S */
    {0,0,0,0,0,0,0} /* space */
};

static void fail(char *out,size_t n,const char *what) { if(out&&n)snprintf(out,n,"%s: %s",what,strerror(errno)); }
static uint64_t property(int fd,uint32_t object,uint32_t type,const char *name) {
    drmModeObjectPropertiesPtr ps=drmModeObjectGetProperties(fd,object,type); if(!ps)return 0;
    uint64_t result=0; for(uint32_t i=0;i<ps->count_props;i++){ drmModePropertyPtr p=drmModeGetProperty(fd,ps->props[i]); if(p){ if(!strcmp(p->name,name))result=ps->prop_values[i]; drmModeFreeProperty(p); if(result)break; }}
    drmModeFreeObjectProperties(ps); return result;
}
static uint32_t property_id(int fd,uint32_t object,uint32_t type,const char *name,uint64_t *value) {
    drmModeObjectPropertiesPtr ps=drmModeObjectGetProperties(fd,object,type);if(!ps)return 0;
    uint32_t result=0;for(uint32_t i=0;i<ps->count_props;i++){drmModePropertyPtr p=drmModeGetProperty(fd,ps->props[i]);if(p){if(!strcmp(p->name,name)){result=p->prop_id;if(value)*value=ps->prop_values[i];}drmModeFreeProperty(p);if(result)break;}}
    drmModeFreeObjectProperties(ps);return result;
}
static uint64_t monotonic_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static int supports_format(const drmModePlane *p,uint32_t format){for(uint32_t i=0;i<p->count_formats;i++)if(p->formats[i]==format)return 1;return 0;}
static int choose_plane(svrt_drm *c,uint32_t format) {
    drmModePlaneResPtr rs=drmModeGetPlaneResources(c->fd); if(!rs)return -1;
    for(uint32_t i=0;i<rs->count_planes&&!c->has_plane;i++){ drmModePlanePtr p=drmModeGetPlane(c->fd,rs->planes[i]); if(!p)continue;
        if(p->possible_crtcs&(1u<<c->crtc_index)){ uint64_t type=property(c->fd,p->plane_id,DRM_MODE_OBJECT_PLANE,"type"); for(uint32_t j=0;j<p->count_formats;j++) if(p->formats[j]==format && (!type||type==DRM_PLANE_TYPE_OVERLAY)){c->plane=p->plane_id;c->has_plane=1;break;} }
        drmModeFreePlane(p);
    } drmModeFreePlaneResources(rs); return c->has_plane?0:-1;
}
static void release_old(svrt_drm *c) {
    if(c->has_fb){drmModeRmFB(c->fd,c->fb);c->has_fb=0;}
    for(int i=0;i<c->handle_count;i++){struct drm_gem_close gc={.handle=c->handles[i]};drmIoctl(c->fd,DRM_IOCTL_GEM_CLOSE,&gc);}
    c->handle_count=0;
}
static void set_zpos(svrt_drm *c,uint32_t plane,uint64_t value){uint32_t id=property_id(c->fd,plane,DRM_MODE_OBJECT_PLANE,"zpos",NULL);if(id)drmModeObjectSetProperty(c->fd,plane,DRM_MODE_OBJECT_PLANE,id,value);}
static int open_hud(svrt_drm *c) {
    drmModePlaneResPtr rs=drmModeGetPlaneResources(c->fd);if(!rs)return -1;
    uint64_t best_z=0;uint32_t best=0;
    for(uint32_t i=0;i<rs->count_planes;i++){drmModePlanePtr p=drmModeGetPlane(c->fd,rs->planes[i]);if(!p)continue;
        uint64_t type=property(c->fd,p->plane_id,DRM_MODE_OBJECT_PLANE,"type"),z=property(c->fd,p->plane_id,DRM_MODE_OBJECT_PLANE,"zpos");
        if(p->plane_id!=c->plane&&(p->possible_crtcs&(1u<<c->crtc_index))&&supports_format(p,DRM_FORMAT_ARGB8888)&&(!type||type==DRM_PLANE_TYPE_OVERLAY)&&(!best||z>=best_z)){best=p->plane_id;best_z=z;}
        drmModeFreePlane(p);
    }drmModeFreePlaneResources(rs);if(!best)return -1;c->hud_plane=best;
    c->hud_height=c->crtc_h<720?32:48;struct drm_mode_create_dumb create={0};create.width=c->crtc_w;create.height=c->hud_height;create.bpp=32;
    if(drmIoctl(c->fd,DRM_IOCTL_MODE_CREATE_DUMB,&create))return -1;c->hud_handle=create.handle;c->hud_pitch=create.pitch;c->hud_size=create.size;
    uint32_t handles[4]={create.handle},pitches[4]={create.pitch},offsets[4]={0};
    if(drmModeAddFB2(c->fd,create.width,create.height,DRM_FORMAT_ARGB8888,handles,pitches,offsets,&c->hud_fb,0))return -1;
    struct drm_mode_map_dumb map={0};map.handle=create.handle;if(drmIoctl(c->fd,DRM_IOCTL_MODE_MAP_DUMB,&map))return -1;
    c->hud_pixels=mmap(NULL,c->hud_size,PROT_READ|PROT_WRITE,MAP_SHARED,c->fd,map.offset);if(c->hud_pixels==MAP_FAILED){c->hud_pixels=NULL;return -1;}
    set_zpos(c,c->plane,1);set_zpos(c,c->hud_plane,2);return 0;
}
static void draw_glyph(svrt_drm *c,int x,int y,int glyph,int scale,uint32_t color){
    for(int row=0;row<7;row++)for(int col=0;col<5;col++)if(glyphs[glyph][row]&(1u<<(4-col)))for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++){
        int px=x+col*scale+xx,py=y+row*scale+yy;if(px>=0&&px<(int)c->crtc_w&&py>=0&&py<(int)c->hud_height)*(uint32_t*)(c->hud_pixels+(size_t)py*c->hud_pitch+(size_t)px*4)=color;
    }
}
static int glyph_index(char ch){if(ch>='0'&&ch<='9')return ch-'0';if(ch=='.')return 10;if(ch=='F')return 11;if(ch=='P')return 12;if(ch=='S')return 13;return 14;}
static void draw_label(svrt_drm *c,int x,const char *text,int scale){
    int width=(int)strlen(text)*6*scale+2*scale,height=9*scale;for(int y=0;y<height;y++)for(int xx=0;xx<width;xx++){int px=x+xx;if(px>=0&&px<(int)c->crtc_w)*(uint32_t*)(c->hud_pixels+(size_t)y*c->hud_pitch+(size_t)px*4)=0xb0000000u;}
    x+=scale;for(const char *p=text;*p;p++,x+=6*scale)draw_glyph(c,x,scale,glyph_index(*p),scale,0xffffffffu);
}
static void update_hud(svrt_drm *c,double fps){
    if(!c->hud_pixels)return;memset(c->hud_pixels,0,c->hud_size);char text[32];snprintf(text,sizeof(text),"%.1f FPS",fps);int scale=c->crtc_h<720?2:4,pad=4*scale;
    draw_label(c,pad,text,scale);draw_label(c,(int)c->crtc_w/2+pad,text,scale);
    if(drmModeSetPlane(c->fd,c->hud_plane,c->crtc,c->hud_fb,0,0,0,c->crtc_w,c->hud_height,0,0,c->crtc_w<<16,c->hud_height<<16))fprintf(stderr,"SVRT: FPS overlay scanout failed: %s\n",strerror(errno));
}
static void count_presented_frame(svrt_drm *c){
    uint64_t now=monotonic_ns();if(!c->fps_started_ns){c->fps_started_ns=now;c->fps_frames=0;update_hud(c,0.0);return;}c->fps_frames++;
    uint64_t elapsed=now-c->fps_started_ns;if(elapsed>=500000000u){update_hud(c,(double)c->fps_frames*1000000000.0/(double)elapsed);c->fps_started_ns=now;c->fps_frames=0;}
}
int svrt_drm_open(svrt_drm **out,SDL_Window *window,char *error,size_t error_size) {
    *out=NULL; svrt_drm *c=calloc(1,sizeof(*c)); if(!c){fail(error,error_size,"calloc");return -1;}
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version); if(!SDL_GetWindowWMInfo(window,&wm)||wm.subsystem!=SDL_SYSWM_KMSDRM){if(error&&error_size)snprintf(error,error_size,"SDL is not using KMSDRM");free(c);return -1;} c->fd=wm.info.kmsdrm.drm_fd;
    drmModeResPtr rs=drmModeGetResources(c->fd); if(!rs){fail(error,error_size,"drmModeGetResources");free(c);return -1;}
    for(int i=0;i<rs->count_connectors&&!c->crtc;i++){drmModeConnectorPtr con=drmModeGetConnector(c->fd,rs->connectors[i]);if(!con)continue;if(con->connection==DRM_MODE_CONNECTED&&con->encoder_id){drmModeEncoderPtr e=drmModeGetEncoder(c->fd,con->encoder_id);if(e&&e->crtc_id){drmModeCrtcPtr cr=drmModeGetCrtc(c->fd,e->crtc_id);if(cr&&cr->mode_valid){c->crtc=cr->crtc_id;c->crtc_w=cr->mode.hdisplay;c->crtc_h=cr->mode.vdisplay;for(int j=0;j<rs->count_crtcs;j++)if(rs->crtcs[j]==c->crtc)c->crtc_index=j;}if(cr)drmModeFreeCrtc(cr);}if(e)drmModeFreeEncoder(e);}drmModeFreeConnector(con);}
    drmModeFreeResources(rs); if(!c->crtc){if(error&&error_size)snprintf(error,error_size,"no active DRM connector/CRTC");free(c);return -1;} *out=c;return 0;
}
int svrt_drm_present(svrt_drm *c,const AVFrame *frame,char *error,size_t error_size) {
    if(!c||!frame||frame->format!=AV_PIX_FMT_DRM_PRIME||!frame->data[0]){if(error&&error_size)snprintf(error,error_size,"frame is not DRM PRIME");return -1;}
    const AVDRMFrameDescriptor *d=(const AVDRMFrameDescriptor*)frame->data[0]; if(d->nb_layers<1||d->nb_objects<1||d->nb_objects>SVRT_MAX_PLANES){if(error&&error_size)snprintf(error,error_size,"unsupported DRM descriptor");return -1;}
    uint32_t fmt=d->layers[0].format; if(!c->has_plane&&choose_plane(c,fmt)){if(error&&error_size)snprintf(error,error_size,"no overlay plane supports DRM format 0x%x",fmt);return -1;}
    uint32_t objects[4]={0},handles[4]={0},pitches[4]={0},offsets[4]={0};uint64_t modifiers[4]={0};int count=0;
    for(int i=0;i<d->nb_objects;i++)if(drmPrimeFDToHandle(c->fd,d->objects[i].fd,&objects[i])){fail(error,error_size,"drmPrimeFDToHandle");for(int j=0;j<i;j++){struct drm_gem_close gc={.handle=objects[j]};drmIoctl(c->fd,DRM_IOCTL_GEM_CLOSE,&gc);}return -1;}
    for(int i=0;i<d->nb_layers;i++)for(int j=0;j<d->layers[i].nb_planes&&count<4;j++,count++){const AVDRMPlaneDescriptor *p=&d->layers[i].planes[j];const AVDRMObjectDescriptor *o=&d->objects[p->object_index];pitches[count]=p->pitch;offsets[count]=p->offset;modifiers[count]=o->format_modifier;handles[count]=objects[p->object_index];}
    uint32_t next=0;int rc=drmModeAddFB2WithModifiers(c->fd,frame->width,frame->height,fmt,handles,pitches,offsets,modifiers,&next,DRM_MODE_FB_MODIFIERS);if(rc&&errno==EINVAL)rc=drmModeAddFB2(c->fd,frame->width,frame->height,fmt,handles,pitches,offsets,&next,0);
    if(rc){fail(error,error_size,"drmModeAddFB2");for(int i=0;i<d->nb_objects;i++){struct drm_gem_close gc={.handle=objects[i]};drmIoctl(c->fd,DRM_IOCTL_GEM_CLOSE,&gc);}return -1;}
    uint32_t dw,dh;int32_t dx,dy;svrt_fit_geometry(c->crtc_w,c->crtc_h,(uint32_t)frame->width,(uint32_t)frame->height,&dx,&dy,&dw,&dh);
    if(drmModeSetPlane(c->fd,c->plane,c->crtc,next,0,dx,dy,dw,dh,0,0,(uint32_t)frame->width<<16,(uint32_t)frame->height<<16)){fail(error,error_size,"drmModeSetPlane");drmModeRmFB(c->fd,next);for(int i=0;i<d->nb_objects;i++){struct drm_gem_close gc={.handle=objects[i]};drmIoctl(c->fd,DRM_IOCTL_GEM_CLOSE,&gc);}return -1;}
    release_old(c);c->fb=next;c->has_fb=1;c->handle_count=d->nb_objects;memcpy(c->handles,objects,sizeof(uint32_t)*d->nb_objects);
    if(!c->hud_state){c->hud_state=open_hud(c)?-1:1;if(c->hud_state<0)fprintf(stderr,"SVRT: FPS overlay unavailable: %s\n",strerror(errno));}if(c->hud_state>0)count_presented_frame(c);return 0;
}
void svrt_drm_close(svrt_drm **ptr){if(!ptr||!*ptr)return;svrt_drm *c=*ptr;*ptr=NULL;release_old(c);if(c->hud_plane)drmModeSetPlane(c->fd,c->hud_plane,c->crtc,0,0,0,0,0,0,0,0,0,0);if(c->hud_pixels)munmap(c->hud_pixels,c->hud_size);if(c->hud_fb)drmModeRmFB(c->fd,c->hud_fb);if(c->hud_handle){struct drm_mode_destroy_dumb destroy={.handle=c->hud_handle};drmIoctl(c->fd,DRM_IOCTL_MODE_DESTROY_DUMB,&destroy);}free(c);}
