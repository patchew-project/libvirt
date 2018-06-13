/*
 * libvirt-domain-checkpoint.h
 * Summary: APIs for management of domain checkpoints
 * Description: Provides APIs for the management of domain checkpoints
 * Author: Eric Blake <eblake@redhat.com>
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
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

#ifndef __VIR_LIBVIRT_DOMAIN_CHECKPOINT_H__
# define __VIR_LIBVIRT_DOMAIN_CHECKPOINT_H__

# ifndef __VIR_LIBVIRT_H_INCLUDES__
#  error "Don't include this file directly, only use libvirt/libvirt.h"
# endif

/**
 * virDomainCheckpoint:
 *
 * A virDomainCheckpoint is a private structure representing a checkpoint of
 * a domain.  A checkpoint is useful for tracking which portions of the
 * domain disks have been altered since a point in time, but by itself does
 * not allow reverting back to that point in time.
 */
typedef struct _virDomainCheckpoint virDomainCheckpoint;

/**
 * virDomainCheckpointPtr:
 *
 * A virDomainCheckpointPtr is pointer to a virDomainCheckpoint
 * private structure, and is the type used to reference a domain
 * checkpoint in the API.
 */
typedef virDomainCheckpoint *virDomainCheckpointPtr;

const char *virDomainCheckpointGetName(virDomainCheckpointPtr checkpoint);
virDomainPtr virDomainCheckpointGetDomain(virDomainCheckpointPtr checkpoint);
virConnectPtr virDomainCheckpointGetConnect(virDomainCheckpointPtr checkpoint);

typedef enum {
    VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE    = (1 << 0), /* Restore or alter
                                                            metadata */
    VIR_DOMAIN_CHECKPOINT_CREATE_CURRENT     = (1 << 1), /* With redefine, make
                                                            checkpoint current */
    VIR_DOMAIN_CHECKPOINT_CREATE_NO_METADATA = (1 << 2), /* Make checkpoint without
                                                            remembering it */
} virDomainCheckpointCreateFlags;

/* Create a checkpoint using the current VM state. */
virDomainCheckpointPtr virDomainCheckpointCreateXML(virDomainPtr domain,
                                                    const char *xmlDesc,
                                                    unsigned int flags);

/* Dump the XML of a checkpoint */
char *virDomainCheckpointGetXMLDesc(virDomainCheckpointPtr checkpoint,
                                    unsigned int flags);

/**
 * virDomainCheckpointListFlags:
 *
 * Flags valid for virDomainListCheckpoints() and
 * virDomainCheckpointListChildren().  Note that the interpretation of
 * flag (1<<0) depends on which function it is passed to; but serves
 * to toggle the per-call default of whether the listing is shallow or
 * recursive.  Remaining bits come in groups; if all bits from a group
 * are 0, then that group is not used to filter results.  */
typedef enum {
    VIR_DOMAIN_CHECKPOINT_LIST_ROOTS       = (1 << 0), /* Filter by checkpoints
                                                          with no parents, when
                                                          listing a domain */
    VIR_DOMAIN_CHECKPOINT_LIST_DESCENDANTS = (1 << 0), /* List all descendants,
                                                          not just children, when
                                                          listing a checkpoint */

    VIR_DOMAIN_CHECKPOINT_LIST_LEAVES      = (1 << 1), /* Filter by checkpoints
                                                          with no children */
    VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES   = (1 << 2), /* Filter by checkpoints
                                                          that have children */

    VIR_DOMAIN_CHECKPOINT_LIST_METADATA    = (1 << 3), /* Filter by checkpoints
                                                          which have metadata */
    VIR_DOMAIN_CHECKPOINT_LIST_NO_METADATA = (1 << 4), /* Filter by checkpoints
                                                          with no metadata */
} virDomainCheckpointListFlags;

/* Get all checkpoint objects for this domain */
int virDomainListCheckpoints(virDomainPtr domain,
                             virDomainCheckpointPtr **checkpoints,
                             unsigned int flags);

/* Get all checkpoint object children for this checkpoint */
int virDomainCheckpointListChildren(virDomainCheckpointPtr checkpoint,
                                    virDomainCheckpointPtr **children,
                                    unsigned int flags);

/* Get a handle to a named checkpoint */
virDomainCheckpointPtr virDomainCheckpointLookupByName(virDomainPtr domain,
                                                       const char *name,
                                                       unsigned int flags);

/* Check whether a domain has a checkpoint which is currently used */
int virDomainHasCurrentCheckpoint(virDomainPtr domain, unsigned int flags);

/* Get a handle to the current checkpoint */
virDomainCheckpointPtr virDomainCheckpointCurrent(virDomainPtr domain,
                                                  unsigned int flags);

/* Get a handle to the parent checkpoint, if one exists */
virDomainCheckpointPtr virDomainCheckpointGetParent(virDomainCheckpointPtr checkpoint,
                                                    unsigned int flags);

/* Determine if a checkpoint is the current checkpoint of its domain.  */
int virDomainCheckpointIsCurrent(virDomainCheckpointPtr checkpoint,
                                 unsigned int flags);

/* Determine if checkpoint has metadata that would prevent domain deletion.  */
int virDomainCheckpointHasMetadata(virDomainCheckpointPtr checkpoint,
                                   unsigned int flags);

/* Delete a checkpoint */
typedef enum {
    VIR_DOMAIN_CHECKPOINT_DELETE_CHILDREN      = (1 << 0), /* Also delete children */
    VIR_DOMAIN_CHECKPOINT_DELETE_METADATA_ONLY = (1 << 1), /* Delete just metadata */
    VIR_DOMAIN_CHECKPOINT_DELETE_CHILDREN_ONLY = (1 << 2), /* Delete just children */
} virDomainCheckpointDeleteFlags;

int virDomainCheckpointDelete(virDomainCheckpointPtr checkpoint,
                              unsigned int flags);

int virDomainCheckpointRef(virDomainCheckpointPtr checkpoint);
int virDomainCheckpointFree(virDomainCheckpointPtr checkpoint);

/* Begin an incremental backup job, possibly creating a checkpoint. */
int virDomainBackupBegin(virDomainPtr domain, const char *diskXml,
                         const char *checkpointXml, unsigned int flags);

/* Learn about an ongoing backup job. */
char *virDomainBackupGetXMLDesc(virDomainPtr domain, int id,
                                unsigned int flags);

/* Complete an incremental backup job. */
int virDomainBackupEnd(virDomainPtr domain, int id, unsigned int flags);

#endif /* __VIR_LIBVIRT_DOMAIN_CHECKPOINT_H__ */
