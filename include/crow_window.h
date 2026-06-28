#ifndef CROW_WINDOW_H
#define CROW_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CROW_TYPE_WINDOW (crow_window_get_type())
G_DECLARE_FINAL_TYPE(CrowWindow, crow_window, CROW, WINDOW, GtkApplicationWindow)

/**
 * Create a new CrowWindow.
 * @param app  The GtkApplication instance
 */
CrowWindow *crow_window_new(GtkApplication *app);

G_END_DECLS

#endif /* CROW_WINDOW_H */
