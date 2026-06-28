#include <gtk/gtk.h>
#include "config.h"
#include "crow_mod.h"

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    /* Test config + scanner */
    gchar *ggst_path = crow_config_load();
    if (ggst_path) {
        g_print("crow: GGST path: %s\n", ggst_path);

        GListStore *store = crow_mod_scan_directory(ggst_path);
        if (store) {
            guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
            g_print("crow: found %u mod(s)\n", n);
            for (guint i = 0; i < n; i++) {
                CrowMod *mod = g_list_model_get_item(G_LIST_MODEL(store), i);
                g_print("  [%s] %s\n",
                        crow_mod_get_enabled(mod) ? "ON " : "OFF",
                        crow_mod_get_name(mod));
                g_object_unref(mod);
            }
            g_object_unref(store);
        }
        g_free(ggst_path);
    } else {
        g_print("crow: no GGST path configured\n");
    }

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Crow");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.crow.modloader",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
