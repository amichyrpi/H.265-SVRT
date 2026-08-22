#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include "ui_boot_anim.h"

typedef enum svrt_ui_state {
    SVRT_UI_SEARCHING,
    SVRT_UI_AUTHORIZING,
    SVRT_UI_WAITING,
    SVRT_UI_FAILED
} svrt_ui_state;
typedef struct svrt_ui {
    SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *steamvr_logo;
    TTF_Font *font; TTF_Font *code_font; svrt_ui_boot_anim boot_animation;
} svrt_ui;

int svrt_ui_open(svrt_ui *ui);
void svrt_ui_close(svrt_ui *ui);
void svrt_ui_draw(svrt_ui *ui, svrt_ui_state state, const char code[5],
                  const char *hostname, const char *detail, uint32_t now_ms);
