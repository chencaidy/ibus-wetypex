#include "account_client.hpp"
#include "check_update.hpp"
#include "engine_client.hpp"
#include "language.hpp"
#include "settings_store.hpp"

#include "../common/json.hpp"

#include "adwaita_backport.h"
#include <json-c/json.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <vector>

using namespace ibus_settings;
using namespace ibus_settings::lang;

namespace {

struct AppState {
  AdwApplication *application = nullptr;
  GtkWidget *window = nullptr;
  GtkWidget *view_stack = nullptr;
  GtkWidget *standalone_switch = nullptr;
  GtkWidget *devices_page = nullptr;
  GtkWidget *sync_status_row = nullptr;
  GtkWidget *sync_clipboard = nullptr;
  GtkWidget *sync_phrase = nullptr;
  GtkWidget *sync_dictionary = nullptr;
  GtkWidget *phrase_group = nullptr;
  std::vector<GtkWidget *> phrase_rows;
  std::vector<GtkWidget *> device_groups;
  int sync_group = 0;
  int sync_functions = 0;
  bool sync_updating = false;
  bool phrases_loaded = false;
};

struct DeviceEntry {
  std::string name;
  std::string subtitle;
  int64_t uin = 0;
};

AppState app;

void refresh_devices_page();
void reload_phrases();

std::string trimmed(std::string text) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

GtkWidget *dim_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_widget_add_css_class(label, "dim-label");
  return label;
}

GtkWidget *make_group(const char *title, const char *description = nullptr) {
  GtkWidget *group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), title);
  if (description)
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
  return group;
}

GtkWidget *action_row(const char *title, const char *subtitle = nullptr) {
  GtkWidget *row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle && *subtitle)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  return row;
}

void on_visible_page_changed(GObject *object, GParamSpec *, gpointer);

void on_switch_notify(GObject *object, GParamSpec *, gpointer) {
  const char *key = static_cast<const char *>(g_object_get_data(object, "setting-key"));
  if (!key)
    return;
  const bool active = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
  write_setting_bool(key, active);
  if (std::strcmp(key, "standalone") == 0)
    refresh_devices_page();
}

GtkWidget *switch_row(const char *title, const char *subtitle, const char *key, bool fallback) {
  auto settings = read_settings();
  GtkWidget *row = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle && *subtitle)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_switch_row_set_active(ADW_SWITCH_ROW(row), setting_bool(settings.get(), key, fallback));
  g_object_set_data_full(G_OBJECT(row), "setting-key", g_strdup(key), g_free);
  g_signal_connect(row, "notify::active", G_CALLBACK(on_switch_notify), nullptr);
  if (std::strcmp(key, "standalone") == 0)
    app.standalone_switch = row;
  return row;
}

void on_combo_string(GObject *object, GParamSpec *, gpointer) {
  const char *key = static_cast<const char *>(g_object_get_data(object, "setting-key"));
  auto *values = static_cast<std::vector<std::string> *>(g_object_get_data(object, "setting-values"));
  if (!key || !values)
    return;
  const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
  if (selected == GTK_INVALID_LIST_POSITION || selected >= values->size())
    return;
  write_setting_string(key, (*values)[selected]);
}

GtkWidget *string_combo_row(const char *title, const char *subtitle, const char *key,
                            const std::vector<std::string> &labels, const std::vector<std::string> &values,
                            int current) {
  GtkWidget *row = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle && *subtitle)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);

  GtkStringList *model = gtk_string_list_new(nullptr);
  for (const auto &label : labels)
    gtk_string_list_append(model, label.c_str());
  adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
  g_object_unref(model);

  current = std::clamp(current, 0, static_cast<int>(labels.size()) - 1);
  adw_combo_row_set_selected(ADW_COMBO_ROW(row), static_cast<guint>(current));
  g_object_set_data_full(G_OBJECT(row), "setting-key", g_strdup(key), g_free);
  g_object_set_data_full(G_OBJECT(row), "setting-values", new std::vector<std::string>(values),
                         [](gpointer data) { delete static_cast<std::vector<std::string> *>(data); });
  g_signal_connect(row, "notify::selected", G_CALLBACK(on_combo_string), nullptr);
  return row;
}

void on_spin_value(GObject *object, GParamSpec *, gpointer) {
  const char *key = static_cast<const char *>(g_object_get_data(object, "setting-key"));
  if (!key)
    return;
  write_setting_int(key, static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(object))));
}

GtkWidget *spin_row(const char *title, const char *subtitle, const char *key, int minimum, int maximum, int current) {
  GtkWidget *row = adw_spin_row_new_with_range(minimum, maximum, 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle && *subtitle)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_spin_row_set_value(ADW_SPIN_ROW(row), current);
  g_object_set_data_full(G_OBJECT(row), "setting-key", g_strdup(key), g_free);
  g_signal_connect(row, "notify::value", G_CALLBACK(on_spin_value), nullptr);
  return row;
}

struct SwitchOption {
  const char *label;
  const char *key;
};

void add_switch_rows(GtkWidget *expander, const std::vector<SwitchOption> &options, json_object *settings) {
  for (const auto &option : options) {
    GtkWidget *row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), option.label);
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), setting_bool(settings, option.key, false));
    g_object_set_data_full(G_OBJECT(row), "setting-key", g_strdup(option.key), g_free);
    g_signal_connect(row, "notify::active", G_CALLBACK(on_switch_notify), nullptr);
    adw_expander_row_add_row(ADW_EXPANDER_ROW(expander), row);
  }
}

GtkWidget *mode_row(const char *title, GtkCheckButton *radio) {
  GtkWidget *row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  adw_action_row_add_prefix(ADW_ACTION_ROW(row), GTK_WIDGET(radio));
  return row;
}

void on_scheme_selected(GObject *object, GParamSpec *, gpointer) {
  const char *key = static_cast<const char *>(g_object_get_data(object, "setting-key"));
  if (!key)
    return;
  const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
  if (selected == GTK_INVALID_LIST_POSITION)
    return;
  write_setting_int(key, static_cast<int>(selected));
  auto *radio = static_cast<GtkCheckButton *>(g_object_get_data(object, "mode-radio"));
  if (radio && !gtk_check_button_get_active(radio))
    gtk_check_button_set_active(radio, TRUE);
}

GtkWidget *scheme_combo_row(const char *title, const char *key, GtkCheckButton *radio,
                            const std::vector<std::string> &items, int current) {
  GtkWidget *row = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

  GtkStringList *model = gtk_string_list_new(nullptr);
  for (const auto &item : items)
    gtk_string_list_append(model, item.c_str());
  adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
  g_object_unref(model);

  current = std::clamp(current, 0, static_cast<int>(items.size()) - 1);
  adw_combo_row_set_selected(ADW_COMBO_ROW(row), static_cast<guint>(current));
  adw_action_row_add_prefix(ADW_ACTION_ROW(row), GTK_WIDGET(radio));
  g_object_set_data_full(G_OBJECT(row), "setting-key", g_strdup(key), g_free);
  g_object_set_data(G_OBJECT(row), "mode-radio", radio);
  g_signal_connect(row, "notify::selected", G_CALLBACK(on_scheme_selected), nullptr);
  return row;
}

GtkWidget *g_wubi_options = nullptr;

void update_mode_sensitivity(const std::string &mode) {
  if (g_wubi_options)
    gtk_widget_set_sensitive(g_wubi_options, mode == "wubi");
}

void on_mode_toggled(GtkCheckButton *button, gpointer) {
  if (!gtk_check_button_get_active(button))
    return;
  const char *mode = static_cast<const char *>(g_object_get_data(G_OBJECT(button), "input-mode"));
  if (!mode)
    return;
  write_setting_string("input_mode", mode);
  write_setting_int("keyboard", std::strcmp(mode, "wubi") == 0 ? 5 : 0);
  update_mode_sensitivity(mode);
}

void clear_phrase_rows() {
  if (!app.phrase_group)
    return;
  for (auto *row : app.phrase_rows)
    adw_preferences_group_remove(ADW_PREFERENCES_GROUP(app.phrase_group), row);
  app.phrase_rows.clear();
}

struct PhraseDialogState {
  GtkWidget *window = nullptr;
  GtkWidget *words = nullptr;
  GtkWidget *key = nullptr;
  GtkWidget *status = nullptr;
  GtkWidget *save = nullptr;
  std::string id;
  bool closed = false;
  bool saving = false;
};

void on_phrase_dialog_destroy(GtkWidget *, gpointer data) { static_cast<PhraseDialogState *>(data)->closed = true; }

void on_save_phrase(GtkButton *, gpointer data) {
  auto *state = static_cast<PhraseDialogState *>(data);
  if (!state || state->saving || state->closed)
    return;

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->words));
  GtkTextIter begin;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &begin, &end);
  gchar *raw = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
  std::string words = trimmed(raw ? raw : "");
  g_free(raw);
  std::string key = trimmed(gtk_editable_get_text(GTK_EDITABLE(state->key)));
  if (words.empty() || key.empty()) {
    gtk_label_set_text(GTK_LABEL(state->status), kPhraseEmptyFields);
    return;
  }

  std::string id = state->id;
  if (id.empty())
    id = std::to_string(g_get_real_time() / 1000);
  state->saving = true;
  gtk_widget_set_sensitive(state->save, FALSE);
  gtk_label_set_text(GTK_LABEL(state->status), kPhraseSaving);
  g_object_ref(state->window);
  request_hotword_set(id, key, words, [state](EngineReply reply) {
    if (!state->closed) {
      if (reply.ok) {
        reload_phrases();
        gtk_window_destroy(GTK_WINDOW(state->window));
      } else {
        state->saving = false;
        gtk_widget_set_sensitive(state->save, TRUE);
        gtk_label_set_text(GTK_LABEL(state->status), reply.error.empty() ? kPhraseSaveFailed : reply.error.c_str());
      }
    }
    g_object_unref(state->window);
  });
}

void show_phrase_dialog(GtkWindow *parent, GtkWidget *existing) {
  std::string id;
  std::string key;
  std::string words;
  if (existing) {
    const char *value = static_cast<const char *>(g_object_get_data(G_OBJECT(existing), "hotword-id"));
    if (value)
      id = value;
    value = static_cast<const char *>(g_object_get_data(G_OBJECT(existing), "hotword-key"));
    if (value)
      key = value;
    value = static_cast<const char *>(g_object_get_data(G_OBJECT(existing), "hotword-words"));
    if (value)
      words = value;
  }

  GtkWidget *window = adw_window_new();
  gtk_window_set_title(GTK_WINDOW(window), id.empty() ? kPhraseDialogAddTitle : kPhraseDialogEditTitle);
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 420);
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  if (parent)
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);

  GtkWidget *toolbar = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *cancel = gtk_button_new_with_label(kCancelLabel);
  GtkWidget *save = gtk_button_new_with_label(kSaveLabel);
  gtk_widget_add_css_class(save, "suggested-action");
  adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), save);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

  GtkWidget *page = adw_preferences_page_new();

  GtkWidget *key_group = adw_preferences_group_new();
  GtkWidget *key_row = adw_entry_row_new();
  GtkWidget *key_entry = wetypex_entry_row_get_entry(key_row);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(key_row), kPhraseKeyTitle);
  gtk_editable_set_text(GTK_EDITABLE(key_entry), key.c_str());
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(key_group), key_row);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(key_group));

  GtkWidget *words_group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(words_group), kPhraseWordsLabel);

  GtkWidget *text_view = gtk_text_view_new();
  gtk_widget_add_css_class(text_view, "inline");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 12);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 12);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 12);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 12);
  GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
  gtk_text_buffer_set_text(text_buffer, words.c_str(), -1);
  GtkWidget *text_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(text_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(text_scroll), text_view);
  gtk_widget_set_size_request(text_scroll, -1, 180);
  gtk_widget_add_css_class(text_scroll, "card");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(words_group), text_scroll);

  GtkWidget *status = dim_label("");
  gtk_widget_set_margin_top(status, 6);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(words_group), status);

  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(words_group));

  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), page);
  adw_window_set_content(ADW_WINDOW(window), toolbar);

  auto *state = new PhraseDialogState{window, text_view, key_entry, status, save, id};
  g_object_set_data_full(G_OBJECT(window), "phrase-dialog-state", state,
                         [](gpointer data) { delete static_cast<PhraseDialogState *>(data); });
  g_signal_connect(window, "destroy", G_CALLBACK(on_phrase_dialog_destroy), state);
  g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
  g_signal_connect(save, "clicked", G_CALLBACK(on_save_phrase), state);
  gtk_window_set_default_widget(GTK_WINDOW(window), save);
  gtk_window_present(GTK_WINDOW(window));
}

void on_add_phrase(GtkButton *, gpointer) {
  if (app.window)
    show_phrase_dialog(GTK_WINDOW(app.window), nullptr);
}

void on_edit_phrase(GtkButton *button, gpointer) {
  GtkWidget *row = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(button), "phrase-row"));
  if (row && app.window)
    show_phrase_dialog(GTK_WINDOW(app.window), row);
}

void on_delete_phrase(GtkButton *button, gpointer) {
  const char *id = static_cast<const char *>(g_object_get_data(G_OBJECT(button), "hotword-id"));
  if (!id || !*id)
    return;
  const std::string phrase_id(id);
  gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
  g_object_ref(button);
  request_hotword_set(phrase_id, "", "", [button](EngineReply reply) {
    if (reply.ok)
      reload_phrases();
    else
      gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
    g_object_unref(button);
  });
}

void reload_phrases() {
  if (!app.phrase_group) {
    g_debug("phrase reload skipped: page group is unavailable");
    return;
  }
  clear_phrase_rows();

  request_hotword_list([](EngineReply reply) {
    if (!app.phrase_group) {
      g_debug("phrase response ignored: page was closed");
      return;
    }
    clear_phrase_rows();
    g_debug("%s", (std::string("phrase response received: ok=") + (reply.ok ? "true" : "false") + ", bytes=" +
                   std::to_string(reply.json.size()) + (reply.error.empty() ? "" : ", error=" + reply.error))
                      .c_str());
    if (!reply.ok)
      return;
    auto json = wire::parse(reply.json);
    auto *hotwords = wire::get(json.get(), "hotwords");
    int count = 0;
    if (hotwords && json_object_is_type(hotwords, json_type_array)) {
      for (size_t i = 0; i < json_object_array_length(hotwords); ++i) {
        auto *item = json_object_array_get_idx(hotwords, i);
        if (!item || !json_object_is_type(item, json_type_object))
          continue;
        const std::string words = wire::str(item, "words");
        const std::string key = wire::str(item, "key");
        const std::string id = wire::str(item, "id");
        if (words.empty())
          continue;

        GtkWidget *row = action_row(words.c_str(), key.c_str());
        g_object_set_data_full(G_OBJECT(row), "hotword-id", g_strdup(id.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(row), "hotword-key", g_strdup(key.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(row), "hotword-words", g_strdup(words.c_str()), g_free);

        GtkWidget *edit = gtk_button_new_from_icon_name("document-edit-symbolic");
        gtk_widget_add_css_class(edit, "flat");
        gtk_widget_set_tooltip_text(edit, kEditTooltip);
        gtk_widget_set_valign(edit, GTK_ALIGN_CENTER);
        g_object_set_data(G_OBJECT(edit), "phrase-row", row);
        g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_phrase), nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), edit);

        GtkWidget *remove = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(remove, "flat");
        gtk_widget_set_tooltip_text(remove, kDeleteTooltip);
        gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
        g_object_set_data_full(G_OBJECT(remove), "hotword-id", g_strdup(id.c_str()), g_free);
        g_signal_connect(remove, "clicked", G_CALLBACK(on_delete_phrase), nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), remove);

        adw_preferences_group_add(ADW_PREFERENCES_GROUP(app.phrase_group), row);
        app.phrase_rows.push_back(row);
        ++count;
      }
    }
    if (count == 0) {
      GtkWidget *empty = action_row(kPhraseEmptyTitle, kPhraseEmptySubtitle);
      adw_preferences_group_add(ADW_PREFERENCES_GROUP(app.phrase_group), empty);
      app.phrase_rows.push_back(empty);
    }
    g_debug("%s", (std::string("phrase list rendered: ") + std::to_string(count) + " entries").c_str());
  });
}

std::string local_device_name() {
  gchar *contents = nullptr;
  gsize length = 0;
  std::string name;
  if (g_file_get_contents("/sys/class/dmi/id/product_name", &contents, &length, nullptr)) {
    name = trimmed(std::string(contents, length));
    g_free(contents);
  }
  return name.empty() ? kLocalDeviceFallbackName : name;
}

std::string platform_name(int platform) {
  switch (platform) {
  case 1:
    return kPlatformAndroid;
  case 2:
    return kPlatformIos;
  case 5:
    return kPlatformLinux;
  case 6:
    return kPlatformWindows;
  default:
    return kPlatformUnknown;
  }
}

bool account_ok(const AccountReply &reply) {
  if (!reply.ok)
    return false;
  auto json = wire::parse(reply.output);
  if (!json)
    return true;
  auto *ok = wire::get(json.get(), "ok");
  return !ok || json_object_get_boolean(ok);
}

struct PairingState {
  GtkWidget *window = nullptr;
  GtkWidget *status = nullptr;
  GtkWidget *code = nullptr;
  guint timer = 0;
  std::string match_code;
  bool in_flight = false;
  bool closed = false;
  bool confirmed = false;
};

void pairing_set_status(PairingState *state, const char *text) {
  if (state && state->status && !state->closed)
    gtk_label_set_text(GTK_LABEL(state->status), text);
}

void pairing_call(PairingState *state, std::vector<std::string> arguments,
                  std::function<void(const AccountReply &)> done) {
  if (!state || state->closed || state->in_flight)
    return;
  state->in_flight = true;
  g_object_ref(state->window);
  run_account(std::move(arguments), [state, done = std::move(done)](AccountReply reply) {
    state->in_flight = false;
    if (!state->closed && done)
      done(reply);
    g_object_unref(state->window);
  });
}

gboolean pairing_poll(gpointer data);

void pairing_start_code(PairingState *state) {
  pairing_set_status(state, kPairingCodeLoading);
  pairing_call(state, {"pairing-code"}, [state](const AccountReply &reply) {
    auto json = wire::parse(reply.output);
    if (!account_ok(reply) || !json) {
      pairing_set_status(state, kPairingCodeUnavailable);
      return;
    }
    const std::string code = wire::str(json.get(), "match_code");
    if (code.size() != 6) {
      pairing_set_status(state, kPairingCodeInvalid);
      return;
    }
    state->match_code = code;
    gtk_label_set_text(GTK_LABEL(state->code), code.c_str());
    pairing_set_status(state, kPairingCodeValid);
    if (!state->timer)
      state->timer = g_timeout_add(1500, pairing_poll, state);
  });
}

gboolean pairing_poll(gpointer data) {
  auto *state = static_cast<PairingState *>(data);
  if (!state || state->closed || state->confirmed || state->match_code.empty())
    return G_SOURCE_CONTINUE;
  if (state->in_flight)
    return G_SOURCE_CONTINUE;

  pairing_call(state, {"pairing-status", state->match_code}, [state](const AccountReply &reply) {
    auto json = wire::parse(reply.output);
    if (!account_ok(reply) || !json)
      return;
    const int status = setting_int(json.get(), "status", -1);
    if (status == 0)
      return;
    if (status == 1) {
      const std::string bind = wire::str(json.get(), "bind_id");
      if (bind.empty())
        return;
      auto *reconfirm = wire::get(json.get(), "show_reconfirm");
      if (reconfirm && json_object_get_boolean(reconfirm)) {
        pairing_set_status(state, kPairingConfirmOnOther);
        return;
      }
      state->confirmed = true;
      if (state->timer) {
        g_source_remove(state->timer);
        state->timer = 0;
      }
      pairing_set_status(state, kPairingConfirming);
      pairing_call(state, {"confirm-bind", bind}, [state](const AccountReply &confirm) {
        if (account_ok(confirm)) {
          pairing_set_status(state, kPairingSuccess);
          refresh_devices_page();
          g_timeout_add_full(
              G_PRIORITY_DEFAULT, 900,
              [](gpointer window) {
                if (GTK_IS_WINDOW(window))
                  gtk_window_destroy(GTK_WINDOW(window));
                return G_SOURCE_REMOVE;
              },
              g_object_ref(state->window), g_object_unref);
        } else {
          state->confirmed = false;
          pairing_set_status(state, kPairingFailed);
        }
      });
      return;
    }
    if (status == 2) {
      if (state->timer) {
        g_source_remove(state->timer);
        state->timer = 0;
      }
      pairing_set_status(state, kPairingCodeExpired);
    }
  });
  return G_SOURCE_CONTINUE;
}

void on_pairing_destroy(GtkWidget *, gpointer data) {
  auto *state = static_cast<PairingState *>(data);
  state->closed = true;
  if (state->timer) {
    g_source_remove(state->timer);
    state->timer = 0;
  }
}

void show_pairing_window(GtkWindow *parent) {
  GtkWidget *window = adw_window_new();
  gtk_window_set_title(GTK_WINDOW(window), kPairingWindowTitle);
  gtk_window_set_default_size(GTK_WINDOW(window), 440, 320);
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  if (parent)
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);

  GtkWidget *toolbar = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 24);

  GtkWidget *title = gtk_label_new(kPairingCodeTitle);
  gtk_widget_add_css_class(title, "title-2");
  gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), title);

  GtkWidget *status = dim_label(kPairingCodeLoading);
  gtk_label_set_xalign(GTK_LABEL(status), 0.5f);
  gtk_widget_set_halign(status, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), status);

  GtkWidget *code = gtk_label_new("");
  gtk_widget_add_css_class(code, "title-1");
  gtk_widget_set_halign(code, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), code);

  GtkWidget *message = dim_label(kPairingCodeMessage);
  gtk_label_set_xalign(GTK_LABEL(message), 0.5f);
  gtk_widget_set_halign(message, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), message);

  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), box);
  adw_window_set_content(ADW_WINDOW(window), toolbar);

  auto *state = new PairingState();
  state->window = window;
  state->status = status;
  state->code = code;
  g_object_set_data_full(G_OBJECT(window), "pairing-state", state,
                         [](gpointer data) { delete static_cast<PairingState *>(data); });
  g_signal_connect(window, "destroy", G_CALLBACK(on_pairing_destroy), state);
  pairing_start_code(state);
  gtk_window_present(GTK_WINDOW(window));
}

void set_sync_status(const char *text) {
  if (app.sync_status_row)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(app.sync_status_row), text);
}

void set_sync_switches_sensitive(bool sensitive) {
  for (auto *control : {app.sync_clipboard, app.sync_phrase, app.sync_dictionary})
    if (control)
      gtk_widget_set_sensitive(control, sensitive);
}

void on_sync_switch(GObject *, GParamSpec *, gpointer) {
  if (app.sync_updating || !app.sync_group)
    return;
  const bool clipboard = app.sync_clipboard && adw_switch_row_get_active(ADW_SWITCH_ROW(app.sync_clipboard));
  const bool phrase = app.sync_phrase && adw_switch_row_get_active(ADW_SWITCH_ROW(app.sync_phrase));
  const bool dictionary = app.sync_dictionary && adw_switch_row_get_active(ADW_SWITCH_ROW(app.sync_dictionary));
  const int mask = (app.sync_functions & ~7) | (clipboard ? 1 : 0) | (phrase ? 2 : 0) | (dictionary ? 4 : 0);
  app.sync_updating = true;
  set_sync_switches_sensitive(false);

  GtkWidget *clipboard_switch = app.sync_clipboard ? g_object_ref(app.sync_clipboard) : nullptr;
  GtkWidget *phrase_switch = app.sync_phrase ? g_object_ref(app.sync_phrase) : nullptr;
  GtkWidget *dictionary_switch = app.sync_dictionary ? g_object_ref(app.sync_dictionary) : nullptr;
  GtkWidget *status = app.sync_status_row ? g_object_ref(app.sync_status_row) : nullptr;

  run_account({"set-functions", std::to_string(app.sync_group), std::to_string(mask)},
              [mask, clipboard_switch, phrase_switch, dictionary_switch, status](AccountReply reply) {
                if (account_ok(reply)) {
                  app.sync_functions = mask;
                  write_setting_bool("device_clipboard_sync", (mask & 1) != 0);
                  write_setting_bool("device_phrase_sync", (mask & 2) != 0);
                  write_setting_bool("device_dictionary_sync", (mask & 4) != 0);
                  if (status)
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(status), kSyncSettingsUpdated);
                  run_account({"sync-now", "--refresh-state"}, [](AccountReply) {});
                } else {
                  app.sync_updating = true;
                  if (clipboard_switch)
                    adw_switch_row_set_active(ADW_SWITCH_ROW(clipboard_switch), (app.sync_functions & 1) != 0);
                  if (phrase_switch)
                    adw_switch_row_set_active(ADW_SWITCH_ROW(phrase_switch), (app.sync_functions & 2) != 0);
                  if (dictionary_switch)
                    adw_switch_row_set_active(ADW_SWITCH_ROW(dictionary_switch), (app.sync_functions & 4) != 0);
                  app.sync_updating = false;
                  if (status)
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(status),
                                                reply.error.empty() ? kSyncSettingsUpdateFailed : reply.error.c_str());
                }
                app.sync_updating = false;
                for (auto *control : {clipboard_switch, phrase_switch, dictionary_switch}) {
                  if (control) {
                    gtk_widget_set_sensitive(control, TRUE);
                    g_object_unref(control);
                  }
                }
                if (status)
                  g_object_unref(status);
              });
}

GtkWidget *sync_switch_row(const char *title, const char *subtitle, bool value, GtkWidget **out_control) {
  GtkWidget *row = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle && *subtitle)
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_switch_row_set_active(ADW_SWITCH_ROW(row), value);
  if (out_control)
    *out_control = row;
  g_signal_connect(row, "notify::active", G_CALLBACK(on_sync_switch), nullptr);
  return row;
}

void add_device_group(GtkWidget *group) {
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(app.devices_page), ADW_PREFERENCES_GROUP(group));
  app.device_groups.push_back(group);
}

void refresh_devices_page() {
  if (!app.devices_page)
    return;
  for (auto *group : app.device_groups)
    adw_preferences_page_remove(ADW_PREFERENCES_PAGE(app.devices_page), ADW_PREFERENCES_GROUP(group));
  app.device_groups.clear();
  app.sync_clipboard = app.sync_phrase = app.sync_dictionary = nullptr;
  app.sync_status_row = nullptr;
  app.sync_group = 0;
  app.sync_functions = 0;

  auto settings = read_settings();
  if (setting_bool(settings.get(), "standalone", false)) {
    GtkWidget *group = make_group(kDevicesGroupTitle);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group),
                              action_row(kStandaloneDeviceTitle, kStandaloneDeviceSubtitle));
    add_device_group(group);
    return;
  }

  gchar *sync_path = g_build_filename(g_get_user_data_dir(), "wetypex", "state", "sync-state.json", nullptr);
  auto sync = read_json_file(sync_path);
  g_free(sync_path);
  const int group_id = setting_int(sync.get(), "group_id", 0);
  const int functions = setting_int(sync.get(), "func_switch", 0);

  if (group_id <= 0) {
    GtkWidget *group = make_group(kDevicesGroupTitle, kDevicesNotPairedDescription);
    GtkWidget *row = action_row(kPairDeviceTitle, kPairDeviceSubtitle);
    GtkWidget *button = gtk_button_new_with_label(kPairActionLabel);
    gtk_widget_add_css_class(button, "suggested-action");
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
                       if (app.window)
                         show_pairing_window(GTK_WINDOW(app.window));
                     }),
                     nullptr);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), button);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    add_device_group(group);
    return;
  }

  app.sync_group = group_id;
  app.sync_functions = functions;

  GtkWidget *sync_group = make_group(kGroupSyncFeatures);
  adw_preferences_group_add(
      ADW_PREFERENCES_GROUP(sync_group),
      sync_switch_row(kSyncClipboardTitle, kSyncClipboardSubtitle, (functions & 1) != 0, &app.sync_clipboard));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(sync_group), sync_switch_row(kSyncPhraseTitle, kSyncPhraseSubtitle,
                                                                               (functions & 2) != 0, &app.sync_phrase));
  adw_preferences_group_add(
      ADW_PREFERENCES_GROUP(sync_group),
      sync_switch_row(kSyncDictionaryTitle, kSyncDictionarySubtitle, (functions & 4) != 0, &app.sync_dictionary));

  GtkWidget *sync_now_row = action_row(kSyncNowTitle, kSyncNowSubtitle);
  GtkWidget *sync_now = gtk_button_new_with_label(kSyncNowActionLabel);
  gtk_widget_set_valign(sync_now, GTK_ALIGN_CENTER);
  g_signal_connect(sync_now, "clicked", G_CALLBACK(+[](GtkButton *button, gpointer) {
                     gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
                     g_object_ref(button);
                     run_account({"sync-now"}, [button](AccountReply reply) {
                       gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
                       set_sync_status(account_ok(reply)
                                           ? kSyncRequested
                                           : (reply.error.empty() ? kSyncRequestFailed : reply.error.c_str()));
                       g_object_unref(button);
                     });
                   }),
                   nullptr);
  adw_action_row_add_suffix(ADW_ACTION_ROW(sync_now_row), sync_now);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(sync_group), sync_now_row);

  app.sync_status_row = action_row(kSyncStatusTitle, kSyncStatusDescription);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(sync_group), app.sync_status_row);
  add_device_group(sync_group);

  GtkWidget *manage_group = make_group(kGroupDeviceManagement);
  GtkWidget *associate = action_row(kAssociateDeviceTitle, kAssociateDeviceSubtitle);
  GtkWidget *associate_button = gtk_button_new_with_label(kPairActionLabel);
  gtk_widget_add_css_class(associate_button, "suggested-action");
  gtk_widget_set_valign(associate_button, GTK_ALIGN_CENTER);
  g_signal_connect(associate_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
                     if (app.window)
                       show_pairing_window(GTK_WINDOW(app.window));
                   }),
                   nullptr);
  adw_action_row_add_suffix(ADW_ACTION_ROW(associate), associate_button);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(manage_group), associate);
  add_device_group(manage_group);

  GtkWidget *device_group = make_group(kGroupMyDevices);
  std::vector<DeviceEntry> entries;
  auto *devices = wire::get(sync.get(), "devices");
  if (devices && json_object_is_type(devices, json_type_array)) {
    for (size_t i = 0; i < json_object_array_length(devices); ++i) {
      auto *item = json_object_array_get_idx(devices, i);
      if (!item || !json_object_is_type(item, json_type_object))
        continue;
      const std::string name = wire::str(item, "name");
      if (name.empty())
        continue;
      DeviceEntry entry;
      entry.name = name;
      entry.uin = wire::number(item, "uin");
      entry.subtitle = platform_name(setting_int(item, "platform", 0));
      const std::string version = wire::str(item, "client_version");
      if (!version.empty())
        entry.subtitle += kDeviceSubtitleSeparator + version;
      entries.push_back(std::move(entry));
    }
  }
  const int64_t self_uin = wire::number(sync.get(), "self_uin");
  bool local_found = false;
  if (self_uin != 0) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->uin != self_uin)
        continue;
      it->subtitle += kLocalDeviceBadge;
      std::rotate(entries.begin(), it, it + 1);
      local_found = true;
      break;
    }
  }
  if (!local_found) {
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(device_group),
                              action_row(local_device_name().c_str(), kLocalDeviceSubtitle));
  }
  for (const auto &entry : entries) {
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(device_group),
                              action_row(entry.name.c_str(), entry.subtitle.c_str()));
  }
  add_device_group(device_group);
}

GtkWidget *build_input_page() {
  auto settings = read_settings();
  GtkWidget *page = adw_preferences_page_new();

  std::string mode = setting_string(settings.get(), "input_mode");
  if (mode.empty())
    mode = setting_int(settings.get(), "keyboard", 0) == 5 ? "wubi" : "pinyin";
  if (mode != "pinyin" && mode != "double_pinyin" && mode != "wubi")
    mode = "pinyin";

  const std::vector<std::string> &schemes = kDoublePinyinSchemes;
  const std::vector<std::string> &wubi_solutions = kWubiSolutions;

  GtkWidget *pinyin = gtk_check_button_new();
  GtkWidget *double_pinyin = gtk_check_button_new();
  GtkWidget *wubi = gtk_check_button_new();
  gtk_accessible_update_property(GTK_ACCESSIBLE(pinyin), GTK_ACCESSIBLE_PROPERTY_LABEL, kInputPinyinTitle, -1);
  gtk_accessible_update_property(GTK_ACCESSIBLE(double_pinyin), GTK_ACCESSIBLE_PROPERTY_LABEL, kInputDoublePinyinTitle,
                                 -1);
  gtk_accessible_update_property(GTK_ACCESSIBLE(wubi), GTK_ACCESSIBLE_PROPERTY_LABEL, kInputWubiTitle, -1);
  gtk_check_button_set_group(GTK_CHECK_BUTTON(double_pinyin), GTK_CHECK_BUTTON(pinyin));
  gtk_check_button_set_group(GTK_CHECK_BUTTON(wubi), GTK_CHECK_BUTTON(pinyin));

  GtkWidget *active = mode == "wubi" ? wubi : mode == "double_pinyin" ? double_pinyin : pinyin;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(active), TRUE);
  for (auto *button : {pinyin, double_pinyin, wubi}) {
    const char *value = button == wubi ? "wubi" : button == double_pinyin ? "double_pinyin" : "pinyin";
    g_object_set_data(G_OBJECT(button), "input-mode", const_cast<char *>(value));
    g_signal_connect(button, "toggled", G_CALLBACK(on_mode_toggled), nullptr);
  }

  GtkWidget *scheme_group = make_group(kGroupInputScheme);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(scheme_group), mode_row(kInputPinyinTitle, GTK_CHECK_BUTTON(pinyin)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(scheme_group),
                            scheme_combo_row(kInputDoublePinyinTitle, "double_pinyin_scheme",
                                             GTK_CHECK_BUTTON(double_pinyin), schemes,
                                             setting_int(settings.get(), "double_pinyin_scheme", 0)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(scheme_group),
                            scheme_combo_row(kInputWubiTitle, "wubi_solution", GTK_CHECK_BUTTON(wubi), wubi_solutions,
                                             setting_int(settings.get(), "wubi_solution", 0)));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(scheme_group));

  GtkWidget *candidate_group = make_group(kGroupCandidates);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(candidate_group),
                            spin_row(kPageSizeTitle, kPageSizeSubtitle, "page_size", 3, 9,
                                     std::clamp(setting_int(settings.get(), "page_size", 5), 3, 9)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(candidate_group),
                            switch_row(kVerticalCandidatesTitle, kVerticalCandidatesSubtitle, "vertical",
                                       setting_bool(settings.get(), "vertical", false)));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(candidate_group));

  GtkWidget *behavior_group = make_group(kGroupBehavior);
  const std::string language = setting_string(settings.get(), "default_language", "chinese");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            string_combo_row(kDefaultLanguageTitle, nullptr, "default_language", kDefaultLanguageLabels,
                                             {"chinese", "english"}, language == "english" ? 1 : 0));
  adw_preferences_group_add(
      ADW_PREFERENCES_GROUP(behavior_group),
      switch_row(kShiftSwitchTitle, nullptr, "shift_switch", setting_bool(settings.get(), "shift_switch", true)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            switch_row(kSmartInputTitle, kSmartInputSubtitle, "smart_input",
                                       setting_bool(settings.get(), "smart_input", true)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            switch_row(kEmojiRecommendTitle, kEmojiRecommendSubtitle, "emoji_recommend",
                                       setting_bool(settings.get(), "emoji_recommend", true)));
  const std::string punctuation_mode = setting_string(settings.get(), "punctuation_mode");
  const int punctuation_index = punctuation_mode == "fullwidth" ? 1 : (punctuation_mode == "halfwidth" ? 2 : 0);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            string_combo_row(kChinesePunctuationTitle, kChinesePunctuationSubtitle, "punctuation_mode",
                                             kPunctuationModes, {"auto", "fullwidth", "halfwidth"}, punctuation_index));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            switch_row(kSlashPunctuationTitle, kSlashPunctuationSubtitle, "slash_punctuation",
                                       setting_bool(settings.get(), "slash_punctuation", true)));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior_group),
                            switch_row(kStandaloneTitle, kStandaloneSubtitle, "standalone",
                                       setting_bool(settings.get(), "standalone", false)));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(behavior_group));

  GtkWidget *advanced_group = make_group(kGroupAdvanced);
  GtkWidget *wubi_expander = adw_expander_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wubi_expander), kWubiOptionsTitle);
  add_switch_rows(wubi_expander,
                  {{kWubiPinyinMixLabel, "wubi_pinyin"},
                   {kWubiUniqueCommitLabel, "wubi_unique_commit"},
                   {kWubiNextCommitLabel, "wubi_next_commit"},
                   {kWubiWildcardCommentLabel, "wubi_wildcard_comment"}},
                  settings.get());
  g_wubi_options = wubi_expander;
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(advanced_group), wubi_expander);

  GtkWidget *emoji_expander = adw_expander_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(emoji_expander), kEmojiTypesTitle);
  add_switch_rows(
      emoji_expander,
      {{kEmojiNormalLabel, "normal_emoji"}, {kEmojiKaomojiLabel, "kaomoji"}, {kEmojiSymbolLabel, "symbol_emoji"}},
      settings.get());
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(advanced_group), emoji_expander);

  GtkWidget *fuzzy_expander = adw_expander_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fuzzy_expander), kFuzzyPinyinTitle);
  add_switch_rows(fuzzy_expander,
                  {{kFuzzyNlLabel, "fuzzy_nl"},
                   {kFuzzyRlLabel, "fuzzy_rl"},
                   {kFuzzyHfLabel, "fuzzy_hf"},
                   {kFuzzyGkLabel, "fuzzy_gk"},
                   {kFuzzyAnAngLabel, "fuzzy_an_ang"},
                   {kFuzzyIanIangLabel, "fuzzy_ian_iang"},
                   {kFuzzyUanUangLabel, "fuzzy_uan_uang"},
                   {kFuzzyCChLabel, "fuzzy_c_ch"},
                   {kFuzzySShLabel, "fuzzy_s_sh"},
                   {kFuzzyZZhLabel, "fuzzy_z_zh"},
                   {kFuzzyHuiFeiLabel, "fuzzy_hui_fei"},
                   {kFuzzyEnEngLabel, "fuzzy_en_eng"},
                   {kFuzzyInIngLabel, "fuzzy_in_ing"},
                   {kFuzzyOnOngLabel, "fuzzy_on_ong"},
                   {kFuzzyHuangWangLabel, "fuzzy_huang_wang"},
                   {kFuzzyUnOngLabel, "fuzzy_un_ong"},
                   {kFuzzyUnIongLabel, "fuzzy_un_iong"},
                   {kFuzzyAnAiLabel, "fuzzy_an_ai"},
                   {kFuzzyEngOngLabel, "fuzzy_eng_ong"}},
                  settings.get());
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(advanced_group), fuzzy_expander);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(advanced_group));

  update_mode_sensitivity(mode);
  return page;
}

GtkWidget *build_phrases_page() {
  GtkWidget *page = adw_preferences_page_new();
  GtkWidget *group = make_group(kPhrasesGroupTitle);
  app.phrase_group = group;

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
  gtk_widget_add_css_class(refresh, "flat");
  gtk_widget_set_tooltip_text(refresh, kRefreshTooltip);
  g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) { reload_phrases(); }), nullptr);
  GtkWidget *add = gtk_button_new_from_icon_name("list-add-symbolic");
  gtk_widget_add_css_class(add, "flat");
  gtk_widget_set_tooltip_text(add, kAddPhraseTooltip);
  g_signal_connect(add, "clicked", G_CALLBACK(on_add_phrase), nullptr);
  gtk_box_append(GTK_BOX(header), refresh);
  gtk_box_append(GTK_BOX(header), add);
  adw_preferences_group_set_header_suffix(ADW_PREFERENCES_GROUP(group), header);

  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  return page;
}

GtkWidget *build_devices_page() {
  GtkWidget *page = adw_preferences_page_new();
  app.devices_page = page;
  refresh_devices_page();
  return page;
}

GtkWidget *build_about_page() {
  GtkWidget *page = adw_preferences_page_new();

  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_halign(hero, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(hero, 12);
  gtk_widget_set_margin_bottom(hero, 12);

  GtkWidget *icon = gtk_image_new_from_icon_name("wetypex");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 96);
  gtk_box_append(GTK_BOX(hero), icon);

  GtkWidget *name = gtk_label_new(kBrandName);
  gtk_widget_add_css_class(name, "title-1");
  gtk_box_append(GTK_BOX(hero), name);

  GtkWidget *version = gtk_label_new("");
  gtk_widget_add_css_class(version, "dim-label");
  try {
    const auto installed = get_installed_version();
    gtk_label_set_text(GTK_LABEL(version), installed.c_str());
  } catch (const std::exception &error) {
    gtk_label_set_text(GTK_LABEL(version), kVersionUnavailable);
    g_warning("Unable to query installed WeTypeX version: %s", error.what());
  }
  gtk_box_append(GTK_BOX(hero), version);

  GtkWidget *hero_group = adw_preferences_group_new();
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(hero_group), hero);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(hero_group));

  GtkWidget *update = action_row(kUpdateTitle);
  GtkWidget *update_button = gtk_button_new_with_label(kUpdateActionLabel);
  gtk_widget_set_valign(update_button, GTK_ALIGN_CENTER);
  setup_update_check(ADW_ACTION_ROW(update), GTK_BUTTON(update_button));
  adw_action_row_add_suffix(ADW_ACTION_ROW(update), update_button);

  GtkWidget *developer = action_row(kDeveloperTitle, kDeveloperName);
  GtkWidget *license = action_row(kLicenseTitle, kLicenseValue);

  GtkWidget *website = action_row(kWebsiteTitle, kWebsite);
  GtkWidget *website_button = gtk_link_button_new_with_label(kWebsite, kOpenActionLabel);
  gtk_widget_set_valign(website_button, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(ADW_ACTION_ROW(website), website_button);

  GtkWidget *feedback = action_row(kFeedbackTitle, kFeedbackSubtitle);
  GtkWidget *feedback_button = gtk_link_button_new_with_label(kIssues, kOpenActionLabel);
  gtk_widget_set_valign(feedback_button, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(ADW_ACTION_ROW(feedback), feedback_button);

  GtkWidget *comments = gtk_label_new(kAboutComments);
  gtk_label_set_justify(GTK_LABEL(comments), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap(GTK_LABEL(comments), TRUE);
  gtk_widget_set_halign(comments, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(comments, "dim-label");

  GtkWidget *group = adw_preferences_group_new();
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), update);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), developer);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), license);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), website);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), feedback);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), comments);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  return page;
}

void on_visible_page_changed(GObject *object, GParamSpec *, gpointer) {
  const char *name = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(object));
  if (!name)
    return;
  if (std::strcmp(name, "phrases") == 0 && !app.phrases_loaded) {
    app.phrases_loaded = true;
    reload_phrases();
  }
  if (std::strcmp(name, "devices") == 0 && !app.sync_updating)
    refresh_devices_page();
}

GtkWidget *create_window() {
  GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app.application));
  gtk_window_set_title(GTK_WINDOW(window), kAppName);
  gtk_window_set_default_size(GTK_WINDOW(window), 800, 800);
  gtk_window_set_default_icon_name("wetypex");

  GtkWidget *toolbar = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *switcher = adw_view_switcher_new();
  app.view_stack = adw_view_stack_new();
  gtk_widget_set_vexpand(app.view_stack, TRUE);
  adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(app.view_stack));
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

  struct PageInfo {
    const char *name;
    const char *title;
    const char *icon;
    GtkWidget *(*create)();
  };
  const PageInfo pages[] = {
      {"input", kPageInputTitle, "input-keyboard-symbolic", build_input_page},
      {"phrases", kPagePhrasesTitle, "user-bookmarks-symbolic", build_phrases_page},
      {"devices", kPageDevicesTitle, "network-workgroup-symbolic", build_devices_page},
      {"about", kPageAboutTitle, "help-about-symbolic", build_about_page},
  };
  for (const auto &page : pages) {
    auto *stack_page = adw_view_stack_add_titled(ADW_VIEW_STACK(app.view_stack), page.create(), page.name, page.title);
    adw_view_stack_page_set_icon_name(stack_page, page.icon);
  }
  g_signal_connect(app.view_stack, "notify::visible-child-name", G_CALLBACK(on_visible_page_changed), nullptr);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), app.view_stack);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar);
  return window;
}

void on_window_destroy(GtkWidget *, gpointer) {
  app.window = nullptr;
  app.view_stack = nullptr;
  app.standalone_switch = nullptr;
  app.devices_page = nullptr;
  app.sync_status_row = nullptr;
  app.sync_clipboard = app.sync_phrase = app.sync_dictionary = nullptr;
  app.phrase_group = nullptr;
  app.phrase_rows.clear();
  app.device_groups.clear();
  app.phrases_loaded = false;
}

void on_activate(GApplication *, gpointer) {
  if (!app.window) {
    app.window = create_window();
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), nullptr);
  }
  gtk_window_present(GTK_WINDOW(app.window));
}

} // namespace

int main(int argc, char **argv) {
  g_set_prgname("wetypex-settings");
  g_set_application_name(kAppName);

  AdwApplication *application = adw_application_new(kAppID, static_cast<GApplicationFlags>(0));
  app.application = application;
  g_signal_connect(application, "activate", G_CALLBACK(on_activate), nullptr);

  const int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}
