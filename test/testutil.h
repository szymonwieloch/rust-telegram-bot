#ifndef TESTUTIL_H
#define TESTUTIL_H

#include "apr_pools.h"
#include "apr_general.h"

/* Global pool used by abts — initialised in initialize() */
extern apr_pool_t *p;

/* Called once at startup by abts's main() */
void initialize(void);

#endif /* TESTUTIL_H */
