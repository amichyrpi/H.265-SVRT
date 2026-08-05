#pragma once

#include <stdint.h>

typedef struct svrt_ui_boot_anim { uint32_t started_ms; } svrt_ui_boot_anim;
void svrt_ui_boot_anim_start(svrt_ui_boot_anim *animation, uint32_t now_ms);
int svrt_ui_boot_anim_active(const svrt_ui_boot_anim *animation, uint32_t now_ms);
uint32_t svrt_ui_boot_anim_elapsed(const svrt_ui_boot_anim *animation, uint32_t now_ms);
