#ifndef CROW_PAK_H
#define CROW_PAK_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * Attempts to analyze a UE4 .pak file to detect which character it modifies.
 * Scans the footer for FPakInfo, reads the IndexOffset, and performs a fast
 * binary search on the index data for "RED/Content/Chara/XXX/".
 * 
 * @param pak_path Absolute path to the .pak file.
 * @return A newly allocated string containing the character's full name, 
 *         or "Unknown / Other" if no character was detected or parsing failed.
 *         Must be freed with g_free().
 */
gchar *crow_pak_detect_character(const gchar *pak_path);

/**
 * Fallback: tries to guess the character based on the mod's file name.
 * 
 * @param name Display name of the mod.
 * @return A newly allocated string with the guessed character name, 
 *         or "Unknown / Other" if guessing fails.
 */
gchar *crow_pak_guess_from_name(const gchar *name);

G_END_DECLS

#endif /* CROW_PAK_H */
