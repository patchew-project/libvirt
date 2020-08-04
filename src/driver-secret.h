/*
 * driver-secret.h: entry points for secret drivers
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifndef __VIR_DRIVER_H_INCLUDES___
# error "Don't include this file directly, only use driver.h"
#endif

enum {
    /* This getValue call is inside libvirt, override the "private" flag.
       This flag cannot be set by outside callers. */
    VIR_SECRET_GET_VALUE_INTERNAL_CALL = 1 << 0,
};

typedef virSecretPtr
(*virDrvSecretLookupByUUID)(virConnectPtr conn,
                            const unsigned char *uuid);

typedef virSecretPtr
(*virDrvSecretLookupByUsage)(virConnectPtr conn,
                             int usageType,
                             const char *usageID);

typedef virSecretPtr
(*virDrvSecretDefineXML)(virConnectPtr conn,
                         const char *xml,
                         unsigned int flags);

typedef char *
(*virDrvSecretGetXMLDesc)(virSecretPtr secret,
                          unsigned int flags);

typedef int
(*virDrvSecretSetValue)(virSecretPtr secret,
                        const unsigned char *value,
                        size_t value_size,
                        unsigned int flags);

typedef unsigned char *
(*virDrvSecretGetValue)(virSecretPtr secret,
                        size_t *value_size,
                        unsigned int flags,
                        unsigned int internalFlags);

typedef int
(*virDrvSecretUndefine)(virSecretPtr secret);

typedef int
(*virDrvConnectNumOfSecrets)(virConnectPtr conn);

typedef int
(*virDrvConnectListSecrets)(virConnectPtr conn,
                            char **uuids,
                            int maxuuids);

typedef int
(*virDrvConnectListAllSecrets)(virConnectPtr conn,
                               virSecretPtr **secrets,
                               unsigned int flags);

typedef int
(*virDrvConnectSecretEventRegisterAny)(virConnectPtr conn,
                                       virSecretPtr secret,
                                       int eventID,
                                       virConnectSecretEventGenericCallback cb,
                                       void *opaque,
                                       virFreeCallback freecb);

typedef int
(*virDrvConnectSecretEventDeregisterAny)(virConnectPtr conn,
                                         int callbackID);

typedef struct _virSecretDriver virSecretDriver;
typedef virSecretDriver *virSecretDriverPtr;

/**
 * _virSecretDriver:
 *
 * Structure associated to a driver for storing secrets, defining the various
 * entry points for it.
 */
struct _virSecretDriver {
    const char *name; /* the name of the driver */
    virDrvConnectNumOfSecrets connectNumOfSecrets;
    virDrvConnectListSecrets connectListSecrets;
    virDrvConnectListAllSecrets connectListAllSecrets;
    virDrvSecretLookupByUUID secretLookupByUUID;
    virDrvSecretLookupByUsage secretLookupByUsage;
    virDrvSecretDefineXML secretDefineXML;
    virDrvSecretGetXMLDesc secretGetXMLDesc;
    virDrvSecretSetValue secretSetValue;
    virDrvSecretGetValue secretGetValue;
    virDrvSecretUndefine secretUndefine;
    virDrvConnectSecretEventRegisterAny connectSecretEventRegisterAny;
    virDrvConnectSecretEventDeregisterAny connectSecretEventDeregisterAny;
};
