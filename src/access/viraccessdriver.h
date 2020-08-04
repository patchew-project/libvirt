/*
 * viraccessdriver.h: access control driver
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "conf/domain_conf.h"
#include "access/viraccessmanager.h"

typedef int (*virAccessDriverCheckConnectDrv)(virAccessManagerPtr manager,
                                              const char *driverName,
                                              virAccessPermConnect av);
typedef int (*virAccessDriverCheckDomainDrv)(virAccessManagerPtr manager,
                                             const char *driverName,
                                             virDomainDefPtr domain,
                                             virAccessPermDomain av);
typedef int (*virAccessDriverCheckInterfaceDrv)(virAccessManagerPtr manager,
                                                const char *driverName,
                                                virInterfaceDefPtr iface,
                                                virAccessPermInterface av);
typedef int (*virAccessDriverCheckNetworkDrv)(virAccessManagerPtr manager,
                                              const char *driverName,
                                              virNetworkDefPtr network,
                                              virAccessPermNetwork av);
typedef int (*virAccessDriverCheckNetworkPortDrv)(virAccessManagerPtr manager,
                                                  const char *driverName,
                                                  virNetworkDefPtr network,
                                                  virNetworkPortDefPtr port,
                                                  virAccessPermNetworkPort av);
typedef int (*virAccessDriverCheckNodeDeviceDrv)(virAccessManagerPtr manager,
                                                 const char *driverName,
                                                 virNodeDeviceDefPtr nodedev,
                                                 virAccessPermNodeDevice av);
typedef int (*virAccessDriverCheckNWFilterDrv)(virAccessManagerPtr manager,
                                               const char *driverName,
                                               virNWFilterDefPtr nwfilter,
                                               virAccessPermNWFilter av);
typedef int (*virAccessDriverCheckNWFilterBindingDrv)(virAccessManagerPtr manager,
                                                      const char *driverName,
                                                      virNWFilterBindingDefPtr binding,
                                                      virAccessPermNWFilterBinding av);
typedef int (*virAccessDriverCheckSecretDrv)(virAccessManagerPtr manager,
                                             const char *driverName,
                                             virSecretDefPtr secret,
                                             virAccessPermSecret av);
typedef int (*virAccessDriverCheckStoragePoolDrv)(virAccessManagerPtr manager,
                                                  const char *driverName,
                                                  virStoragePoolDefPtr pool,
                                                  virAccessPermStoragePool av);
typedef int (*virAccessDriverCheckStorageVolDrv)(virAccessManagerPtr manager,
                                                 const char *driverName,
                                                 virStoragePoolDefPtr pool,
                                                 virStorageVolDefPtr vol,
                                                 virAccessPermStorageVol av);

typedef int (*virAccessDriverSetupDrv)(virAccessManagerPtr manager);
typedef void (*virAccessDriverCleanupDrv)(virAccessManagerPtr manager);

typedef struct _virAccessDriver virAccessDriver;
typedef virAccessDriver *virAccessDriverPtr;

struct _virAccessDriver {
    size_t privateDataLen;
    const char *name;

    virAccessDriverSetupDrv setup;
    virAccessDriverCleanupDrv cleanup;

    virAccessDriverCheckConnectDrv checkConnect;
    virAccessDriverCheckDomainDrv checkDomain;
    virAccessDriverCheckInterfaceDrv checkInterface;
    virAccessDriverCheckNetworkDrv checkNetwork;
    virAccessDriverCheckNetworkPortDrv checkNetworkPort;
    virAccessDriverCheckNodeDeviceDrv checkNodeDevice;
    virAccessDriverCheckNWFilterDrv checkNWFilter;
    virAccessDriverCheckNWFilterBindingDrv checkNWFilterBinding;
    virAccessDriverCheckSecretDrv checkSecret;
    virAccessDriverCheckStoragePoolDrv checkStoragePool;
    virAccessDriverCheckStorageVolDrv checkStorageVol;
};
