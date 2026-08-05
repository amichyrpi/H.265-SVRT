#pragma once

#include <windows.h>

struct UtilityFonts {
  HFONT text;
  HFONT small;
  HFONT title;
  HFONT logo;
};

UtilityFonts ui_fonts_create();
void ui_fonts_destroy(const UtilityFonts &fonts);
void ui_text(HDC dc, const wchar_t *value, RECT rect, COLORREF color,
             HFONT font, UINT align = DT_LEFT);
void ui_rule(HDC dc, int y);
void ui_round_rect(HDC dc, RECT rect, COLORREF fill, COLORREF outline, int radius);
