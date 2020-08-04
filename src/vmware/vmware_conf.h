/*
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright 2010, diateam (www.diateam.net)
 * Copyright (c) 2013, Doug Goldstein (cardoe@cardoe.com)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define NOGUI "nogui"

#include "internal.h"
#include "virdomainobjlist.h"
#include "virthread.h"
#include "virenum.h"

#define VIR_FROM_THIS VIR_FROM_VMWARE
#define PROGRAM_SENTINEL ((char *)0x1)

enum vmwareDriverType {
    VMWARE_DRIVER_PLAYER      = 0, /* VMware Player */
    VMWARE_DRIVER_WORKSTATION = 1, /* VMware Workstation */
    VMWARE_DRIVER_FUSION      = 2, /* VMware Fusion */

    VMWARE_DRIVER_LAST,            /* required last item */
};

VIR_ENUM_DECL(vmwareDriver);

struct vmware_driver {
    virMutex lock;
    virCapsPtr caps;
    virDomainXMLOptionPtr xmlopt;

    virDomainObjListPtr domains;
    unsigned long version;
    int type;
    char *vmrun;
};

typedef struct _vmwareDomain {
    char *vmxPath;
    bool gui;

} vmwareDomain, *vmwareDomainPtr;

void vmwareFreeDriver(struct vmware_driver *driver);

virCapsPtr vmwareCapsInit(void);

int vmwareLoadDomains(struct vmware_driver *driver);

void vmwareSetSentinal(const char **prog, const char *key);

int vmwareExtractVersion(struct vmware_driver *driver);

int vmwareParseVersionStr(int type, const char *buf, unsigned long *version);

int vmwareDomainConfigDisplay(vmwareDomainPtr domain, virDomainDefPtr vmdef);

void  vmwareConstructVmxPath(char *directoryName, char *name,
                             char **vmxPath);

int vmwareVmxPath(virDomainDefPtr vmdef, char **vmxPath);

int vmwareMoveFile(char *srcFile, char *dstFile);

int vmwareMakePath(char *srcDir, char *srcName, char *srcExt,
                   char **outpath);

int vmwareExtractPid(const char * vmxPath);

char *vmwareCopyVMXFileName(const char *datastorePath, void *opaque);
