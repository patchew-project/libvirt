/*
 * libxl_capabilities.h: libxl capabilities generation
 *
 * Copyright (C) 2016 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libxl.h>

#include "virobject.h"
#include "capabilities.h"
#include "domain_capabilities.h"
#include "virfirmware.h"


#ifndef LIBXL_FIRMWARE_DIR
# define LIBXL_FIRMWARE_DIR "/usr/lib/xen/boot"
#endif
#ifndef LIBXL_EXECBIN_DIR
# define LIBXL_EXECBIN_DIR "/usr/lib/xen/bin"
#endif

/* Used for prefix of ifname of any network name generated dynamically
 * by libvirt for Xen, and cannot be used for a persistent network name.  */
#define LIBXL_GENERATED_PREFIX_XEN "vif"

bool libxlCapsHasPVUSB(void) G_GNUC_NO_INLINE;

virCapsPtr
libxlMakeCapabilities(libxl_ctx *ctx);

int
libxlMakeDomainCapabilities(virDomainCapsPtr domCaps,
                            virFirmwarePtr *firmwares,
                            size_t nfirmwares);

int
libxlDomainGetEmulatorType(const virDomainDef *def)
    G_GNUC_NO_INLINE;
