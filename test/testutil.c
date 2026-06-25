/**
 * testutil — Minimal stubs required by abts.c.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testutil.h"

apr_pool_t *p = NULL;

void initialize(void) {
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "FATAL: apr_initialize failed\n");
        exit(1);
    }
    apr_pool_create(&p, NULL);
}
