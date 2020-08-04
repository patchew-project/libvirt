/*
 * virnwfilterobj.h: network filter object processing
 *                  (derived from nwfilter_conf.h)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "nwfilter_conf.h"
#include "virnwfilterbindingobjlist.h"

typedef struct _virNWFilterObj virNWFilterObj;
typedef virNWFilterObj *virNWFilterObjPtr;

typedef struct _virNWFilterObjList virNWFilterObjList;
typedef virNWFilterObjList *virNWFilterObjListPtr;

typedef struct _virNWFilterDriverState virNWFilterDriverState;
typedef virNWFilterDriverState *virNWFilterDriverStatePtr;
struct _virNWFilterDriverState {
    virMutex lock;
    bool privileged;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;

    virNWFilterObjListPtr nwfilters;

    virNWFilterBindingObjListPtr bindings;

    char *stateDir;
    char *configDir;
    char *bindingDir;
};

virNWFilterDefPtr
virNWFilterObjGetDef(virNWFilterObjPtr obj);

virNWFilterDefPtr
virNWFilterObjGetNewDef(virNWFilterObjPtr obj);

bool
virNWFilterObjWantRemoved(virNWFilterObjPtr obj);

virNWFilterObjListPtr
virNWFilterObjListNew(void);

void
virNWFilterObjListFree(virNWFilterObjListPtr nwfilters);

void
virNWFilterObjListRemove(virNWFilterObjListPtr nwfilters,
                         virNWFilterObjPtr obj);

virNWFilterObjPtr
virNWFilterObjListFindByUUID(virNWFilterObjListPtr nwfilters,
                             const unsigned char *uuid);

virNWFilterObjPtr
virNWFilterObjListFindByName(virNWFilterObjListPtr nwfilters,
                             const char *name);

virNWFilterObjPtr
virNWFilterObjListFindInstantiateFilter(virNWFilterObjListPtr nwfilters,
                                        const char *filtername);

virNWFilterObjPtr
virNWFilterObjListAssignDef(virNWFilterObjListPtr nwfilters,
                            virNWFilterDefPtr def);

int
virNWFilterObjTestUnassignDef(virNWFilterObjPtr obj);

typedef bool
(*virNWFilterObjListFilter)(virConnectPtr conn,
                            virNWFilterDefPtr def);

int
virNWFilterObjListNumOfNWFilters(virNWFilterObjListPtr nwfilters,
                                 virConnectPtr conn,
                                 virNWFilterObjListFilter filter);

int
virNWFilterObjListGetNames(virNWFilterObjListPtr nwfilters,
                           virConnectPtr conn,
                           virNWFilterObjListFilter filter,
                           char **const names,
                           int maxnames);

int
virNWFilterObjListExport(virConnectPtr conn,
                         virNWFilterObjListPtr nwfilters,
                         virNWFilterPtr **filters,
                         virNWFilterObjListFilter filter);

int
virNWFilterObjListLoadAllConfigs(virNWFilterObjListPtr nwfilters,
                                 const char *configDir);

void
virNWFilterObjLock(virNWFilterObjPtr obj);

void
virNWFilterObjUnlock(virNWFilterObjPtr obj);
