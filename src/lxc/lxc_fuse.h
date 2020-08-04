/*
 * Copyright (C) 2012 Fujitsu Limited.
 *
 * lxc_fuse.c: fuse filesystem support for libvirt lxc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define FUSE_USE_VERSION 26

#if WITH_FUSE
# include <fuse.h>
#endif

#include "lxc_conf.h"

struct virLXCMeminfo {
    unsigned long long memtotal;
    unsigned long long memusage;
    unsigned long long cached;
    unsigned long long active_anon;
    unsigned long long inactive_anon;
    unsigned long long active_file;
    unsigned long long inactive_file;
    unsigned long long unevictable;
    unsigned long long swaptotal;
    unsigned long long swapusage;
};
typedef struct virLXCMeminfo *virLXCMeminfoPtr;

struct virLXCFuse {
    virDomainDefPtr def;
    virThread thread;
    char *mountpoint;
    struct fuse *fuse;
    struct fuse_chan *ch;
    virMutex lock;
};
typedef struct virLXCFuse virLXCFuse;
typedef struct virLXCFuse *virLXCFusePtr;

int lxcSetupFuse(virLXCFusePtr *f, virDomainDefPtr def);
int lxcStartFuse(virLXCFusePtr f);
void lxcFreeFuse(virLXCFusePtr *f);
