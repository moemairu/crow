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
    GtkWidget   *search_entry;
    GtkWidget   *filter_dropdown;
    GtkWidget   *char_filter_btn;
    GtkWidget   *char_filter_popover;
    GtkWidget   *char_filter_box;
    GtkWidget   *stack;
    GtkWidget   *scrolled_window;
    GtkWidget   *list_view;
    GtkWidget   *empty_box;

    /* Data */
    GListStore         *mod_store;
    GtkFilterListModel *filter_model;
    GtkFilter          *search_filter;
    gchar              *ggst_path;
};

G_DEFINE_TYPE(CrowWindow, crow_window, GTK_TYPE_APPLICATION_WINDOW)

/* ---------- Forward declarations ---------- */

static void crow_window_refresh_mods(CrowWindow *self);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static void on_refresh_clicked(GtkButton *button, gpointer user_data);
static void on_search_changed(GtkSearchEntry *entry, gpointer user_data);
static void on_filter_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
static void on_char_check_toggled(GtkCheckButton *button, gpointer user_data);
static gboolean on_files_dropped(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data);
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

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_box_append(GTK_BOX(box), vbox);

    GtkWidget *label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *char_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(char_label), 0.0f);
    gtk_widget_add_css_class(char_label, "caption");
    gtk_widget_add_css_class(char_label, "dim-label");
    gtk_box_append(GTK_BOX(vbox), char_label);

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
    GtkWidget *vbox = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_first_child(vbox);
    GtkWidget *char_label = gtk_widget_get_last_child(vbox);
    GtkWidget *sw = gtk_widget_get_last_child(box);

    CrowMod *mod = gtk_list_item_get_item(list_item);

    gtk_label_set_text(GTK_LABEL(label), crow_mod_get_name(mod));
    
    const gchar *character = crow_mod_get_character(mod);
    gtk_label_set_text(GTK_LABEL(char_label), character ? character : "Unknown");

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

/* ---------- Search Filter ---------- */

static gboolean mod_search_filter_func(gpointer item, gpointer user_data) {
    CrowWindow *self = CROW_WINDOW(user_data);
    CrowMod *mod = CROW_MOD(item);

    /* Filter by status */
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(self->filter_dropdown));
    if (selected == 1 && !crow_mod_get_enabled(mod)) {
        return FALSE;
    } else if (selected == 2 && crow_mod_get_enabled(mod)) {
        return FALSE;
    }

    /* Filter by character checkbox */
    gboolean has_checked = FALSE;
    gboolean char_matched = FALSE;
    const gchar *mod_char = crow_mod_get_character(mod);
    if (!mod_char) mod_char = "Unknown / Other";

    for (GtkWidget *child = gtk_widget_get_first_child(self->char_filter_box);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        
        if (GTK_IS_CHECK_BUTTON(child)) {
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(child))) {
                has_checked = TRUE;
                const gchar *label = gtk_check_button_get_label(GTK_CHECK_BUTTON(child));
                if (g_strcmp0(mod_char, label) == 0) {
                    char_matched = TRUE;
                }
            }
        }
    }
    
    if (has_checked && !char_matched) {
        return FALSE;
    }

    /* Filter by search text */
    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
    if (!search_text || search_text[0] == '\0') {
        return TRUE;
    }

    const char *mod_name = crow_mod_get_name(mod);
    
    char *lower_name = g_utf8_strdown(mod_name, -1);
    char *lower_search = g_utf8_strdown(search_text, -1);
    
    gboolean match = strstr(lower_name, lower_search) != NULL;
    
    g_free(lower_name);
    g_free(lower_search);
    
    return match;
}

/* ---------- GObject lifecycle ---------- */

static void crow_window_finalize(GObject *object) {
    CrowWindow *self = CROW_WINDOW(object);
    g_clear_pointer(&self->ggst_path, g_free);
    g_clear_object(&self->mod_store);
    g_clear_object(&self->filter_model);
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

    /* Filter Dropdown (right, before refresh) */
    const char *filter_opts[] = {"All Mods", "Enabled", "Disabled", NULL};
    self->filter_dropdown = gtk_drop_down_new_from_strings(filter_opts);
    gtk_widget_set_tooltip_text(self->filter_dropdown, "Filter by status");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(self->header_bar), self->filter_dropdown);
    g_signal_connect(self->filter_dropdown, "notify::selected",
                     G_CALLBACK(on_filter_changed), self);

    /* Character Filter (right, before status filter) */
    self->char_filter_btn = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(self->char_filter_btn), "Characters");
    gtk_widget_set_tooltip_text(self->char_filter_btn, "Filter by Character");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(self->header_bar), self->char_filter_btn);
    
    self->char_filter_popover = gtk_popover_new();
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(self->char_filter_btn), self->char_filter_popover);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_popover_set_child(GTK_POPOVER(self->char_filter_popover), scroll);
    
    self->char_filter_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(self->char_filter_box, 12);
    gtk_widget_set_margin_end(self->char_filter_box, 12);
    gtk_widget_set_margin_top(self->char_filter_box, 12);
    gtk_widget_set_margin_bottom(self->char_filter_box, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->char_filter_box);

    /* Search entry (right, before character filter) */
    self->search_entry = gtk_search_entry_new();
    gtk_widget_set_tooltip_text(self->search_entry, "Search Mods");
    gtk_widget_set_size_request(self->search_entry, 200, -1);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(self->header_bar), self->search_entry);
    g_signal_connect(self->search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), self);

    /* --- Main content: Stack with list view + empty state --- */
    self->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(self->stack, TRUE);
    gtk_widget_set_hexpand(self->stack, TRUE);

    /* List page: header row + scrolled list */
    GtkWidget *list_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Column header row */
    GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(header_row, 12);
    gtk_widget_set_margin_end(header_row, 12);
    gtk_widget_set_margin_top(header_row, 8);
    gtk_widget_set_margin_bottom(header_row, 4);

    GtkWidget *name_header = gtk_label_new("Mod Name");
    gtk_widget_add_css_class(name_header, "dim-label");
    gtk_widget_add_css_class(name_header, "caption");
    gtk_label_set_xalign(GTK_LABEL(name_header), 0.0f);
    gtk_widget_set_hexpand(name_header, TRUE);
    gtk_box_append(GTK_BOX(header_row), name_header);

    GtkWidget *status_header = gtk_label_new("Status");
    gtk_widget_add_css_class(status_header, "dim-label");
    gtk_widget_add_css_class(status_header, "caption");
    gtk_box_append(GTK_BOX(header_row), status_header);

    gtk_box_append(GTK_BOX(list_page), header_row);

    /* Separator under header */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(list_page), sep);

    /* Scrolled window for the list */
    self->scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(self->scrolled_window, TRUE);
    gtk_box_append(GTK_BOX(list_page), self->scrolled_window);

    gtk_stack_add_named(GTK_STACK(self->stack), list_page, "list");

    /* Empty state */
    GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
    
    GtkWidget *empty_icon = gtk_image_new_from_icon_name("folder-open-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
    gtk_widget_add_css_class(empty_icon, "dim-label");
    
    GtkWidget *empty_label = gtk_label_new("No mods installed.");
    gtk_widget_add_css_class(empty_label, "title-2");
    
    gtk_box_append(GTK_BOX(empty_box), empty_icon);
    gtk_box_append(GTK_BOX(empty_box), empty_label);
    
    gtk_stack_add_named(GTK_STACK(self->stack), empty_box, "empty");
    gtk_window_set_child(GTK_WINDOW(self), self->stack);

    /* Show empty state by default */
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "empty");

    /* Initialize store and list view */
    self->mod_store = g_list_store_new(CROW_TYPE_MOD);
    self->ggst_path = NULL;

    /* Search filter & model */
    self->search_filter = GTK_FILTER(gtk_custom_filter_new(
        mod_search_filter_func, self, NULL));
    
    self->filter_model = gtk_filter_list_model_new(
        G_LIST_MODEL(g_object_ref(self->mod_store)), self->search_filter);

    /* --- List View with factory --- */
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), NULL);
    g_signal_connect(factory, "unbind", G_CALLBACK(factory_unbind), NULL);

    GtkNoSelection *selection_model =
        gtk_no_selection_new(G_LIST_MODEL(self->filter_model));

    self->list_view = gtk_list_view_new(GTK_SELECTION_MODEL(selection_model),
                                        factory);
    gtk_widget_add_css_class(self->list_view, "crow-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                  self->list_view);

    /* --- CSS --- */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "listview.crow-list row { "
        "  border-bottom: 1px solid alpha(currentColor, 0.1); "
        "} "
        "listview.crow-list row:last-child { "
        "  border-bottom: none; "
        "} "
        "stack:drop(active) { "
        "  background-color: rgba(40, 120, 240, 0.2); "
        "} "
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* --- Setup main window properties --- */
    gtk_window_set_title(GTK_WINDOW(self), "Crow");
    gtk_window_set_default_size(GTK_WINDOW(self), 700, 500);

    /* Drag-and-drop support (Wayland fallback G_TYPE_STRING) */
    GType dnd_types[2] = { GDK_TYPE_FILE_LIST, G_TYPE_STRING };
    GtkDropTarget *drop_target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    gtk_drop_target_set_gtypes(drop_target, dnd_types, 2);
    g_signal_connect(drop_target, "drop", G_CALLBACK(on_files_dropped), self);
    gtk_widget_add_controller(GTK_WIDGET(self->stack), GTK_EVENT_CONTROLLER(drop_target));

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

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    CrowWindow *self = CROW_WINDOW(user_data);
    gtk_filter_changed(self->search_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void on_filter_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    CrowWindow *self = CROW_WINDOW(user_data);
    gtk_filter_changed(self->search_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void on_char_check_toggled(GtkCheckButton *button, gpointer user_data) {
    (void)button;
    CrowWindow *self = CROW_WINDOW(user_data);
    gtk_filter_changed(self->search_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static gboolean on_files_dropped(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target;
    (void)x;
    (void)y;
    CrowWindow *self = CROW_WINDOW(user_data);

    if (!self->ggst_path) {
        g_printerr("crow: GGST path not set, cannot install dropped mods.\n");
        return FALSE;
    }

    GSList *files = NULL;
    gboolean using_string_uri = FALSE;

    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        GdkFileList *file_list = g_value_get_boxed(value);
        if (file_list) {
            files = gdk_file_list_get_files(file_list);
        }
    } else if (G_VALUE_HOLDS(value, G_TYPE_STRING)) {
        const char *uris = g_value_get_string(value);
        if (uris) {
            gchar **split = g_uri_list_extract_uris(uris);
            for (int i = 0; split && split[i]; i++) {
                files = g_slist_prepend(files, g_file_new_for_uri(split[i]));
                using_string_uri = TRUE;
            }
            g_strfreev(split);
        }
    }

    if (!files) return FALSE;

    gboolean copied_any = FALSE;
    gchar *mods_dir = g_build_filename(self->ggst_path, "RED", "Content", "Paks", "~mods", NULL);
    g_mkdir_with_parents(mods_dir, 0755);

    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src_file = G_FILE(l->data);
        gchar *basename = g_file_get_basename(src_file);
        
        if (!basename) continue;

        if (g_str_has_suffix(basename, ".pak") || 
            g_str_has_suffix(basename, ".sig") ||
            g_str_has_suffix(basename, ".pak.disabled") ||
            g_str_has_suffix(basename, ".sig.disabled")) {
            
            gchar *dest_path = g_build_filename(mods_dir, basename, NULL);
            GFile *dest_file = g_file_new_for_path(dest_path);
            
            GError *error = NULL;
            if (g_file_copy(src_file, dest_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
                g_print("crow: installed %s\n", basename);
                copied_any = TRUE;
            } else {
                g_printerr("crow: failed to copy %s: %s\n", basename, error->message);
                g_error_free(error);
            }
            
            g_object_unref(dest_file);
            g_free(dest_path);
        }
        g_free(basename);
    }

    if (using_string_uri) {
        g_slist_free_full(files, g_object_unref);
    } else {
        /* gdk_file_list_get_files returns a GSList that we must free, and unref the GFiles */
        g_slist_free_full(files, g_object_unref);
    }
    
    g_free(mods_dir);

    if (copied_any) {
        crow_window_refresh_mods(self);
    }

    return TRUE;
}

/* ---------- Mod refresh ---------- */

static void crow_window_refresh_mods(CrowWindow *self) {
    if (!self->ggst_path) return;

    GListStore *new_store = crow_mod_scan_directory(self->ggst_path);
    if (new_store) {
        g_list_store_remove_all(self->mod_store);

        guint n_items = g_list_model_get_n_items(G_LIST_MODEL(new_store));
        for (guint i = 0; i < n_items; i++) {
            gpointer item = g_list_model_get_item(G_LIST_MODEL(new_store), i);
            g_list_store_append(self->mod_store, item);
            g_object_unref(item);
        }

        /* Dynamically populate the character filter checkboxes */
        GtkWidget *child = gtk_widget_get_first_child(self->char_filter_box);
        while (child != NULL) {
            GtkWidget *next = gtk_widget_get_next_sibling(child);
            gtk_box_remove(GTK_BOX(self->char_filter_box), child);
            child = next;
        }

        GHashTable *char_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        for (guint i = 0; i < n_items; i++) {
            CrowMod *mod = CROW_MOD(g_list_model_get_item(G_LIST_MODEL(new_store), i));
            const gchar *chr = crow_mod_get_character(mod);
            if (!chr) chr = "Unknown / Other";
            if (!g_hash_table_lookup(char_set, chr)) {
                g_hash_table_insert(char_set, g_strdup(chr), GINT_TO_POINTER(1));
            }
            g_object_unref(mod);
        }

        GList *keys = g_hash_table_get_keys(char_set);
        keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);

        for (GList *l = keys; l != NULL; l = l->next) {
            const gchar *chr = l->data;
            GtkWidget *chk = gtk_check_button_new_with_label(chr);
            g_signal_connect(chk, "toggled", G_CALLBACK(on_char_check_toggled), self);
            gtk_box_append(GTK_BOX(self->char_filter_box), chk);
        }

        g_list_free(keys);
        g_hash_table_destroy(char_set);
        /* End checkbox population */

        g_object_unref(new_store);

        if (n_items > 0) {
            gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "list");
        } else {
            gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "empty");
        }
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "empty");
    }
}

/* ---------- Public API ---------- */

CrowWindow *crow_window_new(GtkApplication *app) {
    return g_object_new(CROW_TYPE_WINDOW,
                        "application", app,
                        NULL);
}
