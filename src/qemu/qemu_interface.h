/*
 * qemu_interface.h: QEMU interface management
 *
 * Copyright (C) 2014, 2016 Red Hat, Inc.
 * Copyright IBM Corp. 2014
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "qemu_conf.h"
#include "qemu_domain.h"
#include "qemu_slirp.h"

int qemuInterfaceStartDevice(virDomainNetDefPtr net);
int qemuInterfaceStartDevices(virDomainDefPtr def);
int qemuInterfaceStopDevice(virDomainNetDefPtr net);
int qemuInterfaceStopDevices(virDomainDefPtr def);

int qemuInterfaceDirectConnect(virDomainDefPtr def,
                               virQEMUDriverPtr driver,
                               virDomainNetDefPtr net,
                               int *tapfd,
                               size_t tapfdSize,
                               virNetDevVPortProfileOp vmop);

int qemuInterfaceEthernetConnect(virDomainDefPtr def,
                                 virQEMUDriverPtr driver,
                                 virDomainNetDefPtr net,
                                 int *tapfd,
                                 size_t tapfdSize);

int qemuInterfaceBridgeConnect(virDomainDefPtr def,
                               virQEMUDriverPtr driver,
                               virDomainNetDefPtr net,
                               int *tapfd,
                               size_t *tapfdSize)
    ATTRIBUTE_NONNULL(2);

int qemuInterfaceOpenVhostNet(virDomainDefPtr def,
                              virDomainNetDefPtr net,
                              int *vhostfd,
                              size_t *vhostfdSize) G_GNUC_NO_INLINE;

qemuSlirpPtr qemuInterfacePrepareSlirp(virQEMUDriverPtr driver,
                                       virDomainNetDefPtr net);
