#include "ui_boot_anim.h"

void svrt_ui_boot_anim_start(svrt_ui_boot_anim *animation, uint32_t now_ms) { animation->started_ms = now_ms; }
uint32_t svrt_ui_boot_anim_elapsed(const svrt_ui_boot_anim *animation, uint32_t now_ms) { return now_ms - animation->started_ms; }
int svrt_ui_boot_anim_active(const svrt_ui_boot_anim *animation, uint32_t now_ms) { return svrt_ui_boot_anim_elapsed(animation, now_ms) < 4000; }
