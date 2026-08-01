#ifndef SVRT_DRM_GEOMETRY_H
#define SVRT_DRM_GEOMETRY_H
#include <stdint.h>
void svrt_fit_geometry(uint32_t screen_w,uint32_t screen_h,uint32_t frame_w,uint32_t frame_h,int32_t *x,int32_t *y,uint32_t *w,uint32_t *h);
#endif
