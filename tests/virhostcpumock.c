/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "virhostcpu.h"

int
virHostCPUGetThreadsPerSubcore(virArch arch)
{
    int threads_per_subcore = 0;

    /* Emulate SMT=8 on POWER hardware */
    if (ARCH_IS_PPC64(arch))
        threads_per_subcore = 8;

    return threads_per_subcore;
}
