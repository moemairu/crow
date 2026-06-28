#ifndef CROW_CONFIG_H
#define CROW_CONFIG_H

#include <glib.h>

/**
 * Load the GGST installation directory from ~/.config/crow/config.ini.
 * Returns a newly-allocated string with the path, or NULL if not set.
 * The caller must free the returned string with g_free().
 */
gchar *crow_config_load(void);

/**
 * Save the GGST installation directory to ~/.config/crow/config.ini.
 * Creates the config directory if it does not exist.
 */
void crow_config_save(const gchar *ggst_path);

#endif /* CROW_CONFIG_H */
