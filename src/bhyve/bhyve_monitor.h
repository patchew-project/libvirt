/*
 * bhyve_monitor.h: Tear-down or reboot bhyve domains on guest shutdown
 *
 * Copyright (C) 2014 Conrad Meyer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "domain_conf.h"
#include "bhyve_utils.h"

typedef struct _bhyveMonitor bhyveMonitor;
typedef bhyveMonitor *bhyveMonitorPtr;

bhyveMonitorPtr bhyveMonitorOpen(virDomainObjPtr vm, bhyveConnPtr driver);
void bhyveMonitorClose(bhyveMonitorPtr mon);

void bhyveMonitorSetReboot(bhyveMonitorPtr mon);
