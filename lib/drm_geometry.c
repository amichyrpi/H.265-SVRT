#include "drm_geometry.h"
void svrt_fit_geometry(uint32_t sw,uint32_t sh,uint32_t fw,uint32_t fh,int32_t*x,int32_t*y,uint32_t*w,uint32_t*h){if(!sw||!sh||!fw||!fh){*x=*y=0;*w=fw;*h=fh;return;}if((uint64_t)sw*fh>(uint64_t)sh*fw){*h=sh;*w=(uint64_t)sh*fw/fh;*x=(int32_t)(sw-*w)/2;*y=0;}else{*w=sw;*h=(uint64_t)sw*fh/fw;*x=0;*y=(int32_t)(sh-*h)/2;}}
