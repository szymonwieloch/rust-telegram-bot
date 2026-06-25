/**
 * utils — Small, pure string helpers for the weather bot.
 *
 * These are intentionally free of side effects and external dependencies
 * so they can be unit-tested easily.
 */

#include "utils.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

/* ── trim ─────────────────────────────────────────────────────────────────── */

char *utils_trim(char *str)
{
    if (!str || !*str) {
        return str;
    }

    /* Skip leading whitespace */
    char *start = str;
    while (*start && isspace((unsigned char) *start)) {
        start++;
    }

    /* If we reached the end, string is all whitespace */
    if (!*start) {
        str[0] = '\0';
        return str;
    }

    /* Trim trailing whitespace */
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char) *(end - 1))) {
        end--;
    }
    *end = '\0';

    return start;
}

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

/* ── startswith ───────────────────────────────────────────────────────────── */

bool utils_startswith(const char *str, const char *prefix)
{
    if (!str || !prefix) {
        return false;
    }
    size_t len = strlen(prefix);
    return strncmp(str, prefix, len) == 0;
}
