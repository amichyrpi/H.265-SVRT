#include "steamvr.h"

#include "ui.h"

#include <string>

namespace {
std::wstring driver_directory() {
  wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring value(path);
  return value.substr(0, value.find_last_of(L"\\/")) + L"\\..\\svrt";
}
}

void steamvr_save(HWND window, UtilitySettings &settings, bool start) {
  settings.refresh = ui_is_selected(ID_REFRESH_60) ? 60 : 30;
  settings.width = ui_is_selected(ID_RESOLUTION_HD) ? 1920 : 3840;
  settings.height = settings.width == 1920 ? 1080 : 2160;
  settings.verbose = ui_is_selected(ID_VERBOSE);
  const bool was_running = steamvr_is_running();
  const bool should_run = start || was_running;
  const bool stopped = !was_running || stop_steamvr();
  const bool ok = stopped && install_steamvr_driver(driver_directory()) &&
                  write_steamvr_settings(settings, should_run) &&
                  (!should_run || launch_steamvr(driver_directory()));
  if (ok) save_settings(settings);
  MessageBoxW(window, ok ? (should_run ? L"Settings applied and SteamVR restarted." : L"Settings saved.") : L"The SteamVR settings could not be applied.", L"SVRT Utility", MB_OK);
}
