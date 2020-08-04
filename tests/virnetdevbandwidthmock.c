/*
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>
#include <unistd.h>
#include <sys/types.h>

uid_t geteuid(void)
{
    return 0;
}

uid_t getuid(void)
{
    return 0;
}
