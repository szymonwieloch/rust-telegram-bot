/**
 * test_utils — Unit tests for src/utils.c using the abts framework.
 */

#include "abts.h"
#include "../src/utils.h"

#include <string.h>

/* ── Helpers: mutable copies for trim tests ──────────────────────────────── */

static char *copystr(abts_case *tc, const char *src) {
    char *cpy = strdup(src);
    ABTS_PTR_NOTNULL(tc, cpy);
    return cpy;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * utils_trim
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_trim_no_whitespace(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "hello");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "hello", result);
    free(s);
}

static void test_trim_leading_spaces(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "   hello");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "hello", result);
    /* result points inside the original buffer */
    ABTS_TRUE(tc, result >= s);
    free(s);
}

static void test_trim_trailing_spaces(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "hello   ");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "hello", result);
    free(s);
}

static void test_trim_both_sides(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "  hello world  ");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "hello world", result);
    free(s);
}

static void test_trim_tabs_and_newlines(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "\t\n  text \n\t");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "text", result);
    free(s);
}

static void test_trim_all_whitespace(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "    \t  \n  ");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "", result);
    /* result should point to the start (set to empty) */
    ABTS_TRUE(tc, result == s);
    free(s);
}

static void test_trim_empty_string(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "", result);
    free(s);
}

static void test_trim_null(abts_case *tc, void *data) {
    (void)data;
    char *result = utils_trim(NULL);
    ABTS_PTR_EQUAL(tc, NULL, result);
}

static void test_trim_single_char(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, "x");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "x", result);
    free(s);
}

static void test_trim_single_space(abts_case *tc, void *data) {
    (void)data;
    char *s = copystr(tc, " ");
    char *result = utils_trim(s);
    ABTS_STR_EQUAL(tc, "", result);
    free(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * utils_is_blank
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_is_blank_null(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_is_blank(NULL));
}

static void test_is_blank_empty(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_is_blank(""));
}

static void test_is_blank_spaces_only(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_is_blank("   "));
}

static void test_is_blank_tabs_newlines(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_is_blank("\t \n \r"));
}

static void test_is_blank_not_blank(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_is_blank("hello"));
}

static void test_is_blank_leading_whitespace_then_text(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_is_blank("   x"));
}

static void test_is_blank_single_char(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_is_blank("!"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * utils_startswith
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_startswith_match(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_startswith("/start", "/start"));
}

static void test_startswith_prefix_match(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, utils_startswith("/start@MyBot", "/start"));
}

static void test_startswith_no_match(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_startswith("hello", "/start"));
}

static void test_startswith_shorter_than_prefix(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_startswith("/st", "/start"));
}

static void test_startswith_empty_prefix(abts_case *tc, void *data) {
    (void)data;
    /* An empty prefix matches everything */
    ABTS_TRUE(tc, utils_startswith("anything", ""));
}

static void test_startswith_empty_string(abts_case *tc, void *data) {
    (void)data;
    /* Empty string doesn't start with a non-empty prefix */
    ABTS_TRUE(tc, !utils_startswith("", "/start"));
}

static void test_startswith_null_str(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_startswith(NULL, "/start"));
}

static void test_startswith_null_prefix(abts_case *tc, void *data) {
    (void)data;
    ABTS_TRUE(tc, !utils_startswith("/start", NULL));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test registration
 * ═══════════════════════════════════════════════════════════════════════════ */

abts_suite *test_utils(abts_suite *suite) {
    suite = ADD_SUITE(suite);

    /* ── utils_trim ───────────────────────────────────────────────────── */
    abts_run_test(suite, test_trim_no_whitespace, NULL);
    abts_run_test(suite, test_trim_leading_spaces, NULL);
    abts_run_test(suite, test_trim_trailing_spaces, NULL);
    abts_run_test(suite, test_trim_both_sides, NULL);
    abts_run_test(suite, test_trim_tabs_and_newlines, NULL);
    abts_run_test(suite, test_trim_all_whitespace, NULL);
    abts_run_test(suite, test_trim_empty_string, NULL);
    abts_run_test(suite, test_trim_null, NULL);
    abts_run_test(suite, test_trim_single_char, NULL);
    abts_run_test(suite, test_trim_single_space, NULL);

    /* ── utils_is_blank ───────────────────────────────────────────────── */
    abts_run_test(suite, test_is_blank_null, NULL);
    abts_run_test(suite, test_is_blank_empty, NULL);
    abts_run_test(suite, test_is_blank_spaces_only, NULL);
    abts_run_test(suite, test_is_blank_tabs_newlines, NULL);
    abts_run_test(suite, test_is_blank_not_blank, NULL);
    abts_run_test(suite, test_is_blank_leading_whitespace_then_text, NULL);
    abts_run_test(suite, test_is_blank_single_char, NULL);

    /* ── utils_startswith ─────────────────────────────────────────────── */
    abts_run_test(suite, test_startswith_match, NULL);
    abts_run_test(suite, test_startswith_prefix_match, NULL);
    abts_run_test(suite, test_startswith_no_match, NULL);
    abts_run_test(suite, test_startswith_shorter_than_prefix, NULL);
    abts_run_test(suite, test_startswith_empty_prefix, NULL);
    abts_run_test(suite, test_startswith_empty_string, NULL);
    abts_run_test(suite, test_startswith_null_str, NULL);
    abts_run_test(suite, test_startswith_null_prefix, NULL);

    return suite;
}
