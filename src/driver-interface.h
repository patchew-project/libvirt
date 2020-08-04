/*
 * driver-interface.h: entry points for interface drivers
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifndef __VIR_DRIVER_H_INCLUDES___
# error "Don't include this file directly, only use driver.h"
#endif

typedef int
(*virDrvConnectNumOfInterfaces)(virConnectPtr conn);

typedef int
(*virDrvConnectListInterfaces)(virConnectPtr conn,
                               char **const names,
                               int maxnames);

typedef int
(*virDrvConnectNumOfDefinedInterfaces)(virConnectPtr conn);

typedef int
(*virDrvConnectListDefinedInterfaces)(virConnectPtr conn,
                                      char **const names,
                                      int maxnames);

typedef int
(*virDrvConnectListAllInterfaces)(virConnectPtr conn,
                                  virInterfacePtr **ifaces,
                                  unsigned int flags);

typedef virInterfacePtr
(*virDrvInterfaceLookupByName)(virConnectPtr conn,
                               const char *name);

typedef virInterfacePtr
(*virDrvInterfaceLookupByMACString)(virConnectPtr conn,
                                    const char *mac);

typedef char *
(*virDrvInterfaceGetXMLDesc)(virInterfacePtr iface,
                             unsigned int flags);

typedef virInterfacePtr
(*virDrvInterfaceDefineXML)(virConnectPtr conn,
                            const char *xmlDesc,
                            unsigned int flags);

typedef int
(*virDrvInterfaceUndefine)(virInterfacePtr iface);

typedef int
(*virDrvInterfaceCreate)(virInterfacePtr iface,
                         unsigned int flags);

typedef int
(*virDrvInterfaceDestroy)(virInterfacePtr iface,
                          unsigned int flags);

typedef int
(*virDrvInterfaceIsActive)(virInterfacePtr iface);

typedef int
(*virDrvInterfaceChangeBegin)(virConnectPtr conn,
                              unsigned int flags);

typedef int
(*virDrvInterfaceChangeCommit)(virConnectPtr conn,
                               unsigned int flags);

typedef int
(*virDrvInterfaceChangeRollback)(virConnectPtr conn,
                                 unsigned int flags);

typedef struct _virInterfaceDriver virInterfaceDriver;
typedef virInterfaceDriver *virInterfaceDriverPtr;

/**
 * _virInterfaceDriver:
 *
 * Structure associated to a network interface driver, defining the various
 * entry points for it.
 */
struct _virInterfaceDriver {
    const char *name; /* the name of the driver */
    virDrvConnectNumOfInterfaces connectNumOfInterfaces;
    virDrvConnectListInterfaces connectListInterfaces;
    virDrvConnectNumOfDefinedInterfaces connectNumOfDefinedInterfaces;
    virDrvConnectListDefinedInterfaces connectListDefinedInterfaces;
    virDrvConnectListAllInterfaces connectListAllInterfaces;
    virDrvInterfaceLookupByName interfaceLookupByName;
    virDrvInterfaceLookupByMACString interfaceLookupByMACString;
    virDrvInterfaceGetXMLDesc interfaceGetXMLDesc;
    virDrvInterfaceDefineXML interfaceDefineXML;
    virDrvInterfaceUndefine interfaceUndefine;
    virDrvInterfaceCreate interfaceCreate;
    virDrvInterfaceDestroy interfaceDestroy;
    virDrvInterfaceIsActive interfaceIsActive;
    virDrvInterfaceChangeBegin interfaceChangeBegin;
    virDrvInterfaceChangeCommit interfaceChangeCommit;
    virDrvInterfaceChangeRollback interfaceChangeRollback;
};
