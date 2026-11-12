#include "check_update.hpp"

#include "../common/json.hpp"
#include "language.hpp"

#include <glib/gstdio.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/utsname.h>
#include <vector>
#ifdef __GLIBC__
#include <gnu/libc-version.h>
#endif

namespace ibus_settings {
namespace {

using namespace lang;

struct ReleaseInfo {
  std::string version;
  std::string family;
  std::string architecture;
  std::string name;
  std::string package_url;
  std::string checksum_url;
};

struct State {
  ReleaseInfo release;
  bool available = false;
};

struct Operation {
  GtkButton *button;
  bool install;
  ReleaseInfo release;

  ~Operation() { g_object_unref(button); }
};

std::string run(const std::vector<std::string> &arguments) {
  std::vector<const char *> argv;
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  GError *error = nullptr;
  GSubprocess *process = g_subprocess_newv(
      argv.data(), static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
      &error);
  if (!process) {
    std::string message = error->message;
    g_clear_error(&error);
    throw std::runtime_error(message);
  }
  gchar *output = nullptr;
  gchar *errors = nullptr;
  const bool completed = g_subprocess_communicate_utf8(process, nullptr, nullptr, &output, &errors, &error);
  const bool success = completed && g_subprocess_get_successful(process);
  std::string result = output ? output : "";
  std::string message = error ? error->message : (errors && *errors ? errors : "命令执行失败");
  g_free(output);
  g_free(errors);
  g_clear_error(&error);
  g_object_unref(process);
  if (!success)
    throw std::runtime_error(message);
  return result;
}

std::array<unsigned long long, 5> version(const std::string &text) {
  static const std::regex pattern(
      R"(^(?:[0-9]+:)?v?([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9]+))?(?:[.+~][a-zA-Z0-9.+~_-]+)?\s*$)");
  std::smatch match;
  if (!std::regex_match(text, match, pattern))
    throw std::runtime_error("无法解析版本号: " + text);
  std::array<unsigned long long, 5> result{};
  for (size_t i = 0; i < result.size(); ++i)
    if (match[i + 1].matched)
      result[i] = std::stoull(match[i + 1].str());
  return result;
}

std::string family() {
  gchar *id = g_get_os_info(G_OS_INFO_KEY_ID);
  gchar *like = g_get_os_info("ID_LIKE");
  std::string names = std::string(id ? id : "") + " " + (like ? like : "");
  g_free(id);
  g_free(like);
  std::istringstream input(names);
  std::string name;
  bool debian = false, fedora = false;
  while (input >> name) {
    if (name == "arch")
      return "arch";
    debian |= name == "debian" || name == "ubuntu";
    fedora |= name == "fedora" || name == "rhel" || name == "centos";
  }
  if (debian)
    return "debian";
  if (fedora)
    return "fedora";
  throw std::runtime_error("不支持此 Linux 发行版");
}

std::string cpu_architecture() {
  struct utsname system{};
  if (uname(&system) != 0)
    throw std::runtime_error("无法检测 CPU 架构");
  const std::string machine = system.machine;
  if (machine == "x86_64" || machine == "amd64")
    return "x86_64";
  if (machine == "aarch64" || machine == "arm64")
    return "aarch64";
  throw std::runtime_error("不支持此 CPU 架构: " + machine);
}

std::string installed_version(const std::string &system) {
  if (system == "arch")
    return run({"pacman", "-Q", "ibus-wetypex"}).substr(std::string("ibus-wetypex ").size());
  if (system == "debian") {
    const std::string status = run({"dpkg-query", "-W", "-f=${Status}", "ibus-wetypex"});
    if (status != "install ok installed")
      throw std::runtime_error("系统中未安装 ibus-wetypex 软件包");
    return run({"dpkg-query", "-W", "-f=${Version}", "ibus-wetypex"});
  }
  return run({"rpm", "-q", "--qf", "%{VERSION}-%{RELEASE}", "ibus-wetypex"});
}

std::string download(const std::string &url, const std::string &destination = "", const char *limit = "8388608") {
  if (url.compare(0, 8, "https://") != 0)
    throw std::runtime_error("更新地址不是 HTTPS");
  std::vector<std::string> args = {
      "curl",       "--disable", "--fail",         "--silent", "--show-error",      "--location",
      "--proto",    "=https",    "--proto-redir",  "=https",   "--connect-timeout", "20",
      "--max-time", "300",       "--max-filesize", limit,      "--user-agent",      "WeTypeX updater"};
  if (!destination.empty()) {
    args.push_back("--output");
    args.push_back(destination);
  }
  args.push_back("--");
  args.push_back(url);
  return run(args);
}

ReleaseInfo check() {
  ReleaseInfo release;
  release.family = family();
  release.architecture = cpu_architecture();
  if (release.architecture != "x86_64")
    throw std::runtime_error("不支持的 CPU 架构: " + release.architecture);
  const auto current = version(installed_version(release.family));
  const char *api = g_getenv("WETYPE_UPDATE_API");
  const std::string body =
      download(api && *api ? api : "https://api.github.com/repos/chencaidy/ibus-wetypex/releases/latest");
  auto json = wire::parse(body);
  if (!json || !json_object_is_type(json.get(), json_type_object))
    throw std::runtime_error("更新服务返回了无效数据");
  release.version = wire::str(json.get(), "tag_name");
  const auto latest = version(release.version);
  if (latest <= current)
    return release;

  const std::string package_version = R"(([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+-[0-9]+))";
  std::string pattern;
  if (release.family == "arch")
    pattern = "^ibus-wetypex-" + package_version + R"(-x86_64\.pkg\.tar\.zst$)";
  else if (release.family == "debian") {
    bool legacy = false;
#ifdef __GLIBC__
    unsigned int major = 0, minor = 0;
    if (std::sscanf(gnu_get_libc_version(), "%u.%u", &major, &minor) == 2)
      legacy = major < 2 || (major == 2 && minor < 35);
#endif
    pattern = (legacy ? "^ibus-wetypex-legacy_" : "^ibus-wetypex_") + package_version + R"(_amd64\.deb$)";
  } else
    pattern = "^ibus-wetypex-" + package_version + R"(\.x86_64\.rpm$)";
  const std::regex package_pattern(pattern);
  json_object *assets = wire::get(json.get(), "assets");
  if (!assets || !json_object_is_type(assets, json_type_array))
    throw std::runtime_error("更新服务未提供软件包列表");
  for (size_t i = 0; i < json_object_array_length(assets); ++i) {
    json_object *asset = json_object_array_get_idx(assets, i);
    const std::string name = wire::str(asset, "name");
    std::smatch match;
    if (name == "SHA256SUMS")
      release.checksum_url = wire::str(asset, "browser_download_url");
    else if (std::regex_match(name, match, package_pattern) && version(match[1].str()) == latest) {
      release.name = name;
      release.package_url = wire::str(asset, "browser_download_url");
    }
  }
  if (release.name.empty() || release.package_url.empty() || release.checksum_url.empty())
    throw std::runtime_error("此版本没有适用于当前系统的软件包或校验文件");
  return release;
}

struct TemporaryPackage {
  std::string directory;
  std::string path;
  ~TemporaryPackage() {
    if (!path.empty())
      g_unlink(path.c_str());
    if (!directory.empty())
      g_rmdir(directory.c_str());
  }
};

void install(const ReleaseInfo &release) {
  char directory[] = "/tmp/wetypex-update-XXXXXX";
  if (!g_mkdtemp(directory))
    throw std::runtime_error("无法创建临时下载目录");
  TemporaryPackage temp{directory, std::string(directory) + "/" + release.name};
  const std::string checksums = download(release.checksum_url);
  download(release.package_url, temp.path, "1073741824");

  std::ifstream file(temp.path, std::ios::binary);
  if (!file)
    throw std::runtime_error("无法读取下载的软件包");
  GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
  if (!checksum)
    throw std::runtime_error("无法初始化软件包校验");
  std::array<char, 65536> buffer;
  while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
    g_checksum_update(checksum, reinterpret_cast<const guchar *>(buffer.data()), file.gcount());
  const std::string digest = g_checksum_get_string(checksum);
  g_checksum_free(checksum);
  if (file.bad())
    throw std::runtime_error("读取下载的软件包失败");

  std::istringstream lines(checksums);
  std::string line, expected;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string hash, name;
    if (fields >> hash >> name) {
      if (!name.empty() && name[0] == '*')
        name.erase(0, 1);
      if (name == release.name) {
        expected = hash;
        break;
      }
    }
  }
  if (expected != digest)
    throw std::runtime_error("软件包 SHA256 校验失败");

  if (release.family == "arch")
    run({"pkexec", "pacman", "-U", "--noconfirm", temp.path});
  else if (release.family == "debian")
    run({"pkexec", "apt-get", "install", "-y", temp.path});
  else
    run({"pkexec", "dnf", "install", "-y", temp.path});
}

void work(GTask *task, gpointer, gpointer data, GCancellable *) {
  auto *operation = static_cast<Operation *>(data);
  try {
    auto result = std::make_unique<ReleaseInfo>();
    if (operation->install)
      install(operation->release);
    else
      *result = check();
    g_task_return_pointer(task, result.release(), [](gpointer value) { delete static_cast<ReleaseInfo *>(value); });
  } catch (const std::exception &error) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", error.what());
  }
}

void finished(GObject *source, GAsyncResult *result, gpointer) {
  auto *row = ADW_ACTION_ROW(source);
  auto *task = G_TASK(result);
  auto *operation = static_cast<Operation *>(g_task_get_task_data(task));
  auto *state = static_cast<State *>(g_object_get_data(G_OBJECT(row), "update-state"));
  GError *error = nullptr;
  std::unique_ptr<ReleaseInfo> release(static_cast<ReleaseInfo *>(g_task_propagate_pointer(task, &error)));
  if (error) {
    gchar *detail = g_utf8_make_valid(error->message, -1);
    std::string message = std::string(operation->install ? kUpdateInstallFailed : kUpdateFailed) + "：" + detail;
    adw_action_row_set_subtitle(row, message.c_str());
    if (!operation->install)
      gtk_widget_remove_css_class(GTK_WIDGET(operation->button), "suggested-action");
    g_free(detail);
    g_clear_error(&error);
  } else if (operation->install) {
    state->available = false;
    gtk_button_set_label(operation->button, kUpdateActionLabel);
    gtk_widget_remove_css_class(GTK_WIDGET(operation->button), "suggested-action");
    adw_action_row_set_subtitle(row, kUpdateInstalled);
  } else if (release->name.empty()) {
    state->available = false;
    gtk_button_set_label(operation->button, kUpdateActionLabel);
    gtk_widget_remove_css_class(GTK_WIDGET(operation->button), "suggested-action");
    adw_action_row_set_subtitle(row, kUpdateNoUpdate);
  } else {
    state->release = std::move(*release);
    state->available = true;
    gtk_button_set_label(operation->button, kUpdateInstall);
    gtk_widget_add_css_class(GTK_WIDGET(operation->button), "suggested-action");
    const std::string message = std::string(kUpdateAvailable) + state->release.version;
    adw_action_row_set_subtitle(row, message.c_str());
  }
  gtk_widget_set_sensitive(GTK_WIDGET(operation->button), TRUE);
}

void clicked(GtkButton *button, gpointer data) {
  auto *row = ADW_ACTION_ROW(data);
  auto *state = static_cast<State *>(g_object_get_data(G_OBJECT(row), "update-state"));
  gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
  adw_action_row_set_subtitle(row, state->available ? kUpdateInstalling : kUpdateChecking);
  GTask *task = g_task_new(row, nullptr, finished, nullptr);
  auto *operation = new Operation{GTK_BUTTON(g_object_ref(button)), state->available, state->release};
  g_task_set_task_data(task, operation, [](gpointer value) { delete static_cast<Operation *>(value); });
  g_task_run_in_thread(task, work);
  g_object_unref(task);
}

} // namespace

std::string get_installed_version() {
  const std::string system = family();
  return installed_version(system);
}

void setup_update_check(AdwActionRow *row, GtkButton *button) {
  g_object_set_data_full(G_OBJECT(row), "update-state", new State,
                         [](gpointer value) { delete static_cast<State *>(value); });
  g_signal_connect_object(button, "clicked", G_CALLBACK(clicked), row, static_cast<GConnectFlags>(0));
}

} // namespace ibus_settings
