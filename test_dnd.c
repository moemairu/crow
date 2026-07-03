#include <gtk/gtk.h>

static gboolean on_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data) {
    g_print("DROP RECEIVED!\n");
    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        g_print("Type: GdkFileList\n");
    } else {
        g_print("Type: Unknown (%s)\n", G_VALUE_TYPE_NAME(value));
    }
    return TRUE;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

    GtkWidget *label = gtk_label_new("Drop files here");
    gtk_window_set_child(GTK_WINDOW(window), label);

    GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(target, "drop", G_CALLBACK(on_drop), NULL);
    gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(target));

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.DndTest", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
