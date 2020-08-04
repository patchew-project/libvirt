/*
 * Copyright (C) 2012, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"
#include "driver.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.drivermoduletest");

struct testDriverModuleData {
    const char *module;
    const char *regfunc;
};


static int testDriverModule(const void *args)
{
    const struct testDriverModuleData *data = args;

    /* coverity[leaked_storage] */
    if (virDriverLoadModule(data->module, data->regfunc, true) != 0)
        return -1;

    return 0;
}


static int
mymain(void)
{
    int ret = 0;
    struct testDriverModuleData data;

#define TEST_FULL(name, fnc) \
    do  { \
        data.module = name; \
        data.regfunc = fnc; \
        if (virTestRun("Test driver " # name, testDriverModule, &data) < 0) \
            ret = -1; \
    } while (0)

#define TEST(name) TEST_FULL(name, name "Register")

#ifdef WITH_NETWORK
    TEST("network");
#endif
#ifdef WITH_INTERFACE
    TEST("interface");
#endif
#ifdef WITH_STORAGE
    TEST_FULL("storage", "storageRegisterAll");
#endif
#ifdef WITH_NODE_DEVICES
    TEST("nodedev");
#endif
#ifdef WITH_SECRETS
    TEST("secret");
#endif
#ifdef WITH_NWFILTER
    TEST("nwfilter");
#endif
#ifdef WITH_LIBXL
    TEST("libxl");
#endif
#ifdef WITH_QEMU
    TEST("qemu");
#endif
#ifdef WITH_LXC
    TEST("lxc");
#endif
#ifdef WITH_VBOX
    TEST("vbox");
#endif
#ifdef WITH_BHYVE
    TEST("bhyve");
#endif

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
