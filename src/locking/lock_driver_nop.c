/*
 * lock_driver_nop.c: A lock driver which locks nothing
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "lock_driver_nop.h"
#include "viralloc.h"
#include "virlog.h"
#include "viruuid.h"

VIR_LOG_INIT("locking.lock_driver_nop");


static int virLockManagerNopInit(unsigned int version G_GNUC_UNUSED,
                                 const char *configFile G_GNUC_UNUSED,
                                 unsigned int flags_unused G_GNUC_UNUSED)
{
    VIR_DEBUG("version=%u configFile=%s flags=0x%x",
              version, NULLSTR(configFile), flags_unused);

    return 0;
}

static int virLockManagerNopDeinit(void)
{
    VIR_DEBUG(" ");

    return 0;
}


static int virLockManagerNopNew(virLockManagerPtr lock G_GNUC_UNUSED,
                                unsigned int type G_GNUC_UNUSED,
                                size_t nparams G_GNUC_UNUSED,
                                virLockManagerParamPtr params G_GNUC_UNUSED,
                                unsigned int flags_unused G_GNUC_UNUSED)
{
    return 0;
}

static int virLockManagerNopAddResource(virLockManagerPtr lock G_GNUC_UNUSED,
                                        unsigned int type G_GNUC_UNUSED,
                                        const char *name G_GNUC_UNUSED,
                                        size_t nparams G_GNUC_UNUSED,
                                        virLockManagerParamPtr params G_GNUC_UNUSED,
                                        unsigned int flags_unused G_GNUC_UNUSED)
{
    return 0;
}


static int virLockManagerNopAcquire(virLockManagerPtr lock G_GNUC_UNUSED,
                                    const char *state G_GNUC_UNUSED,
                                    unsigned int flags_unused G_GNUC_UNUSED,
                                    virDomainLockFailureAction action G_GNUC_UNUSED,
                                    int *fd G_GNUC_UNUSED)
{
    return 0;
}

static int virLockManagerNopRelease(virLockManagerPtr lock G_GNUC_UNUSED,
                                    char **state,
                                    unsigned int flags_unused G_GNUC_UNUSED)
{
    if (state)
        *state = NULL;

    return 0;
}

static int virLockManagerNopInquire(virLockManagerPtr lock G_GNUC_UNUSED,
                                    char **state,
                                    unsigned int flags_unused G_GNUC_UNUSED)
{
    if (state)
        *state = NULL;

    return 0;
}

static void virLockManagerNopFree(virLockManagerPtr lock G_GNUC_UNUSED)
{
}

virLockDriver virLockDriverNop =
{
    .version = VIR_LOCK_MANAGER_VERSION,
    .flags = 0,

    .drvInit = virLockManagerNopInit,
    .drvDeinit = virLockManagerNopDeinit,

    .drvNew = virLockManagerNopNew,
    .drvFree = virLockManagerNopFree,

    .drvAddResource = virLockManagerNopAddResource,

    .drvAcquire = virLockManagerNopAcquire,
    .drvRelease = virLockManagerNopRelease,

    .drvInquire = virLockManagerNopInquire,
};
