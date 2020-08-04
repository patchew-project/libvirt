/*
 * virnwfilterbindingxml2xmltest.c: network filter binding XML testing
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include <unistd.h>

#include <sys/types.h>
#include <fcntl.h>

#include "internal.h"
#include "testutils.h"
#include "virxml.h"
#include "virnwfilterbindingdef.h"
#include "testutilsqemu.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static int
testCompareXMLToXMLFiles(const char *xml)
{
    char *actual = NULL;
    int ret = -1;
    virNWFilterBindingDefPtr dev = NULL;

    virResetLastError();

    if (!(dev = virNWFilterBindingDefParseFile(xml)))
        goto fail;

    if (!(actual = virNWFilterBindingDefFormat(dev)))
        goto fail;

    if (virTestCompareToFile(actual, xml) < 0)
        goto fail;

    ret = 0;

 fail:
    VIR_FREE(actual);
    virNWFilterBindingDefFree(dev);
    return ret;
}

typedef struct test_parms {
    const char *name;
} test_parms;

static int
testCompareXMLToXMLHelper(const void *data)
{
    int result = -1;
    const test_parms *tp = data;
    char *xml = NULL;

    xml = g_strdup_printf("%s/virnwfilterbindingxml2xmldata/%s.xml", abs_srcdir,
                          tp->name);

    result = testCompareXMLToXMLFiles(xml);

    VIR_FREE(xml);

    return result;
}

static int
mymain(void)
{
    int ret = 0;

#define DO_TEST(NAME) \
    do { \
        test_parms tp = { \
            .name = NAME, \
        }; \
        if (virTestRun("NWFilter XML-2-XML " NAME, \
                       testCompareXMLToXMLHelper, (&tp)) < 0) \
            ret = -1; \
    } while (0)

    DO_TEST("simple");
    DO_TEST("filter-vars");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
