/*
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>

#include "viralloc.h"
#include "virfile.h"
#include "virstring.h"
#include "virusb.h"

#define USB_SYSFS "/sys/bus/usb"
#define FAKE_USB_SYSFS "virusbtestdata/sys_bus_usb"

static int (*realopen)(const char *pathname, int flags, ...);
static DIR *(*realopendir)(const char *name);

static void init_syms(void)
{
    if (realopen)
        return;

    realopen = dlsym(RTLD_NEXT, "open");
    realopendir = dlsym(RTLD_NEXT, "opendir");
    if (!realopen || !realopendir) {
        fprintf(stderr, "Error getting symbols");
        abort();
    }
}

static char *get_fake_path(const char *real_path)
{
    const char *p = NULL;
    char *path = NULL;

    if ((p = STRSKIP(real_path, USB_SYSFS)))
        path = g_strdup_printf("%s/%s/%s", abs_srcdir, FAKE_USB_SYSFS, p);
    else if (!p)
        path = g_strdup(real_path);

    return path;
}

DIR *opendir(const char *name)
{
    char *path;
    DIR* ret;

    init_syms();

    path = get_fake_path(name);

    ret = realopendir(path);
    VIR_FREE(path);
    return ret;
}

int open(const char *pathname, int flags, ...)
{
    char *path;
    int ret;
    va_list ap;
    mode_t mode = 0;

    init_syms();

    path = get_fake_path(pathname);
    if (!path)
        return -1;

    /* The mode argument is mandatory when O_CREAT is set in flags,
     * otherwise the argument is ignored.
     */
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t) va_arg(ap, int);
        va_end(ap);
    }

    ret = realopen(path, flags, mode);

    VIR_FREE(path);
    return ret;
}
