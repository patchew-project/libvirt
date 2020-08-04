/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "rpc/virnetsocket.h"
#include "virutil.h"
#include "internal.h"

int virEventAddTimeout(int frequency G_GNUC_UNUSED,
                       virEventTimeoutCallback cb G_GNUC_UNUSED,
                       void *opaque G_GNUC_UNUSED,
                       virFreeCallback ff G_GNUC_UNUSED)
{
    return 0;
}

int virNetSocketGetUNIXIdentity(virNetSocketPtr sock G_GNUC_UNUSED,
                                uid_t *uid,
                                gid_t *gid,
                                pid_t *pid,
                                unsigned long long *timestamp)
{
    *uid = 666;
    *gid = 7337;
    *pid = 42;
    *timestamp = 12345678;
    return 0;
}

char *virGetUserName(uid_t uid G_GNUC_UNUSED)
{
    return strdup("astrochicken");
}

char *virGetGroupName(gid_t gid G_GNUC_UNUSED)
{
    return strdup("fictionalusers");
}

int virNetSocketGetSELinuxContext(virNetSocketPtr sock G_GNUC_UNUSED,
                                  char **context)
{
    if (!(*context = strdup("foo_u:bar_r:wizz_t:s0-s0:c0.c1023")))
        return -1;
    return 0;
}
