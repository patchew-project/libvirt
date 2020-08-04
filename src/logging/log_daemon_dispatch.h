/*
 * log_daemon_dispatch.h: log management daemon dispatch
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "rpc/virnetserverprogram.h"

extern virNetServerProgramProc virLogManagerProtocolProcs[];
extern size_t virLogManagerProtocolNProcs;
