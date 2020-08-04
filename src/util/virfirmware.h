/*
 * virfirmware.h: Declaration of firmware object and supporting functions
 *
 * Copyright (C) 2016 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virFirmware virFirmware;
typedef virFirmware *virFirmwarePtr;

struct _virFirmware {
    char *name;
    char *nvram;
};


void
virFirmwareFree(virFirmwarePtr firmware);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virFirmware, virFirmwareFree);

void
virFirmwareFreeList(virFirmwarePtr *firmwares, size_t nfirmwares);

int
virFirmwareParse(const char *str, virFirmwarePtr firmware)
    ATTRIBUTE_NONNULL(2);

int
virFirmwareParseList(const char *list,
                     virFirmwarePtr **firmwares,
                     size_t *nfirmwares)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
