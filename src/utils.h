#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Trim leading and trailing whitespace from a mutable C string in-place.
 *
 * Returns a pointer into the original buffer, skipping leading whitespace.
 * Trailing whitespace is replaced with '\0'.
 *
 * If the string is all-whitespace, the first character is set to '\0'
 * and a pointer to it is returned.
 *
 * @param str  Null-terminated, mutable string.
 * @return     Pointer to the trimmed start (still inside the original buffer).
 */
char *utils_trim(char *str);

/**
 * Check whether a string is NULL, empty, or contains only whitespace.
 *
 * @param str  Null-terminated string (may be NULL).
 * @return     true if str is NULL, empty, or all-whitespace.
 */
bool utils_is_blank(const char *str);

/**
 * Check whether `str` starts with `prefix`.
 *
 * @param str     Null-terminated string.
 * @param prefix  Null-terminated prefix.
 * @return        true if str starts with prefix.
 */
bool utils_startswith(const char *str, const char *prefix);

#endif /* UTILS_H */
