/*
 * driver-nodedev.h: entry points for nodedev drivers
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
(*virDrvNodeNumOfDevices)(virConnectPtr conn,
                          const char *cap,
                          unsigned int flags);

typedef int
(*virDrvNodeListDevices)(virConnectPtr conn,
                         const char *cap,
                         char **const names,
                         int maxnames,
                         unsigned int flags);

typedef int
(*virDrvConnectListAllNodeDevices)(virConnectPtr conn,
                                   virNodeDevicePtr **devices,
                                   unsigned int flags);

typedef virNodeDevicePtr
(*virDrvNodeDeviceLookupByName)(virConnectPtr conn,
                                const char *name);

typedef virNodeDevicePtr
(*virDrvNodeDeviceLookupSCSIHostByWWN)(virConnectPtr conn,
                                       const char *wwnn,
                                       const char *wwpn,
                                       unsigned int flags);

typedef char *
(*virDrvNodeDeviceGetXMLDesc)(virNodeDevicePtr dev,
                              unsigned int flags);

typedef char *
(*virDrvNodeDeviceGetParent)(virNodeDevicePtr dev);

typedef int
(*virDrvNodeDeviceNumOfCaps)(virNodeDevicePtr dev);

typedef int
(*virDrvNodeDeviceListCaps)(virNodeDevicePtr dev,
                            char **const names,
                            int maxnames);

typedef virNodeDevicePtr
(*virDrvNodeDeviceCreateXML)(virConnectPtr conn,
                             const char *xmlDesc,
                             unsigned int flags);

typedef int
(*virDrvNodeDeviceDestroy)(virNodeDevicePtr dev);

typedef int
(*virDrvConnectNodeDeviceEventRegisterAny)(virConnectPtr conn,
                                           virNodeDevicePtr dev,
                                           int eventID,
                                           virConnectNodeDeviceEventGenericCallback cb,
                                           void *opaque,
                                           virFreeCallback freecb);

typedef int
(*virDrvConnectNodeDeviceEventDeregisterAny)(virConnectPtr conn,
                                             int callbackID);



typedef struct _virNodeDeviceDriver virNodeDeviceDriver;
typedef virNodeDeviceDriver *virNodeDeviceDriverPtr;

/**
 * _virNodeDeviceDriver:
 *
 * Structure associated with monitoring the devices
 * on a virtualized node.
 *
 */
struct _virNodeDeviceDriver {
    const char *name; /* the name of the driver */
    virDrvNodeNumOfDevices nodeNumOfDevices;
    virDrvNodeListDevices nodeListDevices;
    virDrvConnectListAllNodeDevices connectListAllNodeDevices;
    virDrvConnectNodeDeviceEventRegisterAny connectNodeDeviceEventRegisterAny;
    virDrvConnectNodeDeviceEventDeregisterAny connectNodeDeviceEventDeregisterAny;
    virDrvNodeDeviceLookupByName nodeDeviceLookupByName;
    virDrvNodeDeviceLookupSCSIHostByWWN nodeDeviceLookupSCSIHostByWWN;
    virDrvNodeDeviceGetXMLDesc nodeDeviceGetXMLDesc;
    virDrvNodeDeviceGetParent nodeDeviceGetParent;
    virDrvNodeDeviceNumOfCaps nodeDeviceNumOfCaps;
    virDrvNodeDeviceListCaps nodeDeviceListCaps;
    virDrvNodeDeviceCreateXML nodeDeviceCreateXML;
    virDrvNodeDeviceDestroy nodeDeviceDestroy;
};
