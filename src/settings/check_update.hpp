#pragma once

#include <adwaita.h>
#include <string>

namespace ibus_settings {

std::string get_installed_version();
void setup_update_check(AdwActionRow *row, GtkButton *button);

} // namespace ibus_settings
