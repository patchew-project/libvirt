/*
 * driver-nwfilter.h: entry points for nwfilter drivers
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
(*virDrvConnectNumOfNWFilters)(virConnectPtr conn);

typedef int
(*virDrvConnectListNWFilters)(virConnectPtr conn,
                              char **const names,
                              int maxnames);

typedef int
(*virDrvConnectListAllNWFilters)(virConnectPtr conn,
                                 virNWFilterPtr **filters,
                                 unsigned int flags);

typedef virNWFilterPtr
(*virDrvNWFilterLookupByName)(virConnectPtr conn,
                              const char *name);

typedef virNWFilterPtr
(*virDrvNWFilterLookupByUUID)(virConnectPtr conn,
                              const unsigned char *uuid);

typedef virNWFilterPtr
(*virDrvNWFilterDefineXML)(virConnectPtr conn,
                           const char *xmlDesc);

typedef int
(*virDrvNWFilterUndefine)(virNWFilterPtr nwfilter);

typedef char *
(*virDrvNWFilterGetXMLDesc)(virNWFilterPtr nwfilter,
                            unsigned int flags);

typedef virNWFilterBindingPtr
(*virDrvNWFilterBindingLookupByPortDev)(virConnectPtr conn,
                                        const char *portdev);

typedef int
(*virDrvConnectListAllNWFilterBindings)(virConnectPtr conn,
                                        virNWFilterBindingPtr **bindings,
                                        unsigned int flags);

typedef virNWFilterBindingPtr
(*virDrvNWFilterBindingCreateXML)(virConnectPtr conn,
                                  const char *xml,
                                  unsigned int flags);

typedef char *
(*virDrvNWFilterBindingGetXMLDesc)(virNWFilterBindingPtr binding,
                                   unsigned int flags);

typedef int
(*virDrvNWFilterBindingDelete)(virNWFilterBindingPtr binding);
typedef int
(*virDrvNWFilterBindingRef)(virNWFilterBindingPtr binding);
typedef int
(*virDrvNWFilterBindingFree)(virNWFilterBindingPtr binding);


typedef struct _virNWFilterDriver virNWFilterDriver;
typedef virNWFilterDriver *virNWFilterDriverPtr;

/**
 * _virNWFilterDriver:
 *
 * Structure associated to a network filter driver, defining the various
 * entry points for it.
 */
struct _virNWFilterDriver {
    const char *name; /* the name of the driver */
    virDrvConnectNumOfNWFilters connectNumOfNWFilters;
    virDrvConnectListNWFilters connectListNWFilters;
    virDrvConnectListAllNWFilters connectListAllNWFilters;
    virDrvNWFilterLookupByName nwfilterLookupByName;
    virDrvNWFilterLookupByUUID nwfilterLookupByUUID;
    virDrvNWFilterDefineXML nwfilterDefineXML;
    virDrvNWFilterUndefine nwfilterUndefine;
    virDrvNWFilterGetXMLDesc nwfilterGetXMLDesc;
    virDrvConnectListAllNWFilterBindings connectListAllNWFilterBindings;
    virDrvNWFilterBindingLookupByPortDev nwfilterBindingLookupByPortDev;
    virDrvNWFilterBindingCreateXML nwfilterBindingCreateXML;
    virDrvNWFilterBindingDelete nwfilterBindingDelete;
    virDrvNWFilterBindingGetXMLDesc nwfilterBindingGetXMLDesc;
};
