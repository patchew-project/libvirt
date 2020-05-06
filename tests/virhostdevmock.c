/*
 * Copyright (C) 2020 Red Hat, Inc.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virutil.h"
#include "virmock.h"

static char *(*real_virGetUserRuntimeDirectory)(void);

static void
init_syms(void)
{
    if (real_virGetUserRuntimeDirectory)
        return;

    VIR_MOCK_REAL_INIT(virGetUserRuntimeDirectory);
}

char *
virGetUserRuntimeDirectory(void)
{
    init_syms();

    return g_build_filename(g_getenv("LIBVIRT_FAKE_ROOT_DIR"),
                            "user-runtime-directory", NULL);
}
