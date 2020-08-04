/*
 * Copyright (C) 2010-2012 Red Hat, Inc.
 * Copyright IBM Corp. 2008
 *
 * lxc_cgroup.c: LXC cgroup helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vircgroup.h"
#include "domain_conf.h"
#include "lxc_fuse.h"
#include "virusb.h"

virCgroupPtr virLXCCgroupCreate(virDomainDefPtr def,
                                pid_t initpid,
                                size_t nnicindexes,
                                int *nicindexes);
virCgroupPtr virLXCCgroupJoin(virDomainDefPtr def);
int virLXCCgroupSetup(virDomainDefPtr def,
                      virCgroupPtr cgroup,
                      virBitmapPtr nodemask);

int virLXCCgroupGetMeminfo(virLXCMeminfoPtr meminfo);

int
virLXCSetupHostUSBDeviceCgroup(virUSBDevicePtr dev,
                               const char *path,
                               void *opaque);

int
virLXCTeardownHostUSBDeviceCgroup(virUSBDevicePtr dev,
                                  const char *path,
                                  void *opaque);
