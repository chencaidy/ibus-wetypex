#include "adwaita_backport.h"
#include "language.hpp"

#include <cstddef>
#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <json-c/json.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WETYPE_ACCOUNT_TOOL
#define WETYPE_ACCOUNT_TOOL "wetypex-account"
#endif
#ifndef WETYPE_ENGINE_HOST
#define WETYPE_ENGINE_HOST "wetypex-engine-host"
#endif
#ifndef WETYPE_SUPPORT_DIR
#define WETYPE_SUPPORT_DIR ""
#endif
#ifndef WETYPE_SETTINGS
#define WETYPE_SETTINGS "wetypex-settings"
#endif

using namespace wetypex_transfer::lang;

namespace {

using JsonCallback = std::function<void(json_object *)>;

struct GrpcInfo {
  std::string ip;
  int port = 0;
  std::string ca;
  std::string server_name;
};

std::string data_root() { return std::string(g_get_user_data_dir()) + "/wetypex"; }

std::string transfer_asset(const char *directory, const char *name) {
  return data_root() + "/ui/" + directory + "/" + name;
}

std::string env_or(const char *name, const std::string &fallback) {
  const char *value = g_getenv(name);
  return value && *value ? value : fallback;
}

std::string aux_runtime(const std::string &runtime) {
  gchar *parent = g_path_get_dirname(runtime.c_str());
  std::string path = std::string(parent) + "/wxp2p";
  g_free(parent);
  return path;
}

std::string decode_base64(const char *encoded) {
  gsize size = 0;
  guchar *data = g_base64_decode(encoded, &size);
  std::string decoded;
  if (size)
    decoded.assign(reinterpret_cast<char *>(data), size);
  g_free(data);
  return decoded;
}

void redirect_session_stderr(GSubprocessLauncher *launcher, const std::string &state) {
  const std::string path = state + "/transport.log";
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0) {
    g_warning("Cannot open %s: %s", path.c_str(), g_strerror(errno));
    return;
  }
  fchmod(fd, 0600);
  g_subprocess_launcher_take_stderr_fd(launcher, fd);
}

bool is_virtual_interface(const char *name) {
  static const char *prefixes[] = {"docker", "veth", "virbr", "vmnet", "br-", "tun", "tap"};
  for (const char *prefix : prefixes)
    if (g_str_has_prefix(name, prefix))
      return true;
  return false;
}

std::string local_address() {
  std::string default_interface;
  gchar *routes = nullptr;
  if (g_file_get_contents("/proc/net/route", &routes, nullptr, nullptr)) {
    gchar **lines = g_strsplit(routes, "\n", -1);
    for (guint i = 0; lines[i]; ++i) {
      gchar interface_name[IFNAMSIZ]{};
      char destination[16]{};
      unsigned flags = 0;
      if (sscanf(lines[i], "%15s %15s %*s %x", interface_name, destination, &flags) == 3 &&
          std::strcmp(destination, "00000000") == 0 && (flags & 2)) {
        default_interface = interface_name;
        break;
      }
    }
    g_strfreev(lines);
    g_free(routes);
  }

  ifaddrs *interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0)
    return {};
  std::string fallback;
  std::string private_address;
  std::string preferred;
  for (ifaddrs *entry = interfaces; entry; entry = entry->ifa_next) {
    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP) ||
        (entry->ifa_flags & IFF_LOOPBACK))
      continue;
    const auto *address = reinterpret_cast<const sockaddr_in *>(entry->ifa_addr);
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) || g_str_has_prefix(text, "169.254."))
      continue;
    if (default_interface == entry->ifa_name && !is_virtual_interface(entry->ifa_name)) {
      preferred = text;
      break;
    }
    const uint32_t host = ntohl(address->sin_addr.s_addr);
    if (private_address.empty() && !is_virtual_interface(entry->ifa_name) &&
        ((host >> 24) == 10 || (host >> 16) == 0xc0a8 || (host >> 20) == 0xac1))
      private_address = text;
    if (fallback.empty())
      fallback = text;
  }
  freeifaddrs(interfaces);
  if (!preferred.empty())
    return preferred;
  return private_address.empty() ? fallback : private_address;
}

bool read_varint(const std::string &data, size_t &offset, uint64_t &value) {
  value = 0;
  for (unsigned shift = 0; shift < 64 && offset < data.size(); shift += 7) {
    const uint8_t byte = static_cast<uint8_t>(data[offset++]);
    value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if (!(byte & 0x80))
      return true;
  }
  return false;
}

void parse_endpoint(const std::string &data, GrpcInfo &info) {
  size_t offset = 0;
  while (offset < data.size()) {
    uint64_t tag = 0;
    if (!read_varint(data, offset, tag))
      return;
    const int field = static_cast<int>(tag >> 3);
    const int wire = static_cast<int>(tag & 7);
    if (wire == 0) {
      uint64_t value = 0;
      if (!read_varint(data, offset, value))
        return;
      if (field == 2 && !info.port)
        info.port = static_cast<int>(value);
    } else if (wire == 2) {
      uint64_t size = 0;
      if (!read_varint(data, offset, size) || size > data.size() - offset)
        return;
      if (field == 1 && info.ip.empty())
        info.ip.assign(data, offset, static_cast<size_t>(size));
      offset += static_cast<size_t>(size);
    } else {
      return;
    }
  }
}

void parse_available_nets(const std::string &data, GrpcInfo &info) {
  size_t offset = 0;
  while (offset < data.size()) {
    uint64_t tag = 0, size = 0;
    if (!read_varint(data, offset, tag) || (tag & 7) != 2 || !read_varint(data, offset, size) ||
        size > data.size() - offset)
      return;
    if ((tag >> 3) == 2)
      parse_endpoint(data.substr(offset, static_cast<size_t>(size)), info);
    offset += static_cast<size_t>(size);
  }
}

GrpcInfo parse_grpc_info(const std::string &data) {
  GrpcInfo info;
  size_t offset = 0;
  while (offset < data.size()) {
    uint64_t tag = 0;
    if (!read_varint(data, offset, tag))
      break;
    const int field = static_cast<int>(tag >> 3);
    const int wire = static_cast<int>(tag & 7);
    if (wire == 0) {
      uint64_t value = 0;
      if (!read_varint(data, offset, value))
        break;
      if (field == 2)
        info.port = static_cast<int>(value);
    } else if (wire == 2) {
      uint64_t size = 0;
      if (!read_varint(data, offset, size) || size > data.size() - offset)
        break;
      const std::string value(data, offset, static_cast<size_t>(size));
      offset += static_cast<size_t>(size);
      if (field == 1)
        info.ip = value;
      else if (field == 3)
        info.ca = value;
      else if (field == 4)
        info.server_name = value;
      else if (field == 9)
        parse_available_nets(value, info);
    } else {
      break;
    }
  }
  return info;
}

bool json_boolean(json_object *object, const char *key) {
  json_object *value = nullptr;
  return object && json_object_object_get_ex(object, key, &value) && json_object_get_boolean(value);
}

const char *json_string(json_object *object, const char *key) {
  json_object *value = nullptr;
  if (!object || !json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string))
    return "";
  return json_object_get_string(value);
}

int64_t json_integer(json_object *object, const char *key) {
  json_object *value = nullptr;
  return object && json_object_object_get_ex(object, key, &value) ? json_object_get_int64(value) : 0;
}

json_object *json_child(json_object *object, const char *key) {
  json_object *value = nullptr;
  return object && json_object_object_get_ex(object, key, &value) ? value : nullptr;
}

bool write_private(const std::string &path, const std::string &data) {
  GError *error = nullptr;
  const gboolean ok = g_file_set_contents_full(path.c_str(), data.data(), static_cast<gssize>(data.size()),
                                               G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error);
  if (!ok) {
    g_warning("Cannot write %s: %s", path.c_str(), error ? error->message : "unknown error");
    g_clear_error(&error);
    return false;
  }
  return true;
}

json_object *new_object() { return json_object_new_object(); }

void add_string(json_object *object, const char *key, const std::string &value) {
  json_object_object_add(object, key, json_object_new_string_len(value.data(), value.size()));
}

void add_json(json_object *object, const char *key, json_object *value) {
  if (value)
    json_object_object_add(object, key, json_object_get(value));
}

json_object *parse_json(const char *text, gssize length = -1) {
  if (!text || length == 0)
    return nullptr;
  json_tokener *tokener = json_tokener_new();
  json_object *object =
      json_tokener_parse_ex(tokener, text, length < 0 ? static_cast<int>(strlen(text)) : static_cast<int>(length));
  if (json_tokener_get_error(tokener) != json_tokener_success) {
    if (object)
      json_object_put(object);
    object = nullptr;
  }
  json_tokener_free(tokener);
  return object;
}

} // namespace

class TransferWindow : public std::enable_shared_from_this<TransferWindow> {
public:
  explicit TransferWindow(AdwApplication *application) {
    window_ = adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(window_), kAppTitle);
    gtk_window_set_default_size(GTK_WINDOW(window_), 360, 540);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    g_signal_connect(window_, "close-request", G_CALLBACK(on_close_request), this);

    toolbar_ = adw_toolbar_view_new();
    header_ = adw_header_bar_new();
    back_button_ = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(back_button_, kBackTooltip);
    gtk_widget_set_visible(back_button_, FALSE);
    g_signal_connect(back_button_, "clicked", G_CALLBACK(on_back_clicked), this);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header_), back_button_);
    header_title_ = gtk_label_new(kAppTitle);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_), header_title_);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_), header_);

    stack_ = adw_view_stack_new();
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_), stack_);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), toolbar_);
    build_home_page();
    build_transfer_page();

    if (g_getenv("WETYPE_TRANSFER_DIRECT"))
      g_idle_add(on_direct_start, this);
  }

  ~TransferWindow() { stop_session(); }

  void present() { gtk_window_present(GTK_WINDOW(window_)); }

  GtkWidget *widget() const { return window_; }

  void load_devices() {
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(devices_group_), kLoadingDevices);
    run_account({"group-info"}, [self = shared_from_this()](json_object *result) {
      self->clear_device_list();

      if (!result) {
        adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(self->devices_group_), kDevicesUnavailable);
        return;
      }

      json_object *devices = json_child(result, "devices");
      const int64_t own_uin = json_integer(result, "self_uin");
      bool has_devices = false;
      if (devices && json_object_is_type(devices, json_type_array)) {
        const size_t count = json_object_array_length(devices);
        for (size_t i = 0; i < count; ++i) {
          json_object *device = json_object_array_get_idx(devices, i);
          const int64_t uin = json_integer(device, "uin");
          if (uin <= 0 || uin == own_uin)
            continue;
          const int platform = static_cast<int>(json_integer(device, "platform"));
          const char *icon = platform == 1 || platform == 2 ? "phone-symbolic" : "computer-symbolic";
          GtkWidget *row = self->make_device_row(json_string(device, "name"), nullptr, icon,
                                                 [self, uin] { self->start_device_transfer(uin); });
          adw_preferences_group_add(ADW_PREFERENCES_GROUP(self->devices_group_), row);
          self->device_rows_.push_back(row);
          has_devices = true;
        }
      }
      const char *label = has_devices ? kSelectDeviceDescription : kNoPairedDevices;
      adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(self->devices_group_), label);

      GtkWidget *pair = self->make_device_row(kPairOtherDevices, nullptr, "preferences-system-symbolic",
                                              [self] { self->open_settings(); });
      adw_preferences_group_add(ADW_PREFERENCES_GROUP(self->devices_group_), pair);
      self->device_rows_.push_back(pair);
    });
  }

private:
  struct AccountContext {
    std::shared_ptr<TransferWindow> window;
    JsonCallback callback;
    uint64_t request_id = 0;
    bool session_scoped = false;
    uint64_t session_generation = 0;
  };

  static gboolean on_close_request(GtkWindow *, gpointer data) {
    auto *self = static_cast<TransferWindow *>(data);
    self->closing_ = true;
    self->disconnect_transfer();
    return FALSE;
  }

  static gboolean on_direct_start(gpointer data) {
    auto *self = static_cast<TransferWindow *>(data);
    self->show_transfer_page(true);
    self->start_session();
    return G_SOURCE_REMOVE;
  }

  static void on_back_clicked(GtkButton *, gpointer data) {
    auto *self = static_cast<TransferWindow *>(data);
    self->disconnect_transfer();
    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(self->stack_), "home");
    gtk_widget_set_visible(self->back_button_, FALSE);
    gtk_widget_set_visible(self->send_actions_, FALSE);
    gtk_widget_set_visible(self->progress_, FALSE);
    gtk_label_set_text(GTK_LABEL(self->header_title_), kAppTitle);
    gtk_label_set_text(GTK_LABEL(self->title_), kMyQrCode);
  }

  static gboolean on_poll_timer(gpointer data) {
    static_cast<TransferWindow *>(data)->poll_peer();
    return G_SOURCE_CONTINUE;
  }

  static void on_account_done(GObject *source, GAsyncResult *result, gpointer data) {
    std::unique_ptr<AccountContext> context(static_cast<AccountContext *>(data));
    auto *self = context->window.get();
    GError *error = nullptr;
    gchar *output = nullptr;
    gchar *errors = nullptr;
    const gboolean completed =
        g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &output, &errors, &error);
    json_object *reply = completed && g_subprocess_get_successful(G_SUBPROCESS(source)) ? parse_json(output) : nullptr;
    const bool current_request = context->request_id == self->account_request_id_;
    const bool current_session = !context->session_scoped ||
                                 (self->transfer_active_ && context->session_generation == self->session_generation_);
    if (current_request) {
      self->account_busy_ = false;
      g_clear_object(&self->account_process_);
    }
    if (current_request && !reply && !self->closing_ && current_session) {
      const char *detail = error ? error->message : (errors && *errors ? errors : nullptr);
      self->set_status(detail ? std::string(kAccountOperationFailedPrefix) + detail : kAccountOperationFailed);
    }
    g_free(output);
    g_free(errors);
    g_clear_error(&error);
    if (current_request && !self->closing_ && current_session)
      context->callback(reply);
    if (reply)
      json_object_put(reply);
  }

  static void on_session_line(GObject *source, GAsyncResult *result, gpointer data) {
    std::unique_ptr<std::shared_ptr<TransferWindow>> context(static_cast<std::shared_ptr<TransferWindow> *>(data));
    auto *self = context->get();
    GError *error = nullptr;
    gsize length = 0;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, &length, &error);
    const bool current_stream = G_DATA_INPUT_STREAM(source) == self->session_output_;
    if (line) {
      if (current_stream && !self->closing_)
        self->handle_session_event(line, length);
      g_free(line);
      if (current_stream && !self->closing_ && !self->stopping_ && self->session_)
        g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(source), G_PRIORITY_DEFAULT, self->session_cancel_,
                                            on_session_line,
                                            new std::shared_ptr<TransferWindow>(context->get()->shared_from_this()));
    } else if (current_stream && !self->closing_ && error &&
               !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      self->set_status(std::string(kReadTransferStatusFailedPrefix) + error->message);
    }
    g_clear_error(&error);
  }

  static void on_files_selected(GListModel *files, gpointer data) {
    using Context = std::pair<std::shared_ptr<TransferWindow>, bool>;
    std::unique_ptr<Context> context(static_cast<Context *>(data));
    auto *self = context->first.get();
    if (!self->closing_ && files) {
      const guint count = g_list_model_get_n_items(files);
      for (guint i = 0; i < count; ++i) {
        GFile *file = G_FILE(g_list_model_get_item(files, i));
        gchar *path = g_file_get_path(file);
        if (path) {
          self->send_file_command(context->second ? "upload_directory" : "upload", path);
          g_free(path);
        }
        g_object_unref(file);
      }
    }
  }

  static void on_choose_files(TransferWindow *self) {
    auto *context = new std::pair<std::shared_ptr<TransferWindow>, bool>(self->shared_from_this(), false);
    wetypex_file_dialog_open_multiple(GTK_WINDOW(self->window_), kSendFile, kSendAction, kCancelAction,
                                      on_files_selected, context);
  }

  static void on_choose_directory(TransferWindow *self) {
    auto *context = new std::pair<std::shared_ptr<TransferWindow>, bool>(self->shared_from_this(), true);
    wetypex_file_dialog_select_folder(GTK_WINDOW(self->window_), kSendDirectory, kSendAction, kCancelAction,
                                      on_files_selected, context);
  }

  void start_device_transfer(int64_t uin) {
    bound_uin_ = std::to_string(uin);
    show_transfer_page(false);
    start_session();
  }

  void build_home_page() {
    GtkWidget *page = adw_preferences_page_new();

    devices_group_ = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(devices_group_), kMyDevices);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(devices_group_));

    GtkWidget *actions = adw_preferences_group_new();
    GtkWidget *send_to_others = make_device_row(kSendToOthers, kViewMyQrCode, "network-transmit-symbolic", [this] {
      show_transfer_page(true);
      start_session();
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), send_to_others);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(actions));
    adw_view_stack_add_named(ADW_VIEW_STACK(stack_), page, "home");
  }

  GtkWidget *make_device_row(const char *title, const char *subtitle, const char *icon,
                             std::function<void()> activate) {
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != nullptr)
      adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    GtkWidget *image = gtk_image_new_from_icon_name(icon);
    adw_action_row_add_prefix(ADW_ACTION_ROW(row), image);
    GtkWidget *arrow = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(arrow, "flat");
    gtk_widget_set_valign(arrow, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), arrow);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), arrow);
    g_object_set_data_full(G_OBJECT(arrow), "activate", new std::function<void()>(std::move(activate)),
                           [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
    g_signal_connect(arrow, "clicked", G_CALLBACK(on_action_row_clicked), nullptr);
    return row;
  }

  static void on_action_row_clicked(GtkButton *button, gpointer) {
    auto *activate = static_cast<std::function<void()> *>(g_object_get_data(G_OBJECT(button), "activate"));
    if (activate)
      (*activate)();
  }

  void clear_device_list() {
    for (GtkWidget *row : device_rows_)
      adw_preferences_group_remove(ADW_PREFERENCES_GROUP(devices_group_), row);
    device_rows_.clear();
  }

  void build_transfer_page() {
    GtkWidget *page = adw_preferences_page_new();
    GtkWidget *group = adw_preferences_group_new();

    title_ = gtk_label_new(kMyQrCode);
    gtk_widget_add_css_class(title_, "title-2");
    gtk_widget_set_margin_top(title_, 8);
    gtk_widget_set_margin_bottom(title_, 8);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), title_);

    qr_ = gtk_picture_new();
    wetypex_picture_set_content_fit_contain(GTK_PICTURE(qr_));
    gtk_widget_set_hexpand(qr_, TRUE);
    gtk_widget_set_halign(qr_, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(qr_, 16);
    gtk_widget_set_margin_bottom(qr_, 16);
    gtk_widget_set_margin_start(qr_, 16);
    gtk_widget_set_margin_end(qr_, 16);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), qr_);

    status_ = gtk_label_new(kInitializingTransfer);
    gtk_label_set_wrap(GTK_LABEL(status_), TRUE);
    gtk_label_set_justify(GTK_LABEL(status_), GTK_JUSTIFY_CENTER);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), status_);

    progress_ = gtk_progress_bar_new();
    gtk_widget_set_visible(progress_, FALSE);
    gtk_widget_set_margin_top(progress_, 16);
    gtk_widget_set_margin_bottom(progress_, 16);
    gtk_widget_set_margin_start(progress_, 16);
    gtk_widget_set_margin_end(progress_, 16);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), progress_);

    send_actions_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *send_file_button = gtk_button_new_with_label(kSendFile);
    GtkWidget *send_directory_button = gtk_button_new_with_label(kSendDirectory);
    g_signal_connect_swapped(send_file_button, "clicked", G_CALLBACK(on_choose_files), this);
    g_signal_connect_swapped(send_directory_button, "clicked", G_CALLBACK(on_choose_directory), this);
    gtk_box_set_homogeneous(GTK_BOX(send_actions_), TRUE);
    gtk_widget_set_hexpand(send_file_button, TRUE);
    gtk_widget_set_hexpand(send_directory_button, TRUE);
    gtk_widget_set_margin_top(send_actions_, 8);
    gtk_widget_set_margin_bottom(send_actions_, 12);
    gtk_widget_set_margin_start(send_actions_, 12);
    gtk_widget_set_margin_end(send_actions_, 12);
    gtk_widget_set_hexpand(send_actions_, TRUE);
    gtk_box_append(GTK_BOX(send_actions_), send_file_button);
    gtk_box_append(GTK_BOX(send_actions_), send_directory_button);
    gtk_widget_set_sensitive(send_actions_, FALSE);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));

    adw_view_stack_add_named(ADW_VIEW_STACK(stack_), page, "transfer");
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar_), send_actions_);
    gtk_widget_set_visible(send_actions_, FALSE);
  }

  void set_status(const std::string &text) { gtk_label_set_text(GTK_LABEL(status_), text.c_str()); }

  void open_settings() {
    const char *argv[] = {WETYPE_SETTINGS, nullptr};
    GError *error = nullptr;
    if (!g_spawn_async(nullptr, const_cast<char **>(argv), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                       &error)) {
      g_warning("Failed to open settings: %s", error->message);
      g_clear_error(&error);
    }
  }

  void show_transfer_page(bool show_qr) {
    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(stack_), "transfer");
    gtk_widget_set_visible(back_button_, TRUE);
    gtk_widget_set_visible(send_actions_, TRUE);
    gtk_label_set_text(GTK_LABEL(header_title_), kInviteDevice);
    gtk_widget_set_visible(progress_, FALSE);
    gtk_widget_set_sensitive(send_actions_, FALSE);
    if (!show_qr) {
      gtk_label_set_text(GTK_LABEL(title_), kAppTitle);
      gtk_picture_set_filename(GTK_PICTURE(qr_),
                               transfer_asset("transfer-images", "img_connection_pc_to_phone.png").c_str());
      set_status(kInvitingPairedDevice);
    }
    gtk_window_present(GTK_WINDOW(window_));
  }

  void set_qr_from_base64(const char *encoded) {
    gsize size = 0;
    guchar *data = g_base64_decode(encoded, &size);
    GBytes *bytes = g_bytes_new_take(data, size);
    GError *error = nullptr;
    GdkTexture *texture = gdk_texture_new_from_bytes(bytes, &error);
    if (!texture) {
      set_status(std::string(kShowTransferQrFailedPrefix) + (error ? error->message : kInvalidImageFormat));
      g_clear_error(&error);
    } else {
      gtk_picture_set_paintable(GTK_PICTURE(qr_), GDK_PAINTABLE(texture));
      g_object_unref(texture);
    }
    g_bytes_unref(bytes);
  }

  void start_session() {
    if (session_)
      stop_session();
    ++session_generation_;
    transfer_active_ = true;
    const std::string root = data_root();
    const std::string runtime = env_or("WETYPE_FLURRY_RUNTIME", root + "/runtime/flurry");
    const std::string host = env_or("WETYPE_ENGINE_HOST", WETYPE_ENGINE_HOST);
    const std::string state = root + "/state/transfer";
    const char *downloads_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    const std::string downloads = std::string(downloads_dir ? downloads_dir : g_get_home_dir()) + "/WeTypeX";
    const std::string parts = state + "/parts";
    if (g_mkdir_with_parents(parts.c_str(), 0700) != 0 || g_mkdir_with_parents(downloads.c_str(), 0700) != 0) {
      set_status(kCreateTransferDirectoryFailed);
      return;
    }
    wxp_started_ = false;
    grpc_advertised_ = false;
    last_dispatch_.clear();
    const std::string wxp2p = aux_runtime(runtime);
    preloaded_wxp2p_ = g_file_test((wxp2p + "/manifest.txt").c_str(), G_FILE_TEST_EXISTS);

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE));
    redirect_session_stderr(launcher, state);
    g_subprocess_launcher_setenv(launcher, "WETYPE_SUPPORT_DIR", WETYPE_SUPPORT_DIR, TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_FLURRY_DOWNLOAD_DIR", downloads.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_FLURRY_TEMP_DIR", parts.c_str(), TRUE);
    if (preloaded_wxp2p_) {
      g_subprocess_launcher_setenv(launcher, "WETYPE_AUX_RUNTIME", wxp2p.c_str(), TRUE);
      g_subprocess_launcher_unsetenv(launcher, "WETYPE_WXP2P_DISPATCH_FILE");
    }
    const char *argv[] = {host.c_str(), runtime.c_str(), preloaded_wxp2p_ ? "flurry-wxp2p-server" : "flurry-server",
                          nullptr};
    GError *error = nullptr;
    session_ = g_subprocess_launcher_spawnv(launcher, argv, &error);
    g_object_unref(launcher);
    if (!session_) {
      set_status(std::string(kStartTransferCoreFailedPrefix) + (error ? error->message : kUnknownError));
      g_clear_error(&error);
      return;
    }
    stopping_ = false;
    g_clear_object(&session_cancel_);
    session_cancel_ = g_cancellable_new();
    GInputStream *stream = g_subprocess_get_stdout_pipe(session_);
    session_output_ = g_data_input_stream_new(stream);
    g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(session_output_), G_PRIORITY_DEFAULT, session_cancel_,
                                        on_session_line, new std::shared_ptr<TransferWindow>(shared_from_this()));
  }

  void stop_session() {
    stop_polling();
    if (!session_)
      return;
    stopping_ = true;
    if (session_cancel_)
      g_cancellable_cancel(session_cancel_);
    send_session("quit");
    g_subprocess_force_exit(session_);
    g_subprocess_wait(session_, nullptr, nullptr);
    g_clear_object(&session_output_);
    g_clear_object(&session_cancel_);
    g_clear_object(&session_);
  }

  void disconnect_transfer() {
    transfer_active_ = false;
    ++session_generation_;
    stop_session();
    if (account_process_) {
      g_subprocess_force_exit(account_process_);
      g_clear_object(&account_process_);
      account_busy_ = false;
      ++account_request_id_;
    }
    transfer_code_.clear();
    bound_uin_.clear();
    last_dispatch_.clear();
    wxp_started_ = false;
    grpc_advertised_ = false;
    gtk_widget_set_sensitive(send_actions_, FALSE);
    gtk_widget_set_visible(progress_, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 0.0);
    set_status(kConnectionClosed);
  }

  void start_wxp2p(const std::string &dispatch, json_object *peer) {
    if (dispatch.empty() || dispatch == last_dispatch_)
      return;
    last_dispatch_ = dispatch;
    if (wxp_started_)
      grpc_advertised_ = false;
    const std::string state = data_root() + "/state/transfer";
    const std::string dispatch_path = state + "/wxp2p-dispatch.bin";
    if (!write_private(dispatch_path, dispatch)) {
      set_status(kSavePhoneDispatchFailed);
      return;
    }
    const std::string ca_path = state + "/peer-ca.pem";
    const std::string ca = decode_base64(json_string(peer, "public_cer"));
    if (ca.empty() || !write_private(ca_path, ca)) {
      set_status(kSavePhoneCredentialsFailed);
      return;
    }
    if (preloaded_wxp2p_ && !wxp_started_ && session_) {
      wxp_started_ = true;
      set_status(kConnectingToPhone);
      send_session("wxp2p_start", "dispatch_path", dispatch_path.c_str(), "remote_ca_file", ca_path.c_str());
      return;
    }
    stop_session();
    const std::string root = data_root();
    const std::string runtime = env_or("WETYPE_FLURRY_RUNTIME", root + "/runtime/flurry");
    const std::string host = env_or("WETYPE_ENGINE_HOST", WETYPE_ENGINE_HOST);
    const std::string wxp2p = aux_runtime(runtime);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE));
    redirect_session_stderr(launcher, state);
    g_subprocess_launcher_setenv(launcher, "WETYPE_AUX_RUNTIME", wxp2p.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_WXP2P_DISPATCH_FILE", dispatch_path.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_FLURRY_REMOTE_CA_FILE", ca_path.c_str(), TRUE);
    const char *downloads_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    const std::string downloads = std::string(downloads_dir ? downloads_dir : g_get_home_dir()) + "/WeTypeX";
    g_subprocess_launcher_setenv(launcher, "WETYPE_SUPPORT_DIR", WETYPE_SUPPORT_DIR, TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_FLURRY_DOWNLOAD_DIR", downloads.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "WETYPE_FLURRY_TEMP_DIR", (state + "/parts").c_str(), TRUE);
    const char *argv[] = {host.c_str(), runtime.c_str(), "flurry-wxp2p-server", nullptr};
    GError *error = nullptr;
    session_ = g_subprocess_launcher_spawnv(launcher, argv, &error);
    g_object_unref(launcher);
    if (!session_) {
      set_status(std::string(kStartTransferCoreFailedPrefix) + (error ? error->message : kUnknownError));
      g_clear_error(&error);
      return;
    }
    stopping_ = false;
    session_cancel_ = g_cancellable_new();
    session_output_ = g_data_input_stream_new(g_subprocess_get_stdout_pipe(session_));
    g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(session_output_), G_PRIORITY_DEFAULT, session_cancel_,
                                        on_session_line, new std::shared_ptr<TransferWindow>(shared_from_this()));
    set_status(kConnectingToPhone);
    wxp_started_ = true;
  }

  void handle_session_event(const char *line, gsize length) {
    json_object *event = parse_json(line, static_cast<gssize>(length));
    if (!event)
      return;
    const std::string type = json_string(event, "event");
    if (type == "ready") {
      preloaded_wxp2p_ = std::strcmp(json_string(event, "transport"), "wxp2p") == 0;
      request_code(event);
    } else if (type == "grpc_ready") {
      advertise_grpc(event);
    } else if (type == "role_started") {
      set_status(kDeviceFoundConnecting);
    } else if (type == "connected") {
      stop_polling();
      gtk_picture_set_filename(GTK_PICTURE(qr_),
                               transfer_asset("transfer-images", "img_connection_pc_to_phone.png").c_str());
      gtk_label_set_text(GTK_LABEL(title_), kAppTitle);
      set_status(kConnectedCanTransfer);
      gtk_widget_set_sensitive(send_actions_, TRUE);
    } else if (type == "disconnected") {
      set_status(kDeviceDisconnected);
      gtk_widget_set_sensitive(send_actions_, FALSE);
      if (!transfer_code_.empty())
        start_polling();
    } else if (type == "connect_end") {
      set_status(kConnectionRetrying);
      gtk_widget_set_sensitive(send_actions_, FALSE);
      if (!transfer_code_.empty())
        start_polling();
    } else if (g_str_has_suffix(type.c_str(), "_progress")) {
      const int64_t done = json_integer(event, "transferred");
      const int64_t total = json_integer(event, "total");
      gtk_widget_set_visible(progress_, TRUE);
      gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_),
                                    total > 0 ? std::clamp(static_cast<double>(done) / total, 0.0, 1.0) : 0.0);
      set_status(g_str_has_prefix(type.c_str(), "upload") ? kSendingFileProgress : kReceivingFileProgress);
    } else if (g_str_has_suffix(type.c_str(), "_done")) {
      if (json_integer(event, "status") == 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1.0);
        set_status(g_str_has_prefix(type.c_str(), "upload") ? kFileSent : kFileSaved);
      } else {
        gtk_widget_set_visible(progress_, FALSE);
        const char *error = json_string(event, "error");
        set_status(std::string(g_str_has_prefix(type.c_str(), "upload") ? kFileSendFailed : kFileSaveFailed) +
                   (*error ? std::string("：") + error : ""));
      }
    } else if (type == "start_failed") {
      set_status(kUnableToEstablishSecureConnection);
    }
    json_object_put(event);
  }

  void request_code(json_object *identity) {
    const bool wxp2p = std::strcmp(json_string(identity, "transport"), "wxp2p") == 0;
    const std::string ip = local_address();
    if (!wxp2p && ip.empty()) {
      set_status(kNoLanAddress);
      return;
    }
    const std::string path = data_root() + "/state/p2p-info.json";
    const std::string directory = data_root() + "/state";
    if (g_mkdir_with_parents(directory.c_str(), 0700) != 0) {
      set_status(kCreateTransferSessionDirectoryFailed);
      return;
    }
    json_object *info = new_object();
    add_string(info, "ip", ip);
    if (!wxp2p) {
      add_json(info, "port", json_child(identity, "port"));
    }
    add_json(info, "ca_cert", json_child(identity, "ca_cert"));
    add_json(info, "server_name", json_child(identity, "server_name"));
    const std::string content = json_object_to_json_string_ext(info, JSON_C_TO_STRING_PLAIN);
    const bool saved = write_private(path, content);
    json_object_put(info);
    if (!saved) {
      set_status(kSaveTransferSessionFailed);
      return;
    }
    std::vector<std::string> arguments;
    if (bound_uin_.empty())
      arguments = {"p2p-code", path};
    else
      arguments = {"p2p-init", bound_uin_, path};
    run_account(
        arguments,
        [this](json_object *result) {
          if (!result)
            return;
          const char *code = json_string(result, "transfer_code");
          transfer_code_ = code;
          last_dispatch_.clear();
          if (!json_boolean(result, "ok") || transfer_code_.empty() ||
              (bound_uin_.empty() && !*json_string(result, "transfer_image"))) {
            set_status(kCreateTransferQrFailed);
            return;
          }
          if (bound_uin_.empty())
            set_qr_from_base64(json_string(result, "transfer_image"));
          const int minutes = std::max<int64_t>(1, json_integer(result, "expiration") / 60000);
          set_status(std::string(bound_uin_.empty() ? kQrExpiresIn : kInviteExpiresIn) + std::to_string(minutes) +
                     kMinutesUntilExpiry);
          start_polling();
        },
        true);
  }

  void poll_peer() {
    if (account_busy_ || transfer_code_.empty())
      return;
    run_account(
        {"p2p-peer", transfer_code_},
        [this](json_object *result) {
          if (!result || !json_boolean(result, "ok"))
            return;
          json_object *peer = json_child(result, "peer");
          const std::string dispatch = decode_base64(json_string(result, "dispatch_buf"));
          if (!dispatch.empty()) {
            start_wxp2p(dispatch, peer);
            return;
          }
          if (!json_boolean(peer, "ready"))
            return;
          const GrpcInfo grpc = parse_grpc_info(decode_base64(json_string(peer, "grpc_net_info")));
          if (grpc.ca.empty())
            return;
          stop_polling();
          const std::string ca_path = data_root() + "/state/transfer/peer-ca.pem";
          if (!write_private(ca_path, grpc.ca)) {
            set_status(kSavePeerCredentialsFailed);
            return;
          }
          send_path_command(ca_path);
        },
        true);
  }

  void advertise_grpc(json_object *identity) {
    if (grpc_advertised_ || transfer_code_.empty())
      return;
    const std::string ip = local_address();
    const int port = static_cast<int>(json_integer(identity, "port"));
    if (ip.empty() || port <= 0) {
      set_status(kPublishPhoneEndpointFailed);
      return;
    }
    grpc_advertised_ = true;
    stop_polling();
    const std::string path = data_root() + "/state/transfer/p2p-update.json";
    json_object *info = new_object();
    add_string(info, "ip", ip);
    json_object_object_add(info, "port", json_object_new_int(port));
    add_json(info, "ca_cert", json_child(identity, "ca_cert"));
    add_json(info, "server_name", json_child(identity, "server_name"));
    json_object_object_add(info, "preferred_client", json_object_new_boolean(FALSE));
    const std::string content = json_object_to_json_string_ext(info, JSON_C_TO_STRING_PLAIN);
    const bool saved = write_private(path, content);
    json_object_put(info);
    if (!saved) {
      set_status(kSaveTransferEndpointFailed);
      grpc_advertised_ = false;
      start_polling();
      return;
    }
    run_account(
        {"p2p-update", transfer_code_, path},
        [this](json_object *result) {
          if (!result || !json_boolean(result, "ok")) {
            set_status(kPublishSecureEndpointFailed);
            grpc_advertised_ = false;
            start_polling();
            return;
          }
          start_polling();
        },
        true);
  }

  void run_account(const std::vector<std::string> &arguments, JsonCallback callback, bool session_scoped = false) {
    if (account_busy_)
      return;
    account_busy_ = true;
    const uint64_t generation = session_generation_;
    const uint64_t request_id = ++account_request_id_;
    const std::string tool = env_or("WETYPE_ACCOUNT_TOOL", WETYPE_ACCOUNT_TOOL);
    std::vector<const char *> argv{tool.c_str()};
    for (const auto &argument : arguments)
      argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    GError *error = nullptr;
    GSubprocess *process = g_subprocess_newv(
        argv.data(), static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);
    if (!process) {
      account_busy_ = false;
      set_status(std::string(kStartAccountServiceFailedPrefix) + (error ? error->message : kUnknownError));
      g_clear_error(&error);
      if (callback && request_id == account_request_id_ &&
          (!session_scoped || (transfer_active_ && session_generation_ == generation)))
        callback(nullptr);
      return;
    }
    account_process_ = G_SUBPROCESS(g_object_ref(process));
    auto *context = new AccountContext{shared_from_this(), std::move(callback), request_id, session_scoped, generation};
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr, on_account_done, context);
    g_object_unref(process);
  }

  void send_session(const char *action, const char *key1 = nullptr, const char *value1 = nullptr,
                    const char *key2 = nullptr, const char *value2 = nullptr) {
    if (!session_)
      return;
    json_object *command = new_object();
    add_string(command, "action", action);
    if (key1 && value1)
      add_string(command, key1, value1);
    if (key2 && value2)
      add_string(command, key2, value2);
    std::string data = json_object_to_json_string_ext(command, JSON_C_TO_STRING_PLAIN);
    data.push_back('\n');
    json_object_put(command);
    GOutputStream *input = g_subprocess_get_stdin_pipe(session_);
    GError *error = nullptr;
    if (!input || !g_output_stream_write_all(input, data.data(), data.size(), nullptr, nullptr, &error)) {
      if (error) {
        set_status(std::string(kSendCommandToTransferCoreFailedPrefix) + error->message);
        g_clear_error(&error);
      }
    }
  }

  void send_session(const char *action, const char *key, const std::string &value) {
    send_session(action, key, value.c_str());
  }

  void send_path_command(const std::string &ca_path) { send_session("server", "remote_ca_file", ca_path); }

  void send_file_command(const char *action, const char *path) { send_session(action, "path", path); }

  void stop_polling() {
    if (poll_timer_) {
      g_source_remove(poll_timer_);
      poll_timer_ = 0;
    }
  }

  void start_polling() {
    if (!poll_timer_)
      poll_timer_ = g_timeout_add(250, on_poll_timer, this);
  }

private:
  GtkWidget *window_ = nullptr;
  GtkWidget *toolbar_ = nullptr;
  GtkWidget *header_ = nullptr;
  GtkWidget *header_title_ = nullptr;
  GtkWidget *back_button_ = nullptr;
  GtkWidget *stack_ = nullptr;
  GtkWidget *devices_group_ = nullptr;
  std::vector<GtkWidget *> device_rows_;
  GtkWidget *title_ = nullptr;
  GtkWidget *qr_ = nullptr;
  GtkWidget *status_ = nullptr;
  GtkWidget *progress_ = nullptr;
  GtkWidget *send_actions_ = nullptr;
  GSubprocess *session_ = nullptr;
  GSubprocess *account_process_ = nullptr;
  GDataInputStream *session_output_ = nullptr;
  GCancellable *session_cancel_ = nullptr;
  guint poll_timer_ = 0;
  std::string transfer_code_;
  std::string bound_uin_;
  std::string last_dispatch_;
  bool account_busy_ = false;
  uint64_t account_request_id_ = 0;
  uint64_t session_generation_ = 0;
  bool transfer_active_ = false;
  bool preloaded_wxp2p_ = false;
  bool wxp_started_ = false;
  bool grpc_advertised_ = false;
  bool stopping_ = false;
  bool closing_ = false;
};

namespace {
void on_activate(AdwApplication *application, gpointer) {
  if (GtkWindow *active = gtk_application_get_active_window(GTK_APPLICATION(application))) {
    gtk_window_present(active);
    return;
  }
  auto window = std::make_shared<TransferWindow>(application);
  g_object_set_data_full(G_OBJECT(window->widget()), "wetypex-transfer-state",
                         new std::shared_ptr<TransferWindow>(window),
                         [](gpointer data) { delete static_cast<std::shared_ptr<TransferWindow> *>(data); });
  window->load_devices();
  window->present();
}
} // namespace

int main(int argc, char **argv) {
  g_set_prgname("wetypex-transfer");
  g_set_application_name(kAppName);

  AdwApplication *application = adw_application_new(kAppID, static_cast<GApplicationFlags>(0));
  g_signal_connect(application, "activate", G_CALLBACK(on_activate), nullptr);

  const int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}
