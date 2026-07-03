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
    if (strncmp(code, "BGT", 3) == 0) return "Bridget";
    if (strncmp(code, "SIN", 3) == 0) return "Sin Kiske";
    if (strncmp(code, "BED", 3) == 0) return "Bedman?";
    if (strncmp(code, "ASK", 3) == 0) return "Asuka R#";
    if (strncmp(code, "JHN", 3) == 0) return "Johnny";
    if (strncmp(code, "ELP", 3) == 0) return "Elphelt Valentine";
    if (strncmp(code, "ABA", 3) == 0) return "A.B.A";
    if (strncmp(code, "SLY", 3) == 0) return "Slayer";
    if (strncmp(code, "UZK", 3) == 0) return "Unika";
    if (strncmp(code, "LUC", 3) == 0) return "Lucy";
    if (strncmp(code, "USG", 3) == 0) return "Jam Kuradoberi";
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

    /* Search for character paths using shorter string since root path varies */
    const char *target = "Chara/";
    size_t target_len = strlen(target);
    const gchar *detected_char = NULL;

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
