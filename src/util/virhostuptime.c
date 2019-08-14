/*
 * virhostuptime.c: helper APIs for host uptime
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#if defined(__linux__) || defined(__FreeBSD__)
# include <utmpx.h>
#endif

#include "virhostuptime.h"


/**
 * virHostGetBootTime:
 * @when: UNIX timestamp of boot time
 *
 * Get a UNIX timestamp of host boot time and store it at @when.
 * Note that this function is not reentrant.
 *
 * Return: 0 on success,
 *        -1 otherwise.
 */
int
virHostGetBootTime(unsigned long long *when ATTRIBUTE_UNUSED)
{
#if defined(__linux__) || defined(__FreeBSD__)
    struct utmpx id = {.ut_type = BOOT_TIME};
    struct utmpx *res = NULL;

    if (!(res = getutxid(&id))) {
        int theerrno = errno;
        endutxent();
        return -theerrno;
    }

    *when = res->ut_tv.tv_sec;
    endutxent();
    return 0;
#else
    /* Unfortunately, mingw lacks utmp */
    errno = ENOSYS;
    return -errno;
#endif
}
