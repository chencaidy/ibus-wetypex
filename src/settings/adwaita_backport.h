#pragma once

#include <adwaita.h>

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
