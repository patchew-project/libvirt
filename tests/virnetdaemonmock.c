/*
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"
#include <time.h>

#define VIR_FROM_THIS VIR_FROM_NONE

#ifndef WIN32
time_t time(time_t *t)
{
    const time_t ret = 1234567890;
    if (t)
        *t = ret;
    return ret;
}
#endif
