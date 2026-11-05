// Synchronizes the clipboard through the original WeType service.
//
// The GNOME Shell extension is the clipboard source: it reports local copies
// (Changed), we fetch the content once, and this daemon uploads it. Remote
// content goes back through the same extension (SetText/SetImage).
//
// The extension may be disabled/enabled or restarted at any time. The daemon
// watches its bus name: whenever a new owner appears it re-verifies the owner
// and subscribes again; while it is absent, synchronization is paused.
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <csignal>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../common/json.hpp"

static const char *NAME = "org.gnome.Shell.Extensions.WeTypeXClipWatcher";
static const char *PATH = "/org/gnome/Shell/Extensions/WeTypeXClipWatcher";
static const char *IFACE = "org.gnome.Shell.Extensions.WeTypeXClipWatcher";
static const char *ERR_EXPIRED = "org.gnome.Shell.Extensions.WeTypeXClipWatcher.Error.Expired";
static const char *ERR_WRONG_TYPE = "org.gnome.Shell.Extensions.WeTypeXClipWatcher.Error.WrongType";
static const char *ERR_ACCESS_DENIED = "org.gnome.Shell.Extensions.WeTypeXClipWatcher.Error.AccessDenied";
static const char *ERR_UNAVAILABLE = "org.gnome.Shell.Extensions.WeTypeXClipWatcher.Error.Unavailable";
static const char *SHELL_EXE = "/usr/bin/gnome-shell";

static const guint MAX_TEXT_BYTES = 64 * 1024;
static const guint MAX_IMAGE_BYTES = 8 * 1024 * 1024;
// The account service rejects larger clipboard text.
static const guint MAX_SYNC_TEXT_BYTES = 16384;

static const guint GROUP_POLL_SEC = 3;
static const guint ACCOUNT_TIMEOUT_SEC = 12;
static const guint64 CLIPBOARD_FUNCTION = 1;

#ifndef WETYPE_ACCOUNT_TOOL
#define WETYPE_ACCOUNT_TOOL "wetypex-account"
#endif

static std::string short_digest(const std::string &digest) { return digest.substr(0, 12); }

struct SyncState {
  guint64 group_id = 0;
  guint64 remote_version = 0;
  std::string local_kind;
  std::string local_digest;
  bool clipboard_enabled = false;
  bool baseline_pending = true;
};

struct LocalClip {
  bool known = false;
  std::string kind; // "text", "image" or "empty"
  std::string data;
  std::string digest;
};

struct Daemon {
  // Run mode
  bool once = false; // --once: one sync round, then exit
  int exit_code = 0; // process exit status, set by the paths that quit the loop

  // Main loop and bus resources (created in main, released on exit)
  GMainLoop *loop = nullptr;
  GDBusConnection *conn = nullptr;
  gulong closed_handler = 0; // "closed" signal on conn
  guint subscription = 0;    // match rule for the Changed signal
  guint name_watch = 0;      // watch on the extension's bus name
  guint timer = 0;           // periodic sync_tick source, 0 once removed

  // Extension link
  std::string ext_owner;      // unique name of the current owner, empty when absent
  bool ext_ready = false;     // owner verified and Subscribe succeeded
  bool attach_logged = false; // the current attach failure was already reported
  guint64 epoch = 0;          // bumped whenever the extension appears, vanishes or drops our subscription

  // Configuration
  std::string state_dir;
  std::string account_tool;

  // Sync state
  SyncState sync;            // persisted cursors and baseline (sync-runtime.json)
  LocalClip local;           // what the extension last reported as the local clipboard
  std::string failed_digest; // last upload the service refused, to log it once
};

static std::string sha256_hex(const std::string &data) {
  g_autofree gchar *hex =
      g_compute_checksum_for_data(G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(data.data()), data.size());
  return hex ? hex : "";
}

static bool write_private(const std::string &path, const std::string &data) {
  g_autoptr(GError) err = nullptr;
  if (g_file_set_contents_full(path.c_str(), data.data(), static_cast<gssize>(data.size()),
                               G_FILE_SET_CONTENTS_CONSISTENT, 0600, &err))
    return true;
  g_warning("Cannot write %s: %s", path.c_str(), err->message);
  return false;
}

static std::string state_directory() {
  const char *override = g_getenv("WETYPE_STATE_DIR");
  if (override && *override)
    return override;
  return std::string(g_get_user_data_dir()) + "/wetypex/state";
}

static bool prepare_state_dir(std::string *dir) {
  *dir = state_directory();
  if (g_mkdir_with_parents(dir->c_str(), 0700) != 0) {
    g_warning("Cannot create %s: %s", dir->c_str(), g_strerror(errno));
    return false;
  }
  chmod(dir->c_str(), 0700);
  return true;
}

// Only one daemon may own the cursors, or two polls would upload the same copy
// twice and overwrite each other's runtime file.
static int acquire_lock(const std::string &dir) {
  const std::string path = dir + "/syncd.lock";
  const int fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0) {
    g_warning("Cannot open %s: %s", path.c_str(), g_strerror(errno));
    return -1;
  }
  fchmod(fd, 0600);
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    g_warning("%s", g_strerror(errno));
    return -2; // another instance is running
  }
  return fd;
}

static void load_runtime(Daemon &daemon) {
  const std::string path = daemon.state_dir + "/sync-runtime.json";
  g_autofree gchar *contents = nullptr;
  if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr))
    return;
  auto json = wire::parse(contents);
  if (!json || !json_object_is_type(json.get(), json_type_object))
    return;
  daemon.sync.group_id = static_cast<guint64>(wire::number(json.get(), "group_id", 0));
  daemon.sync.remote_version = static_cast<guint64>(wire::number(json.get(), "remote_version", 0));
  daemon.sync.local_kind = wire::str(json.get(), "local_kind");
  daemon.sync.local_digest = wire::str(json.get(), "local_digest");
  daemon.sync.clipboard_enabled = wire::number(json.get(), "clipboard_enabled", 0) != 0;
  daemon.sync.baseline_pending =
      wire::number(json.get(), "clipboard_baseline_pending", daemon.sync.clipboard_enabled ? 0 : 1) != 0;
}

static void save_runtime(const Daemon &daemon) {
  auto json = wire::object();
  wire::put(json.get(), "group_id", static_cast<int64_t>(daemon.sync.group_id));
  wire::put(json.get(), "remote_version", static_cast<int64_t>(daemon.sync.remote_version));
  wire::put(json.get(), "local_kind", daemon.sync.local_kind);
  wire::put(json.get(), "local_digest", daemon.sync.local_digest);
  wire::boolean(json.get(), "clipboard_enabled", daemon.sync.clipboard_enabled);
  wire::boolean(json.get(), "clipboard_baseline_pending", daemon.sync.baseline_pending);
  write_private(daemon.state_dir + "/sync-runtime.json", wire::dump(json.get()) + "\n");
}

struct AccountRun {
  GMainLoop *loop = nullptr;
  GSubprocess *process = nullptr;
  std::string output;
  bool succeeded = false;
  bool timed_out = false;
};

static gboolean abort_account(gpointer data) {
  auto *run = static_cast<AccountRun *>(data);
  run->timed_out = true;
  g_subprocess_force_exit(run->process);
  return G_SOURCE_REMOVE;
}

static void account_finished(GObject *source, GAsyncResult *result, gpointer data) {
  auto *run = static_cast<AccountRun *>(data);
  g_autoptr(GError) err = nullptr;
  g_autoptr(GBytes) out = nullptr;
  g_autoptr(GBytes) errbytes = nullptr;
  g_subprocess_communicate_finish(G_SUBPROCESS(source), result, &out, &errbytes, &err);
  if (out) {
    gsize size = 0;
    const auto *payload = static_cast<const char *>(g_bytes_get_data(out, &size));
    run->output.assign(payload, size);
  }
  run->succeeded = !err && g_subprocess_get_successful(run->process);
  g_main_loop_quit(run->loop);
}

// Runs `wetypex-account` and returns its JSON reply, or an empty pointer when
// it failed, timed out, or printed something that is not a JSON object.
static wire::Json call_account(const Daemon &daemon, const std::vector<std::string> &arguments) {
  std::vector<const gchar *> argv;
  argv.reserve(arguments.size() + 2);
  argv.push_back(daemon.account_tool.c_str());
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);

  AccountRun run;
  std::string output;
  bool succeeded = false;
  {
    // A private context keeps the clipboard watcher out of the nested loop:
    // neither the poll timer nor a Changed signal may run mid-request.
    g_autoptr(GMainContext) context = g_main_context_new();
    g_main_context_push_thread_default(context);
    g_autoptr(GError) err = nullptr;
    run.process = g_subprocess_newv(
        argv.data(), static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
        &err);
    if (run.process) {
      run.loop = g_main_loop_new(context, FALSE);
      const guint watchdog = g_timeout_add_seconds(ACCOUNT_TIMEOUT_SEC, abort_account, &run);
      g_subprocess_communicate_async(run.process, nullptr, nullptr, account_finished, &run);
      g_main_loop_run(run.loop);
      if (!run.timed_out)
        g_source_remove(watchdog);
      succeeded = run.succeeded;
      output = std::move(run.output);
      g_main_loop_unref(run.loop);
      g_clear_object(&run.process);
    } else {
      g_warning("Cannot run %s: %s", daemon.account_tool.c_str(), err->message);
    }
    g_main_context_pop_thread_default(context);
  }
  if (!succeeded || output.empty())
    return {};
  auto json = wire::parse(output);
  if (!json || !json_object_is_type(json.get(), json_type_object))
    return {};
  return json;
}

// ---------------------------------------------------------------------------
// Extension connection management
// ---------------------------------------------------------------------------

static void remember_local(Daemon &daemon, const char *kind, const std::string &data) {
  daemon.local.known = true;
  daemon.local.kind = kind;
  daemon.local.data = data;
  daemon.local.digest = sha256_hex(data);
}

// The extension process is gone or going away: expected while it restarts.
static bool is_service_gone(const GError *err) {
  return g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
         g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
         g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
         g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED) ||
         g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CLOSED) || g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

// A copy that was replaced by a newer one (or whose TTL elapsed) before we
// fetched it; the newer Changed signal already carries the fresh serial. A
// WrongType race (content replaced by the other kind) is equally harmless, and
// so are failures caused by the extension restarting.
static bool is_remote_err(GError *err, const char *name) {
  if (!g_dbus_error_is_remote_error(err))
    return false;
  g_autofree gchar *remote = g_dbus_error_get_remote_error(err);
  return g_strcmp0(remote, name) == 0;
}

// Unavailable: the extension could not verify us this time (e.g. a credential
// lookup timed out). It keeps the subscription, so just try again later.
static bool is_ignorable(GError *err) {
  if (is_service_gone(err))
    return true;
  return is_remote_err(err, ERR_EXPIRED) || is_remote_err(err, ERR_WRONG_TYPE) || is_remote_err(err, ERR_UNAVAILABLE);
}

// The extension dropped our subscription without changing owner (it answers
// AccessDenied once we are no longer in its table). Subscribe is idempotent on
// its side, so go back to the unattached state: the next tick re-verifies the
// owner and subscribes again. Cursors are untouched, nothing is lost.
static void request_resubscribe(Daemon &daemon) {
  if (!daemon.ext_ready)
    return;
  g_warning("Subscription lost (access denied), resubscribing");
  daemon.ext_ready = false;
  daemon.attach_logged = false;
  daemon.epoch++;
}

// The owner must be the real gnome-shell, otherwise any process could claim the
// name and receive our clipboard.
static bool verify_owner(GDBusConnection *conn, const gchar *owner) {
  g_autoptr(GError) err = nullptr;
  g_autoptr(GVariant) c = g_dbus_connection_call_sync(
      conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionCredentials",
      g_variant_new("(s)", owner), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &err);
  if (!c) {
    g_debug("Failed to get credentials: %s", err->message);
    return false;
  }
  g_autoptr(GVariant) dict = g_variant_get_child_value(c, 0);
  guint32 pid = 0;
  if (!g_variant_lookup(dict, "ProcessID", "u", &pid))
    return false;

  g_autofree gchar *link = g_strdup_printf("/proc/%u/exe", pid);
  g_autofree gchar *exe = g_file_read_link(link, nullptr);
  if (!exe)
    return false;
  // A package upgrade leaves " (deleted)" on the path of the running binary.
  std::string path = exe;
  static const std::string deleted_suffix = " (deleted)";
  if (path.size() > deleted_suffix.size() &&
      path.compare(path.size() - deleted_suffix.size(), deleted_suffix.size(), deleted_suffix) == 0)
    path.resize(path.size() - deleted_suffix.size());
  return path == SHELL_EXE;
}

// Verify the current owner and subscribe. Safe to call repeatedly: it is a
// no-op once attached, and a failure (for instance the name is already owned
// but the object is not exported yet) is simply retried on the next tick.
static bool try_attach(Daemon &daemon) {
  if (daemon.ext_ready)
    return true;
  if (daemon.ext_owner.empty())
    return false;

  if (!verify_owner(daemon.conn, daemon.ext_owner.c_str())) {
    if (!daemon.attach_logged) {
      daemon.attach_logged = true;
      g_warning("Owner %s of %s is not %s, refusing to subscribe", daemon.ext_owner.c_str(), NAME, SHELL_EXE);
    }
    return false;
  }

  g_autoptr(GError) err = nullptr;
  g_autoptr(GVariant) reply =
      g_dbus_connection_call_sync(daemon.conn, daemon.ext_owner.c_str(), PATH, IFACE, "Subscribe", nullptr, nullptr,
                                  G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err);
  if (!reply) {
    if (!daemon.attach_logged) {
      daemon.attach_logged = true;
      g_warning("Subscribe failed, will retry: %s", err->message);
    } else {
      g_debug("Subscribe still failing: %s", err->message);
    }
    return false;
  }

  // The new instance has its own serials and knows nothing of what was copied
  // before. Start from an empty local view: the next sync round records it
  // without uploading, and later copies are observed as new events.
  remember_local(daemon, "empty", "");
  daemon.ext_ready = true;
  daemon.attach_logged = false;
  g_message("Connected to clipboard extension (%s)", daemon.ext_owner.c_str());
  return true;
}

static void on_name_appeared(GDBusConnection *, const gchar *, const gchar *owner, gpointer user_data) {
  auto *daemon = static_cast<Daemon *>(user_data);
  daemon->epoch++;
  daemon->ext_owner = owner ? owner : "";
  daemon->ext_ready = false; // a new owner has an empty subscriber table
  daemon->attach_logged = false;
  try_attach(*daemon);
}

static void on_name_vanished(GDBusConnection *, const gchar *, gpointer user_data) {
  auto *daemon = static_cast<Daemon *>(user_data);
  daemon->epoch++;
  if (daemon->ext_ready)
    g_warning("Clipboard extension went away, waiting for it to return");
  daemon->ext_owner.clear();
  daemon->ext_ready = false;
  daemon->attach_logged = false;
  remember_local(*daemon, "empty", "");
}

static void on_connection_closed(GDBusConnection *, gboolean, GError *error, gpointer user_data) {
  auto *daemon = static_cast<Daemon *>(user_data);
  g_warning("Session bus connection closed: %s", error ? error->message : "unknown reason");
  daemon->exit_code = 1; // non-zero so a supervisor restarts us on a new bus
  if (daemon->loop)
    g_main_loop_quit(daemon->loop);
}

// ---------------------------------------------------------------------------
// Clipboard fetch / set
// ---------------------------------------------------------------------------

struct Fetch {
  Daemon *daemon;
  std::string mime;
  guint64 epoch; // extension instance the request was sent to
};

// A remote copy we wrote is now the local one. The extension suppresses the
// echo of its own writes, so both views have to be updated here or the next
// poll would upload the same content back.
static void adopt_local(Daemon &daemon, const char *kind, const std::string &data) {
  remember_local(daemon, kind, data);
  daemon.sync.local_kind = kind;
  daemon.sync.local_digest = daemon.local.digest;
  g_debug("clipboard write: %s %zu bytes (sha256 %s)", kind, data.size(), short_digest(daemon.local.digest).c_str());
}

static void on_text_fetched(GObject *source, GAsyncResult *result, gpointer data) {
  const std::unique_ptr<Fetch> fetch(static_cast<Fetch *>(data));
  g_autoptr(GError) err = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &err);
  // The extension restarted while the request was in flight: stale answer.
  if (fetch->epoch != fetch->daemon->epoch || !fetch->daemon->ext_ready)
    return;
  if (!reply) {
    if (is_remote_err(err, ERR_ACCESS_DENIED))
      request_resubscribe(*fetch->daemon);
    else if (!is_ignorable(err))
      g_warning("GetText failed: %s", err->message);
    return;
  }
  const gchar *text = nullptr;
  g_variant_get(reply, "(&s)", &text);
  if (!text)
    return;
  const std::string value(text);
  if (value.size() > MAX_TEXT_BYTES) {
    g_warning("Ignoring text of %zu bytes, max %u", value.size(), MAX_TEXT_BYTES);
    return;
  }
  remember_local(*fetch->daemon, "text", value);
  g_debug("clipboard read: text %zu bytes (sha256 %s)", value.size(),
          short_digest(fetch->daemon->local.digest).c_str());
}

static void on_image_fetched(GObject *source, GAsyncResult *result, gpointer data) {
  const std::unique_ptr<Fetch> fetch(static_cast<Fetch *>(data));
  g_autoptr(GError) err = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &err);
  if (fetch->epoch != fetch->daemon->epoch || !fetch->daemon->ext_ready)
    return;
  if (!reply) {
    if (is_remote_err(err, ERR_ACCESS_DENIED))
      request_resubscribe(*fetch->daemon);
    else if (!is_ignorable(err))
      g_warning("GetImage failed: %s", err->message);
    return;
  }
  // g_variant_get_fixed_array() returns a pointer into the variant, so the
  // copy has to be taken while `child` is alive.
  g_autoptr(GVariant) child = g_variant_get_child_value(reply, 0);
  gsize len = 0;
  const auto *bytes = static_cast<const char *>(g_variant_get_fixed_array(child, &len, 1));
  if (!bytes || len == 0) {
    g_warning("GetImage returned no data");
    return;
  }
  if (len > MAX_IMAGE_BYTES) {
    g_warning("Ignoring image of %zu bytes (%s), max %u", len, fetch->mime.c_str(), MAX_IMAGE_BYTES);
    return;
  }
  remember_local(*fetch->daemon, "image", std::string(bytes, len));
  g_debug("clipboard read: image %zu bytes (%s, sha256 %s)", len, fetch->mime.c_str(),
          short_digest(fetch->daemon->local.digest).c_str());
}

// The signal only carries the serial, length and mime type;
// the content must be fetched explicitly
static void on_changed(GDBusConnection *conn, const gchar *, const gchar *, const gchar *, const gchar *,
                       GVariant *params, gpointer user_data) {
  auto *daemon = static_cast<Daemon *>(user_data);
  // A late signal from an instance we are no longer attached to.
  if (!daemon->ext_ready || daemon->ext_owner.empty())
    return;
  guint32 serial = 0, len = 0;
  const gchar *mime = nullptr;
  g_variant_get(params, "(uu&s)", &serial, &len, &mime);
  if (!mime)
    return;
  g_debug("clipboard changed: serial=%u mime=%s length=%u", serial, mime, len);
  // Talk to the verified unique name, so a replacement owner can never answer
  // a request meant for the previous instance.
  const gchar *dest = daemon->ext_owner.c_str();
  // Never fetch what would be rejected on arrival anyway.
  if (g_str_has_prefix(mime, "image/")) {
    if (len > MAX_IMAGE_BYTES) {
      g_debug("ignoring image: %u bytes, max %u", len, MAX_IMAGE_BYTES);
      return;
    }
    g_dbus_connection_call(conn, dest, PATH, IFACE, "GetImage", g_variant_new("(u)", serial), G_VARIANT_TYPE("(ay)"),
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, on_image_fetched,
                           new Fetch{daemon, mime, daemon->epoch});
    return;
  }
  if (len > MAX_TEXT_BYTES) {
    g_debug("ignoring text: %u bytes, max %u", len, MAX_TEXT_BYTES);
    return;
  }
  g_dbus_connection_call(conn, dest, PATH, IFACE, "GetText", g_variant_new("(u)", serial), G_VARIANT_TYPE("(s)"),
                         G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, on_text_fetched,
                         new Fetch{daemon, mime, daemon->epoch});
}

// The local copy a remote write must not replace when it completes.
struct LocalMark {
  std::string kind;
  std::string digest;
};

static bool local_unchanged(const Daemon &daemon, const LocalMark &expected) {
  return !daemon.local.known || (daemon.local.kind == expected.kind && daemon.local.digest == expected.digest);
}

// Writing through the extension also suppresses the echo of the copy, so the
// digest recorded by the caller stays authoritative.
static bool set_remote_text(Daemon &daemon, const std::string &text, const LocalMark &expected) {
  if (!daemon.ext_ready) {
    g_debug("skipping remote text: extension not connected");
    return false;
  }
  if (!local_unchanged(daemon, expected)) {
    g_debug("skipping remote text: the local clipboard changed meanwhile");
    return false;
  }
  g_debug("calling SetText: %zu bytes", text.size());
  g_autoptr(GError) err = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_sync(daemon.conn, daemon.ext_owner.c_str(), PATH, IFACE, "SetText",
                                                          g_variant_new("(s)", text.c_str()), nullptr,
                                                          G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err);
  if (!reply) {
    // The cursor does not advance, so the message is retried after reconnect.
    if (is_remote_err(err, ERR_ACCESS_DENIED))
      request_resubscribe(daemon);
    else if (is_ignorable(err))
      g_debug("SetText failed, will retry: %s", err->message);
    else
      g_warning("SetText failed: %s", err->message);
    return false;
  }
  return true;
}

static bool set_remote_image(Daemon &daemon, const std::string &image, const LocalMark &expected) {
  if (!daemon.ext_ready) {
    g_debug("skipping remote image: extension not connected");
    return false;
  }
  if (!local_unchanged(daemon, expected)) {
    g_debug("skipping remote image: the local clipboard changed meanwhile");
    return false;
  }
  g_debug("calling SetImage: %zu bytes", image.size());
  // SetImage sniffs the format, so the bytes the peer sent go through as-is.
  GVariant *payload = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, image.data(), image.size(), 1);
  g_autoptr(GVariant) reply = nullptr;
  g_autoptr(GError) err = nullptr;
  reply = g_dbus_connection_call_sync(daemon.conn, daemon.ext_owner.c_str(), PATH, IFACE, "SetImage",
                                      g_variant_new("(@ay)", payload), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                                      &err);
  if (!reply) {
    if (is_remote_err(err, ERR_ACCESS_DENIED))
      request_resubscribe(daemon);
    else if (is_ignorable(err))
      g_debug("SetImage failed, will retry: %s", err->message);
    else
      g_warning("SetImage failed: %s", err->message);
    return false;
  }
  return true;
}

static bool decode_base64(const std::string &input, std::string *output) {
  if (input.size() % 4 != 0)
    return false;
  for (const char c : input)
    if (!g_ascii_isalnum(c) && c != '+' && c != '/' && c != '=')
      return false;
  gsize size = 0;
  g_autofree guchar *data = g_base64_decode(input.c_str(), &size);
  if (!data || size == 0)
    return false;
  output->assign(reinterpret_cast<const char *>(data), size);
  return true;
}

static bool is_image_message(int64_t type) { return type == 5 || type == 6 || type == 100; }

// One exchange with the account service. Returns false when the group cannot be
// queried at all, so the caller backs off before retrying.
static bool sync_once(Daemon &daemon) {
  auto group = call_account(daemon, {"group-info", std::to_string(daemon.sync.remote_version)});
  if (!group)
    return false;
  if (wire::number(group.get(), "ok", 0) == 0)
    return false;
  write_private(daemon.state_dir + "/sync-state.json", wire::dump(group.get()) + "\n");

  const auto group_id = static_cast<guint64>(wire::number(group.get(), "group_id", 0));
  const auto functions = static_cast<guint64>(wire::number(group.get(), "func_switch", 0));
  const bool clipboard_enabled = group_id != 0 && (functions & CLIPBOARD_FUNCTION) != 0;
  const bool same_group = group_id == daemon.sync.group_id;
  const bool was_clipboard_enabled = daemon.sync.clipboard_enabled && same_group;

  // Clipboard versions are scoped to a device group: on first start,
  // re-pairing, or disabled sync, establish a baseline and never upload what
  // predates that transition.
  if (!same_group) {
    daemon.sync.group_id = group_id;
    daemon.sync.remote_version = 0;
    daemon.sync.baseline_pending = true;
  }
  if (!clipboard_enabled || !was_clipboard_enabled) {
    if (!daemon.sync.baseline_pending)
      g_debug("clipboard sync (re)enabled: building a new baseline");
    daemon.sync.baseline_pending = true;
  }

  if (clipboard_enabled && daemon.sync.baseline_pending && daemon.local.known) {
    daemon.sync.local_kind = daemon.local.kind;
    daemon.sync.local_digest = daemon.local.digest;
    daemon.sync.baseline_pending = false;
    g_debug("baseline: %s %zu bytes, cursor %llu", daemon.sync.local_kind.c_str(), daemon.local.data.size(),
            static_cast<unsigned long long>(daemon.sync.remote_version));
  }

  if (clipboard_enabled && daemon.local.known && !daemon.sync.baseline_pending) {
    const LocalMark expected = {daemon.local.kind, daemon.local.digest};
    const auto version = static_cast<guint64>(wire::number(group.get(), "clipboard_version", 0));
    const std::string remote = wire::str(group.get(), "clipboard");
    const int64_t remote_type = wire::number(group.get(), "clipboard_type", 0);
    const std::string remote_binary = wire::str(group.get(), "clipboard_binary");
    const auto from_uin = static_cast<guint64>(wire::number(group.get(), "clipboard_from_uin", 0));
    const auto self_uin = static_cast<guint64>(wire::number(group.get(), "self_uin", 0));
    int64_t expire_at = wire::number(group.get(), "clipboard_expire_at", 0);
    if (expire_at > 0 && expire_at < 100000000000LL)
      expire_at *= 1000;
    const bool expired = expire_at > 0 && expire_at <= g_get_real_time() / 1000;

    const bool local_changed = daemon.sync.local_digest.empty() || expected.kind != daemon.sync.local_kind ||
                               expected.digest != daemon.sync.local_digest;
    if (local_changed && expected.kind == "empty") {
      // The service has no empty-message type. Remember the state so the same
      // content copied again is still observed as a new event, upload nothing.
      daemon.sync.local_kind = expected.kind;
      daemon.sync.local_digest = expected.digest;
      daemon.failed_digest.clear();
      g_debug("local clipboard is empty: nothing to upload");
    } else if (local_changed) {
      const size_t size = daemon.local.data.size();
      const auto previous_version = daemon.sync.remote_version;
      g_debug("uploading local %s: %zu bytes, cursor %llu", expected.kind.c_str(), size,
              static_cast<unsigned long long>(previous_version));
      wire::Json sent;
      if (expected.kind == "image") {
        const std::string path = daemon.state_dir + "/clipboard-outbox.png";
        if (write_private(path, daemon.local.data))
          sent = call_account(daemon, {"send-image", std::to_string(daemon.sync.group_id), path});
      } else {
        sent = call_account(daemon, {"send-clipboard", std::to_string(daemon.sync.group_id), daemon.local.data});
      }
      if (sent && wire::number(sent.get(), "ok", 0) != 0) {
        // SendClipboardMsgRsp.version is the same monotonic cursor the native
        // clients advance after a local upload.
        daemon.sync.remote_version =
            std::max(daemon.sync.remote_version, static_cast<guint64>(wire::number(sent.get(), "version", 0)));
        daemon.sync.local_kind = expected.kind;
        daemon.sync.local_digest = expected.digest;
        daemon.failed_digest.clear();
        g_debug("clipboard upload: %s %zu bytes, version %llu -> %llu", expected.kind.c_str(), size,
                static_cast<unsigned long long>(previous_version),
                static_cast<unsigned long long>(daemon.sync.remote_version));
      } else if (daemon.failed_digest != expected.digest) {
        // The same copy is retried every poll; report it once, not every 3s.
        daemon.failed_digest = expected.digest;
        g_debug("clipboard upload failed: %s %zu bytes, cursor stays %llu", expected.kind.c_str(), size,
                static_cast<unsigned long long>(daemon.sync.remote_version));
      } else {
        g_debug("clipboard upload still failing: %s %zu bytes", expected.kind.c_str(), size);
      }
    } else if (version > daemon.sync.remote_version && expired) {
      g_debug("skipping expired remote message: version %llu", static_cast<unsigned long long>(version));
      daemon.sync.remote_version = version;
    } else if (version > daemon.sync.remote_version && from_uin && self_uin && from_uin == self_uin) {
      // A response can race the upload acknowledgement. Advance the cursor,
      // but never paste our own clipboard back over the user.
      g_debug("skipping own remote message: version %llu", static_cast<unsigned long long>(version));
      daemon.sync.remote_version = version;
    } else if (version > daemon.sync.remote_version && is_image_message(remote_type) && !remote_binary.empty()) {
      std::string image;
      const bool usable = decode_base64(remote_binary, &image) && image.size() <= MAX_IMAGE_BYTES;
      if (usable && !image.empty() && set_remote_image(daemon, image, expected)) {
        adopt_local(daemon, "image", image);
        daemon.sync.remote_version = version;
      } else if (!usable) {
        daemon.sync.remote_version = version;
      }
    } else if (version > daemon.sync.remote_version && !remote.empty()) {
      if (remote.size() <= MAX_SYNC_TEXT_BYTES && g_utf8_validate(remote.data(), remote.size(), nullptr) &&
          set_remote_text(daemon, remote, expected)) {
        adopt_local(daemon, "text", remote);
        daemon.sync.remote_version = version;
      }
    } else if (version > daemon.sync.remote_version) {
      // Do not retry malformed or unsupported messages forever.
      daemon.sync.remote_version = version;
    }
  }

  daemon.sync.clipboard_enabled = clipboard_enabled;
  save_runtime(daemon);
  return true;
}

static gboolean sync_tick(gpointer data) {
  auto *daemon = static_cast<Daemon *>(data);

  // Without the extension there is nothing to read from or write to, and the
  // cursors must not move: pause until it is back (this also retries a
  // Subscribe that failed because the object was not exported yet).
  if (!daemon->ext_ready && !try_attach(*daemon)) {
    if (daemon->once) {
      g_warning("Clipboard extension is not available");
      daemon->exit_code = 1;
      daemon->timer = 0; // the source is removed by returning below
      g_main_loop_quit(daemon->loop);
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  }

  const bool ok = sync_once(*daemon);
  if (daemon->once) {
    daemon->timer = 0;
    g_main_loop_quit(daemon->loop);
    return G_SOURCE_REMOVE;
  }
  if (!ok) {
    g_warning("Account sync query failed");
  }
  return G_SOURCE_CONTINUE;
}

static gboolean quit_loop(gpointer data) {
  g_main_loop_quit(static_cast<GMainLoop *>(data));
  return G_SOURCE_REMOVE;
}

static std::string tool_path(const char *variable, const char *fallback) {
  const char *override = g_getenv(variable);
  if (override && *override)
    return override;
  g_autofree gchar *found = g_find_program_in_path(fallback);
  return found ? found : fallback;
}

static void print_usage(const char *prog) {
  g_print("Usage:\n"
          "  %s           synchronize the clipboard while the extension is running\n"
          "  %s --once    run one synchronization round and exit\n"
          "  %s -h        show this help\n"
          "\n",
          prog, prog, prog);
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  signal(SIGPIPE, SIG_IGN);
  g_set_prgname(argv[0]);

  Daemon daemon;
  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "--once") == 0) {
      daemon.once = true;
    } else if (g_strcmp0(argv[i], "-h") == 0 || g_strcmp0(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      g_warning("Unknown argument: %s", argv[i]);
      print_usage(argv[0]);
      return 2;
    }
  }

  if (!prepare_state_dir(&daemon.state_dir))
    return 1;
  daemon.account_tool = tool_path("WETYPE_ACCOUNT_TOOL", WETYPE_ACCOUNT_TOOL);

  const int lock = acquire_lock(daemon.state_dir);
  if (lock == -2)
    return 0; // another daemon owns the cursors
  if (lock < 0)
    return 1;
  load_runtime(daemon);
  // The extension only reports copies it observes. Until the user copies
  // something here, seed the local view with an empty selection: otherwise no
  // baseline can be established and a copy made on another device would be
  // ignored until the first local copy.
  remember_local(daemon, "empty", "");

  g_autoptr(GError) err = nullptr;
  daemon.conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
  if (!daemon.conn) {
    g_warning("Failed to connect to session bus: %s", err->message);
    return 1;
  }
  // Leave the exit path to us so cleanup runs and the exit code is explicit.
  g_dbus_connection_set_exit_on_close(daemon.conn, FALSE);

  daemon.loop = g_main_loop_new(nullptr, FALSE);
  daemon.closed_handler = g_signal_connect(daemon.conn, "closed", G_CALLBACK(on_connection_closed), &daemon);

  // Match the signal before becoming an authorized subscriber: the service
  // only unicasts Changed to subscribers, so nothing is emitted to us until
  // Subscribe succeeds, and no copy slips through the gap in between.
  // The sender is the well-known name, which GDBus keeps resolving across
  // owner changes, so this rule survives extension restarts.
  daemon.subscription = g_dbus_connection_signal_subscribe(daemon.conn, NAME, IFACE, "Changed", PATH, nullptr,
                                                           G_DBUS_SIGNAL_FLAGS_NONE, on_changed, &daemon, nullptr);
  if (daemon.subscription == 0) {
    g_warning("Cannot subscribe to the Changed signal");
    g_signal_handler_disconnect(daemon.conn, daemon.closed_handler);
    g_main_loop_unref(daemon.loop);
    return 1;
  }

  // Verification and Subscribe happen in the name callbacks, every time the
  // extension (re)appears. Starting before the extension is fine: we wait.
  daemon.name_watch = g_bus_watch_name_on_connection(daemon.conn, NAME, G_BUS_NAME_WATCHER_FLAGS_NONE, on_name_appeared,
                                                     on_name_vanished, &daemon, nullptr);

  // Quit through the main loop so the cleanup below still runs.
  g_unix_signal_add(SIGINT, quit_loop, daemon.loop);
  g_unix_signal_add(SIGTERM, quit_loop, daemon.loop);
  daemon.timer = g_timeout_add_seconds(GROUP_POLL_SEC, sync_tick, &daemon);

  g_main_loop_run(daemon.loop);

  if (daemon.timer)
    g_source_remove(daemon.timer);
  g_bus_unwatch_name(daemon.name_watch);
  g_dbus_connection_signal_unsubscribe(daemon.conn, daemon.subscription);
  g_signal_handler_disconnect(daemon.conn, daemon.closed_handler);
  g_main_loop_unref(daemon.loop);
  g_clear_object(&daemon.conn);
  close(lock);
  return daemon.exit_code;
}
