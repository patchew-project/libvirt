/*
 * virdomainsnapshotobjlist.h: handle a tree of snapshot objects
 *                  (derived from snapshot_conf.h)
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef LIBVIRT_VIRDOMAINSNAPSHOTOBJLIST_H
# define LIBVIRT_VIRDOMAINSNAPSHOTOBJLIST_H

# include "internal.h"
# include "virdomainsnapshotobj.h"
# include "virbuffer.h"

/* Filter that returns true if a given moment matches the filter flags */
typedef bool (*virDomainMomentObjListFilter)(virDomainMomentObjPtr obj,
                                             unsigned int flags);

virDomainSnapshotObjListPtr virDomainSnapshotObjListNew(void);
void virDomainSnapshotObjListFree(virDomainSnapshotObjListPtr snapshots);

int virDomainSnapshotObjListParse(const char *xmlStr,
                                  const unsigned char *domain_uuid,
                                  virDomainSnapshotObjListPtr snapshots,
                                  virCapsPtr caps,
                                  virDomainXMLOptionPtr xmlopt,
                                  unsigned int flags);
int virDomainSnapshotObjListFormat(virBufferPtr buf,
                                   const char *uuidstr,
                                   virDomainSnapshotObjListPtr snapshots,
                                   virCapsPtr caps,
                                   virDomainXMLOptionPtr xmlopt,
                                   unsigned int flags);

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
int virDomainSnapshotObjListSize(virDomainSnapshotObjListPtr snapshots);
virDomainMomentObjPtr virDomainSnapshotGetCurrent(virDomainSnapshotObjListPtr snapshots);
const char *virDomainSnapshotGetCurrentName(virDomainSnapshotObjListPtr snapshots);
bool virDomainSnapshotIsCurrentName(virDomainSnapshotObjListPtr snapshots,
                                    const char *name);
void virDomainSnapshotSetCurrent(virDomainSnapshotObjListPtr snapshots,
                                 virDomainMomentObjPtr snapshot);
bool virDomainSnapshotObjListRemove(virDomainSnapshotObjListPtr snapshots,
                                    virDomainMomentObjPtr snapshot);
void virDomainSnapshotObjListRemoveAll(virDomainSnapshotObjListPtr snapshots);
int virDomainSnapshotForEach(virDomainSnapshotObjListPtr snapshots,
                             virHashIterator iter,
                             void *data);
int virDomainSnapshotUpdateRelations(virDomainSnapshotObjListPtr snapshots);

int virDomainListSnapshots(virDomainSnapshotObjListPtr snapshots,
                           virDomainMomentObjPtr from,
                           virDomainPtr dom,
                           virDomainSnapshotPtr **snaps,
                           unsigned int flags);

#endif /* LIBVIRT_VIRDOMAINSNAPSHOTOBJLIST_H */
