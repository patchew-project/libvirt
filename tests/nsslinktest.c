/*
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"
#include "libvirt_nss.h"

int main(int argc G_GNUC_UNUSED,
         char *argv[] G_GNUC_UNUSED)
{
    int err, herrno; /* Dummy variables to prevent SIGSEGV */

    /* The only aim of this test is to catch link errors as those
     * are hard to trace for resulting .so library. Therefore,
     * the fact this test has been built successfully means
     * there's no linkage problem and therefore success is
     * returned. */
    NSS_NAME(gethostbyname)(NULL, NULL, NULL, 0, &err, &herrno);

    return EXIT_SUCCESS;
}
