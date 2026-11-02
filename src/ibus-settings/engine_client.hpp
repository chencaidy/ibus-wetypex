#pragma once

#include <functional>
#include <string>

namespace ibus_settings {

struct EngineReply {
  bool ok = false;
  std::string json;
  std::string error;
};

using EngineCallback = std::function<void(EngineReply)>;

void request_hotword_list(EngineCallback done);
void request_hotword_set(const std::string &id, const std::string &key,
                         const std::string &words, EngineCallback done);

} // namespace ibus_settings
