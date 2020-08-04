/*
 * node_device_driver.h: node device enumeration
 *
 * Copyright (C) 2008 Virtual Iron Software, Inc.
 * Copyright (C) 2008 David F. Lively
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "driver.h"
#include "virnodedeviceobj.h"
#include "vircommand.h"

#define LINUX_NEW_DEVICE_WAIT_TIME 60

#ifdef WITH_HAL
int
halNodeRegister(void);
#endif

#ifdef WITH_UDEV
int
udevNodeRegister(void);
#endif

void
nodeDeviceLock(void);

void
nodeDeviceUnlock(void);

extern virNodeDeviceDriverStatePtr driver;

int
nodedevRegister(void);

virDrvOpenStatus nodeConnectOpen(virConnectPtr conn,
                                 virConnectAuthPtr auth,
                                 virConfPtr conf,
                                 unsigned int flags);
int nodeConnectClose(virConnectPtr conn);
int nodeConnectIsSecure(virConnectPtr conn);
int nodeConnectIsEncrypted(virConnectPtr conn);
int nodeConnectIsAlive(virConnectPtr conn);

int
nodeNumOfDevices(virConnectPtr conn,
                 const char *cap,
                 unsigned int flags);

int nodeListDevices(virConnectPtr conn,
                    const char *cap,
                    char **const names,
                    int maxnames,
                    unsigned int flags);

int
nodeConnectListAllNodeDevices(virConnectPtr conn,
                              virNodeDevicePtr **devices,
                              unsigned int flags);

virNodeDevicePtr
nodeDeviceLookupByName(virConnectPtr conn,
                       const char *name);

virNodeDevicePtr
nodeDeviceLookupSCSIHostByWWN(virConnectPtr conn,
                              const char *wwnn,
                              const char *wwpn,
                              unsigned int flags);

char *
nodeDeviceGetXMLDesc(virNodeDevicePtr dev,
                     unsigned int flags);

char *
nodeDeviceGetParent(virNodeDevicePtr dev);

int
nodeDeviceNumOfCaps(virNodeDevicePtr dev);

int
nodeDeviceListCaps(virNodeDevicePtr dev,
                   char **const names,
                   int maxnames);

virNodeDevicePtr
nodeDeviceCreateXML(virConnectPtr conn,
                    const char *xmlDesc,
                    unsigned int flags);

int
nodeDeviceDestroy(virNodeDevicePtr dev);

int
nodeConnectNodeDeviceEventRegisterAny(virConnectPtr conn,
                                      virNodeDevicePtr dev,
                                      int eventID,
                                      virConnectNodeDeviceEventGenericCallback callback,
                                      void *opaque,
                                      virFreeCallback freecb);
int
nodeConnectNodeDeviceEventDeregisterAny(virConnectPtr conn,
                                        int callbackID);

virCommandPtr
nodeDeviceGetMdevctlStartCommand(virNodeDeviceDefPtr def,
                                 char **uuid_out);
virCommandPtr
nodeDeviceGetMdevctlStopCommand(const char *uuid);
