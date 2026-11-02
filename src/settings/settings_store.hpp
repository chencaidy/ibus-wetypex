#pragma once

#include "../common/json.hpp"

#include <json-c/json.h>

#include <string>

namespace ibus_settings {

std::string config_path();
wire::Json read_settings();
wire::Json read_json_file(const std::string &path);

bool setting_bool(json_object *object, const char *key, bool fallback);
int setting_int(json_object *object, const char *key, int fallback);
std::string setting_string(json_object *object, const char *key,
                           const std::string &fallback = {});

bool write_setting_bool(const char *key, bool value);
bool write_setting_int(const char *key, int value);
bool write_setting_string(const char *key, const std::string &value);

} // namespace ibus_settings
