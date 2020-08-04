/*
 * virhostuptime.c: helper APIs for host uptime
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#ifdef HAVE_GETUTXID
# include <utmpx.h>
#endif

#include "virhostuptime.h"
#include "viralloc.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"
#include "virtime.h"
#include "virthread.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.virhostuptime");

static unsigned long long bootTime;
static int bootTimeErrno;
static virOnceControl virHostGetBootTimeOnce = VIR_ONCE_CONTROL_INITIALIZER;

#if defined(__linux__)
# define UPTIME_FILE  "/proc/uptime"
static int
virHostGetBootTimeProcfs(unsigned long long *btime)
{
    unsigned long long now;
    double up;
    g_autofree char *buf = NULL;
    char *tmp;

    if (virTimeMillisNow(&now) < 0)
        return -errno;

    /* 1KiB limit is more than enough. */
    if (virFileReadAll(UPTIME_FILE, 1024, &buf) < 0)
        return -errno;

    /* buf contains two doubles now:
     *   $uptime $idle_time
     * We're interested only in the first one */
    if (!(tmp = strchr(buf, ' '))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("uptime file has unexpected format '%s'"),
                       buf);
        return -EINVAL;
    }

    *tmp = '\0';

    if (virStrToDouble(buf, NULL, &up) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to parse uptime value '%s'"),
                       buf);
        return -EINVAL;
    }

    *btime = now / 1000 - up + 0.5;

    return 0;
}
#endif /* defined(__linux__) */

#if defined(HAVE_GETUTXID) || defined(__linux__)
static void
virHostGetBootTimeOnceInit(void)
{
# ifdef HAVE_GETUTXID
    struct utmpx id = {.ut_type = BOOT_TIME};
    struct utmpx *res = NULL;

    if (!(res = getutxid(&id))) {
        bootTimeErrno = errno;
    } else {
        bootTime = res->ut_tv.tv_sec;
    }

    endutxent();
# endif /* HAVE_GETUTXID */

# ifdef __linux__
    if (bootTimeErrno != 0 || bootTime == 0)
        bootTimeErrno = -virHostGetBootTimeProcfs(&bootTime);
# endif /* __linux__ */
}

#else /* !defined(HAVE_GETUTXID) && !defined(__linux__) */

static void
virHostGetBootTimeOnceInit(void)
{
    bootTimeErrno = ENOSYS;
}
#endif /* !defined(HAVE_GETUTXID) && !defined(__linux__) */

/**
 * virHostGetBootTime:
 * @when: UNIX timestamp of boot time
 *
 * Get a UNIX timestamp of host boot time and store it at @when.
 *
 * Return: 0 on success,
 *        -1 otherwise.
 */
int
virHostGetBootTime(unsigned long long *when)
{
    if (virHostBootTimeInit() < 0)
        return -1;

    if (bootTimeErrno) {
        errno = bootTimeErrno;
        return -1;
    }

    *when = bootTime;
    return 0;
}


int
virHostBootTimeInit(void)
{
    if (virOnce(&virHostGetBootTimeOnce, virHostGetBootTimeOnceInit) < 0)
        return -1;

    return 0;
}
