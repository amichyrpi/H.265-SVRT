#ifndef SVRT_DRM_PRESENTER_H
#define SVRT_DRM_PRESENTER_H
#include <SDL.h>
#include <libavutil/frame.h>
typedef struct svrt_drm svrt_drm;
int svrt_drm_open(svrt_drm **out, SDL_Window *window, char *error, size_t error_size);
int svrt_drm_present(svrt_drm *ctx, const AVFrame *frame, char *error, size_t error_size);
int svrt_drm_present_dual(svrt_drm *ctx, const AVFrame *main_frame,
                          const AVFrame *extra_frame, char *error,
                          size_t error_size);
void svrt_drm_close(svrt_drm **ctx);
#endif
