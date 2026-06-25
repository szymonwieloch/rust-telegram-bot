/**
 * test_utils — Unit tests for src/utils.c using the abts framework.
 */

#include "abts.h"
#include "../src/utils.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * utils_is_blank
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_is_blank_null(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, utils_is_blank(NULL));
}

static void test_is_blank_empty(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, utils_is_blank(""));
}

static void test_is_blank_spaces_only(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, utils_is_blank("   "));
}

static void test_is_blank_tabs_newlines(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, utils_is_blank("\t \n \r"));
}

static void test_is_blank_not_blank(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, !utils_is_blank("hello"));
}

static void test_is_blank_leading_whitespace_then_text(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, !utils_is_blank("   x"));
}

static void test_is_blank_single_char(abts_case *tc, void *data)
{
    (void) data;
    ABTS_TRUE(tc, !utils_is_blank("!"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test registration
 * ═══════════════════════════════════════════════════════════════════════════ */

abts_suite *test_utils(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    /* ── utils_is_blank ───────────────────────────────────────────────── */
    abts_run_test(suite, test_is_blank_null, NULL);
    abts_run_test(suite, test_is_blank_empty, NULL);
    abts_run_test(suite, test_is_blank_spaces_only, NULL);
    abts_run_test(suite, test_is_blank_tabs_newlines, NULL);
    abts_run_test(suite, test_is_blank_not_blank, NULL);
    abts_run_test(suite, test_is_blank_leading_whitespace_then_text, NULL);
    abts_run_test(suite, test_is_blank_single_char, NULL);

    return suite;
}
