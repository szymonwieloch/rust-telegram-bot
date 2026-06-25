#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>

/**
 * Check whether a string is NULL, empty, or contains only whitespace.
 *
 * @param str  Null-terminated string (may be NULL).
 * @return     true if str is NULL, empty, or all-whitespace.
 */
bool utils_is_blank(const char *str);

#endif /* UTILS_H */
