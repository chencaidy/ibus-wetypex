#include "../common/json.hpp"

#include <fcntl.h>
#include <glib-unix.h>
#include <ibus.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#define g_debug_func(fmt, ...) g_debug("%s: " fmt, __func__, ##__VA_ARGS__)

extern char **environ;

namespace {

struct Settings {
  std::string inputMode = "pinyin";
  std::string defaultLanguage = "chinese";
  int doublePinyinScheme = 0;
  int wubiSolution = 0;
  int pageSize = 5;
  bool shiftSwitch = true;
  bool standalone = false;
  std::string punctuationMode = "auto";
  bool slashPunctuation = true;
  bool vertical = false;
  bool smartInput = true;
  bool emojiRecommend = true;
  bool wechatEmoji = false;
  bool normalEmoji = true;
  bool kaomoji = true;
  bool largeEmoji = false;
  bool symbolEmoji = true;
  bool wubiPinyin = false;
  bool wubiUniqueCommit = false;
  bool wubiNextCommit = false;
  bool wubiWildcardComment = false;
  std::array<bool, 19> fuzzy{};
};

struct PunctuationEntry {
  char ascii;
  const char *normal;
  const char *english;
  uint8_t pairType;
};

static constexpr std::array<PunctuationEntry, 32> kPunctuation = {{
    {'!', "！", "!", 0}, {'"', "“”", "\"\"", 2}, {'#', "#", "#", 0},     {'$', "¥", "$", 0},     {'%', "%", "%", 0},
    {'&', "&", "&", 0},  {'\'', "‘’", "''", 2},  {'(', "（）", "()", 1}, {')', "）", ")", 0},    {'*', "*", "*", 0},
    {'+', "+", "+", 0},  {',', "，", ",", 0},    {'-', "-", "-", 0},     {'.', "。", ".", 0},    {'/', "/", "/", 0},
    {':', "：", ":", 0}, {';', "；", ";", 0},    {'<', "《》", "<>", 1}, {'=', "=", "=", 0},     {'>', "》", ">", 0},
    {'?', "？", "?", 0}, {'@', "@", "@", 0},     {'[', "【】", "[]", 1}, {'\\', "、", "\\", 0},  {']', "】", "]", 0},
    {'^', "……", "^", 0}, {'_', "——", "_", 0},    {'`', "·", "`", 0},     {'{', "「」", "{}", 1}, {'|', "|", "|", 0},
    {'}', "」", "}", 0}, {'~', "～", "~", 0},
}};

struct EngineState;

struct DeferredKey {
  guint keyval;
  guint keycode;
  guint modifiers;
};

typedef struct _WeTypeEngine {
  IBusEngine parent;
  EngineState *state;
} WeTypeEngine;

typedef struct _WeTypeEngineClass {
  IBusEngineClass parent;
} WeTypeEngineClass;

G_DEFINE_TYPE(WeTypeEngine, wetype_engine, IBUS_TYPE_ENGINE)

struct EngineState {
  WeTypeEngine *owner = nullptr;
  uint64_t session = 0;
  uint64_t epoch = 1;
  uint64_t sequence = 0;
  uint64_t applied = 0;
  uint64_t appliedUpdate = 0;
  uint64_t inputSequence = 0;
  uint64_t revision = 0;
  int readFd = -1;
  int writeFd = -1;
  pid_t backendPid = -1;
  guint readSource = 0;
  guint writeSource = 0;
  std::string inbound;
  std::string outbound;
  std::string preedit;
  std::string displayPreedit;
  std::string renderedPreedit;
  size_t cursor = 0;
  size_t editableBegin = 0;
  size_t displayCursor = 0;
  size_t renderedCursor = 0;
  std::vector<std::string> candidates;
  std::vector<bool> cloud;
  std::vector<DeferredKey> deferredKeys;
  IBusPropList *properties = nullptr;
  IBusProperty *modeProperty = nullptr;
  IBusProperty *restartProperty = nullptr;
  int candidatePage = 0;
  int candidateCursor = 0;
  int contentPurpose = IBUS_INPUT_PURPOSE_FREE_FORM;
  Settings settings;
  bool focused = false;
  bool enabled = false;
  bool active = false;
  bool english = false;
  bool shiftCandidate = false;
  bool shiftWasUsed = false;
  uint8_t pressedShifts = 0;
  int64_t shiftPressedAt = 0;
  bool doubleQuoteLeft = true;
  bool singleQuoteLeft = true;
  bool backendReady = false;
  bool backendFailed = false;
  bool selectionPending = false;
  bool renderedPreeditVisible = false;
  bool renderedPreeditInitialized = false;
  bool isCursorBehindAscii = false;
};

uint64_t nextSession = 0;
bool sharedEnglishMode = false;
bool sharedModeInitialized = false;
pid_t syncPid = -1;
guint syncWatch = 0;

bool jsonBool(json_object *object, const char *key, bool fallback) {
  auto *value = wire::get(object, key);
  if (!value)
    return fallback;
  if (json_object_is_type(value, json_type_boolean) || json_object_is_type(value, json_type_int))
    return json_object_get_boolean(value);
  return fallback;
}

int jsonInt(json_object *object, const char *key, int fallback) {
  auto *value = wire::get(object, key);
  return value && json_object_is_type(value, json_type_int) ? json_object_get_int(value) : fallback;
}

Settings readSettings() {
  Settings settings;
  const std::filesystem::path path = std::filesystem::path(g_get_user_config_dir()) / "ibus/wetypex/wetypex.json";
  std::ifstream file(path);
  if (!file)
    return settings;
  std::string contents((std::istreambuf_iterator<char>(file)), {});
  auto json = wire::parse(contents);
  if (!json || !json_object_is_type(json.get(), json_type_object)) {
    g_warning("Ignoring invalid WeTypeX settings at %s", path.c_str());
    return settings;
  }
  auto mode = wire::str(json.get(), "input_mode");
  if (mode.empty())
    mode = jsonInt(json.get(), "keyboard", 0) == 5 ? "wubi" : "pinyin";
  if (mode == "pinyin" || mode == "double_pinyin" || mode == "wubi")
    settings.inputMode = mode;
  settings.defaultLanguage = wire::str(json.get(), "default_language", "chinese");
  settings.doublePinyinScheme = std::clamp(jsonInt(json.get(), "double_pinyin_scheme", 0), 0, 6);
  settings.wubiSolution = std::clamp(jsonInt(json.get(), "wubi_solution", 0), 0, 2);
  settings.pageSize = std::clamp(jsonInt(json.get(), "page_size", 5), 3, 9);
  settings.shiftSwitch = jsonBool(json.get(), "shift_switch", true);
  settings.standalone = jsonBool(json.get(), "standalone", false);
  settings.punctuationMode = wire::str(json.get(), "punctuation_mode", "auto");
  settings.slashPunctuation = jsonBool(json.get(), "slash_punctuation", true);
  settings.vertical = jsonBool(json.get(), "vertical", false);
  settings.smartInput = jsonBool(json.get(), "smart_input", true);
  settings.emojiRecommend = jsonBool(json.get(), "emoji_recommend", true);
  settings.wechatEmoji = jsonBool(json.get(), "wechat_emoji", false);
  settings.normalEmoji = jsonBool(json.get(), "normal_emoji", true);
  settings.kaomoji = jsonBool(json.get(), "kaomoji", true);
  settings.largeEmoji = jsonBool(json.get(), "large_emoji", false);
  settings.symbolEmoji = jsonBool(json.get(), "symbol_emoji", true);
  settings.wubiPinyin = jsonBool(json.get(), "wubi_pinyin", false);
  settings.wubiUniqueCommit = jsonBool(json.get(), "wubi_unique_commit", false);
  settings.wubiNextCommit = jsonBool(json.get(), "wubi_next_commit", false);
  settings.wubiWildcardComment = jsonBool(json.get(), "wubi_wildcard_comment", false);
  constexpr std::array<const char *, 19> fuzzyKeys = {
      "fuzzy_nl",       "fuzzy_rl",       "fuzzy_hf",     "fuzzy_gk",     "fuzzy_an_ang",
      "fuzzy_ian_iang", "fuzzy_uan_uang", "fuzzy_c_ch",   "fuzzy_s_sh",   "fuzzy_z_zh",
      "fuzzy_hui_fei",  "fuzzy_en_eng",   "fuzzy_in_ing", "fuzzy_on_ong", "fuzzy_huang_wang",
      "fuzzy_un_ong",   "fuzzy_un_iong",  "fuzzy_an_ai",  "fuzzy_eng_ong"};
  for (size_t i = 0; i < fuzzyKeys.size(); ++i)
    settings.fuzzy[i] = jsonBool(json.get(), fuzzyKeys[i], false);
  return settings;
}

void clearUi(EngineState *state) {
  auto *engine = IBUS_ENGINE(state->owner);
  ibus_engine_update_preedit_text(engine, ibus_text_new_from_string(""), 0, FALSE);
  ibus_engine_update_lookup_table(engine, ibus_lookup_table_new(5, 0, FALSE, FALSE), FALSE);
  state->renderedPreedit.clear();
  state->renderedCursor = 0;
  state->renderedPreeditVisible = false;
  state->renderedPreeditInitialized = true;
}

gboolean onProcessKey(IBusEngine *engine, guint keyval, guint keycode, guint modifiers);

size_t previousUtf8(const std::string &text, size_t position) {
  position = std::min(position, text.size());
  if (!position)
    return 0;
  const char *at = text.c_str() + position;
  const char *previous = g_utf8_find_prev_char(text.c_str(), at);
  return previous ? static_cast<size_t>(previous - text.c_str()) : 0;
}

size_t nextUtf8(const std::string &text, size_t position) {
  position = std::min(position, text.size());
  if (position >= text.size())
    return text.size();
  const char *next = g_utf8_next_char(text.c_str() + position);
  return std::min(static_cast<size_t>(next - text.c_str()), text.size());
}

guint utf8Cursor(const std::string &text, size_t bytePosition) {
  bytePosition = std::min(bytePosition, text.size());
  return static_cast<guint>(g_utf8_pointer_to_offset(text.c_str(), text.c_str() + bytePosition));
}

void updateUi(EngineState *state) {
  auto *engine = IBUS_ENGINE(state->owner);
  const std::string &shown = state->displayPreedit.empty() ? state->preedit : state->displayPreedit;
  const size_t byteCursor = state->displayPreedit.empty() ? state->cursor : state->displayCursor;
  const bool preeditVisible = !shown.empty();
  const guint cursor = utf8Cursor(shown, byteCursor);
  if (!state->renderedPreeditInitialized || shown != state->renderedPreedit || cursor != state->renderedCursor ||
      preeditVisible != state->renderedPreeditVisible) {
    ibus_engine_update_preedit_text(engine, ibus_text_new_from_string(shown.c_str()), cursor, preeditVisible);
    state->renderedPreedit = shown;
    state->renderedCursor = cursor;
    state->renderedPreeditVisible = preeditVisible;
    state->renderedPreeditInitialized = true;
  }

  const bool pending = !state->backendFailed && (state->inputSequence > state->applied || state->selectionPending);
  const bool showCandidates = !state->preedit.empty() && !pending && !state->candidates.empty();
  if (pending && !state->preedit.empty())
    return;
  auto *table = ibus_lookup_table_new(
      static_cast<guint>(state->settings.pageSize),
      static_cast<guint>(std::max(0, state->candidateCursor % state->settings.pageSize)), TRUE, FALSE);
  ibus_lookup_table_set_orientation(table,
                                    state->settings.vertical ? IBUS_ORIENTATION_VERTICAL : IBUS_ORIENTATION_HORIZONTAL);
  for (size_t i = 0; i < state->candidates.size(); ++i) {
    std::string candidate =
        i < state->cloud.size() && state->cloud[i] ? "☁ " + state->candidates[i] : state->candidates[i];
    ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(candidate.c_str()));
    const std::string label = std::to_string(i % static_cast<size_t>(state->settings.pageSize) + 1);
    ibus_lookup_table_append_label(table, ibus_text_new_from_string(label.c_str()));
  }
  if (!state->candidates.empty()) {
    state->candidateCursor = std::clamp(state->candidateCursor, 0, static_cast<int>(state->candidates.size()) - 1);
    ibus_lookup_table_set_cursor_pos(table, static_cast<guint>(state->candidateCursor));
    state->candidatePage = state->candidateCursor / state->settings.pageSize;
  } else {
    state->candidateCursor = 0;
    state->candidatePage = 0;
  }
  ibus_engine_update_lookup_table(engine, table, showCandidates);
}

void closeBackend(EngineState *state, bool fromReadWatch = false);
void processResponse(EngineState *state, const std::string &line);

gboolean backendReadable(gint fd, GIOCondition condition, gpointer data) {
  auto *state = static_cast<EngineState *>(data);
  char buffer[8192];
  for (;;) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      state->inbound.append(buffer, static_cast<size_t>(count));
      if (state->inbound.size() > 1048576) {
        closeBackend(state, true);
        state->backendFailed = true;
        if (state->active)
          updateUi(state);
        return G_SOURCE_REMOVE;
      }
      continue;
    }
    if (count == 0) {
      closeBackend(state, true);
      state->backendFailed = true;
      if (state->active)
        updateUi(state);
      return G_SOURCE_REMOVE;
    }
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    g_warning("WeTypeX backend sockets read failed: %s", g_strerror(errno));
    closeBackend(state, true);
    state->backendFailed = true;
    if (state->active)
      updateUi(state);
    return G_SOURCE_REMOVE;
  }
  while (true) {
    const auto end = state->inbound.find('\n');
    if (end == std::string::npos)
      break;
    auto line = state->inbound.substr(0, end);
    state->inbound.erase(0, end + 1);
    processResponse(state, line);
  }
  if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
    closeBackend(state, true);
    state->backendFailed = true;
    if (state->active)
      updateUi(state);
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

gboolean backendWritable(gint fd, GIOCondition condition, gpointer data) {
  auto *state = static_cast<EngineState *>(data);
  while (!state->outbound.empty()) {
    const ssize_t count = ::send(fd, state->outbound.data(), state->outbound.size(), MSG_NOSIGNAL);
    if (count > 0) {
      state->outbound.erase(0, static_cast<size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return G_SOURCE_CONTINUE;
    g_warning("WeTypeX backend sockets write failed: %s", g_strerror(errno));
    state->writeSource = 0;
    closeBackend(state, false);
    state->backendFailed = true;
    if (state->active)
      updateUi(state);
    return G_SOURCE_REMOVE;
  }
  state->writeSource = 0;
  if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
    closeBackend(state, false);
    state->backendFailed = true;
    if (state->active)
      updateUi(state);
  }
  return G_SOURCE_REMOVE;
}

void closeBackend(EngineState *state, bool fromReadWatch) {
  if (state->readSource && !fromReadWatch) {
    g_source_remove(state->readSource);
    state->readSource = 0;
  } else if (fromReadWatch) {
    state->readSource = 0;
  }
  if (state->writeSource) {
    g_source_remove(state->writeSource);
    state->writeSource = 0;
  }
  if (state->readFd >= 0)
    close(state->readFd);
  if (state->writeFd >= 0)
    close(state->writeFd);
  state->readFd = state->writeFd = -1;
  state->inbound.clear();
  state->outbound.clear();
  state->backendReady = false;
  if (state->backendPid > 0) {
    kill(state->backendPid, SIGKILL);
    while (waitpid(state->backendPid, nullptr, 0) < 0 && errno == EINTR) {
    }
    state->backendPid = -1;
  }
  g_debug("WeTypeX backend closed.");
}

bool startBackend(EngineState *state) {
  if (state->backendPid > 0)
    return true;
  int toChild[2] = {-1, -1};
  int fromChild[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, toChild) != 0 ||
      socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fromChild) != 0) {
    if (toChild[0] >= 0) {
      close(toChild[0]);
      close(toChild[1]);
    }
    g_warning("Unable to create the WeTypeX backend sockets: %s", g_strerror(errno));
    return false;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, toChild[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, fromChild[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, toChild[0]);
  posix_spawn_file_actions_addclose(&actions, toChild[1]);
  posix_spawn_file_actions_addclose(&actions, fromChild[0]);
  posix_spawn_file_actions_addclose(&actions, fromChild[1]);
  const char *override = g_getenv("WETYPE_BACKEND_LAUNCHER");
  std::string launcher = override && *override ? override : WETYPE_LAUNCHER;
  char *argv[] = {launcher.data(), nullptr};
  const int result = posix_spawn(&state->backendPid, launcher.c_str(), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(toChild[0]);
  close(fromChild[1]);
  if (result != 0) {
    close(toChild[1]);
    close(fromChild[0]);
    state->backendPid = -1;
    g_warning("Unable to start the WeTypeX backend: %s", g_strerror(result));
    return false;
  }
  state->writeFd = toChild[1];
  state->readFd = fromChild[0];
  fcntl(state->writeFd, F_SETFL, fcntl(state->writeFd, F_GETFL) | O_NONBLOCK);
  fcntl(state->readFd, F_SETFL, fcntl(state->readFd, F_GETFL) | O_NONBLOCK);
  state->readSource = g_unix_fd_add(state->readFd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL),
                                    backendReadable, state);
  state->backendFailed = false;
  g_debug("WeTypeX backend running.");
  return true;
}

void resetComposition(EngineState *state, bool sendReset);

void restartBackend(EngineState *state) {
  g_message("WeTypeX backend restarting...");
  resetComposition(state, false);
  closeBackend(state);
  state->backendFailed = false;
  state->settings = readSettings();
  updateUi(state);
}

void queueLine(EngineState *state, const std::string &line) {
  state->outbound += line;
  state->outbound += '\n';
  if (!state->writeSource && state->writeFd >= 0)
    state->writeSource = g_unix_fd_add(state->writeFd, static_cast<GIOCondition>(G_IO_OUT | G_IO_HUP | G_IO_ERR),
                                       backendWritable, state);
}

void putSettings(json_object *request, const Settings &settings) {
  wire::put(request, "candidate_updates", true);
  wire::put(request, "keyboard", int64_t(settings.inputMode == "wubi" ? 5 : 0));
  constexpr std::array<int, 7> doubleSchemes = {5, 1, 2, 3, 4, 6, 7};
  wire::put(request, "double_scheme",
            int64_t(settings.inputMode == "double_pinyin" ? doubleSchemes[settings.doublePinyinScheme] : 0));
  wire::put(request, "wubi_solution", int64_t(settings.wubiSolution));
  wire::put(request, "wubi_pinyin", settings.wubiPinyin);
  wire::put(request, "wubi_unique_commit", settings.wubiUniqueCommit);
  wire::put(request, "wubi_next_commit", settings.wubiNextCommit);
  wire::put(request, "wubi_wildcard_comment", settings.wubiWildcardComment);
  wire::put(request, "smart_input", settings.smartInput);
  wire::put(request, "emoji_recommend", settings.emojiRecommend);
  wire::put(request, "wechat_emoji", false);
  wire::put(request, "normal_emoji", settings.normalEmoji);
  wire::put(request, "kaomoji", settings.kaomoji);
  wire::put(request, "large_emoji", false);
  wire::put(request, "symbol_emoji", settings.symbolEmoji);
  wire::put(request, "v_mode", false);
  constexpr std::array<const char *, 19> fuzzyKeys = {
      "fuzzy_nl",       "fuzzy_rl",       "fuzzy_hf",     "fuzzy_gk",     "fuzzy_an_ang",
      "fuzzy_ian_iang", "fuzzy_uan_uang", "fuzzy_c_ch",   "fuzzy_s_sh",   "fuzzy_z_zh",
      "fuzzy_hui_fei",  "fuzzy_en_eng",   "fuzzy_in_ing", "fuzzy_on_ong", "fuzzy_huang_wang",
      "fuzzy_un_ong",   "fuzzy_un_iong",  "fuzzy_an_ai",  "fuzzy_eng_ong"};
  for (size_t i = 0; i < fuzzyKeys.size(); ++i)
    wire::put(request, fuzzyKeys[i], settings.fuzzy[i]);
}

bool sendRequest(EngineState *state, const char *operation, const std::string &key = "", int index = 0,
                 int64_t revision = -1, int64_t cursor = -1, const std::string &mapped = "") {
  if (!startBackend(state))
    return false;
  auto request = wire::object();
  wire::put(request.get(), "session", int64_t(state->session));
  wire::put(request.get(), "seq", int64_t(++state->sequence));
  wire::put(request.get(), "epoch", int64_t(state->epoch));
  wire::put(request.get(), "op", std::string(operation));
  wire::put(request.get(), "key", key);
  wire::put(request.get(), "index", int64_t(index));
  wire::put(request.get(), "revision", revision);
  wire::put(request.get(), "cursor", cursor);
  const bool chinesePunctuation = state->settings.punctuationMode == "fullwidth" ||
                                  (state->settings.punctuationMode == "auto" && !state->isCursorBehindAscii);
  wire::put(request.get(), "chinese_punctuation", int64_t(chinesePunctuation));
  wire::put(request.get(), "mapped", mapped);
  putSettings(request.get(), state->settings);
  const std::string line = wire::dump(request.get());
  if (line.size() > 65536) {
    g_warning("Refusing oversized WeTypeX backend request");
    return false;
  }
  if (std::string_view(operation) == "key" || std::string_view(operation) == "backspace" ||
      std::string_view(operation) == "delete" || std::string_view(operation) == "move")
    state->inputSequence = state->sequence;
  queueLine(state, line);
  return true;
}

void processResponse(EngineState *state, const std::string &line) {
  auto json = wire::parse(line);
  if (!json || !json_object_is_type(json.get(), json_type_object))
    return;
  if (wire::str(json.get(), "event") == "ready") {
    state->backendReady = true;
    state->backendFailed = false;
    return;
  }
  if (static_cast<uint64_t>(wire::number(json.get(), "session")) != state->session)
    return;
  const auto sequence = static_cast<uint64_t>(wire::number(json.get(), "seq"));
  const auto epoch = static_cast<uint64_t>(wire::number(json.get(), "epoch"));
  const auto updateId = static_cast<uint64_t>(wire::number(json.get(), "update_id"));
  const bool pushed = wire::str(json.get(), "event") == "update";
  if (epoch != state->epoch || sequence < state->applied || sequence > state->sequence ||
      (sequence == state->applied && (!pushed || updateId <= state->appliedUpdate)))
    return;
  state->applied = sequence;
  state->appliedUpdate = updateId;
  if (!state->active)
    return;
  auto *commits = wire::get(json.get(), "commits");
  if (commits && json_object_is_type(commits, json_type_array)) {
    for (size_t i = 0; i < json_object_array_length(commits); ++i) {
      auto *value = json_object_array_get_idx(commits, i);
      if (json_object_is_type(value, json_type_string))
        ibus_engine_commit_text(IBUS_ENGINE(state->owner), ibus_text_new_from_string(json_object_get_string(value)));
    }
  }
  if (sequence != state->sequence)
    return;
  state->selectionPending = false;
  state->backendFailed = false;
  state->preedit = wire::str(json.get(), "preedit");
  state->cursor = std::min<size_t>(
      std::max<int64_t>(0, wire::number(json.get(), "cursor", static_cast<int64_t>(state->preedit.size()))),
      state->preedit.size());
  state->editableBegin =
      std::min<size_t>(std::max<int64_t>(0, wire::number(json.get(), "editable_begin")), state->preedit.size());
  state->displayPreedit = wire::str(json.get(), "display_preedit");
  state->displayCursor =
      std::min<size_t>(std::max<int64_t>(0, wire::number(json.get(), "display_cursor")), state->displayPreedit.size());
  state->revision = static_cast<uint64_t>(wire::number(json.get(), "revision"));
  state->candidates.clear();
  state->cloud.clear();
  auto *candidates = wire::get(json.get(), "candidates");
  auto *cloud = wire::get(json.get(), "cloud");
  if (candidates && json_object_is_type(candidates, json_type_array)) {
    const size_t count = std::min<size_t>(50, json_object_array_length(candidates));
    for (size_t i = 0; i < count; ++i) {
      auto *candidate = json_object_array_get_idx(candidates, i);
      if (!json_object_is_type(candidate, json_type_string))
        continue;
      state->candidates.emplace_back(json_object_get_string(candidate));
      auto *cloudFlag = cloud && json_object_is_type(cloud, json_type_array) && i < json_object_array_length(cloud)
                            ? json_object_array_get_idx(cloud, i)
                            : nullptr;
      state->cloud.push_back(cloudFlag && json_object_get_boolean(cloudFlag));
    }
  }
  if (state->candidates.empty()) {
    state->candidatePage = 0;
    state->candidateCursor = 0;
  } else {
    state->candidateCursor = std::clamp(state->candidateCursor, 0, static_cast<int>(state->candidates.size()) - 1);
    state->candidatePage = state->candidateCursor / state->settings.pageSize;
  }
  updateUi(state);
  auto deferred = std::move(state->deferredKeys);
  state->deferredKeys.clear();
  for (const auto &key : deferred)
    onProcessKey(IBUS_ENGINE(state->owner), key.keyval, key.keycode, key.modifiers);
}

void resetComposition(EngineState *state, bool sendReset) {
  ++state->epoch;
  state->preedit.clear();
  state->displayPreedit.clear();
  state->cursor = 0;
  state->editableBegin = 0;
  state->displayCursor = 0;
  state->candidates.clear();
  state->cloud.clear();
  state->candidatePage = 0;
  state->candidateCursor = 0;
  state->inputSequence = 0;
  state->selectionPending = false;
  state->deferredKeys.clear();
  state->shiftCandidate = false;
  state->shiftWasUsed = false;
  state->pressedShifts = 0;
  clearUi(state);
  if (sendReset)
    sendRequest(state, "reset");
}

void updateModeProperty(EngineState *state) {
  if (!state->modeProperty)
    return;

  if (state->backendFailed) {
    ibus_property_set_symbol(state->modeProperty, ibus_text_new_from_string("✕"));
    ibus_property_set_label(state->modeProperty, ibus_text_new_from_string("核心不可用"));
  } else {
    if (state->english) {
      ibus_property_set_symbol(state->modeProperty, ibus_text_new_from_string("英"));
      ibus_property_set_label(state->modeProperty, ibus_text_new_from_string("英文"));
    } else {
      ibus_property_set_symbol(state->modeProperty, ibus_text_new_from_string("中"));
      ibus_property_set_label(state->modeProperty, ibus_text_new_from_string("中文"));
    }
  }

  ibus_engine_update_property(IBUS_ENGINE(state->owner), state->modeProperty);
}

void registerProperties(EngineState *state) {
  if (!state->properties) {
    state->properties = ibus_prop_list_new();
    // Sink the floating reference: ibus_engine_register_properties() only
    // unrefs *floating* lists, so this list survives every re-publish.
    g_object_ref_sink(state->properties);

    auto *mode =
        ibus_property_new("InputMode", PROP_TYPE_NORMAL, ibus_text_new_from_string("中文"), "input-keyboard",
                          ibus_text_new_from_string("当前输入模式"), TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    // The list takes its own reference, so keep a borrowed pointer only.
    ibus_prop_list_append(state->properties, mode);
    state->modeProperty = mode;

    auto *restart = ibus_property_new("RestartBackend", PROP_TYPE_NORMAL, ibus_text_new_from_string("重启核心"),
                                      "system-restart", ibus_text_new_from_string("重启 WeTypeX 核心进程"), TRUE, TRUE,
                                      PROP_STATE_UNCHECKED, nullptr);
    ibus_prop_list_append(state->properties, restart);
    state->restartProperty = mode;
  }

  ibus_engine_register_properties(IBUS_ENGINE(state->owner), state->properties);
}

void setEnglishMode(EngineState *state, bool english) {
  sharedEnglishMode = english;
  sharedModeInitialized = true;
  if (state->english == english) {
    return;
  }
  if (!state->preedit.empty()) {
    ibus_engine_commit_text(IBUS_ENGINE(state->owner), ibus_text_new_from_string(state->preedit.c_str()));
    resetComposition(state, true);
  }
  state->english = english;
  state->shiftCandidate = false;
  state->shiftWasUsed = false;
  state->candidates.clear();
  state->cloud.clear();
  updateUi(state);
}

void activate(EngineState *state) {
  const bool active = state->enabled && state->focused && state->contentPurpose != IBUS_INPUT_PURPOSE_PASSWORD &&
                      state->contentPurpose != IBUS_INPUT_PURPOSE_PIN;
  if (active == state->active)
    return;
  state->active = active;
  if (active) {
    state->settings = readSettings();
    if (!sharedModeInitialized) {
      sharedEnglishMode = state->settings.defaultLanguage == "english";
      sharedModeInitialized = true;
    }
    state->english = sharedEnglishMode;
    updateModeProperty(state);
    sendRequest(state, "open");
    updateUi(state);
    if (!state->settings.standalone && syncPid <= 0) {
      const char *override = g_getenv("WETYPE_SYNCD_LAUNCHER");
      std::string launcher = override && *override ? override : WETYPE_SYNCD;
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
      posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
      char *argv[] = {launcher.data(), nullptr};
      const int result = posix_spawn(&syncPid, launcher.c_str(), &actions, nullptr, argv, environ);
      posix_spawn_file_actions_destroy(&actions);
      if (result != 0) {
        syncPid = -1;
        g_warning("Unable to start remote clipboard synchronization: %s", g_strerror(result));
      } else {
        syncWatch = g_child_watch_add(
            syncPid,
            [](GPid pid, gint, gpointer) {
              if (pid == syncPid) {
                syncPid = -1;
                syncWatch = 0;
              }
              g_spawn_close_pid(pid);
            },
            nullptr);
      }
    }
  } else {
    resetComposition(state, true);
    sendRequest(state, "close");
    clearUi(state);
  }
}

bool isCommandModifier(guint modifiers) {
  constexpr guint mask = IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_MOD3_MASK | IBUS_MOD4_MASK | IBUS_MOD5_MASK |
                         IBUS_SUPER_MASK | IBUS_HYPER_MASK | IBUS_META_MASK;
  return (modifiers & mask) != 0;
}

bool splitPunctuationPair(const std::string &text, std::string &left, std::string &right) {
  if (g_utf8_strlen(text.c_str(), -1) != 2)
    return false;
  const char *middle = g_utf8_offset_to_pointer(text.c_str(), 1);
  left.assign(text.c_str(), middle - text.c_str());
  right.assign(middle);
  return true;
}

bool processPunctuation(EngineState *state, gunichar unicode) {
  if (unicode < 0x20 || unicode > 0x7e)
    return false;

  const char ascii = static_cast<char>(unicode);
  auto it = std::find_if(kPunctuation.begin(), kPunctuation.end(),
                         [ascii](const auto &entry) { return entry.ascii == ascii; });
  if (it == kPunctuation.end())
    return false;

  auto *engine = IBUS_ENGINE(state->owner);
  std::string mapped;
  std::string left;
  std::string right;

  const bool chinesePunctuation = state->settings.punctuationMode == "fullwidth" ||
                                  (state->settings.punctuationMode == "auto" && !state->isCursorBehindAscii);
  if (chinesePunctuation) {
    if (ascii == '/' && state->settings.slashPunctuation) {
      mapped = "、";
    } else {
      mapped = it->normal;
    }
  } else {
    mapped = it->english;
  }

  if (it->pairType && splitPunctuationPair(mapped, left, right)) {
    // IBus cannot place the caret inside committed text: clients such as GTK4
    // and Qt drop ForwardKeyEvent, which leaves the caret after an inserted
    // closing symbol. Commit the opening symbol only; quotes alternate so the
    // closing side stays reachable. symbol_auto_pair therefore has no effect
    // in this frontend.
    if (it->pairType == 2) {
      bool &leftNext = ascii == '"' ? state->doubleQuoteLeft : state->singleQuoteLeft;
      mapped = leftNext ? left : right;
      leftNext = !leftNext;
    } else {
      mapped = left;
    }
  }

  const bool hasComposition = !state->preedit.empty();
  std::string commit =
      hasComposition ? (!state->candidates.empty() ? state->candidates.front() : state->preedit) : std::string();
  commit += mapped;
  ibus_engine_commit_text(engine, ibus_text_new_from_string(commit.c_str()));
  if (hasComposition)
    resetComposition(state, true);
  return true;
}

void selectCandidate(EngineState *state, int index, bool pageRelative = false) {
  if (state->preedit.empty())
    return;
  const bool hasCurrentCandidates = state->inputSequence <= state->applied && !state->candidates.empty();
  int globalIndex = index;
  if (index < 0)
    globalIndex = state->candidateCursor;
  else if (pageRelative)
    globalIndex += state->candidatePage * state->settings.pageSize;
  if (hasCurrentCandidates && (globalIndex < 0 || globalIndex >= static_cast<int>(state->candidates.size())))
    return;
  const int64_t expected = hasCurrentCandidates ? static_cast<int64_t>(state->revision) : -1;
  if (!sendRequest(state, "select", "", std::max(0, globalIndex), expected))
    return;
  state->selectionPending = true;
  state->candidateCursor = std::max(0, globalIndex);
  state->candidates.clear();
  state->cloud.clear();
  updateUi(state);
}

void updateCursor(EngineState *state, size_t position) {
  position = std::clamp(position, state->editableBegin, state->preedit.size());
  if (position == state->cursor)
    return;
  state->cursor = position;
  state->displayPreedit.clear();
  sendRequest(state, "move", "", 0, -1, static_cast<int64_t>(position));
  updateUi(state);
}

gboolean onProcessKey(IBusEngine *engine, guint keyval, guint keycode, guint modifiers) {
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  const bool release = (modifiers & IBUS_RELEASE_MASK) != 0;
  const guint cleanModifiers = modifiers & ~IBUS_RELEASE_MASK;

  if (keyval == IBUS_KEY_Super_L || keyval == IBUS_KEY_Super_R || keyval == IBUS_KEY_Hyper_L ||
      keyval == IBUS_KEY_Hyper_R || keyval == IBUS_KEY_Meta_L || keyval == IBUS_KEY_Meta_R ||
      isCommandModifier(cleanModifiers)) {
    // A modifier chord disarms the bare-Shift tap, like ascii_composer's
    // multi-modifier guard.
    state->shiftCandidate = false;
    state->shiftWasUsed = false;
    return FALSE;
  }

  // Shift is only an input-method gesture when it is pressed and released on
  // its own, but it is never consumed: clients track modifier state from the
  // keys they receive, so swallowing the press/release pair makes Shift+click
  // lose the selection and leaves the client one modifier event behind.
  if (keyval == IBUS_KEY_Shift_L || keyval == IBUS_KEY_Shift_R) {
    const uint8_t bit = keyval == IBUS_KEY_Shift_L ? 1 : 2;
    if (release) {
      state->pressedShifts &= static_cast<uint8_t>(~bit);
      const bool switchMode = state->pressedShifts == 0 && state->active && state->shiftCandidate &&
                              !state->shiftWasUsed && state->settings.shiftSwitch &&
                              // librime's ascii_composer only treats a Shift press-release pair
                              // within 500 ms as a tap; a held Shift means it was used as a
                              // modifier (Shift+click), so it must not toggle the language.
                              g_get_monotonic_time() - state->shiftPressedAt <= 500000;
      state->shiftCandidate = false;
      state->shiftWasUsed = false;
      if (switchMode) {
        setEnglishMode(state, !state->english);
        updateModeProperty(state);
      }
      return FALSE;
    }
    state->pressedShifts |= bit;
    state->shiftCandidate = state->active && state->settings.shiftSwitch;
    state->shiftWasUsed = false;
    state->shiftPressedAt = g_get_monotonic_time();
    return FALSE;
  }

  // Any other key invalidates a pending bare-Shift switch, including keys that
  // are buffered while a candidate selection reply is outstanding.
  if (!release && state->shiftCandidate) {
    state->shiftWasUsed = true;
    state->shiftCandidate = false;
  }

  if (state->selectionPending && keyval != IBUS_KEY_Escape) {
    if (state->deferredKeys.size() >= 64) {
      g_warning("Passing IBus key through after selection queue overflow");
      return FALSE;
    }
    state->deferredKeys.push_back({keyval, keycode, modifiers});
    return TRUE;
  }

  if (!state->active)
    return FALSE;

  if (release)
    return FALSE;

  if (!state->preedit.empty()) {
    if (keyval == IBUS_KEY_Escape) {
      resetComposition(state, true);
      return TRUE;
    }

    if (keyval == IBUS_KEY_Return || keyval == IBUS_KEY_KP_Enter) {
      if (!sendRequest(state, "raw")) {
        ibus_engine_commit_text(engine, ibus_text_new_from_string(state->preedit.c_str()));
        resetComposition(state, false);
      }
      return TRUE;
    }

    if (keyval == IBUS_KEY_space) {
      selectCandidate(state, -1);
      return TRUE;
    }

    if ((keyval >= IBUS_KEY_1 && keyval <= IBUS_KEY_9) || (keyval >= IBUS_KEY_KP_1 && keyval <= IBUS_KEY_KP_9)) {
      const guint digit = keyval >= IBUS_KEY_KP_1 ? keyval - IBUS_KEY_KP_1 : keyval - IBUS_KEY_1;
      if (digit < static_cast<guint>(state->settings.pageSize)) {
        selectCandidate(state, static_cast<int>(digit), true);
        return TRUE;
      }
      return FALSE;
    }

    if (keyval == IBUS_KEY_BackSpace) {
      if (state->cursor > state->editableBegin) {
        const auto previous = previousUtf8(state->preedit, state->cursor);
        state->preedit.erase(previous, state->cursor - previous);
        state->cursor = previous;
        state->displayPreedit.clear();
        sendRequest(state, "backspace");
        updateUi(state);
      }
      return TRUE;
    }

    if (keyval == IBUS_KEY_Delete) {
      if (state->cursor < state->preedit.size()) {
        const auto next = nextUtf8(state->preedit, state->cursor);
        state->preedit.erase(state->cursor, next - state->cursor);
        state->displayPreedit.clear();
        sendRequest(state, "delete");
        updateUi(state);
      }
      return TRUE;
    }

    if (keyval == IBUS_KEY_Left) {
      updateCursor(state, previousUtf8(state->preedit, state->cursor));
      return TRUE;
    }

    if (keyval == IBUS_KEY_Right) {
      updateCursor(state, nextUtf8(state->preedit, state->cursor));
      return TRUE;
    }

    if (keyval == IBUS_KEY_Home) {
      updateCursor(state, state->editableBegin);
      return TRUE;
    }

    if (keyval == IBUS_KEY_End) {
      updateCursor(state, state->preedit.size());
      return TRUE;
    }

    if (keyval == IBUS_KEY_Page_Up || keyval == IBUS_KEY_Page_Down) {
      const int pageCount =
          static_cast<int>((state->candidates.size() + state->settings.pageSize - 1) / state->settings.pageSize);
      if (pageCount > 1) {
        state->candidatePage =
            std::clamp(state->candidatePage + (keyval == IBUS_KEY_Page_Down ? 1 : -1), 0, pageCount - 1);
        state->candidateCursor = state->candidatePage * state->settings.pageSize;
        updateUi(state);
      }
      return TRUE;
    }

    if (keyval == IBUS_KEY_Up || keyval == IBUS_KEY_Down) {
      if (!state->candidates.empty()) {
        state->candidateCursor = std::clamp(state->candidateCursor + (keyval == IBUS_KEY_Down ? 1 : -1), 0,
                                            static_cast<int>(state->candidates.size()) - 1);
        state->candidatePage = state->candidateCursor / state->settings.pageSize;
        updateUi(state);
      }
      return TRUE;
    }
  }

  gunichar unicode = ibus_keyval_to_unicode(keyval);
  g_debug_func("unicode=U+%04X", unicode);

  if ((cleanModifiers & IBUS_SHIFT_MASK) && unicode >= 'a' && unicode <= 'z')
    unicode -= 'a' - 'A';
  const bool letter = (unicode >= 'a' && unicode <= 'z') || (unicode >= 'A' && unicode <= 'Z');
  const bool separator = unicode == '\'' && !state->preedit.empty() && state->settings.inputMode != "wubi";

  if (state->english)
    return FALSE;

  if (letter || separator) {
    char text[2] = {static_cast<char>(unicode), '\0'};
    state->cursor = std::min(state->cursor, state->preedit.size());
    state->preedit.insert(state->cursor, text, 1);
    ++state->cursor;
    state->displayPreedit.clear();
    state->candidates.clear();
    state->cloud.clear();
    state->candidatePage = state->candidateCursor = 0;
    sendRequest(state, "key", text);
    updateUi(state);
    return TRUE;
  }

  if (processPunctuation(state, unicode)) {
    return TRUE;
  }

  return FALSE;
}

void onFocusIn(IBusEngine *engine) {
  g_debug("onFocusIn");
  auto *self = reinterpret_cast<WeTypeEngine *>(engine);
  self->state->focused = true;
  registerProperties(self->state);
  updateModeProperty(self->state);
  activate(self->state);
}

void onFocusOut(IBusEngine *engine) {
  g_debug("onFocusOut");
  auto *self = reinterpret_cast<WeTypeEngine *>(engine);
  self->state->focused = false;
  activate(self->state);
}

void onEnable(IBusEngine *engine) {
  g_debug("onEnable");
  ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
  auto *self = reinterpret_cast<WeTypeEngine *>(engine);
  self->state->enabled = true;
  activate(self->state);
}

void onDisable(IBusEngine *engine) {
  g_debug("onDisable");
  auto *self = reinterpret_cast<WeTypeEngine *>(engine);
  self->state->enabled = false;
  activate(self->state);
}

void onReset(IBusEngine *engine) {
  g_debug("onReset");
  auto *self = reinterpret_cast<WeTypeEngine *>(engine);
  resetComposition(self->state, self->state->active);
}

void onPageUp(IBusEngine *engine) {
  g_debug("onPageUp");
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  if (state->candidatePage > 0) {
    state->candidatePage--;
    state->candidateCursor = state->candidatePage * state->settings.pageSize;
    updateUi(state);
  }
}

void onPageDown(IBusEngine *engine) {
  g_debug("onPageDown");
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  const int pages =
      static_cast<int>((state->candidates.size() + state->settings.pageSize - 1) / state->settings.pageSize);
  if (state->candidatePage + 1 < pages) {
    state->candidatePage++;
    state->candidateCursor = state->candidatePage * state->settings.pageSize;
    updateUi(state);
  }
}

void onCursorUp(IBusEngine *engine) {
  g_debug("onCursorUp");
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  if (state->candidateCursor > 0) {
    state->candidateCursor--;
    state->candidatePage = state->candidateCursor / state->settings.pageSize;
    updateUi(state);
  }
}

void onCursorDown(IBusEngine *engine) {
  g_debug("onCursorDown");
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  if (state->candidateCursor + 1 < static_cast<int>(state->candidates.size())) {
    state->candidateCursor++;
    state->candidatePage = state->candidateCursor / state->settings.pageSize;
    updateUi(state);
  }
}

void onCandidateClicked(IBusEngine *engine, guint index, guint, guint) {
  g_debug("onCandidateClicked");
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  selectCandidate(state, static_cast<int>(index));
}

void onContentType(IBusEngine *engine, guint purpose, guint hints) {
  g_debug_func("purpose=%d hints=%d", purpose, hints);
  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  state->contentPurpose = static_cast<int>(purpose);
  activate(state);
}

void onSetSurroundingText(IBusEngine *engine, IBusText *ibus_text, guint cursor_pos, guint) {
  const gchar *text = ibus_text ? ibus_text_get_text(ibus_text) : nullptr;
  if (!text || cursor_pos == 0) {
    return;
  }

  const gchar *cursor_ptr = g_utf8_offset_to_pointer(text, cursor_pos);
  const gchar *prev_ptr = g_utf8_find_prev_char(text, cursor_ptr);
  if (prev_ptr) {
    const gunichar unicode = g_utf8_get_char(prev_ptr);
    auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
    state->isCursorBehindAscii = (unicode >= 0x20 && unicode <= 0x7e) ? true : false;
    g_debug_func("prev=U+%04X isCursorBehindAscii=%d", unicode, state->isCursorBehindAscii);
  }
}

void onPropertyActivate(IBusEngine *engine, const gchar *propertyName, guint) {
  g_debug("onPropertyActivate");

  if (!propertyName)
    return;

  auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
  if (g_strcmp0(propertyName, "InputMode") == 0) {
    // The panel exposes the mode property as a clickable item; activating it
    // toggles Chinese/English, same as ibus-rime.
    setEnglishMode(state, !state->english);
  } else if (g_strcmp0(propertyName, "RestartBackend") == 0) {
    restartBackend(state);
  }
  updateModeProperty(state);
}

void onSetCapabilities(IBusEngine *engine, guint caps) {
  g_debug_func("caps=0x%08x", caps);
  if (caps & IBUS_CAP_SURROUNDING_TEXT) {
    ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
  } else {
    auto *state = reinterpret_cast<WeTypeEngine *>(engine)->state;
    state->isCursorBehindAscii = true;
  }
}

void finalize(GObject *object) {
  auto *self = reinterpret_cast<WeTypeEngine *>(object);
  if (self->state) {
    auto *state = self->state;
    if (state->backendPid > 0 && state->active) {
      sendRequest(state, "close");
      usleep(10000);
    }
    closeBackend(state);
    if (state->properties)
      g_object_unref(state->properties);
    delete state;
    self->state = nullptr;
  }
  G_OBJECT_CLASS(wetype_engine_parent_class)->finalize(object);
}

void wetype_engine_class_init(WeTypeEngineClass *klass) {
  auto *objectClass = G_OBJECT_CLASS(klass);
  objectClass->finalize = finalize;
  auto *engineClass = IBUS_ENGINE_CLASS(klass);
  engineClass->process_key_event = onProcessKey;
  engineClass->focus_in = onFocusIn;
  engineClass->focus_out = onFocusOut;
  engineClass->reset = onReset;
  engineClass->enable = onEnable;
  engineClass->disable = onDisable;
  // engineClass->set_cursor_location = onSetCursorLocation;
  engineClass->set_capabilities = onSetCapabilities;
  engineClass->page_up = onPageUp;
  engineClass->page_down = onPageDown;
  engineClass->cursor_up = onCursorUp;
  engineClass->cursor_down = onCursorDown;
  engineClass->property_activate = onPropertyActivate;
  // engineClass->property_show = onPropertyShow;
  // engineClass->property_hide = onPropertyHide;
  engineClass->candidate_clicked = onCandidateClicked;
  engineClass->set_surrounding_text = onSetSurroundingText;
  engineClass->set_content_type = onContentType;
}

void wetype_engine_init(WeTypeEngine *self) {
  self->state = new EngineState;
  self->state->owner = self;
  self->state->session = ++nextSession;
  self->state->settings = readSettings();
  if (!sharedModeInitialized) {
    sharedEnglishMode = self->state->settings.defaultLanguage == "english";
    sharedModeInitialized = true;
  }
  self->state->english = sharedEnglishMode;
}

} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  g_set_prgname("ibus-wetypex");

  // Create
  ibus_init();

  auto *bus = ibus_bus_new();
  if (!ibus_bus_is_connected(bus)) {
    g_printerr("Cannot connect to the IBus daemon\n");
    g_object_unref(bus);
    return 1;
  }

  auto *factory = ibus_factory_new(ibus_bus_get_connection(bus));
  ibus_factory_add_engine(factory, "wetypex", wetype_engine_get_type());

  if (!ibus_bus_request_name(bus, "org.freedesktop.IBus.Wetypex", 0)) {
    g_printerr("Cannot register the WeTypeX IBus service name\n");
    g_object_unref(bus);
    return 1;
  }

  auto *loop = g_main_loop_new(nullptr, FALSE);
  g_signal_connect(bus, "disconnected",
                   G_CALLBACK(+[](IBusBus *, gpointer data) { g_main_loop_quit(static_cast<GMainLoop *>(data)); }),
                   loop);

  // Loop
  g_main_loop_run(loop);

  // Destory
  g_main_loop_unref(loop);
  g_object_unref(factory);
  g_object_unref(bus);

  return 0;
}
