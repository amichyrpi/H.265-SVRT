#pragma once

#include <windows.h>

#include "ui_util.h"

enum : int { ID_REFRESH_60 = 100, ID_REFRESH_30, ID_RESOLUTION_HD, ID_RESOLUTION_4K,
             ID_CODE, ID_PAIR, ID_UNPAIR, ID_SAVE, ID_START, ID_VERBOSE };

void ui_create_controls(HWND window, HINSTANCE instance);
void ui_paint(HWND window, HDC dc, const UtilityFonts &fonts);
void ui_draw_button(const DRAWITEMSTRUCT *item, const UtilityFonts &fonts);
void ui_select(HWND window, int selected, int unselected);
void ui_toggle_verbose(HWND window);
bool ui_is_selected(int id);
