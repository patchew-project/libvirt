/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "testutils.h"

#include "virlog.h"

struct testLogData {
    const char *str;
    int count;
    bool pass;
};

static int
testLogMatch(const void *opaque)
{
    const struct testLogData *data = opaque;

    bool got = virLogProbablyLogMessage(data->str);
    if (got != data->pass) {
        VIR_TEST_DEBUG("Expected '%d' but got '%d' for '%s'",
                       data->pass, got, data->str);
        return -1;
    }
    return 0;
}

static int
testLogParseOutputs(const void *opaque)
{
    int ret = -1;
    int noutputs;
    virLogOutputPtr *outputs = NULL;
    const struct testLogData *data = opaque;

    noutputs = virLogParseOutputs(data->str, &outputs);
    if (noutputs < 0) {
        if (!data->pass) {
            VIR_TEST_DEBUG("Got expected error: %s",
                           virGetLastErrorMessage());
            virResetLastError();
            ret = 0;
            goto cleanup;
        }
    } else if (noutputs != data->count) {
            VIR_TEST_DEBUG("Expected number of parsed outputs is %d, "
                           "but got %d", data->count, noutputs);
            goto cleanup;
    } else if (!data->pass) {
        VIR_TEST_DEBUG("Test should have failed");
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virLogOutputListFree(outputs, noutputs);
    return ret;
}

static int
testLogParseFilters(const void *opaque)
{
    int ret = -1;
    int nfilters;
    virLogFilterPtr *filters = NULL;
    const struct testLogData *data = opaque;

    nfilters = virLogParseFilters(data->str, &filters);
    if (nfilters < 0) {
        if (!data->pass) {
            VIR_TEST_DEBUG("Got expected error: %s",
                           virGetLastErrorMessage());
            virResetLastError();
            ret = 0;
            goto cleanup;
        }
    } else if (nfilters != data->count) {
        VIR_TEST_DEBUG("Expected number of parsed outputs is %d, "
                       "but got %d", data->count, nfilters);
        goto cleanup;
    } else if (!data->pass) {
        VIR_TEST_DEBUG("Test should have failed");
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virLogFilterListFree(filters, nfilters);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;

#define DO_TEST_FULL(name, test, str, count, pass) \
    do { \
        struct testLogData data = { \
            str, count, pass \
        }; \
        if (virTestRun(name, test, &data) < 0) \
            ret = -1; \
    } while (0)

#define TEST_LOG_MATCH_FAIL(str) \
    DO_TEST_FULL("testLogMatch " # str, testLogMatch, str, 0, false)
#define TEST_LOG_MATCH(str) \
    DO_TEST_FULL("testLogMatch " # str, testLogMatch, str, 0, true)

#define TEST_PARSE_OUTPUTS_FAIL(str, count) \
    DO_TEST_FULL("testLogParseOutputs " # str, testLogParseOutputs, str, count, false)
#define TEST_PARSE_OUTPUTS(str, count) \
    DO_TEST_FULL("testLogParseOutputs " # str, testLogParseOutputs, str, count, true)
#define TEST_PARSE_FILTERS_FAIL(str, count) \
    DO_TEST_FULL("testLogParseFilters " # str, testLogParseFilters, str, count, false)
#define TEST_PARSE_FILTERS(str, count) \
    DO_TEST_FULL("testLogParseFilters " # str, testLogParseFilters, str, count, true)


    TEST_LOG_MATCH("2013-10-11 15:43:43.866+0000: 28302: info : libvirt version: 1.1.3");

    TEST_LOG_MATCH_FAIL("libvirt:  error : cannot execute binary /usr/libexec/libvirt_lxc: No such file or directory");
    TEST_PARSE_OUTPUTS("1:file:/dev/null", 1);
    TEST_PARSE_OUTPUTS("1:file:/dev/null  2:stderr", 2);
    TEST_PARSE_OUTPUTS_FAIL("foo:stderr", 1);
    TEST_PARSE_OUTPUTS_FAIL("1:bar", 1);
    TEST_PARSE_OUTPUTS_FAIL("1:stderr:foobar", 1);
    TEST_PARSE_FILTERS("1:foo", 1);
    TEST_PARSE_FILTERS("1:foo 2:bar  3:foobar", 3);
    TEST_PARSE_FILTERS_FAIL("5:foo", 1);
    TEST_PARSE_FILTERS_FAIL("1:", 1);
    TEST_PARSE_FILTERS_FAIL(":foo", 1);
    TEST_PARSE_FILTERS_FAIL("1:+", 1);

    return ret;
}

VIR_TEST_MAIN(mymain)
