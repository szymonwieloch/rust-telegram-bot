/**
 * utils — Small, pure string helpers for the weather bot.
 */

#include "utils.h"

#include <ctype.h>
#include <stdbool.h>

/* ── is_blank ─────────────────────────────────────────────────────────────── */

bool utils_is_blank(const char *str)
{
    if (!str) {
        return true;
    }
    while (*str) {
        if (!isspace((unsigned char) *str)) {
            return false;
        }
        str++;
    }
    return true;
}
