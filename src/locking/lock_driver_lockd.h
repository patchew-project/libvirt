/*
 * lock_driver_lockd.h: Locking for domain lifecycle operations
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

enum virLockSpaceProtocolAcquireResourceFlags {
        VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_SHARED = (1 << 0),
        VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_AUTOCREATE = (1 << 1),
};
