#include "crow_mod.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* ---------- GObject internals ---------- */

struct _CrowMod {
    GObject parent_instance;
    gchar   *name;
    gchar   *path;
    gboolean enabled;
};

enum {
    PROP_0,
    PROP_NAME,
    PROP_PATH,
    PROP_ENABLED,
    N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL };

G_DEFINE_TYPE(CrowMod, crow_mod, G_TYPE_OBJECT)

static void crow_mod_finalize(GObject *object) {
    CrowMod *self = CROW_MOD(object);
    g_free(self->name);
    g_free(self->path);
    G_OBJECT_CLASS(crow_mod_parent_class)->finalize(object);
}

static void crow_mod_get_property(GObject *object, guint prop_id,
                                  GValue *value, GParamSpec *pspec) {
    CrowMod *self = CROW_MOD(object);
    switch (prop_id) {
        case PROP_NAME:    g_value_set_string(value, self->name);    break;
        case PROP_PATH:    g_value_set_string(value, self->path);    break;
        case PROP_ENABLED: g_value_set_boolean(value, self->enabled); break;
        default:           G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void crow_mod_set_property(GObject *object, guint prop_id,
                                  const GValue *value, GParamSpec *pspec) {
    CrowMod *self = CROW_MOD(object);
    switch (prop_id) {
        case PROP_NAME:
            g_free(self->name);
            self->name = g_value_dup_string(value);
            break;
        case PROP_PATH:
            g_free(self->path);
            self->path = g_value_dup_string(value);
            break;
        case PROP_ENABLED:
            self->enabled = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void crow_mod_class_init(CrowModClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize     = crow_mod_finalize;
    object_class->get_property = crow_mod_get_property;
    object_class->set_property = crow_mod_set_property;

    props[PROP_NAME] = g_param_spec_string(
        "name", "Name", "Display name of the mod",
        NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    props[PROP_PATH] = g_param_spec_string(
        "path", "Path", "Absolute path to the mod file",
        NULL, G_PARAM_READWRITE);

    props[PROP_ENABLED] = g_param_spec_boolean(
        "enabled", "Enabled", "Whether the mod is active",
        FALSE, G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPS, props);
}

static void crow_mod_init(CrowMod *self) {
    self->name    = NULL;
    self->path    = NULL;
    self->enabled = FALSE;
}

/* ---------- Public API ---------- */

CrowMod *crow_mod_new(const gchar *name, const gchar *path, gboolean enabled) {
    return g_object_new(CROW_TYPE_MOD,
                        "name", name,
                        "path", path,
                        "enabled", enabled,
                        NULL);
}

const gchar *crow_mod_get_name(CrowMod *self) {
    g_return_val_if_fail(CROW_IS_MOD(self), NULL);
    return self->name;
}

const gchar *crow_mod_get_path(CrowMod *self) {
    g_return_val_if_fail(CROW_IS_MOD(self), NULL);
    return self->path;
}

gboolean crow_mod_get_enabled(CrowMod *self) {
    g_return_val_if_fail(CROW_IS_MOD(self), FALSE);
    return self->enabled;
}

/* ---------- Mod Scanner ---------- */

/**
 * Derive a clean display name from a filename.
 * Strips ".pak.disabled" or ".pak" suffix.
 */
static gchar *derive_display_name(const gchar *filename) {
    if (g_str_has_suffix(filename, ".pak.disabled")) {
        return g_strndup(filename, strlen(filename) - strlen(".pak.disabled"));
    }
    if (g_str_has_suffix(filename, ".pak")) {
        return g_strndup(filename, strlen(filename) - strlen(".pak"));
    }
    return g_strdup(filename);
}

GListStore *crow_mod_scan_directory(const gchar *ggst_path) {
    if (!ggst_path) return NULL;

    gchar *mods_dir = g_build_filename(ggst_path, "RED", "Content", "Paks",
                                       "~mods", NULL);

    /* Create the directory if it doesn't exist (same as Fireseal) */
    if (g_mkdir_with_parents(mods_dir, 0755) != 0) {
        fprintf(stderr, "crow: failed to create mods directory: %s\n", mods_dir);
        g_free(mods_dir);
        return NULL;
    }

    GDir *dir = g_dir_open(mods_dir, 0, NULL);
    if (!dir) {
        fprintf(stderr, "crow: failed to open mods directory: %s\n", mods_dir);
        g_free(mods_dir);
        return NULL;
    }

    GListStore *store = g_list_store_new(CROW_TYPE_MOD);
    const gchar *entry;

    while ((entry = g_dir_read_name(dir)) != NULL) {
        gboolean is_enabled;

        if (g_str_has_suffix(entry, ".pak.disabled")) {
            is_enabled = FALSE;
        } else if (g_str_has_suffix(entry, ".pak")) {
            is_enabled = TRUE;
        } else {
            continue; /* skip .sig and other files */
        }

        gchar *full_path = g_build_filename(mods_dir, entry, NULL);
        gchar *display_name = derive_display_name(entry);

        CrowMod *mod = crow_mod_new(display_name, full_path, is_enabled);
        g_list_store_append(store, mod);

        g_object_unref(mod);
        g_free(full_path);
        g_free(display_name);
    }

    g_dir_close(dir);
    g_free(mods_dir);

    return store;
}

/* ---------- Smart Toggle ---------- */

gboolean crow_mod_set_enabled(CrowMod *self, gboolean enabled) {
    g_return_val_if_fail(CROW_IS_MOD(self), FALSE);

    if (self->enabled == enabled) return TRUE; /* already in desired state */

    const gchar *old_path = self->path;
    gchar *new_pak_path = NULL;

    if (enabled) {
        /* .pak.disabled → .pak */
        if (!g_str_has_suffix(old_path, ".pak.disabled")) return FALSE;
        new_pak_path = g_strndup(old_path,
                                 strlen(old_path) - strlen(".disabled"));
    } else {
        /* .pak → .pak.disabled */
        if (!g_str_has_suffix(old_path, ".pak")) return FALSE;
        new_pak_path = g_strconcat(old_path, ".disabled", NULL);
    }

    /* Rename the .pak file */
    if (g_rename(old_path, new_pak_path) != 0) {
        fprintf(stderr, "crow: failed to rename %s → %s\n", old_path, new_pak_path);
        g_free(new_pak_path);
        return FALSE;
    }

    /* Handle companion .sig file */
    gchar *old_sig_path = NULL;
    gchar *new_sig_path = NULL;

    if (enabled) {
        /* Companion would be .sig.disabled → .sig */
        /* old_path ends with .pak.disabled, derive .sig.disabled */
        gchar *base = g_strndup(old_path,
                                strlen(old_path) - strlen(".pak.disabled"));
        old_sig_path = g_strconcat(base, ".sig.disabled", NULL);
        new_sig_path = g_strconcat(base, ".sig", NULL);
        g_free(base);
    } else {
        /* Companion would be .sig → .sig.disabled */
        gchar *base = g_strndup(old_path,
                                strlen(old_path) - strlen(".pak"));
        old_sig_path = g_strconcat(base, ".sig", NULL);
        new_sig_path = g_strconcat(base, ".sig.disabled", NULL);
        g_free(base);
    }

    if (g_file_test(old_sig_path, G_FILE_TEST_EXISTS)) {
        if (g_rename(old_sig_path, new_sig_path) != 0) {
            fprintf(stderr, "crow: warning: failed to rename sig %s → %s\n",
                    old_sig_path, new_sig_path);
            /* Not a fatal error — the .pak was already renamed */
        }
    }

    g_free(old_sig_path);
    g_free(new_sig_path);

    /* Update internal state */
    g_free(self->path);
    self->path = new_pak_path;
    self->enabled = enabled;

    g_object_notify_by_pspec(G_OBJECT(self), props[PROP_PATH]);
    g_object_notify_by_pspec(G_OBJECT(self), props[PROP_ENABLED]);

    return TRUE;
}
