/*
 * qemu_validate.h: QEMU general validation functions
 *
 * Copyright IBM Corp, 2020
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "domain_conf.h"
#include "qemu_capabilities.h"
#include "qemu_conf.h"

int qemuValidateDomainDef(const virDomainDef *def, void *opaque);
int qemuValidateDomainDeviceDefDisk(const virDomainDiskDef *disk,
                                    const virDomainDef *def,
                                    virQEMUCapsPtr qemuCaps);
int qemuValidateDomainDeviceDef(const virDomainDeviceDef *dev,
                                const virDomainDef *def,
                                void *opaque);
