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
};

static void fail(char *out,size_t n,const char *what) { if(out&&n)snprintf(out,n,"%s: %s",what,strerror(errno)); }
static uint64_t property(int fd,uint32_t object,uint32_t type,const char *name) {
    drmModeObjectPropertiesPtr ps=drmModeObjectGetProperties(fd,object,type); if(!ps)return 0;
    uint64_t result=0; for(uint32_t i=0;i<ps->count_props;i++){ drmModePropertyPtr p=drmModeGetProperty(fd,ps->props[i]); if(p){ if(!strcmp(p->name,name))result=ps->prop_values[i]; drmModeFreeProperty(p); if(result)break; }}
    drmModeFreeObjectProperties(ps); return result;
}
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
    release_old(c);c->fb=next;c->has_fb=1;c->handle_count=d->nb_objects;memcpy(c->handles,objects,sizeof(uint32_t)*d->nb_objects);return 0;
}
void svrt_drm_close(svrt_drm **ptr){if(!ptr||!*ptr)return;svrt_drm *c=*ptr;*ptr=NULL;release_old(c);free(c);}
