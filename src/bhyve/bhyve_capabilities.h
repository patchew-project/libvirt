/*
 * bhyve_capabilities.h: bhyve capabilities module
 *
 * Copyright (C) 2014 Semihalf
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "capabilities.h"
#include "conf/domain_capabilities.h"

#include "bhyve_utils.h"

virCapsPtr virBhyveCapsBuild(void);
int virBhyveDomainCapsFill(virDomainCapsPtr caps,
                           unsigned int bhyvecaps,
                           virDomainCapsStringValuesPtr firmwares);
virDomainCapsPtr virBhyveDomainCapsBuild(bhyveConnPtr,
                                         const char *emulatorbin,
                                         const char *machine,
                                         virArch arch,
                                         virDomainVirtType virttype);

/* These are bit flags: */
typedef enum {
    BHYVE_GRUB_CAP_CONSDEV = 1,
} virBhyveGrubCapsFlags;

typedef enum {
    BHYVE_CAP_RTC_UTC = 1 << 0,
    BHYVE_CAP_AHCI32SLOT = 1 << 1,
    BHYVE_CAP_NET_E1000 = 1 << 2,
    BHYVE_CAP_LPC_BOOTROM = 1 << 3,
    BHYVE_CAP_FBUF = 1 << 4,
    BHYVE_CAP_XHCI = 1 << 5,
    BHYVE_CAP_CPUTOPOLOGY = 1 << 6,
} virBhyveCapsFlags;

int virBhyveProbeGrubCaps(virBhyveGrubCapsFlags *caps);
int virBhyveProbeCaps(unsigned int *caps);
