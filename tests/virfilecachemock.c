/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include <unistd.h>

#include "internal.h"


int
unlink(const char *path G_GNUC_UNUSED)
{
    return 0;
}
