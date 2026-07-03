#include "crow_pak.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define PAK_MAGIC 0x5A6F12E1
#define FOOTER_SCAN_SIZE 512
#define MAX_INDEX_SIZE (50 * 1024 * 1024) /* 50 MB sanity limit */

/* UE4 FPakInfo essential fields */
#pragma pack(push, 1)
struct FPakInfoBase {
    uint32_t magic;
    uint32_t version;
    uint64_t index_offset;
    uint64_t index_size;
    uint8_t  index_hash[20];
};
#pragma pack(pop)

static const gchar *crow_pak_map_character(const char *code) {
    if (strncmp(code, "SOL", 3) == 0) return "Sol Badguy";
    if (strncmp(code, "KYK", 3) == 0) return "Ky Kiske";
    if (strncmp(code, "MAY", 3) == 0) return "May";
    if (strncmp(code, "AXL", 3) == 0) return "Axl Low";
    if (strncmp(code, "CHP", 3) == 0) return "Chipp Zanuff";
    if (strncmp(code, "POT", 3) == 0) return "Potemkin";
    if (strncmp(code, "FAU", 3) == 0) return "Faust";
    if (strncmp(code, "MLI", 3) == 0) return "Millia Rage";
    if (strncmp(code, "ZAT", 3) == 0) return "Zato-1";
    if (strncmp(code, "RAM", 3) == 0) return "Ramlethal Valentine";
    if (strncmp(code, "LEO", 3) == 0) return "Leo Whitefang";
    if (strncmp(code, "NAG", 3) == 0) return "Nagoriyuki";
    if (strncmp(code, "GIO", 3) == 0) return "Giovanna";
    if (strncmp(code, "INO", 3) == 0) return "I-No";
    if (strncmp(code, "ANJ", 3) == 0) return "Anji Mito";
    if (strncmp(code, "JKO", 3) == 0) return "Jack-O'";
    if (strncmp(code, "GLD", 3) == 0) return "Goldlewis Dickinson";
    if (strncmp(code, "BKN", 3) == 0) return "Baiken";
    if (strncmp(code, "TST", 3) == 0) return "Testament";
    if (strncmp(code, "BRG", 3) == 0) return "Bridget";
    if (strncmp(code, "SIN", 3) == 0) return "Sin Kiske";
    if (strncmp(code, "BED", 3) == 0) return "Bedman?";
    if (strncmp(code, "ASK", 3) == 0) return "Asuka R#";
    if (strncmp(code, "JHN", 3) == 0) return "Johnny";
    if (strncmp(code, "ELP", 3) == 0) return "Elphelt Valentine";
    if (strncmp(code, "ABA", 3) == 0) return "A.B.A";
    if (strncmp(code, "SLY", 3) == 0) return "Slayer";
    if (strncmp(code, "UZK", 3) == 0) return "Unika";
    if (strncmp(code, "LUC", 3) == 0) return "Lucy";
    return NULL;
}

gchar *crow_pak_detect_character(const gchar *pak_path) {
    FILE *f = fopen(pak_path, "rb");
    if (!f) return g_strdup("Unknown / Other");

    /* Seek to the end of file to read footer */
    if (fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    off_t file_size = ftello(f);
    if (file_size < 0 || (size_t)file_size < sizeof(struct FPakInfoBase)) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    /* Read the last FOOTER_SCAN_SIZE bytes */
    off_t scan_size = (file_size < FOOTER_SCAN_SIZE) ? file_size : FOOTER_SCAN_SIZE;
    if (fseeko(f, file_size - scan_size, SEEK_SET) != 0) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    uint8_t buffer[FOOTER_SCAN_SIZE];
    size_t read_bytes = fread(buffer, 1, scan_size, f);
    if (read_bytes < sizeof(struct FPakInfoBase)) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    /* Scan backwards for Magic */
    struct FPakInfoBase *pak_info = NULL;
    for (ssize_t i = read_bytes - sizeof(struct FPakInfoBase); i >= 0; i--) {
        if (*(uint32_t *)(&buffer[i]) == PAK_MAGIC) {
            pak_info = (struct FPakInfoBase *)(&buffer[i]);
            break;
        }
    }

    if (!pak_info) {
        fclose(f);
        return g_strdup("Unknown / Other"); /* Not a UE4 pak or magic not found */
    }

    uint64_t index_offset = pak_info->index_offset;
    uint64_t index_size = pak_info->index_size;

    if (index_size == 0 || index_size > MAX_INDEX_SIZE || index_offset >= (uint64_t)file_size) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    /* Read the Index */
    if (fseeko(f, index_offset, SEEK_SET) != 0) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    char *index_buffer = g_malloc(index_size);
    if (!index_buffer) {
        fclose(f);
        return g_strdup("Unknown / Other");
    }

    if (fread(index_buffer, 1, index_size, f) != index_size) {
        g_free(index_buffer);
        fclose(f);
        return g_strdup("Unknown / Other");
    }
    fclose(f);

    /* Search for character paths */
    const char *target = "RED/Content/Chara/";
    size_t target_len = strlen(target);
    const gchar *detected_char = NULL;

    /* A simple binary scan (memmem is GNU extension, we'll write a simple loop to be portable, 
       or use glib's g_strstr_len if applicable. g_strstr_len expects null-terminated or len. ) 
       Wait, index_buffer is binary data, but strings are UTF-8 or ASCII without nulls sometimes or prefixed by length.
       We can just scan byte by byte.
    */
    for (size_t i = 0; i <= index_size - target_len - 3; i++) {
        if (memcmp(&index_buffer[i], target, target_len) == 0) {
            char code[4] = {0};
            code[0] = index_buffer[i + target_len];
            code[1] = index_buffer[i + target_len + 1];
            code[2] = index_buffer[i + target_len + 2];
            
            const gchar *mapped = crow_pak_map_character(code);
            if (mapped) {
                detected_char = mapped;
                break;
            }
        }
    }

    g_free(index_buffer);

    if (detected_char) {
        return g_strdup(detected_char);
    }

    return g_strdup("Unknown / Other");
}

gchar *crow_pak_guess_from_name(const gchar *name) {
    if (!name) return g_strdup("Unknown / Other");
    
    gchar *lower = g_utf8_strdown(name, -1);
    gchar *result = NULL;

    if (strstr(lower, "sol") || strstr(lower, "badguy")) result = "Sol Badguy";
    else if (strstr(lower, "ky") || strstr(lower, "kiske")) result = "Ky Kiske";
    else if (strstr(lower, "may")) result = "May";
    else if (strstr(lower, "axl") || strstr(lower, "low")) result = "Axl Low";
    else if (strstr(lower, "chipp") || strstr(lower, "zanuff")) result = "Chipp Zanuff";
    else if (strstr(lower, "pot") || strstr(lower, "potemkin")) result = "Potemkin";
    else if (strstr(lower, "faust")) result = "Faust";
    else if (strstr(lower, "millia") || strstr(lower, "mli")) result = "Millia Rage";
    else if (strstr(lower, "zato") || strstr(lower, "jotaro")) result = "Zato-1"; /* User specific case JoTaro01 */
    else if (strstr(lower, "ram") || strstr(lower, "ramlethal")) result = "Ramlethal Valentine";
    else if (strstr(lower, "leo") || strstr(lower, "whitefang")) result = "Leo Whitefang";
    else if (strstr(lower, "nago") || strstr(lower, "nagoriyuki")) result = "Nagoriyuki";
    else if (strstr(lower, "gio") || strstr(lower, "giovanna")) result = "Giovanna";
    else if (strstr(lower, "ino") || strstr(lower, "i-no")) result = "I-No";
    else if (strstr(lower, "anji") || strstr(lower, "mito")) result = "Anji Mito";
    else if (strstr(lower, "jack") || strstr(lower, "jacko")) result = "Jack-O'";
    else if (strstr(lower, "goldlewis") || strstr(lower, "dickinson")) result = "Goldlewis Dickinson";
    else if (strstr(lower, "baiken") || strstr(lower, "jam")) result = "Baiken"; /* Jam Chie might be over Baiken */
    else if (strstr(lower, "testament")) result = "Testament";
    else if (strstr(lower, "bridget")) result = "Bridget";
    else if (strstr(lower, "sin")) result = "Sin Kiske";
    else if (strstr(lower, "bedman")) result = "Bedman?";
    else if (strstr(lower, "asuka")) result = "Asuka R#";
    else if (strstr(lower, "johnny")) result = "Johnny";
    else if (strstr(lower, "elphelt") || strstr(lower, "elp")) result = "Elphelt Valentine";
    else if (strstr(lower, "aba") || strstr(lower, "a.b.a")) result = "A.B.A";
    else if (strstr(lower, "slayer")) result = "Slayer";
    else if (strstr(lower, "unika")) result = "Unika";
    else if (strstr(lower, "lucy")) result = "Lucy";
    else result = "Unknown / Other";

    gchar *ret = g_strdup(result);
    g_free(lower);
    return ret;
}
