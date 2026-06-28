#include "crow_window.h"
#include "config.h"
#include "crow_mod.h"

#include <stdio.h>

/* ---------- CrowWindow definition ---------- */

struct _CrowWindow {
    GtkApplicationWindow parent_instance;

    /* Widgets */
    GtkWidget   *header_bar;
    GtkWidget   *settings_btn;
    GtkWidget   *refresh_btn;
    GtkWidget   *scrolled_window;
    GtkWidget   *list_view;

    /* Data */
    GListStore  *mod_store;
    gchar       *ggst_path;
};

G_DEFINE_TYPE(CrowWindow, crow_window, GTK_TYPE_APPLICATION_WINDOW)

/* ---------- Forward declarations ---------- */

static void crow_window_refresh_mods(CrowWindow *self);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static void on_refresh_clicked(GtkButton *button, gpointer user_data);
static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data);
static void crow_window_prompt_directory(CrowWindow *self);

/* ---------- Switch toggle handler ---------- */

static void on_switch_toggled(GtkSwitch *sw, GParamSpec *pspec,
                              gpointer user_data) {
    (void)pspec;
    CrowMod *mod = CROW_MOD(user_data);
    gboolean desired = gtk_switch_get_active(sw);

    if (desired == crow_mod_get_enabled(mod)) return; /* no change */

    if (!crow_mod_set_enabled(mod, desired)) {
        /* Rename failed — revert the switch without re-triggering */
        gulong handler_id = GPOINTER_TO_UINT(
            g_object_get_data(G_OBJECT(sw), "toggle-handler-id"));
        g_signal_handler_block(sw, handler_id);
        gtk_switch_set_active(sw, crow_mod_get_enabled(mod));
        g_signal_handler_unblock(sw, handler_id);

        fprintf(stderr, "crow: failed to toggle mod: %s\n",
                crow_mod_get_name(mod));
    }
}

/* ---------- ListView Factory Callbacks ---------- */

static void factory_setup(GtkSignalListItemFactory *factory,
                          GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    (void)user_data;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), sw);

    gtk_list_item_set_child(list_item, box);
}

static void factory_bind(GtkSignalListItemFactory *factory,
                         GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    (void)user_data;

    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *label = gtk_widget_get_first_child(box);
    GtkWidget *sw = gtk_widget_get_last_child(box);

    CrowMod *mod = gtk_list_item_get_item(list_item);

    gtk_label_set_text(GTK_LABEL(label), crow_mod_get_name(mod));

    /* Block signal before setting initial state to avoid triggering toggle */
    gulong handler_id = g_signal_connect(sw, "notify::active",
                                         G_CALLBACK(on_switch_toggled), mod);
    g_object_set_data(G_OBJECT(sw), "toggle-handler-id",
                      GUINT_TO_POINTER(handler_id));

    g_signal_handler_block(sw, handler_id);
    gtk_switch_set_active(GTK_SWITCH(sw), crow_mod_get_enabled(mod));
    g_signal_handler_unblock(sw, handler_id);
}

static void factory_unbind(GtkSignalListItemFactory *factory,
                           GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    (void)user_data;

    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *sw = gtk_widget_get_last_child(box);

    gulong handler_id = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(sw), "toggle-handler-id"));
    if (handler_id > 0) {
        g_signal_handler_disconnect(sw, handler_id);
        g_object_set_data(G_OBJECT(sw), "toggle-handler-id", NULL);
    }
}

/* ---------- GObject lifecycle ---------- */

static void crow_window_finalize(GObject *object) {
    CrowWindow *self = CROW_WINDOW(object);
    g_clear_pointer(&self->ggst_path, g_free);
    g_clear_object(&self->mod_store);
    G_OBJECT_CLASS(crow_window_parent_class)->finalize(object);
}

static void crow_window_class_init(CrowWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crow_window_finalize;
}

static void crow_window_init(CrowWindow *self) {
    /* --- Header Bar --- */
    self->header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(self), self->header_bar);

    /* Title */
    GtkWidget *title_label = gtk_label_new("Crow");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(self->header_bar), title_label);

    /* Settings button (left) */
    self->settings_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_set_tooltip_text(self->settings_btn, "Set GGST Directory");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(self->header_bar), self->settings_btn);
    g_signal_connect(self->settings_btn, "clicked",
                     G_CALLBACK(on_settings_clicked), self);

    /* Refresh button (right) */
    self->refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(self->refresh_btn, "Refresh Mods");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(self->header_bar), self->refresh_btn);
    g_signal_connect(self->refresh_btn, "clicked",
                     G_CALLBACK(on_refresh_clicked), self);

    /* --- Main content --- */
    self->scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(self->scrolled_window, TRUE);
    gtk_widget_set_hexpand(self->scrolled_window, TRUE);
    gtk_window_set_child(GTK_WINDOW(self), self->scrolled_window);

    /* Initialize store and list view */
    self->mod_store = g_list_store_new(CROW_TYPE_MOD);
    self->ggst_path = NULL;

    /* --- List View with factory --- */
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), NULL);
    g_signal_connect(factory, "unbind", G_CALLBACK(factory_unbind), NULL);

    GtkNoSelection *selection_model =
        gtk_no_selection_new(G_LIST_MODEL(g_object_ref(self->mod_store)));

    self->list_view = gtk_list_view_new(GTK_SELECTION_MODEL(selection_model),
                                        factory);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                  self->list_view);

    /* Window defaults */
    gtk_window_set_default_size(GTK_WINDOW(self), 800, 500);

    /* Load config and either show mods or prompt for directory */
    self->ggst_path = crow_config_load();
    if (self->ggst_path) {
        crow_window_refresh_mods(self);
    } else {
        /* Defer the dialog to after the window is presented */
        g_idle_add_once((GSourceOnceFunc)crow_window_prompt_directory, self);
    }
}

/* ---------- Directory picker (GtkFileDialog — GTK 4.10+) ---------- */

static void crow_window_prompt_directory(CrowWindow *self) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select GGST Installation Directory");

    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(self), NULL,
                                  on_folder_selected, self);
    g_object_unref(dialog);
}

static void on_folder_selected(GObject *source, GAsyncResult *result,
                                gpointer user_data) {
    CrowWindow *self = CROW_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *folder = gtk_file_dialog_select_folder_finish(dialog, result, &error);
    if (!folder) {
        if (error) {
            /* User cancelled — not an error we need to report */
            if (error->code != GTK_DIALOG_ERROR_DISMISSED) {
                fprintf(stderr, "crow: folder selection error: %s\n",
                        error->message);
            }
            g_error_free(error);
        }
        return;
    }

    gchar *path = g_file_get_path(folder);
    g_object_unref(folder);

    if (path) {
        crow_config_save(path);
        g_free(self->ggst_path);
        self->ggst_path = path;
        crow_window_refresh_mods(self);
    }
}

/* ---------- Button handlers ---------- */

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    crow_window_prompt_directory(CROW_WINDOW(user_data));
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    crow_window_refresh_mods(CROW_WINDOW(user_data));
}

/* ---------- Mod refresh ---------- */

static void crow_window_refresh_mods(CrowWindow *self) {
    if (!self->ggst_path) return;

    GListStore *new_store = crow_mod_scan_directory(self->ggst_path);
    if (!new_store) return;

    /* Replace old store contents */
    g_list_store_remove_all(self->mod_store);

    guint n = g_list_model_get_n_items(G_LIST_MODEL(new_store));
    for (guint i = 0; i < n; i++) {
        CrowMod *mod = g_list_model_get_item(G_LIST_MODEL(new_store), i);
        g_list_store_append(self->mod_store, mod);
        g_object_unref(mod);
    }

    g_object_unref(new_store);

    g_print("crow: loaded %u mod(s)\n", n);
}

/* ---------- Public API ---------- */

CrowWindow *crow_window_new(GtkApplication *app) {
    return g_object_new(CROW_TYPE_WINDOW,
                        "application", app,
                        NULL);
}
