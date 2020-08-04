/*
 * qemu_firmware.h: QEMU firmware
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "qemu_conf.h"
#include "virarch.h"
#include "virfirmware.h"

typedef struct _qemuFirmware qemuFirmware;
typedef qemuFirmware *qemuFirmwarePtr;

void
qemuFirmwareFree(qemuFirmwarePtr fw);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(qemuFirmware, qemuFirmwareFree);

qemuFirmwarePtr
qemuFirmwareParse(const char *path);

char *
qemuFirmwareFormat(qemuFirmwarePtr fw);

int
qemuFirmwareFetchConfigs(char ***firmwares,
                         bool privileged);

int
qemuFirmwareFillDomain(virQEMUDriverPtr driver,
                       virDomainDefPtr def,
                       unsigned int flags);

int
qemuFirmwareGetSupported(const char *machine,
                         virArch arch,
                         bool privileged,
                         uint64_t *supported,
                         bool *secure,
                         virFirmwarePtr **fws,
                         size_t *nfws);

G_STATIC_ASSERT(VIR_DOMAIN_OS_DEF_FIRMWARE_LAST <= 64);
