/*
 * qemu_namespace.h: QEMU domain namespace helpers
 *
 * Copyright (C) 2006-2020 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virenum.h"
#include "qemu_conf.h"
#include "virconf.h"

typedef enum {
    QEMU_DOMAIN_NS_MOUNT = 0,
    QEMU_DOMAIN_NS_LAST
} qemuDomainNamespace;
VIR_ENUM_DECL(qemuDomainNamespace);

int qemuDomainEnableNamespace(virDomainObjPtr vm,
                              qemuDomainNamespace ns);

bool qemuDomainNamespaceEnabled(virDomainObjPtr vm,
                                qemuDomainNamespace ns);

int qemuDomainUnshareNamespace(virQEMUDriverConfigPtr cfg,
                               virSecurityManagerPtr mgr,
                               virDomainObjPtr vm);

int qemuDomainBuildNamespace(virQEMUDriverConfigPtr cfg,
                             virDomainObjPtr vm);

void qemuDomainDestroyNamespace(virQEMUDriverPtr driver,
                                virDomainObjPtr vm);

bool qemuDomainNamespaceAvailable(qemuDomainNamespace ns);

int qemuDomainNamespaceSetupDisk(virDomainObjPtr vm,
                                 virStorageSourcePtr src);

int qemuDomainNamespaceTeardownDisk(virDomainObjPtr vm,
                                    virStorageSourcePtr src);

int qemuDomainNamespaceSetupHostdev(virDomainObjPtr vm,
                                    virDomainHostdevDefPtr hostdev);

int qemuDomainNamespaceTeardownHostdev(virDomainObjPtr vm,
                                       virDomainHostdevDefPtr hostdev);

int qemuDomainNamespaceSetupMemory(virDomainObjPtr vm,
                                   virDomainMemoryDefPtr memory);

int qemuDomainNamespaceTeardownMemory(virDomainObjPtr vm,
                                      virDomainMemoryDefPtr memory);

int qemuDomainNamespaceSetupChardev(virDomainObjPtr vm,
                                    virDomainChrDefPtr chr);

int qemuDomainNamespaceTeardownChardev(virDomainObjPtr vm,
                                       virDomainChrDefPtr chr);

int qemuDomainNamespaceSetupRNG(virDomainObjPtr vm,
                                virDomainRNGDefPtr rng);

int qemuDomainNamespaceTeardownRNG(virDomainObjPtr vm,
                                   virDomainRNGDefPtr rng);

int qemuDomainNamespaceSetupInput(virDomainObjPtr vm,
                                  virDomainInputDefPtr input);

int qemuDomainNamespaceTeardownInput(virDomainObjPtr vm,
                                     virDomainInputDefPtr input);
