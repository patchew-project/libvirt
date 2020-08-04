/*
 * virdomaincheckpointobjlist.h: handle a tree of checkpoint objects
 *                  (derived from virdomainsnapshotobjlist.h)
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virdomainmomentobjlist.h"
#include "virbuffer.h"

virDomainCheckpointObjListPtr
virDomainCheckpointObjListNew(void);

void
virDomainCheckpointObjListFree(virDomainCheckpointObjListPtr checkpoints);

virDomainMomentObjPtr
virDomainCheckpointAssignDef(virDomainCheckpointObjListPtr checkpoints,
                             virDomainCheckpointDefPtr def);

virDomainMomentObjPtr
virDomainCheckpointFindByName(virDomainCheckpointObjListPtr checkpoints,
                              const char *name);

virDomainMomentObjPtr
virDomainCheckpointGetCurrent(virDomainCheckpointObjListPtr checkpoints);

const char *
virDomainCheckpointGetCurrentName(virDomainCheckpointObjListPtr checkpoints);

void
virDomainCheckpointSetCurrent(virDomainCheckpointObjListPtr checkpoints,
                              virDomainMomentObjPtr checkpoint);

bool
virDomainCheckpointObjListRemove(virDomainCheckpointObjListPtr checkpoints,
                                 virDomainMomentObjPtr checkpoint);

void
virDomainCheckpointObjListRemoveAll(virDomainCheckpointObjListPtr checkpoints);

int
virDomainCheckpointForEach(virDomainCheckpointObjListPtr checkpoints,
                           virHashIterator iter,
                           void *data);

void
virDomainCheckpointLinkParent(virDomainCheckpointObjListPtr checkpoints,
                              virDomainMomentObjPtr chk);

int
virDomainCheckpointUpdateRelations(virDomainCheckpointObjListPtr checkpoints,
                                   virDomainMomentObjPtr *leaf);

int
virDomainCheckpointCheckCycles(virDomainCheckpointObjListPtr checkpoints,
                               virDomainCheckpointDefPtr def,
                               const char *domname);

#define VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES \
               (VIR_DOMAIN_CHECKPOINT_LIST_LEAVES       | \
                VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES)

#define VIR_DOMAIN_CHECKPOINT_FILTERS_ALL \
               (VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES)

int
virDomainListCheckpoints(virDomainCheckpointObjListPtr checkpoints,
                         virDomainMomentObjPtr from,
                         virDomainPtr dom,
                         virDomainCheckpointPtr **objs,
                         unsigned int flags);

static inline virDomainCheckpointDefPtr
virDomainCheckpointObjGetDef(virDomainMomentObjPtr obj)
{
    return (virDomainCheckpointDefPtr) obj->def;
}
