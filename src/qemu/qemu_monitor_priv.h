/*
 * qemu_monitor_priv.h: interaction with QEMU monitor console (private)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_QEMU_MONITOR_PRIV_H_ALLOW
# error "qemu_monitor_priv.h may only be included by qemu_monitor.c or test suites"
#endif /* LIBVIRT_QEMU_MONITOR_PRIV_H_ALLOW */

#pragma once

#include "qemu_monitor.h"

void
qemuMonitorResetCommandID(qemuMonitorPtr mon);
