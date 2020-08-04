/*
 * qemu_dbus.h: QEMU dbus daemon
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "qemu_domain.h"

char *qemuDBusGetAddress(virQEMUDriverPtr driver,
                         virDomainObjPtr vm);

int qemuDBusStart(virQEMUDriverPtr driver,
                  virDomainObjPtr vm);

void qemuDBusStop(virQEMUDriverPtr driver,
                  virDomainObjPtr vm);

int qemuDBusVMStateAdd(virDomainObjPtr vm, const char *id);

void qemuDBusVMStateRemove(virDomainObjPtr vm, const char *id);

int qemuDBusSetupCgroup(virQEMUDriverPtr driver,
                        virDomainObjPtr vm);
