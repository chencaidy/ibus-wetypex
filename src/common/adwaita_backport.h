#pragma once

#include <adwaita.h>

#if GTK_CHECK_VERSION(4, 8, 0)
static inline void wetypex_picture_set_content_fit_contain(GtkPicture *picture) {
  gtk_picture_set_content_fit(picture, GTK_CONTENT_FIT_CONTAIN);
}
#else
static inline void wetypex_picture_set_content_fit_contain(GtkPicture *picture) {
  gtk_picture_set_keep_aspect_ratio(picture, TRUE);
}
#endif

using WetypexFileSelectionCallback = void (*)(GListModel *files, gpointer user_data);

#if GTK_CHECK_VERSION(4, 10, 0)
struct WetypexFileSelectionContext {
  WetypexFileSelectionCallback callback;
  gpointer user_data;
  gboolean select_folder;
};

static inline void wetypex_file_dialog_complete(GObject *source, GAsyncResult *result, gpointer user_data) {
  auto *context = static_cast<WetypexFileSelectionContext *>(user_data);
  GError *error = nullptr;
  GListModel *files = nullptr;
  if (context->select_folder) {
    GFile *folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
    if (folder) {
      GListStore *selected = g_list_store_new(G_TYPE_FILE);
      g_list_store_append(selected, folder);
      files = G_LIST_MODEL(selected);
      g_object_unref(folder);
    }
  } else {
    files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);
  }
  if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning("File chooser failed: %s", error->message);
  context->callback(files, context->user_data);
  g_clear_object(&files);
  g_clear_error(&error);
  g_free(context);
}

static inline GtkFileDialog *wetypex_file_dialog_new(const char *title, const char *accept_label) {
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, title);
  gtk_file_dialog_set_accept_label(dialog, accept_label);
  gtk_file_dialog_set_modal(dialog, TRUE);
  return dialog;
}

static inline void wetypex_file_dialog_open_multiple(GtkWindow *parent, const char *title, const char *accept_label,
                                                     const char *cancel_label, WetypexFileSelectionCallback callback,
                                                     gpointer user_data) {
  (void)cancel_label;
  GtkFileDialog *dialog = wetypex_file_dialog_new(title, accept_label);
  auto *context = g_new(WetypexFileSelectionContext, 1);
  *context = {callback, user_data, FALSE};
  gtk_file_dialog_open_multiple(dialog, parent, nullptr, wetypex_file_dialog_complete, context);
  g_object_unref(dialog);
}

static inline void wetypex_file_dialog_select_folder(GtkWindow *parent, const char *title, const char *accept_label,
                                                     const char *cancel_label, WetypexFileSelectionCallback callback,
                                                     gpointer user_data) {
  (void)cancel_label;
  GtkFileDialog *dialog = wetypex_file_dialog_new(title, accept_label);
  auto *context = g_new(WetypexFileSelectionContext, 1);
  *context = {callback, user_data, TRUE};
  gtk_file_dialog_select_folder(dialog, parent, nullptr, wetypex_file_dialog_complete, context);
  g_object_unref(dialog);
}
#else
struct WetypexFileSelectionContext {
  WetypexFileSelectionCallback callback;
  gpointer user_data;
};

static inline void wetypex_file_chooser_response(GtkNativeDialog *dialog, int response, gpointer user_data) {
  auto *context = static_cast<WetypexFileSelectionContext *>(user_data);
  GListModel *files = nullptr;
  if (response == GTK_RESPONSE_ACCEPT)
    files = gtk_file_chooser_get_files(GTK_FILE_CHOOSER(dialog));
  context->callback(files, context->user_data);
  g_clear_object(&files);
  g_object_unref(dialog);
  g_free(context);
}

static inline void wetypex_file_chooser_open(GtkWindow *parent, const char *title, const char *accept_label,
                                             const char *cancel_label, GtkFileChooserAction action,
                                             gboolean select_multiple, WetypexFileSelectionCallback callback,
                                             gpointer user_data) {
  GtkFileChooserNative *dialog = gtk_file_chooser_native_new(title, parent, action, accept_label, cancel_label);
  if (select_multiple)
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
  auto *context = g_new(WetypexFileSelectionContext, 1);
  *context = {callback, user_data};
  g_signal_connect(dialog, "response", G_CALLBACK(wetypex_file_chooser_response), context);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static inline void wetypex_file_dialog_open_multiple(GtkWindow *parent, const char *title, const char *accept_label,
                                                     const char *cancel_label, WetypexFileSelectionCallback callback,
                                                     gpointer user_data) {
  wetypex_file_chooser_open(parent, title, accept_label, cancel_label, GTK_FILE_CHOOSER_ACTION_OPEN, TRUE, callback,
                            user_data);
}

static inline void wetypex_file_dialog_select_folder(GtkWindow *parent, const char *title, const char *accept_label,
                                                     const char *cancel_label, WetypexFileSelectionCallback callback,
                                                     gpointer user_data) {
  wetypex_file_chooser_open(parent, title, accept_label, cancel_label, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, FALSE,
                            callback, user_data);
}
#endif

#if !ADW_CHECK_VERSION(1, 4, 0)
#define ADW_SWITCH_ROW(obj) (obj)
#define ADW_SPIN_ROW(obj) (obj)
#define ADW_TOOLBAR_VIEW(obj) (obj)

static inline GtkWidget *wetypex_switch_row_new() {
  GtkWidget *row = adw_action_row_new();
  GtkWidget *toggle = gtk_switch_new();
  gtk_widget_set_halign(toggle, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(row), "wetypex-switch", toggle);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), toggle);
  return row;
}

static inline gboolean wetypex_switch_row_get_active(GObject *row) {
  auto *toggle = static_cast<GtkWidget *>(g_object_get_data(row, "wetypex-switch"));
  return toggle ? gtk_switch_get_active(GTK_SWITCH(toggle)) : FALSE;
}

static inline void wetypex_switch_row_set_active(GObject *row, gboolean active) {
  auto *toggle = static_cast<GtkWidget *>(g_object_get_data(row, "wetypex-switch"));
  if (toggle)
    gtk_switch_set_active(GTK_SWITCH(toggle), active);
}

static inline GtkWidget *wetypex_spin_row_new_with_range(double minimum, double maximum, double step) {
  GtkWidget *row = adw_action_row_new();
  GtkWidget *spin = gtk_spin_button_new_with_range(minimum, maximum, step);
  gtk_widget_set_halign(spin, GTK_ALIGN_END);
  gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(row), "wetypex-spin", spin);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), spin);
  return row;
}

static inline double wetypex_spin_row_get_value(GObject *row) {
  auto *spin = static_cast<GtkWidget *>(g_object_get_data(row, "wetypex-spin"));
  return spin ? gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) : 0.0;
}

static inline void wetypex_spin_row_set_value(GObject *row, double value) {
  auto *spin = static_cast<GtkWidget *>(g_object_get_data(row, "wetypex-spin"));
  if (spin)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
}

static inline GtkWidget *wetypex_toolbar_view_new() { return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); }

static inline void adw_toolbar_view_add_top_bar(GtkWidget *toolbar, GtkWidget *bar) {
  gtk_box_append(GTK_BOX(toolbar), bar);
}

static inline void adw_toolbar_view_set_content(GtkWidget *toolbar, GtkWidget *content) {
  auto *old_content = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(toolbar), "wetypex-content"));
  if (old_content)
    gtk_box_remove(GTK_BOX(toolbar), old_content);
  if (content) {
    g_object_set_data_full(G_OBJECT(toolbar), "wetypex-content", g_object_ref(content), g_object_unref);
    gtk_box_append(GTK_BOX(toolbar), content);
  }
}

static inline void adw_toolbar_view_add_bottom_bar(GtkWidget *toolbar, GtkWidget *bar) {
  gtk_box_append(GTK_BOX(toolbar), bar);
}

static inline GtkWidget *wetypex_entry_row_new() {
  GtkWidget *row = adw_action_row_new();
  GtkWidget *entry = gtk_entry_new();
  gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(row), "wetypex-entry", entry);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), entry);
  return row;
}

#define adw_switch_row_new() wetypex_switch_row_new()
#define adw_switch_row_get_active(obj) wetypex_switch_row_get_active(G_OBJECT(obj))
#define adw_switch_row_set_active(obj, value) wetypex_switch_row_set_active(G_OBJECT(obj), value)
#define adw_spin_row_new_with_range(minimum, maximum, step) wetypex_spin_row_new_with_range(minimum, maximum, step)
#define adw_spin_row_get_value(obj) wetypex_spin_row_get_value(G_OBJECT(obj))
#define adw_spin_row_set_value(obj, value) wetypex_spin_row_set_value(G_OBJECT(obj), value)
#define adw_toolbar_view_new() wetypex_toolbar_view_new()
#define adw_entry_row_new() wetypex_entry_row_new()
#endif

static inline GtkWidget *wetypex_entry_row_get_entry(GtkWidget *row) {
#if ADW_CHECK_VERSION(1, 4, 0)
  return row;
#else
  return static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(row), "wetypex-entry"));
#endif
}
