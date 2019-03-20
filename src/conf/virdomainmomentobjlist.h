/*
 * virdomainmomentobjlist.h: handle a tree of moment objects
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

#ifndef LIBVIRT_VIRDOMAINMOMENTOBJLIST_H
# define LIBVIRT_VIRDOMAINMOMENTOBJLIST_H

# include "internal.h"
# include "virconftypes.h"
# include "virhash.h"

/* Filter that returns true if a given moment matches the filter flags */
typedef bool (*virDomainMomentObjListFilter)(virDomainMomentObjPtr obj,
                                             unsigned int flags);

/* Struct that allows tracing hierarchical relationships between
 * multiple virDomainMoment objects. The opaque type
 * virDomainMomentObjList then maintains both a hash of these structs
 * (for quick lookup by name) and a metaroot (which is the parent of
 * all user-visible roots), so that all other objects always have a
 * valid parent object; the tree structure is currently maintained via
 * a linked list. */
struct _virDomainMomentObj {
    /* Public field */
    virDomainMomentDefPtr def; /* non-NULL except for metaroot */

    /* Private fields, use accessors instead */
    virDomainMomentObjPtr parent; /* non-NULL except for metaroot, before
                                     virDomainMomentUpdateRelations, or
                                     after virDomainMomentDropParent */
    virDomainMomentObjPtr sibling; /* NULL if last child of parent */
    size_t nchildren;
    virDomainMomentObjPtr first_child; /* NULL if no children */
};

int virDomainMomentForEachChild(virDomainMomentObjPtr moment,
                                virHashIterator iter,
                                void *data);
int virDomainMomentForEachDescendant(virDomainMomentObjPtr moment,
                                     virHashIterator iter,
                                     void *data);
void virDomainMomentDropParent(virDomainMomentObjPtr moment);
void virDomainMomentDropChildren(virDomainMomentObjPtr moment);
void virDomainMomentMoveChildren(virDomainMomentObjPtr from,
                                 virDomainMomentObjPtr to);
void virDomainMomentSetParent(virDomainMomentObjPtr moment,
                              virDomainMomentObjPtr parent);

virDomainMomentObjListPtr virDomainMomentObjListNew(virDomainMomentObjListFilter filter);
void virDomainMomentObjListFree(virDomainMomentObjListPtr moments);

virDomainMomentObjPtr virDomainMomentAssignDef(virDomainMomentObjListPtr moments,
                                               virDomainMomentDefPtr def);

int virDomainMomentObjListGetNames(virDomainMomentObjListPtr moments,
                                   virDomainMomentObjPtr from,
                                   char **const names,
                                   int maxnames,
                                   unsigned int flags);
virDomainMomentObjPtr virDomainMomentFindByName(virDomainMomentObjListPtr moments,
                                                const char *name);
int virDomainMomentObjListSize(virDomainMomentObjListPtr moments);
virDomainMomentObjPtr virDomainMomentGetCurrent(virDomainMomentObjListPtr moments);
const char *virDomainMomentGetCurrentName(virDomainMomentObjListPtr moments);
bool virDomainMomentIsCurrentName(virDomainMomentObjListPtr moments,
                                  const char *name);
void virDomainMomentSetCurrent(virDomainMomentObjListPtr moments,
                               virDomainMomentObjPtr moment);
bool virDomainMomentObjListRemove(virDomainMomentObjListPtr moments,
                                  virDomainMomentObjPtr moment);
void virDomainMomentObjListRemoveAll(virDomainMomentObjListPtr moments);
int virDomainMomentForEach(virDomainMomentObjListPtr moments,
                           virHashIterator iter,
                           void *data);
int virDomainMomentUpdateRelations(virDomainMomentObjListPtr moments);

#endif /* LIBVIRT_VIRDOMAINMOMENTOBJLIST_H */
