#include "settings_store.hpp"

#include <glib.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>

namespace ibus_settings {
namespace {

bool write_object(json_object *object) {
  const auto path = config_path();
  const char *text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
  if (!text)
    return false;
  std::string contents(text);
  contents.push_back('\n');
  GError *error = nullptr;
  const bool ok = g_file_set_contents_full(
      path.c_str(), contents.data(), static_cast<gssize>(contents.size()),
      static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE), 0644, &error);
  if (!ok) {
    g_warning("Unable to write %s: %s", path.c_str(), error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  return ok;
}

} // namespace

std::string config_path() {
  gchar *directory = g_build_filename(g_get_user_config_dir(), "fcitx5", nullptr);
  g_mkdir_with_parents(directory, 0755);
  gchar *path = g_build_filename(directory, "wetypex.json", nullptr);
  std::string result(path);
  g_free(path);
  g_free(directory);
  return result;
}

wire::Json read_json_file(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    return {};
  std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return wire::parse(contents);
}

wire::Json read_settings() {
  auto json = read_json_file(config_path());
  if (!json || !json_object_is_type(json.get(), json_type_object))
    return wire::object();
  return json;
}

bool setting_bool(json_object *object, const char *key, bool fallback) {
  auto *value = wire::get(object, key);
  if (!value)
    return fallback;
  if (json_object_is_type(value, json_type_boolean) || json_object_is_type(value, json_type_int))
    return json_object_get_boolean(value);
  return fallback;
}

int setting_int(json_object *object, const char *key, int fallback) {
  auto *value = wire::get(object, key);
  if (!value)
    return fallback;
  if (json_object_is_type(value, json_type_int))
    return static_cast<int>(json_object_get_int64(value));
  if (json_object_is_type(value, json_type_double)) {
    const double number = json_object_get_double(value);
    if (std::isfinite(number) && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max())
      return static_cast<int>(number);
  }
  return fallback;
}

std::string setting_string(json_object *object, const char *key, const std::string &fallback) {
  auto *value = wire::get(object, key);
  return value && json_object_is_type(value, json_type_string)
             ? std::string(json_object_get_string(value), json_object_get_string_len(value))
             : fallback;
}

bool write_setting_bool(const char *key, bool value) {
  auto settings = read_settings();
  json_object_object_add(settings.get(), key, json_object_new_boolean(value));
  return write_object(settings.get());
}

bool write_setting_int(const char *key, int value) {
  auto settings = read_settings();
  json_object_object_add(settings.get(), key, json_object_new_int(value));
  return write_object(settings.get());
}

bool write_setting_string(const char *key, const std::string &value) {
  auto settings = read_settings();
  json_object_object_add(settings.get(), key, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
  return write_object(settings.get());
}

} // namespace ibus_settings
