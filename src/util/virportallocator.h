/*
 * virportallocator.h: Allocate & track TCP port allocations
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virobject.h"

typedef struct _virPortAllocatorRange virPortAllocatorRange;
typedef virPortAllocatorRange *virPortAllocatorRangePtr;

virPortAllocatorRangePtr
virPortAllocatorRangeNew(const char *name,
                         unsigned short start,
                         unsigned short end);

void virPortAllocatorRangeFree(virPortAllocatorRangePtr range);

int virPortAllocatorAcquire(const virPortAllocatorRange *range,
                            unsigned short *port);

int virPortAllocatorRelease(unsigned short port);

int virPortAllocatorSetUsed(unsigned short port);
