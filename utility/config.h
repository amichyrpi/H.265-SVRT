#pragma once
#include <string>
struct UtilitySettings { std::string host="ROOT.local", client_id; int refresh=60; int width=1920; int height=1080; bool verbose=false; };
UtilitySettings load_settings();
void save_settings(const UtilitySettings &settings);
bool write_steamvr_settings(const UtilitySettings &settings, bool enabled);
bool steamvr_is_running();
bool stop_steamvr();
bool install_steamvr_driver(const std::wstring &driver_directory);
bool launch_steamvr(const std::wstring &driver_directory);
