/*
 * Copyright (C) 2019 Red Hat, Inc.
 * Copyright (C) 2019 IBM Corp.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>
#include "virprocess.h"

int
virProcessSetMaxMemLock(pid_t pid G_GNUC_UNUSED, unsigned long long bytes G_GNUC_UNUSED)
{
    return 0;
}
