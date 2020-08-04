/*
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#ifdef WITH_NSS
# include "virmock.h"
# include <sys/types.h>
# include <dirent.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>

# include "configmake.h"
# include "virstring.h"
# include "viralloc.h"

static int (*real_open)(const char *path, int flags, ...);
static DIR * (*real_opendir)(const char *name);
static int (*real_access)(const char *path, int mode);

# define LEASEDIR LOCALSTATEDIR "/lib/libvirt/dnsmasq/"

/*
 * Functions to load the symbols and init the environment
 */
static void
init_syms(void)
{
    if (real_open)
        return;

    VIR_MOCK_REAL_INIT(open);
    VIR_MOCK_REAL_INIT(opendir);
    VIR_MOCK_REAL_INIT(access);
}

static int
getrealpath(char **newpath,
            const char *path)
{
    if (STRPREFIX(path, LEASEDIR)) {
        *newpath = g_strdup_printf("%s/nssdata/%s",
                                   abs_srcdir,
                                   path + strlen(LEASEDIR));
    } else {
        *newpath = g_strdup(path);
    }

    return 0;
}

int
open(const char *path, int flags, ...)
{
    int ret;
    char *newpath = NULL;

    init_syms();

    if (STRPREFIX(path, LEASEDIR) &&
        getrealpath(&newpath, path) < 0)
        return -1;

    if (flags & O_CREAT) {
        va_list ap;
        mode_t mode;
        va_start(ap, flags);
        mode = (mode_t) va_arg(ap, int);
        va_end(ap);
        ret = real_open(newpath ? newpath : path, flags, mode);
    } else {
        ret = real_open(newpath ? newpath : path, flags);
    }

    free(newpath);
    return ret;
}

DIR *
opendir(const char *path)
{
    DIR *ret;
    char *newpath = NULL;

    init_syms();

    if (STRPREFIX(path, LEASEDIR) &&
        getrealpath(&newpath, path) < 0)
        return NULL;

    ret = real_opendir(newpath ? newpath : path);

    free(newpath);
    return ret;
}

int
access(const char *path, int mode)
{
    int ret;
    char *newpath = NULL;

    init_syms();

    if (STRPREFIX(path, LEASEDIR) &&
        getrealpath(&newpath, path) < 0)
        return -1;

    ret = real_access(newpath ? newpath : path, mode);

    free(newpath);
    return ret;
}
#else
/* Nothing to override if NSS plugin is not enabled */
#endif
