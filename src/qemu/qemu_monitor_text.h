/*
 * qemu_monitor_text.h: interaction with QEMU monitor console
 *
 * Copyright (C) 2006-2009, 2011-2012 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "qemu_monitor.h"

int qemuMonitorTextAddDrive(qemuMonitorPtr mon,
                             const char *drivestr);

int qemuMonitorTextDriveDel(qemuMonitorPtr mon,
                             const char *drivestr);

int qemuMonitorTextCreateSnapshot(qemuMonitorPtr mon, const char *name);
int qemuMonitorTextLoadSnapshot(qemuMonitorPtr mon, const char *name);
int qemuMonitorTextDeleteSnapshot(qemuMonitorPtr mon, const char *name);
