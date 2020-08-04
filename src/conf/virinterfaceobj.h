/*
 * virinterfaceobj.h: interface object handling entry points
 *                    (derived from interface_conf.h)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virInterfaceObj virInterfaceObj;
typedef virInterfaceObj *virInterfaceObjPtr;

typedef struct _virInterfaceObjList virInterfaceObjList;
typedef virInterfaceObjList *virInterfaceObjListPtr;

void
virInterfaceObjEndAPI(virInterfaceObjPtr *obj);

virInterfaceDefPtr
virInterfaceObjGetDef(virInterfaceObjPtr obj);

bool
virInterfaceObjIsActive(virInterfaceObjPtr obj);

void
virInterfaceObjSetActive(virInterfaceObjPtr obj,
                         bool active);

virInterfaceObjListPtr
virInterfaceObjListNew(void);

int
virInterfaceObjListFindByMACString(virInterfaceObjListPtr interfaces,
                                   const char *mac,
                                   char **const matches,
                                   int maxmatches);

virInterfaceObjPtr
virInterfaceObjListFindByName(virInterfaceObjListPtr interfaces,
                              const char *name);

void
virInterfaceObjFree(virInterfaceObjPtr obj);

virInterfaceObjListPtr
virInterfaceObjListClone(virInterfaceObjListPtr interfaces);

virInterfaceObjPtr
virInterfaceObjListAssignDef(virInterfaceObjListPtr interfaces,
                             virInterfaceDefPtr def);

void
virInterfaceObjListRemove(virInterfaceObjListPtr interfaces,
                          virInterfaceObjPtr obj);

typedef bool
(*virInterfaceObjListFilter)(virConnectPtr conn,
                             virInterfaceDefPtr def);

int
virInterfaceObjListNumOfInterfaces(virInterfaceObjListPtr interfaces,
                                   bool wantActive);

int
virInterfaceObjListGetNames(virInterfaceObjListPtr interfaces,
                            bool wantActive,
                            char **const names,
                            int maxnames);

int
virInterfaceObjListExport(virConnectPtr conn,
                          virInterfaceObjListPtr ifaceobjs,
                          virInterfacePtr **ifaces,
                          virInterfaceObjListFilter filter,
                          unsigned int flags);
