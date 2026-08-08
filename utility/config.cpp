#include "config.h"
#include <windows.h>
#include <tlhelp32.h>
#include <fstream>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <cstdio>
#include <cctype>

static std::wstring ini_path() { wchar_t path[MAX_PATH]; GetEnvironmentVariableW(L"APPDATA",path,MAX_PATH); std::wstring dir=std::wstring(path)+L"\\SVRT Utility"; CreateDirectoryW(dir.c_str(),nullptr); return dir+L"\\settings.ini"; }
static std::wstring widen(const std::string &value) { if(value.empty())return {};const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring out(count,L'\0');MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count);return out; }
static std::string narrow(const std::wstring &value) { if(value.empty())return {};const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(count,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr);return out; }
static std::string get(const wchar_t *key,const wchar_t *fallback=L"") { wchar_t value[256]{};const auto path=ini_path();GetPrivateProfileStringW(L"svrt",key,fallback,value,256,path.c_str());return narrow(value); }
static int get_int(const wchar_t *key,int fallback){try{return std::stoi(get(key));}catch(const std::exception&){return fallback;}}
UtilitySettings load_settings(){ UtilitySettings s;s.host=get(L"host",L"ROOT.local");s.client_id=get(L"client_id");s.refresh=get_int(L"refresh",60);s.width=get_int(L"width",2160);s.height=get_int(L"height",1080);s.verbose=get(L"verbose",L"0")=="1";if(s.refresh!=30&&s.refresh!=60)s.refresh=60;if(s.width==1920)s.width=2160;else if(s.width==3840)s.width=4320;else if(s.width!=2160&&s.width!=4320)s.width=2160;s.height=s.width==2160?1080:2160;return s; }
static void put(const wchar_t *key,const std::wstring &value){const auto path=ini_path();WritePrivateProfileStringW(L"svrt",key,value.c_str(),path.c_str());}
void save_settings(const UtilitySettings&s){put(L"host",widen(s.host));put(L"client_id",widen(s.client_id));put(L"refresh",std::to_wstring(s.refresh));put(L"width",std::to_wstring(s.width));put(L"height",std::to_wstring(s.height));put(L"verbose",s.verbose?L"1":L"0");}
static size_t matching_brace(const std::string &json, size_t open) {
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = open; i < json.size(); ++i) {
    const char ch = json[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
      continue;
    }
    if (ch == '"') { quoted = true; continue; }
    if (ch == '{') ++depth;
    else if (ch == '}' && --depth == 0) return i;
  }
  return std::string::npos;
}

static bool find_object(const std::string &json, const char *key,
                        size_t &begin, size_t &end) {
  const std::string token = "\"" + std::string(key) + "\"";
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0; i < json.size(); ++i) {
    const char ch = json[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
      continue;
    }
    if (ch == '"') {
      if (depth == 1 && json.compare(i, token.size(), token) == 0) {
        size_t colon = i + token.size();
        while (colon < json.size() && std::isspace(static_cast<unsigned char>(json[colon]))) ++colon;
        if (colon < json.size() && json[colon] == ':') {
          ++colon;
          while (colon < json.size() && std::isspace(static_cast<unsigned char>(json[colon]))) ++colon;
          if (colon < json.size() && json[colon] == '{') {
            const size_t close = matching_brace(json, colon);
            if (close != std::string::npos) { begin = i; end = close; return true; }
          }
        }
      }
      quoted = true;
    } else if (ch == '{') ++depth;
    else if (ch == '}') --depth;
  }
  return false;
}

static void set_value(std::string &section, const char *key, const std::string &value) {
  const std::string token = "\"" + std::string(key) + "\"";
  size_t open = section.find('{');
  const size_t close = open == std::string::npos ? std::string::npos : matching_brace(section, open);
  if (close == std::string::npos) return;
  bool quoted = false, escaped = false;
  int depth = 0;
  for (size_t i = open; i < close; ++i) {
    const char ch = section[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
      continue;
    }
    if (ch == '"') {
      if (depth == 1 && section.compare(i, token.size(), token) == 0) {
        size_t colon = i + token.size();
        while (colon < close && std::isspace(static_cast<unsigned char>(section[colon]))) ++colon;
        if (colon >= close || section[colon] != ':') continue;
        size_t first = ++colon;
        while (first < close && std::isspace(static_cast<unsigned char>(section[first]))) ++first;
        size_t last = first;
        bool value_quoted = false, value_escaped = false;
        int nested = 0;
        for (; last < close; ++last) {
          const char value_ch = section[last];
          if (value_quoted) {
            if (value_escaped) value_escaped = false;
            else if (value_ch == '\\') value_escaped = true;
            else if (value_ch == '"') value_quoted = false;
          } else if (value_ch == '"') value_quoted = true;
          else if (value_ch == '{' || value_ch == '[') ++nested;
          else if (value_ch == '}' || value_ch == ']') { if (nested) --nested; }
          else if (!nested && value_ch == ',') break;
        }
        size_t replacement_end = last;
        while (replacement_end > first && std::isspace(static_cast<unsigned char>(section[replacement_end - 1]))) --replacement_end;
        section.replace(first, replacement_end - first, value);
        return;
      }
      quoted = true;
    } else if (ch == '{') ++depth;
    else if (ch == '}') --depth;
  }
  const auto content = section.find_last_not_of(" \t\r\n", close - 1);
  const bool empty = content == std::string::npos || content == open;
  section.insert(close, std::string(empty ? "\n      " : ",\n      ") +
                         "\"" + key + "\" : " + value + "\n");
}

#ifdef SVRT_CONFIG_TESTING
bool config_json_regression_test() {
  const std::string json =
      "{\n"
      "  \"steamvr\" : { \"nested\" : { \"text\" : \"brace } in string\" }, "
      "\"supersampleScale\" : 0.5 },\n"
      "  \"power\" : { \"message\" : \"{ still a string }\", "
      "\"turnOffScreensTimeout\" : 10.0 }\n"
      "}";
  size_t begin = 0, end = 0;
  if (!find_object(json, "steamvr", begin, end)) return false;
  std::string steamvr = json.substr(begin, end - begin + 1);
  set_value(steamvr, "supersampleScale", "1.0");
  if (steamvr.find("\"nested\" : { \"text\" : \"brace } in string\" }") == std::string::npos ||
      steamvr.find("\"supersampleScale\" : 1.0") == std::string::npos)
    return false;
  if (!find_object(json, "power", begin, end)) return false;
  std::string power = json.substr(begin, end - begin + 1);
  set_value(power, "turnOffScreensTimeout", "0.0");
  return power.find("\"message\" : \"{ still a string }\"") != std::string::npos &&
         power.find("\"turnOffScreensTimeout\" : 0.0") != std::string::npos;
}
#endif

static std::string json_string(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (character < 0x20) {
          char buffer[7]; std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
          escaped += buffer;
        } else escaped.push_back(static_cast<char>(character));
    }
  }
  escaped.push_back('"');
  return escaped;
}

bool steamvr_is_running() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W process{sizeof(process)};
  bool found = false;
  if (Process32FirstW(snapshot, &process)) {
    do {
      if (!_wcsicmp(process.szExeFile, L"vrserver.exe")) { found = true; break; }
    } while (Process32NextW(snapshot, &process));
  }
  CloseHandle(snapshot);
  return found;
}

static std::wstring steam_root() {
  wchar_t root[MAX_PATH]{};
  DWORD size = sizeof(root);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                   RRF_RT_REG_SZ, nullptr, root, &size) == ERROR_SUCCESS)
    return root;
  if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", root, MAX_PATH)) return {};
  return std::wstring(root) + L"\\Steam";
}

static std::string driver_section(const UtilitySettings &settings, bool enabled) {
  return "\"driver_svrt\" : {\n"
      "  \"enable\" : " + std::string(enabled ? "true" : "false") + ",\n"
      "  \"receiver_host\" : " + json_string(settings.host) + ",\n"
      "  \"receiver_port\" : 9944,\n"
      "  \"status_port\" : 9945,\n"
      "  \"audio_port\" : 9946,\n"
      "  \"display_frequency\" : " + std::to_string(settings.refresh) + ",\n"
      "  \"render_width\" : " + std::to_string(settings.width / 2) + ",\n"
      "  \"render_height\" : " + std::to_string(settings.height) + "\n"
      "}";
}

bool write_steamvr_settings(const UtilitySettings &settings, bool enabled) {
  if (steamvr_is_running()) return false;
  const std::wstring root = steam_root();
  if (root.empty()) return false;
  const std::wstring path = root + L"\\config\\steamvr.vrsettings";
  std::ifstream in(path);
  if (!in) return false;
  std::string json((std::istreambuf_iterator<char>(in)), {});
  // Windows cannot atomically replace a file while our CRT stream still owns
  // a non-delete-sharing handle to it.
  in.close();
  size_t begin = 0, end = 0;
  if (!find_object(json, "driver_svrt", begin, end)) {
    const size_t root = json.find('{');
    const size_t closing = root == std::string::npos ? std::string::npos : matching_brace(json, root);
    if (closing == std::string::npos) return false;
    const auto previous = json.find_last_not_of(" \t\r\n", closing - 1);
    const bool empty = previous == std::string::npos || previous == root;
    json.insert(closing, std::string(empty ? "\n  " : ",\n  ") + driver_section(settings, enabled) + "\n");
  } else {
    // This section belongs exclusively to SVRT, so replacing it atomically is
    // safer than retaining stale or malformed values from an interrupted save.
    json.replace(begin, end - begin + 1, driver_section(settings, enabled));
  }
  if (find_object(json, "steamvr", begin, end)) {
    {
      const size_t steamvr_begin = begin, steamvr_end = end;
      std::string section = json.substr(steamvr_begin, steamvr_end - steamvr_begin + 1);
      // Repair files written by the old "$1" regex replacement bug.
      section = std::regex_replace(section, std::regex("\\n[ \\t]*\\.0[ \\t]*"), "");
      section = std::regex_replace(section, std::regex(",[ \\t\\r\\n]*,"), ",");
      set_value(section, "supersampleManualOverride", "true");
      set_value(section, "supersampleScale", "1.0");
      set_value(section, "renderTargetMultiplier", "1.0");
      json.replace(steamvr_begin, steamvr_end - steamvr_begin + 1, section);
    }
  }
  if (find_object(json, "power", begin, end)) {
    {
      const size_t power_begin = begin, power_end = end;
      std::string section = json.substr(power_begin, power_end - power_begin + 1);
      set_value(section, "turnOffScreensTimeout", "0.0");
      json.replace(power_begin, power_end - power_begin + 1, section);
    }
  }
  const std::wstring temporary = path + L".svrt.tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    out << json;
    if (!out) { DeleteFileW(temporary.c_str()); return false; }
    out.close();
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

static bool run_and_wait(std::wstring command, DWORD timeout_ms) {
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
  const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0;
}

static void terminate_process(const wchar_t *name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  PROCESSENTRY32W process{sizeof(process)};
  if (Process32FirstW(snapshot, &process)) {
    do {
      if (_wcsicmp(process.szExeFile, name)) continue;
      HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                  process.th32ProcessID);
      if (handle) { TerminateProcess(handle, 0); WaitForSingleObject(handle, 2000); CloseHandle(handle); }
    } while (Process32NextW(snapshot, &process));
  }
  CloseHandle(snapshot);
}

bool stop_steamvr() {
  if (!steamvr_is_running()) return true;
  const std::wstring monitor = steam_root() + L"\\steamapps\\common\\SteamVR\\bin\\win64\\vrmonitor.exe";
  std::wstring command = L"\"" + monitor + L"\" -shutdown";
  run_and_wait(command, 3000);
  for (int elapsed = 0; elapsed < 5000 && steamvr_is_running(); elapsed += 100) Sleep(100);
  if (steamvr_is_running()) {
    // Some SteamVR beta builds ignore the shutdown command while waiting for
    // an HMD. Keep this fallback bounded and scoped to SteamVR executables.
    terminate_process(L"vrdashboard.exe");
    terminate_process(L"vrwebhelper.exe");
    terminate_process(L"vrcompositor.exe");
    terminate_process(L"vrmonitor.exe");
    terminate_process(L"vrserver.exe");
  }
  for (int elapsed = 0; elapsed < 5000 && steamvr_is_running(); elapsed += 100) Sleep(100);
  return !steamvr_is_running();
}

bool install_steamvr_driver(const std::wstring &driver) {
  namespace fs = std::filesystem;
  const fs::path source(driver);
  const fs::path destination = fs::path(steam_root()) / L"steamapps" / L"common" /
                               L"SteamVR" / L"drivers" / L"svrt";
  std::error_code error;
  if (!fs::is_directory(source, error) || error) return false;
  fs::create_directories(destination, error);
  if (error) return false;
  for (fs::recursive_directory_iterator item(source, error), end;
       !error && item != end; item.increment(error)) {
    const fs::path relative = fs::relative(item->path(), source, error);
    if (error) break;
    const fs::path target = destination / relative;
    if (item->is_directory()) fs::create_directories(target, error);
    else if (item->is_regular_file()) {
      fs::create_directories(target.parent_path(), error);
      if (!error) fs::copy_file(item->path(), target,
                                fs::copy_options::overwrite_existing, error);
    }
  }
  return !error;
}

bool launch_steamvr(const std::wstring &driver) {
  const std::wstring root = steam_root() + L"\\steamapps\\common\\SteamVR\\bin\\win64\\";
  std::wstring reg = L"\"" + root + L"vrpathreg.exe\" adddriver \"" + driver + L"\"";
  if (!run_and_wait(reg, 10000)) return false;
  std::wstring monitor = L"\"" + root + L"vrmonitor.exe\"";
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, monitor.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process)) return false;
  CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
}
