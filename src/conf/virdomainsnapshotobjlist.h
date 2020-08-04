/*
 * virdomainsnapshotobjlist.h: handle a tree of snapshot objects
 *                  (derived from snapshot_conf.h)
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

virDomainSnapshotObjListPtr virDomainSnapshotObjListNew(void);
void virDomainSnapshotObjListFree(virDomainSnapshotObjListPtr snapshots);

virDomainMomentObjPtr virDomainSnapshotAssignDef(virDomainSnapshotObjListPtr snapshots,
                                                 virDomainSnapshotDefPtr def);

int virDomainSnapshotObjListGetNames(virDomainSnapshotObjListPtr snapshots,
                                     virDomainMomentObjPtr from,
                                     char **const names, int maxnames,
                                     unsigned int flags);
int virDomainSnapshotObjListNum(virDomainSnapshotObjListPtr snapshots,
                                virDomainMomentObjPtr from,
                                unsigned int flags);
virDomainMomentObjPtr virDomainSnapshotFindByName(virDomainSnapshotObjListPtr snapshots,
                                                  const char *name);
virDomainMomentObjPtr virDomainSnapshotGetCurrent(virDomainSnapshotObjListPtr snapshots);
const char *virDomainSnapshotGetCurrentName(virDomainSnapshotObjListPtr snapshots);
void virDomainSnapshotSetCurrent(virDomainSnapshotObjListPtr snapshots,
                                 virDomainMomentObjPtr snapshot);
bool virDomainSnapshotObjListRemove(virDomainSnapshotObjListPtr snapshots,
                                    virDomainMomentObjPtr snapshot);
void virDomainSnapshotObjListRemoveAll(virDomainSnapshotObjListPtr snapshots);
int virDomainSnapshotForEach(virDomainSnapshotObjListPtr snapshots,
                             virHashIterator iter,
                             void *data);
void virDomainSnapshotLinkParent(virDomainSnapshotObjListPtr snapshots,
                                 virDomainMomentObjPtr moment);
int virDomainSnapshotUpdateRelations(virDomainSnapshotObjListPtr snapshots);
int virDomainSnapshotCheckCycles(virDomainSnapshotObjListPtr snapshots,
                                 virDomainSnapshotDefPtr def,
                                 const char *domname);

#define VIR_DOMAIN_SNAPSHOT_FILTERS_METADATA \
               (VIR_DOMAIN_SNAPSHOT_LIST_METADATA     | \
                VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA)

#define VIR_DOMAIN_SNAPSHOT_FILTERS_LEAVES \
               (VIR_DOMAIN_SNAPSHOT_LIST_LEAVES       | \
                VIR_DOMAIN_SNAPSHOT_LIST_NO_LEAVES)

#define VIR_DOMAIN_SNAPSHOT_FILTERS_STATUS \
               (VIR_DOMAIN_SNAPSHOT_LIST_INACTIVE     | \
                VIR_DOMAIN_SNAPSHOT_LIST_ACTIVE       | \
                VIR_DOMAIN_SNAPSHOT_LIST_DISK_ONLY)

#define VIR_DOMAIN_SNAPSHOT_FILTERS_LOCATION \
               (VIR_DOMAIN_SNAPSHOT_LIST_INTERNAL     | \
                VIR_DOMAIN_SNAPSHOT_LIST_EXTERNAL)

#define VIR_DOMAIN_SNAPSHOT_FILTERS_ALL \
               (VIR_DOMAIN_SNAPSHOT_FILTERS_METADATA  | \
                VIR_DOMAIN_SNAPSHOT_FILTERS_LEAVES    | \
                VIR_DOMAIN_SNAPSHOT_FILTERS_STATUS    | \
                VIR_DOMAIN_SNAPSHOT_FILTERS_LOCATION)

int virDomainListSnapshots(virDomainSnapshotObjListPtr snapshots,
                           virDomainMomentObjPtr from,
                           virDomainPtr dom,
                           virDomainSnapshotPtr **snaps,
                           unsigned int flags);

/* Access the snapshot-specific definition from a given list member. */
static inline virDomainSnapshotDefPtr
virDomainSnapshotObjGetDef(virDomainMomentObjPtr obj)
{
    return (virDomainSnapshotDefPtr) obj->def;
}
