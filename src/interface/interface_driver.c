/*
 * interface_driver.c: loads the appropriate backend
 *
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2012 Doug Goldstein <cardoe@cardoe.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <config.h>

#include "interface_driver.h"

int
interfaceRegister(void)
{
#ifdef WITH_NETCF
    /* Attempt to load the netcf based backend first */
    if (netcfIfaceRegister() == 0)
        return 0;
#endif /* WITH_NETCF */
#if WITH_UDEV
    /* If there's no netcf or it failed to load, register the udev backend */
    if (udevIfaceRegister() == 0)
        return 0;
#endif /* WITH_UDEV */
    return -1;
}
