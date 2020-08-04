/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>
#include <unistd.h>

#include "internal.h"
#include "virfile.h"

bool
virFileExists(const char *path)
{
    if (STREQ(path, "/dev/kvm"))
        return true;
    return access(path, F_OK) == 0;
}
