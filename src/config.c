#include "config.h"

#include <glib.h>
#include <stdio.h>

#define CROW_CONFIG_DIR  "crow"
#define CROW_CONFIG_FILE "config.ini"
#define CROW_CONFIG_GROUP "General"
#define CROW_CONFIG_KEY   "ggst_path"

/**
 * Build the full path to the config file: ~/.config/crow/config.ini
 * Respects $XDG_CONFIG_HOME via g_get_user_config_dir().
 * Returns a newly-allocated string — caller must g_free().
 */
static gchar *get_config_path(void) {
    return g_build_filename(g_get_user_config_dir(),
                            CROW_CONFIG_DIR,
                            CROW_CONFIG_FILE,
                            NULL);
}

gchar *crow_config_load(void) {
    gchar *config_path = get_config_path();
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;
    gchar *value = NULL;

    if (!g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_NONE, &error)) {
        /* File missing on first run is expected — not an error */
        if (error->domain != G_FILE_ERROR || error->code != G_FILE_ERROR_NOENT) {
            fprintf(stderr, "crow: failed to load config: %s\n", error->message);
        }
        g_error_free(error);
        g_key_file_free(keyfile);
        g_free(config_path);
        return NULL;
    }

    value = g_key_file_get_string(keyfile, CROW_CONFIG_GROUP, CROW_CONFIG_KEY, &error);
    if (error) {
        /* Key missing — treated as "not configured yet" */
        g_error_free(error);
        value = NULL;
    }

    /* Treat empty string as not set */
    if (value && value[0] == '\0') {
        g_free(value);
        value = NULL;
    }

    g_key_file_free(keyfile);
    g_free(config_path);

    return value;
}

void crow_config_save(const gchar *ggst_path) {
    gchar *config_path = get_config_path();
    gchar *config_dir = g_path_get_dirname(config_path);
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;

    /* Create ~/.config/crow/ if it doesn't exist */
    if (g_mkdir_with_parents(config_dir, 0755) != 0) {
        fprintf(stderr, "crow: failed to create config directory: %s\n", config_dir);
        g_free(config_dir);
        g_free(config_path);
        g_key_file_free(keyfile);
        return;
    }

    /* Load existing file to preserve other keys (future-proofing) */
    g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_NONE, NULL);

    g_key_file_set_string(keyfile, CROW_CONFIG_GROUP, CROW_CONFIG_KEY, ggst_path);

    if (!g_key_file_save_to_file(keyfile, config_path, &error)) {
        fprintf(stderr, "crow: failed to save config: %s\n", error->message);
        g_error_free(error);
    }

    g_key_file_free(keyfile);
    g_free(config_dir);
    g_free(config_path);
}
