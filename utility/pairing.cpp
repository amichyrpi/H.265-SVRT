#include "pairing.h"

#include "network.h"
#include "ui.h"

#include <string>
#include <cctype>
#include <algorithm>
#include <cstring>

void pairing_pair(HWND window, UtilitySettings &settings) {
  wchar_t code[8]{}; GetDlgItemTextW(window, ID_CODE, code, 8);
  char number[8]{}; WideCharToMultiByte(CP_UTF8, 0, code, -1, number, sizeof(number), nullptr, nullptr);
  if (std::strlen(number) != 4 ||
      !std::all_of(number, number + 4, [](unsigned char value) {
        return std::isdigit(value) != 0;
      })) {
    MessageBoxW(window, L"Enter the four-digit headset code.", L"SVRT Utility",
                MB_OK | MB_ICONERROR);
    return;
  }
  if (settings.client_id.empty()) {
    wchar_t name[64]{}; DWORD length = 64; GetComputerNameW(name, &length);
    char client[64]{}; WideCharToMultiByte(CP_UTF8, 0, name, -1, client, sizeof(client), nullptr, nullptr);
    settings.client_id = "win-" + std::string(client);
  }
  std::string response;
  const bool ok = svrt_request(settings.host, "SVRT/1 PAIR " + std::string(number) + " " + settings.client_id + "\n", response);
  MessageBoxW(window, ok && response.find("PAIRED") != std::string::npos ? L"Headset paired successfully." : L"Pairing failed. Check the headset code and network.", L"SVRT Utility", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

void pairing_unpair(HWND window, const UtilitySettings &settings) {
  std::string response;
  const bool ok = !settings.client_id.empty() && svrt_request(settings.host, "SVRT/1 UNPAIR " + settings.client_id + "\n", response);
  MessageBoxW(window, ok && response.find("UNPAIRED") != std::string::npos ? L"Headset unpaired." : L"Unpair failed.", L"SVRT Utility", MB_OK);
}
