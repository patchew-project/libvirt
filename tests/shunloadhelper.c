/*
 * Copyright (C) 2011, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

/*
 * This is a helper for shunloadtest.c. This function is built into
 * a shared library and linked with libvirto.so
 *
 * The function initializes libvirt and primes the thread local with
 * an error which needs to be freed at thread exit
 */

#include <config.h>


#include "internal.h"

static void shunloadError(void *userData G_GNUC_UNUSED,
                          virErrorPtr error G_GNUC_UNUSED)
{
}

int shunloadStart(void);

int shunloadStart(void)
{
    virConnectPtr conn;

    virSetErrorFunc(NULL, shunloadError);
    if (virInitialize() < 0)
        return -1;

    conn = virConnectOpen("test:///default");
    virDomainDestroy(NULL);
    if (conn) {
        virConnectClose(conn);
        return 0;
    }
    return -1;
}
