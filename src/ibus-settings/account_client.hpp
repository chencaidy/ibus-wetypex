#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ibus_settings {

struct AccountReply {
  bool ok = false;
  std::string output;
  std::string error;
};

using AccountCallback = std::function<void(AccountReply)>;

void run_account(std::vector<std::string> arguments, AccountCallback done);

} // namespace ibus_settings
