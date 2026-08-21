#pragma once
#include <string>
struct UtilitySettings { std::string host="ROOT.local", client_id; int refresh=60; int width=2880; int height=1600; bool verbose=false; };
UtilitySettings load_settings();
void save_settings(const UtilitySettings &settings);
bool write_steamvr_settings(const UtilitySettings &settings, bool enabled);
#ifdef SVRT_CONFIG_TESTING
bool config_json_regression_test();
#endif
bool steamvr_is_running();
bool stop_steamvr();
bool install_steamvr_driver(const std::wstring &driver_directory);
bool launch_steamvr(const std::wstring &driver_directory);
