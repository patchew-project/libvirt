/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>


#include "testutils.h"

#include "virkeycode.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"

#include "virlockspace.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.keycodetest");

static int testKeycodeMapping(const void *data G_GNUC_UNUSED)
{
    int got;

#define TRANSLATE(from, to, val, want) \
    do { \
        if ((got = virKeycodeValueTranslate(VIR_KEYCODE_SET_##from, \
                                            VIR_KEYCODE_SET_##to, \
                                            val)) != want) { \
            fprintf(stderr, "Translating %d from %s to %s, got %d want %d\n", \
                    val, #from, #to, got, want); \
            return -1; \
        } \
    } while (0)

    TRANSLATE(LINUX, LINUX, 111, 111);
    TRANSLATE(LINUX, USB, 111, 76);
    TRANSLATE(LINUX, QNUM, 88, 88);
    TRANSLATE(LINUX, QNUM, 160, 163);
    TRANSLATE(ATSET2, ATSET3, 131, 55);
    TRANSLATE(OSX, WIN32, 90, 131);
    TRANSLATE(OSX, ATSET1, 90, 90);
    TRANSLATE(OSX, ATSET1, 3200, -1);

#undef TRANSLATE

    return 0;
}


static int testKeycodeStrings(const void *data G_GNUC_UNUSED)
{
    int got;

#define TRANSLATE(from, str, want) \
    do { \
        if ((got = virKeycodeValueFromString(VIR_KEYCODE_SET_##from, \
                                             str)) != want) { \
            fprintf(stderr, "Converting %s from %s, got %d want %d\n", \
                    str, #from, got, want); \
            return -1; \
        } \
    } while (0)

    TRANSLATE(LINUX, "KEY_DELETE", 111);
    TRANSLATE(LINUX, "KEY_RFKILL", 524);
    TRANSLATE(LINUX, "KEY_WIBBLE",
              -1);
    TRANSLATE(OSX, "Function", 0x3f);
    TRANSLATE(WIN32, "VK_UP", 0x26);

#undef TRANSLATE

    return 0;
}

static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("Keycode mapping ", testKeycodeMapping, NULL) < 0)
        ret = -1;
    if (virTestRun("Keycode strings ", testKeycodeStrings, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
