/*
 * virnwfilterbindingobjlist.h: nwfilter binding object list utilities
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnwfilterbindingobj.h"

typedef struct _virNWFilterBindingObjList virNWFilterBindingObjList;
typedef virNWFilterBindingObjList *virNWFilterBindingObjListPtr;

virNWFilterBindingObjListPtr
virNWFilterBindingObjListNew(void);

virNWFilterBindingObjPtr
virNWFilterBindingObjListFindByPortDev(virNWFilterBindingObjListPtr bindings,
                                       const char *name);

virNWFilterBindingObjPtr
virNWFilterBindingObjListAdd(virNWFilterBindingObjListPtr bindings,
                             virNWFilterBindingDefPtr def);

void
virNWFilterBindingObjListRemove(virNWFilterBindingObjListPtr bindings,
                                virNWFilterBindingObjPtr binding);

int
virNWFilterBindingObjListLoadAllConfigs(virNWFilterBindingObjListPtr bindings,
                                        const char *configDir);


typedef int (*virNWFilterBindingObjListIterator)(virNWFilterBindingObjPtr binding,
                                                 void *opaque);

int
virNWFilterBindingObjListForEach(virNWFilterBindingObjListPtr bindings,
                                 virNWFilterBindingObjListIterator callback,
                                 void *opaque);

typedef bool (*virNWFilterBindingObjListACLFilter)(virConnectPtr conn,
                                                   virNWFilterBindingDefPtr def);

int
virNWFilterBindingObjListExport(virNWFilterBindingObjListPtr bindings,
                                virConnectPtr conn,
                                virNWFilterBindingPtr **bindinglist,
                                virNWFilterBindingObjListACLFilter filter);
