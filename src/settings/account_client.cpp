#include "account_client.hpp"

#include <gio/gio.h>
#include <glib.h>

#include <memory>

#ifndef WETYPE_ACCOUNT_TOOL
#define WETYPE_ACCOUNT_TOOL "wetypex-account"
#endif

namespace ibus_settings {
namespace {

struct State {
  AccountCallback done;
  bool finished = false;
};

void finish(GObject *source, GAsyncResult *result, gpointer user_data) {
  std::unique_ptr<State> state(static_cast<State *>(user_data));
  if (state->finished)
    return;
  state->finished = true;

  GError *error = nullptr;
  gchar *output = nullptr;
  gchar *errors = nullptr;
  const gboolean completed = g_subprocess_communicate_utf8_finish(
      G_SUBPROCESS(source), result, &output, &errors, &error);

  AccountReply reply;
  reply.ok = completed && g_subprocess_get_successful(G_SUBPROCESS(source));
  if (output)
    reply.output.assign(output);
  if (!reply.ok) {
    if (error)
      reply.error = error->message;
    else if (errors && *errors)
      reply.error = errors;
    else
      reply.error = "账户操作失败";
  }
  g_free(output);
  g_free(errors);
  g_clear_error(&error);
  if (state->done)
    state->done(std::move(reply));
}

} // namespace

void run_account(std::vector<std::string> arguments, AccountCallback done) {
  const char *configured = g_getenv("WETYPE_ACCOUNT_TOOL");
  const char *tool =
      configured && *configured ? configured : WETYPE_ACCOUNT_TOOL;

  std::vector<const char *> argv;
  argv.push_back(tool);
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);

  GSubprocessLauncher *launcher =
      g_subprocess_launcher_new(static_cast<GSubprocessFlags>(
          G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
  GError *error = nullptr;
  GSubprocess *process =
      g_subprocess_launcher_spawnv(launcher, argv.data(), &error);
  g_object_unref(launcher);
  if (!process) {
    AccountReply reply;
    reply.error = error ? error->message : "无法启动账户服务";
    g_clear_error(&error);
    if (done)
      done(std::move(reply));
    return;
  }

  auto state = std::make_unique<State>();
  state->done = std::move(done);
  g_subprocess_communicate_utf8_async(process, nullptr, nullptr, finish,
                                      state.release());
  g_object_unref(process);
}

} // namespace ibus_settings
