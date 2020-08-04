/*
 * virnodesuspend.h: Support for suspending a node (host machine)
 *
 * Copyright (C) 2011 Srivatsa S. Bhat <srivatsa.bhat@linux.vnet.ibm.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

int virNodeSuspend(unsigned int target,
                   unsigned long long duration,
                   unsigned int flags);

int virNodeSuspendGetTargetMask(unsigned int *bitmask);
