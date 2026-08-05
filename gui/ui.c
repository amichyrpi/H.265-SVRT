#include "ui.h"
#include "ui_anim.h"

#include <SDL_image.h>
#include <string.h>

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *value, int center_x, int y, uint8_t alpha) {
    SDL_Color color = {255, 255, 255, alpha};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect target = {center_x - surface->w / 2, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &target);
    SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
}

int svrt_ui_open(svrt_ui *ui) {
    memset(ui, 0, sizeof(*ui));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) || TTF_Init() || !IMG_Init(IMG_INIT_PNG)) return -1;
    ui->window = SDL_CreateWindow("SVRT", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_FULLSCREEN_DESKTOP);
    ui->renderer = ui->window ? SDL_CreateRenderer(ui->window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    ui->font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 42);
    ui->code_font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 120);
    ui->steamvr_logo = ui->renderer ? IMG_LoadTexture(ui->renderer, SVRT_GUI_STEAMVR_LOGO_PATH) : NULL;
    if (!ui->renderer || !ui->font || !ui->code_font) { svrt_ui_close(ui); return -1; }
    svrt_ui_boot_anim_start(&ui->boot_animation, SDL_GetTicks());
    return 0;
}

void svrt_ui_close(svrt_ui *ui) {
    if (ui->steamvr_logo) SDL_DestroyTexture(ui->steamvr_logo);
    if (ui->code_font) TTF_CloseFont(ui->code_font);
    if (ui->font) TTF_CloseFont(ui->font);
    if (ui->renderer) SDL_DestroyRenderer(ui->renderer);
    if (ui->window) SDL_DestroyWindow(ui->window);
    IMG_Quit(); TTF_Quit(); SDL_Quit(); memset(ui, 0, sizeof(*ui));
}

static void draw_mono_eye(svrt_ui *ui, svrt_ui_state state, const char code[5],
                          int x, int width, int height, uint8_t alpha, int boot) {
    const int center_x = x + width / 2;
    if (boot) {
        draw_text(ui->renderer, ui->code_font, "SVRT", center_x, height / 2 - 70, alpha);
        return;
    }
    if (state == SVRT_UI_PAIRING)
        draw_text(ui->renderer, ui->font, "pairing", center_x, height / 2 - 180, alpha);
    else if (state == SVRT_UI_WAITING)
        draw_text(ui->renderer, ui->font, "waiting for", center_x, height / 2 - 180, alpha);
    if (ui->steamvr_logo) {
        int logo_width = 0, logo_height = 0;
        SDL_QueryTexture(ui->steamvr_logo, NULL, NULL, &logo_width, &logo_height);
        const int target_width = width > 900 ? 440 : width / 2;
        const int target_height = logo_height * target_width / logo_width;
        SDL_Rect target = {center_x - target_width / 2, height / 2 - target_height / 2,
                           target_width, target_height};
        SDL_SetTextureAlphaMod(ui->steamvr_logo, alpha);
        SDL_RenderCopy(ui->renderer, ui->steamvr_logo, NULL, &target);
    } else {
        draw_text(ui->renderer, ui->code_font, "SteamVR", center_x, height / 2 - 70, alpha);
    }
    if (state == SVRT_UI_UNPAIRED && code && code[0])
        draw_text(ui->renderer, ui->code_font, code, center_x, height / 2 + 90, 255);
}

void svrt_ui_draw(svrt_ui *ui, svrt_ui_state state, const char code[5], uint32_t now_ms) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui->renderer);
    const uint8_t alpha = svrt_ui_blink_opacity(svrt_ui_boot_anim_elapsed(&ui->boot_animation, now_ms));
    const int boot = svrt_ui_boot_anim_active(&ui->boot_animation, now_ms);
    /* The physical panel is a side-by-side eye framebuffer.  Draw the exact
       same scene per eye: in the HMD this is one monoscopic view. */
    draw_mono_eye(ui, state, code, 0, width / 2, height, alpha, boot);
    draw_mono_eye(ui, state, code, width / 2, width - width / 2, height, alpha, boot);
    SDL_RenderPresent(ui->renderer);
}
