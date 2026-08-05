#include "ui_anim.h"

#include <math.h>

uint8_t svrt_ui_blink_opacity(uint32_t elapsed_ms) {
    const float phase = (float)(elapsed_ms % 4000u) / 4000.0f;
    const float value = 0.25f + 0.75f * (0.5f + 0.5f * sinf(phase * 6.2831853f));
    return (uint8_t)(value * 255.0f);
}
