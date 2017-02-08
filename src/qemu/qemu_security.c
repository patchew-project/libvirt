/*
 * qemu_security.c: QEMU security management
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Michal Privoznik <mprivozn@redhat.com>
 */

#include <config.h>

#include "qemu_domain.h"
#include "qemu_security.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_process");

#define PROLOGUE(F, type)                                                   \
int                                                                         \
qemuSecurity##F(virQEMUDriverPtr driver,                                    \
                virDomainObjPtr vm,                                         \
                type var)                                                   \
{                                                                           \
    int ret = -1;                                                           \
                                                                            \
    if (qemuDomainNamespaceEnabled(vm, QEMU_DOMAIN_NS_MOUNT) &&             \
        virSecurityManagerTransactionStart(driver->securityManager) < 0)    \
        goto cleanup;                                                       \

#define EPILOGUE                                                            \
    if (qemuDomainNamespaceEnabled(vm, QEMU_DOMAIN_NS_MOUNT) &&             \
        virSecurityManagerTransactionCommit(driver->securityManager,        \
                                            vm->pid) < 0)                   \
        goto cleanup;                                                       \
                                                                            \
    ret = 0;                                                                \
 cleanup:                                                                   \
    virSecurityManagerTransactionAbort(driver->securityManager);            \
    return ret;                                                             \
}

#define WRAP1(F, type)                                                      \
    PROLOGUE(F, type)                                                       \
    if (virSecurityManager##F(driver->securityManager,                      \
                              vm->def,                                      \
                              var) < 0)                                     \
        goto cleanup;                                                       \
                                                                            \
    EPILOGUE

#define WRAP2(F, type)                                                      \
    PROLOGUE(F, type)                                                       \
    if (virSecurityManager##F(driver->securityManager,                      \
                              vm->def,                                      \
                              var, NULL) < 0)                               \
        goto cleanup;                                                       \
                                                                            \
    EPILOGUE

WRAP1(SetAllLabel, const char *)

void
qemuSecurityRestoreAllLabel(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            bool migrated)
{
    /* In contrast to qemuSecuritySetAllLabel, do not use
     * secdriver transactions here. This function is called from
     * qemuProcessStop() which is meant to do cleanup after qemu
     * process died. If it did do, the namespace is gone as qemu
     * was the only process running there. We would not succeed
     * in entering the namespace then. */
    virSecurityManagerRestoreAllLabel(driver->securityManager,
                                      vm->def,
                                      migrated);
}


WRAP1(SetDiskLabel, virDomainDiskDefPtr)
WRAP1(RestoreDiskLabel, virDomainDiskDefPtr)

WRAP2(SetHostdevLabel, virDomainHostdevDefPtr)
WRAP2(RestoreHostdevLabel, virDomainHostdevDefPtr)
