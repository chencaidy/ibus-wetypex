#include "engine_client.hpp"

#include "../common/json.hpp"

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixsocketaddress.h>
#include <glib.h>
#include <json-c/json.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>

#ifndef WETYPE_BACKEND
#define WETYPE_BACKEND "fcitx5-wetypex-backend"
#endif

namespace ibus_settings {
namespace {

constexpr std::size_t kMaxResponse = 1 << 20;

std::string control_socket_path() {
  gchar *path = g_build_filename(g_get_user_data_dir(), "fcitx5-wetypex",
                                 "state", "control.sock", nullptr);
  std::string result(path);
  g_free(path);
  return result;
}

std::string socket_request(const std::string &request, std::string *error) {
  GSocketClient *client = g_socket_client_new();
  g_socket_client_set_timeout(client, 2);
  GSocketAddress *address =
      g_unix_socket_address_new(control_socket_path().c_str());
  GError *err = nullptr;
  GSocketConnection *connection = g_socket_client_connect(
      client, G_SOCKET_CONNECTABLE(address), nullptr, &err);
  g_object_unref(address);
  if (!connection) {
    if (error)
      *error = err ? err->message : "input core is not running";
    g_clear_error(&err);
    g_object_unref(client);
    return {};
  }

  std::string line = request;
  line.push_back('\n');
  gsize written = 0;
  GOutputStream *output =
      g_io_stream_get_output_stream(G_IO_STREAM(connection));
  const bool sent = g_output_stream_write_all(output, line.data(), line.size(),
                                              &written, nullptr, &err) &&
                    written == line.size();
  std::string response;
  if (sent) {
    GInputStream *input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    char buffer[4096];
    while (response.find('\n') == std::string::npos &&
           response.size() < kMaxResponse) {
      const gssize count =
          g_input_stream_read(input, buffer, sizeof(buffer), nullptr, &err);
      if (count <= 0)
        break;
      response.append(buffer, static_cast<std::size_t>(count));
    }
  }
  if (!sent || response.empty()) {
    if (error)
      *error = err ? err->message : "input core did not respond";
  } else {
    const auto newline = response.find('\n');
    if (newline != std::string::npos)
      response.erase(newline);
    if (error)
      error->clear();
  }
  g_clear_error(&err);
  g_io_stream_close(G_IO_STREAM(connection), nullptr, nullptr);
  g_object_unref(connection);
  g_object_unref(client);
  return response;
}

bool read_line_fd(int fd, std::string &pending, std::string &line,
                  int timeout_ms) {
  const gint64 deadline =
      g_get_monotonic_time() + static_cast<gint64>(timeout_ms) * 1000;
  for (;;) {
    const auto newline = pending.find('\n');
    if (newline != std::string::npos) {
      line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      return true;
    }
    const gint64 remaining = deadline - g_get_monotonic_time();
    if (remaining <= 0)
      return false;
    pollfd descriptor{fd, POLLIN, 0};
    const int result = poll(&descriptor, 1, static_cast<int>(remaining / 1000));
    if (result < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (result == 0)
      return false;
    char buffer[4096];
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count <= 0)
      return false;
    pending.append(buffer, static_cast<std::size_t>(count));
    if (pending.size() > kMaxResponse)
      return false;
  }
}

std::string backend_request(const std::string &request, std::string *error) {
  const char *configured = g_getenv("WETYPE_BACKEND_LAUNCHER");
  const char *tool = configured && *configured ? configured : WETYPE_BACKEND;
  GSubprocessLauncher *launcher =
      g_subprocess_launcher_new(static_cast<GSubprocessFlags>(
          G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE));
  GError *err = nullptr;
  GSubprocess *process =
      g_subprocess_launcher_spawn(launcher, &err, tool, nullptr);
  g_object_unref(launcher);
  if (!process) {
    if (error)
      *error = err ? err->message : "unable to start the input core";
    g_clear_error(&err);
    return {};
  }

  GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(process);
  GOutputStream *stdin_stream = g_subprocess_get_stdin_pipe(process);
  int fd = -1;
  if (stdout_stream && G_IS_UNIX_INPUT_STREAM(stdout_stream))
    fd = g_unix_input_stream_get_fd(G_UNIX_INPUT_STREAM(stdout_stream));
  if (fd < 0 || !stdin_stream) {
    if (error)
      *error = err ? err->message : "input core pipes are unavailable";
    g_clear_error(&err);
    g_subprocess_force_exit(process);
    g_subprocess_wait(process, nullptr, nullptr);
    g_object_unref(process);
    return {};
  }

  std::string pending;
  std::string line;
  bool ready = false;
  const gint64 deadline = g_get_monotonic_time() + 20 * G_TIME_SPAN_SECOND;
  while (g_get_monotonic_time() < deadline && !ready) {
    if (!read_line_fd(fd, pending, line, 1000))
      continue;
    auto event = wire::parse(line);
    ready = event && wire::str(event.get(), "event") == "ready";
  }
  if (!ready) {
    if (error)
      *error = "输入核心启动超时";
    g_subprocess_force_exit(process);
    g_subprocess_wait(process, nullptr, nullptr);
    g_object_unref(process);
    return {};
  }

  std::string request_line = request;
  request_line.push_back('\n');
  gsize written = 0;
  bool sent =
      g_output_stream_write_all(stdin_stream, request_line.data(),
                                request_line.size(), &written, nullptr, &err) &&
      written == request_line.size();
  std::string response;
  if (sent) {
    while (g_get_monotonic_time() < deadline) {
      if (read_line_fd(fd, pending, line, 1000)) {
        response = line;
        break;
      }
    }
  }
  if (!sent || response.empty()) {
    if (error)
      *error = err ? err->message : "输入核心没有响应";
  } else if (error) {
    error->clear();
  }
  g_clear_error(&err);
  const char quit[] = "{\"op\":\"quit\",\"session\":1,\"seq\":0}\n";
  gsize ignored = 0;
  g_output_stream_write_all(stdin_stream, quit, sizeof(quit) - 1, &ignored,
                            nullptr, nullptr);
  g_subprocess_force_exit(process);
  g_subprocess_wait(process, nullptr, nullptr);
  g_object_unref(process);
  return response;
}

std::string run_request(const std::string &request, std::string *error) {
  std::string reply = socket_request(request, error);
  if (!reply.empty())
    return reply;
  return backend_request(request, error);
}

struct Job {
  std::string request;
  EngineCallback done;
  EngineReply reply;
};

gboolean deliver(gpointer data) {
  auto *job = static_cast<Job *>(data);
  if (job->done)
    job->done(std::move(job->reply));
  return G_SOURCE_REMOVE;
}

void free_job(gpointer data) { delete static_cast<Job *>(data); }

void start_request(std::string request, EngineCallback done) {
  auto *job = new Job{std::move(request), std::move(done), {}};
  std::thread([job] {
    std::string error;
    const std::string reply = run_request(job->request, &error);
    job->reply.ok = !reply.empty();
    job->reply.json = reply;
    job->reply.error = error;
    g_idle_add_full(G_PRIORITY_DEFAULT, deliver, job, free_job);
  }).detach();
}

std::string make_request(const char *operation, const std::string &id,
                         const std::string &key, const std::string &words) {
  auto request = wire::object();
  wire::put(request.get(), "session", int64_t(1));
  wire::put(request.get(), "seq", int64_t(g_get_real_time() / 1000));
  wire::put(request.get(), "epoch", int64_t(1));
  wire::put(request.get(), "op", std::string(operation));
  if (std::strcmp(operation, "hotword_set") == 0) {
    wire::put(request.get(), "id", id);
    wire::put(request.get(), "key", key);
    wire::put(request.get(), "words", words);
  }
  return wire::dump(request.get());
}

} // namespace

void request_hotword_list(EngineCallback done) {
  start_request(make_request("hotword_list", {}, {}, {}), std::move(done));
}

void request_hotword_set(const std::string &id, const std::string &key,
                         const std::string &words, EngineCallback done) {
  start_request(make_request("hotword_set", id, key, words), std::move(done));
}

} // namespace ibus_settings
