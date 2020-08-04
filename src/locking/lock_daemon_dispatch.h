/*
 * lock_daemon_dispatch.h: lock management daemon dispatch
 *
 * Copyright (C) 2006-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "rpc/virnetserverprogram.h"

extern virNetServerProgramProc virLockSpaceProtocolProcs[];
extern size_t virLockSpaceProtocolNProcs;
