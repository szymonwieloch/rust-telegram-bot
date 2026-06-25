#ifndef ABTS_TESTS_H
#define ABTS_TESTS_H

#include "abts.h"
#include "testutil.h"

/* Forward declarations of test suite constructors */
abts_suite *test_utils(abts_suite *suite);

const struct testlist {
    abts_suite *(*func)(abts_suite *suite);
} alltests[] = {
    {test_utils}
};

#endif /* ABTS_TESTS_H */
