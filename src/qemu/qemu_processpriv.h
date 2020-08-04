/*
 * qemu_processpriv.h: private declarations for QEMU process management
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_QEMU_PROCESSPRIV_H_ALLOW
# error "qemu_process_priv.h may only be included by qemu_process.c or test suites"
#endif /* LIBVIRT_QEMU_PROCESSPRIV_H_ALLOW */

#pragma once

#include "domain_conf.h"
#include "qemu_monitor.h"

/*
 * This header file should never be used outside unit tests.
 */

int qemuProcessHandleDeviceDeleted(qemuMonitorPtr mon,
                                   virDomainObjPtr vm,
                                   const char *devAlias,
                                   void *opaque);

int qemuProcessQMPInitMonitor(qemuMonitorPtr mon);
