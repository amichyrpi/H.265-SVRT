#include "ui.h"

namespace {
int refresh_selected = ID_REFRESH_60;
int resolution_selected = ID_RESOLUTION_HD;
bool verbose_selected = false;

HWND button(HWND parent, const wchar_t *label, int id, int x, int y, int width, int height) {
  return CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, width, height,
                       parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}
}

void ui_create_controls(HWND window, HINSTANCE instance) {
  button(window, L"60 Hz", ID_REFRESH_60, 260, 86, 137, 37);
  button(window, L"30 Hz", ID_REFRESH_30, 407, 86, 137, 37);
  button(window, L"1080p", ID_RESOLUTION_HD, 260, 140, 137, 37);
  button(window, L"4K", ID_RESOLUTION_4K, 407, 140, 137, 37);
  CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_NUMBER,
                15, 243, 168, 30, window, reinterpret_cast<HMENU>(ID_CODE), instance, nullptr);
  button(window, L"Pair", ID_PAIR, 192, 243, 170, 30);
  button(window, L"Unpair", ID_UNPAIR, 371, 243, 173, 30);
  button(window, L"Verbose measurements", ID_VERBOSE, 15, 490, 255, 32);
  button(window, L"Save changes", ID_SAVE, 15, 548, 262, 38);
  button(window, L"Start SteamVR", ID_START, 282, 548, 262, 38);
}

void ui_paint(HWND window, HDC dc, const UtilityFonts &fonts) {
  RECT client; GetClientRect(window, &client);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  ui_text(dc, L"SVRT", {0, 15, 584, 54}, RGB(250, 250, 250), fonts.logo, DT_CENTER);
  ui_rule(dc, 68);
  ui_text(dc, L"Display Refresh Rate", {15, 90, 240, 120}, RGB(232, 232, 236), fonts.text);
  ui_rule(dc, 130);
  ui_text(dc, L"Resolution", {15, 144, 240, 174}, RGB(232, 232, 236), fonts.text);
  ui_rule(dc, 184);
  ui_text(dc, L"PAIR HEADSET", {15, 199, 300, 224}, RGB(232, 232, 236), fonts.title);
  ui_text(dc, L"Enter the four-digit code shown in the headset.", {15, 220, 500, 241}, RGB(190, 190, 196), fonts.small);
  ui_rule(dc, 288);
  ui_text(dc, L"SESSION", {15, 304, 285, 328}, RGB(232, 232, 236), fonts.title);
  ui_text(dc, L"Usage time:\nnot yet recorded", {15, 340, 270, 382}, RGB(232, 232, 236), fonts.text);
  ui_text(dc, L"Longest session:\nnot yet recorded", {295, 340, 560, 382}, RGB(232, 232, 236), fonts.text);
  ui_rule(dc, 400);
  ui_text(dc, L"MEASUREMENTS", {15, 416, 300, 440}, RGB(232, 232, 236), fonts.title);
  ui_text(dc, L"Show the live framerate and latency report in a terminal.", {15, 444, 555, 466}, RGB(190, 190, 196), fonts.small);
  ui_rule(dc, 533);
  ui_text(dc, L"Settings are saved to the headset driver configuration.", {15, 610, 560, 632}, RGB(166, 166, 172), fonts.small);
}

void ui_draw_button(const DRAWITEMSTRUCT *item, const UtilityFonts &fonts) {
  const bool selected = ui_is_selected(item->CtlID);
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const bool destructive = item->CtlID == ID_UNPAIR;
  const bool primary = item->CtlID == ID_SAVE || item->CtlID == ID_START || item->CtlID == ID_PAIR;
  COLORREF fill = selected ? RGB(0, 0, 255) : RGB(54, 54, 58);
  if (primary) fill = pressed ? RGB(0, 0, 200) : RGB(66, 66, 71);
  if (destructive) fill = pressed ? RGB(0, 0, 200) : RGB(92, 38, 54);
  RECT rect = item->rcItem;
  // Owner-drawn child controls otherwise leave the default white background
  // exposed around a rounded outline.  Match the parent before the shape.
  FillRect(item->hDC, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  ui_round_rect(item->hDC, rect, fill, selected || pressed ? RGB(94, 94, 255) : RGB(84, 84, 90), 9);
  wchar_t label[80]{}; GetWindowTextW(item->hwndItem, label, 80);
  ui_text(item->hDC, label, rect, RGB(250, 250, 250), fonts.text, DT_CENTER);
}

void ui_select(HWND window, int selected, int unselected) {
  if (selected == ID_REFRESH_60 || selected == ID_REFRESH_30) refresh_selected = selected;
  if (selected == ID_RESOLUTION_HD || selected == ID_RESOLUTION_4K) resolution_selected = selected;
  InvalidateRect(window, nullptr, FALSE);
}

void ui_toggle_verbose(HWND window) {
  verbose_selected = !verbose_selected;
  InvalidateRect(window, nullptr, FALSE);
}

bool ui_is_selected(int id) {
  return id == refresh_selected || id == resolution_selected ||
         (id == ID_VERBOSE && verbose_selected);
}
