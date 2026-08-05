#include "pairing.h"

#include "ui.h"

#include <SDL.h>

void svrt_pairing_gui_show(svrt_status_server *server, volatile sig_atomic_t *quitting) {
    svrt_ui ui;
    if (!server || svrt_ui_open(&ui)) return;
    uint32_t waiting_since = 0;
    while (!*quitting) {
        char code[5] = {0};
        svrt_status_server_pairing_code(server, code);
        svrt_ui_state state = SVRT_UI_UNPAIRED;
        if (svrt_status_server_pairing_in_progress(server)) state = SVRT_UI_PAIRING;
        else if (svrt_status_server_is_paired(server)) state = SVRT_UI_WAITING;
        const uint32_t now = SDL_GetTicks();
        svrt_ui_draw(&ui, state, code, now);
        if (state == SVRT_UI_WAITING && !svrt_ui_boot_anim_active(&ui.boot_animation, now)) {
            if (!waiting_since) waiting_since = now;
            if (now - waiting_since >= 1000) break;
        } else waiting_since = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) *quitting = 1;
        SDL_Delay(16);
    }
    svrt_ui_close(&ui);
}
