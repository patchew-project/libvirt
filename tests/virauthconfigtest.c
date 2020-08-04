/*
 * Copyright (C) 2012, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <signal.h>

#include "testutils.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"

#include "virauthconfig.h"

#define VIR_FROM_THIS VIR_FROM_RPC

VIR_LOG_INIT("tests.authconfigtest");

struct ConfigLookupData {
    virAuthConfigPtr config;
    const char *hostname;
    const char *service;
    const char *credname;
    const char *expect;
};

static int testAuthLookup(const void *args)
{
    const struct ConfigLookupData *data = args;
    g_autofree char *actual = NULL;
    int rv;

    rv = virAuthConfigLookup(data->config,
                             data->service,
                             data->hostname,
                             data->credname,
                             &actual);

    if (rv < 0)
        return -1;

    if (data->expect) {
        if (!actual ||
            STRNEQ(actual, data->expect)) {
            VIR_WARN("Expected value '%s' for '%s' '%s' '%s', but got '%s'",
                     data->expect, data->hostname,
                     data->service, data->credname,
                     NULLSTR(actual));
            return -1;
        }
    } else {
        if (actual) {
            VIR_WARN("Did not expect a value for '%s' '%s' '%s', but got '%s'",
                     data->hostname,
                     data->service, data->credname,
                     actual);
            return -1;
        }
    }

    return 0;
}


static int
mymain(void)
{
    int ret = 0;

    virAuthConfigPtr config;

#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif /* WIN32 */

#define TEST_LOOKUP(config, hostname, service, credname, expect) \
    do  { \
        const struct ConfigLookupData data = { \
            config, hostname, service, credname, expect \
        }; \
        if (virTestRun("Test Lookup " hostname "-" service "-" credname, \
                        testAuthLookup, &data) < 0) \
            ret = -1; \
    } while (0)

    const char *confdata =
        "[credentials-test]\n"
        "username=fred\n"
        "password=123456\n"
        "\n"
        "[credentials-prod]\n"
        "username=bar\n"
        "password=letmein\n"
        "\n"
        "[auth-libvirt-test1.example.com]\n"
        "credentials=test\n"
        "\n"
        "[auth-libvirt-test2.example.com]\n"
        "credentials=test\n"
        "\n"
        "[auth-libvirt-demo3.example.com]\n"
        "credentials=test\n"
        "\n"
        "[auth-libvirt-prod1.example.com]\n"
        "credentials=prod\n";

    if (!(config = virAuthConfigNewData("auth.conf", confdata, strlen(confdata))))
        return EXIT_FAILURE;

    TEST_LOOKUP(config, "test1.example.com", "libvirt", "username", "fred");
    TEST_LOOKUP(config, "test1.example.com", "vnc", "username", NULL);
    TEST_LOOKUP(config, "test1.example.com", "libvirt", "realm", NULL);
    TEST_LOOKUP(config, "test66.example.com", "libvirt", "username", NULL);
    TEST_LOOKUP(config, "prod1.example.com", "libvirt", "username", "bar");
    TEST_LOOKUP(config, "prod1.example.com", "libvirt", "password", "letmein");

    virAuthConfigFree(config);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
