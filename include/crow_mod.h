#ifndef CROW_MOD_H
#define CROW_MOD_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CROW_TYPE_MOD (crow_mod_get_type())
G_DECLARE_FINAL_TYPE(CrowMod, crow_mod, CROW, MOD, GObject)

/**
 * Create a new CrowMod instance.
 * @param name    Display name (filename without .pak/.pak.disabled extension)
 * @param path    Full absolute path to the .pak or .pak.disabled file
 * @param enabled TRUE if the mod is active (.pak), FALSE if disabled (.pak.disabled)
 */
CrowMod *crow_mod_new(const gchar *name, const gchar *path, gboolean enabled);

/* Property accessors */
const gchar *crow_mod_get_name(CrowMod *self);
const gchar *crow_mod_get_path(CrowMod *self);
const gchar *crow_mod_get_character(CrowMod *self);
gboolean     crow_mod_get_enabled(CrowMod *self);

/**
 * Scan the ~mods directory and return a GListStore populated with CrowMod objects.
 * @param ggst_path  The GGST installation directory
 * @return A new GListStore, or NULL on error. Caller owns the reference.
 */
GListStore *crow_mod_scan_directory(const gchar *ggst_path);

/**
 * Enable or disable a mod by renaming its .pak (and companion .sig) file.
 * Updates the mod's internal path and enabled properties on success.
 * @return TRUE on success, FALSE on failure.
 */
gboolean crow_mod_set_enabled(CrowMod *self, gboolean enabled);

/**
 * Delete the mod's file(s) from the disk.
 * This deletes the primary file (.pak) and the companion (.sig) if it exists.
 * @return TRUE on success, FALSE on error.
 */
gboolean crow_mod_delete(CrowMod *self);

G_END_DECLS

#endif /* CROW_MOD_H */
