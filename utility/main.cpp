#include <windows.h>

#include "config.h"
#include "pairing.h"
#include "steamvr.h"
#include "ui.h"

namespace {
constexpr int kWindowWidth = 600;
constexpr int kWindowHeight = 720;
UtilitySettings settings;
UtilityFonts fonts;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_PAINT: { PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); ui_paint(window, dc, fonts); EndPaint(window, &paint); return 0; }
    case WM_DRAWITEM: ui_draw_button(reinterpret_cast<DRAWITEMSTRUCT *>(l), fonts); return TRUE;
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_REFRESH_60: ui_select(window, ID_REFRESH_60, ID_REFRESH_30); break;
        case ID_REFRESH_30: ui_select(window, ID_REFRESH_30, ID_REFRESH_60); break;
        case ID_RESOLUTION_HD: ui_select(window, ID_RESOLUTION_HD, ID_RESOLUTION_4K); break;
        case ID_RESOLUTION_4K: ui_select(window, ID_RESOLUTION_4K, ID_RESOLUTION_HD); break;
        case ID_PAIR: pairing_pair(window, settings); break;
        case ID_UNPAIR: pairing_unpair(window, settings); break;
        case ID_SAVE: steamvr_save(window, settings, false); break;
        case ID_START: steamvr_save(window, settings, true); break;
        case ID_VERBOSE: ui_toggle_verbose(window); break;
      }
      return 0;
    case WM_DESTROY: ui_fonts_destroy(fonts); PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  fonts = ui_fonts_create();
  WNDCLASSW type{}; type.hInstance = instance; type.lpszClassName = L"SvrtUtility"; type.lpfnWndProc = window_proc;
  type.hCursor = LoadCursor(nullptr, IDC_ARROW); type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    ui_fonts_destroy(fonts);
    MessageBoxW(nullptr, L"The utility window class could not be registered.",
                L"SVRT Utility", MB_OK | MB_ICONERROR);
    return 1;
  }
  HWND window = CreateWindowExW(0, type.lpszClassName, L"SVRT Utility", WS_CAPTION | WS_SYSMENU, 200, 140, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, nullptr);
  if (!window) {
    ui_fonts_destroy(fonts);
    MessageBoxW(nullptr, L"The utility window could not be created.",
                L"SVRT Utility", MB_OK | MB_ICONERROR);
    return 1;
  }
  ui_create_controls(window, instance);
  settings = load_settings();
  ui_select(window, settings.refresh == 60 ? ID_REFRESH_60 : ID_REFRESH_30, settings.refresh == 60 ? ID_REFRESH_30 : ID_REFRESH_60);
  ui_select(window, settings.width <= 2160 ? ID_RESOLUTION_HD : ID_RESOLUTION_4K, settings.width <= 2160 ? ID_RESOLUTION_4K : ID_RESOLUTION_HD);
  if (settings.verbose) ui_toggle_verbose(window);
  ShowWindow(window, show); UpdateWindow(window);
  MSG message;
  for (;;) {
    const int result = GetMessageW(&message, nullptr, 0, 0);
    if (result > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    else if (result == 0) break;
    else {
      MessageBoxW(window, L"The utility message loop failed.",
                  L"SVRT Utility", MB_OK | MB_ICONERROR);
      return 1;
    }
  }
  return 0;
}
