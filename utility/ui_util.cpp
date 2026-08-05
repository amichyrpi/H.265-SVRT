#include "ui_util.h"

namespace {
HFONT make_font(int points, int weight = FW_NORMAL) {
  HDC dc = GetDC(nullptr);
  const int height = -MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72);
  ReleaseDC(nullptr, dc);
  return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH, L"Segoe UI");
}
}

UtilityFonts ui_fonts_create() {
  return {make_font(12), make_font(10), make_font(14, FW_MEDIUM), make_font(27, FW_SEMIBOLD)};
}

void ui_fonts_destroy(const UtilityFonts &fonts) {
  DeleteObject(fonts.text); DeleteObject(fonts.small); DeleteObject(fonts.title); DeleteObject(fonts.logo);
}

void ui_text(HDC dc, const wchar_t *value, RECT rect, COLORREF color, HFONT font, UINT align) {
  SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); SelectObject(dc, font);
  DrawTextW(dc, value, -1, &rect, align | DT_VCENTER | DT_SINGLELINE);
}

void ui_rule(HDC dc, int y) {
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(52, 52, 56));
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, 8, y, nullptr); LineTo(dc, 575, y);
  SelectObject(dc, old); DeleteObject(pen);
}

void ui_round_rect(HDC dc, RECT rect, COLORREF fill, COLORREF outline, int radius) {
  HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, outline);
  HGDIOBJ old_brush = SelectObject(dc, brush); HGDIOBJ old_pen = SelectObject(dc, pen);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
}
