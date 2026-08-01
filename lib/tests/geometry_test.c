#include "drm_geometry.h"
#include <assert.h>
int main(void){int32_t x,y;uint32_t w,h;svrt_fit_geometry(3840,2160,2880,1600,&x,&y,&w,&h);assert(x==0&&y==13&&w==3840&&h==2133);svrt_fit_geometry(1920,1080,1600,1600,&x,&y,&w,&h);assert(x==420&&y==0&&w==1080&&h==1080);svrt_fit_geometry(0,0,640,480,&x,&y,&w,&h);assert(x==0&&y==0&&w==640&&h==480);return 0;}
