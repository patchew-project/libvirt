/*
 * libvirt-domain-checkpoint.c: entry points for virDomainCheckpointPtr APIs
 *
 * Copyright (C) 2006-2014, 2018 Red Hat, Inc.
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

#include <config.h>

#include "datatypes.h"
#include "virlog.h"

VIR_LOG_INIT("libvirt.domain-checkpoint");

#define VIR_FROM_THIS VIR_FROM_DOMAIN_CHECKPOINT

/**
 * virDomainCheckpointGetName:
 * @checkpoint: a checkpoint object
 *
 * Get the public name for that checkpoint
 *
 * Returns a pointer to the name or NULL, the string need not be deallocated
 * as its lifetime will be the same as the checkpoint object.
 */
const char *
virDomainCheckpointGetName(virDomainCheckpointPtr checkpoint)
{
    VIR_DEBUG("checkpoint=%p", checkpoint);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, NULL);

    return checkpoint->name;
}


/**
 * virDomainCheckpointGetDomain:
 * @checkpoint: a checkpoint object
 *
 * Provides the domain pointer associated with a checkpoint.  The
 * reference counter on the domain is not increased by this
 * call.
 *
 * Returns the domain or NULL.
 */
virDomainPtr
virDomainCheckpointGetDomain(virDomainCheckpointPtr checkpoint)
{
    VIR_DEBUG("checkpoint=%p", checkpoint);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, NULL);

    return checkpoint->domain;
}


/**
 * virDomainCheckpointGetConnect:
 * @checkpoint: a checkpoint object
 *
 * Provides the connection pointer associated with a checkpoint.  The
 * reference counter on the connection is not increased by this
 * call.
 *
 * Returns the connection or NULL.
 */
virConnectPtr
virDomainCheckpointGetConnect(virDomainCheckpointPtr checkpoint)
{
    VIR_DEBUG("checkpoint=%p", checkpoint);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, NULL);

    return checkpoint->domain->conn;
}


/**
 * virDomainCheckpointCreateXML:
 * @domain: a domain object
 * @xmlDesc: description of the checkpoint to create
 * @flags: bitwise-OR of supported virDomainCheckpointCreateFlags
 *
 * Create a new checkpoint using @xmlDesc on a running @domain.
 * Typically, it is more common to create a new checkpoint as part of
 * kicking off a backup job with virDomainBackupBegin(); however, it
 * is also possible to start a checkpoint without a backup.
 *
 * See formatcheckpoint.html#CheckpointAttributes document for more
 * details on @xmlDesc.
 *
 * If @flags includes VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE, then this
 * is a request to reinstate checkpoint metadata that was previously
 * discarded, rather than creating a new checkpoint.  When redefining
 * checkpoint metadata, the current checkpoint will not be altered
 * unless the VIR_DOMAIN_CHECKPOINT_CREATE_CURRENT flag is also
 * present.  It is an error to request the
 * VIR_DOMAIN_CHECKPOINT_CREATE_CURRENT flag without
 * VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE.
 *
 * If @flags includes VIR_DOMAIN_CHECKPOINT_CREATE_NO_METADATA, then
 * the domain's disk images are modified according to @xmlDesc, but
 * then the just-created checkpoint has its metadata deleted.  This
 * flag is incompatible with VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE.
 *
 * Returns an (opaque) new virDomainCheckpointPtr on success, or NULL
 * on failure.
 */
virDomainCheckpointPtr
virDomainCheckpointCreateXML(virDomainPtr domain,
                             const char *xmlDesc,
                             unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "xmlDesc=%s, flags=0x%x", xmlDesc, flags);

    virResetLastError();

    virCheckDomainReturn(domain, NULL);
    conn = domain->conn;

    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(conn->flags, error);

    VIR_REQUIRE_FLAG_GOTO(VIR_DOMAIN_CHECKPOINT_CREATE_CURRENT,
                          VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE,
                          error);

    VIR_EXCLUSIVE_FLAGS_GOTO(VIR_DOMAIN_CHECKPOINT_CREATE_REDEFINE,
                             VIR_DOMAIN_CHECKPOINT_CREATE_NO_METADATA,
                             error);

    if (conn->driver->domainCheckpointCreateXML) {
        virDomainCheckpointPtr ret;
        ret = conn->driver->domainCheckpointCreateXML(domain, xmlDesc, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virDomainCheckpointGetXMLDesc:
 * @checkpoint: a domain checkpoint object
 * @flags: bitwise-OR of subset of virDomainXMLFlags
 *
 * Provide an XML description of the domain checkpoint.
 *
 * No security-sensitive data will be included unless @flags contains
 * VIR_DOMAIN_XML_SECURE; this flag is rejected on read-only
 * connections.  For this API, @flags should not contain either
 * VIR_DOMAIN_XML_INACTIVE or VIR_DOMAIN_XML_UPDATE_CPU.
 *
 * Returns a 0 terminated UTF-8 encoded XML instance, or NULL in case of error.
 *         the caller must free() the returned value.
 */
char *
virDomainCheckpointGetXMLDesc(virDomainCheckpointPtr checkpoint,
                              unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("checkpoint=%p, flags=0x%x", checkpoint, flags);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, NULL);
    conn = checkpoint->domain->conn;

    if ((conn->flags & VIR_CONNECT_RO) && (flags & VIR_DOMAIN_XML_SECURE)) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("virDomainCheckpointGetXMLDesc with secure flag"));
        goto error;
    }

    if (conn->driver->domainCheckpointGetXMLDesc) {
        char *ret;
        ret = conn->driver->domainCheckpointGetXMLDesc(checkpoint, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virDomainListCheckpoints:
 * @domain: a domain object
 * @checkpoints: pointer to variable to store the array containing checkpoint
 *               objects, or NULL if the list is not required (just returns
 *               number of checkpoints)
 * @flags: bitwise-OR of supported virDomainCheckpoinListFlags
 *
 * Collect the list of domain checkpoints for the given domain, and allocate
 * an array to store those objects.
 *
 * By default, this command covers all checkpoints; it is also possible to
 * limit things to just checkpoints with no parents, when @flags includes
 * VIR_DOMAIN_CHECKPOINT_LIST_ROOTS.  Additional filters are provided in
 * groups, where each group contains bits that describe mutually exclusive
 * attributes of a checkpoint, and where all bits within a group describe
 * all possible checkpoints.  Some hypervisors might reject explicit bits
 * from a group where the hypervisor cannot make a distinction.  For a
 * group supported by a given hypervisor, the behavior when no bits of a
 * group are set is identical to the behavior when all bits in that group
 * are set.  When setting bits from more than one group, it is possible to
 * select an impossible combination, in that case a hypervisor may return
 * either 0 or an error.
 *
 * The first group of @flags is VIR_DOMAIN_CHECKPOINT_LIST_LEAVES and
 * VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES, to filter based on checkpoints that
 * have no further children (a leaf checkpoint).
 *
 * The next group of @flags is VIR_DOMAIN_CHECKPOINT_LIST_METADATA and
 * VIR_DOMAIN_CHECKPOINT_LIST_NO_METADATA, for filtering checkpoints based on
 * whether they have metadata that would prevent the removal of the last
 * reference to a domain.
 *
 * Returns the number of domain checkpoints found or -1 and sets @checkpoints
 * to NULL in case of error.  On success, the array stored into @checkpoints
 * is guaranteed to have an extra allocated element set to NULL but not
 * included in the return count, to make iteration easier.  The caller is
 * responsible for calling virDomainCheckpointFree() on each array element,
 * then calling free() on @checkpoints.
 */
int
virDomainListCheckpoints(virDomainPtr domain,
                         virDomainCheckpointPtr **checkpoints,
                         unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "checkpoints=%p, flags=0x%x", checkpoints, flags);

    virResetLastError();

    if (checkpoints)
        *checkpoints = NULL;

    virCheckDomainReturn(domain, -1);
    conn = domain->conn;

    if (conn->driver->domainListCheckpoints) {
        int ret = conn->driver->domainListCheckpoints(domain, checkpoints,
                                                      flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointListChildren:
 * @checkpoint: a domain checkpoint object
 * @children: pointer to variable to store the array containing checkpoint
 *            objects, or NULL if the list is not required (just returns
 *            number of checkpoints)
 * @flags: bitwise-OR of supported virDomainCheckpointListFlags
 *
 * Collect the list of domain checkpoints that are children of the given
 * checkpoint, and allocate an array to store those objects.
 *
 * By default, this command covers only direct children; it is also possible
 * to expand things to cover all descendants, when @flags includes
 * VIR_DOMAIN_CHECKPOINT_LIST_DESCENDANTS.  Also, some filters are provided in
 * groups, where each group contains bits that describe mutually exclusive
 * attributes of a snapshot, and where all bits within a group describe
 * all possible snapshots.  Some hypervisors might reject explicit bits
 * from a group where the hypervisor cannot make a distinction.  For a
 * group supported by a given hypervisor, the behavior when no bits of a
 * group are set is identical to the behavior when all bits in that group
 * are set.  When setting bits from more than one group, it is possible to
 * select an impossible combination, in that case a hypervisor may return
 * either 0 or an error.
 *
 * The first group of @flags is VIR_DOMAIN_CHECKPOINT_LIST_LEAVES and
 * VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES, to filter based on checkpoints that
 * have no further children (a leaf checkpoint).
 *
 * The next group of @flags is VIR_DOMAIN_CHECKPOINT_LIST_METADATA and
 * VIR_DOMAIN_CHECKPOINT_LIST_NO_METADATA, for filtering checkpoints based on
 * whether they have metadata that would prevent the removal of the last
 * reference to a domain.
 *
 * Returns the number of domain checkpoints found or -1 and sets @children to
 * NULL in case of error.  On success, the array stored into @children is
 * guaranteed to have an extra allocated element set to NULL but not included
 * in the return count, to make iteration easier.  The caller is responsible
 * for calling virDomainCheckpointFree() on each array element, then calling
 * free() on @children.
 */
int
virDomainCheckpointListChildren(virDomainCheckpointPtr checkpoint,
                                virDomainCheckpointPtr **children,
                                unsigned int flags)
{
    virConnectPtr conn;

    VIR_DEBUG("checkpoint=%p, children=%p, flags=0x%x",
              checkpoint, children, flags);

    virResetLastError();

    if (children)
        *children = NULL;

    virCheckDomainCheckpointReturn(checkpoint, -1);
    conn = checkpoint->domain->conn;

    if (conn->driver->domainCheckpointListChildren) {
        int ret = conn->driver->domainCheckpointListChildren(checkpoint,
                                                             children, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointLookupByName:
 * @domain: a domain object
 * @name: name for the domain checkpoint
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Try to lookup a domain checkpoint based on its name.
 *
 * Returns a domain checkpoint object or NULL in case of failure.  If the
 * domain checkpoint cannot be found, then the VIR_ERR_NO_DOMAIN_CHECKPOINT
 * error is raised.
 */
virDomainCheckpointPtr
virDomainCheckpointLookupByName(virDomainPtr domain,
                                const char *name,
                                unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "name=%s, flags=0x%x", name, flags);

    virResetLastError();

    virCheckDomainReturn(domain, NULL);
    conn = domain->conn;

    virCheckNonNullArgGoto(name, error);

    if (conn->driver->domainCheckpointLookupByName) {
        virDomainCheckpointPtr dom;
        dom = conn->driver->domainCheckpointLookupByName(domain, name, flags);
        if (!dom)
            goto error;
        return dom;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virDomainHasCurrentCheckpoint:
 * @domain: pointer to the domain object
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Determine if the domain has a current checkpoint.
 *
 * Returns 1 if such checkpoint exists, 0 if it doesn't, -1 on error.
 */
int
virDomainHasCurrentCheckpoint(virDomainPtr domain, unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "flags=0x%x", flags);

    virResetLastError();

    virCheckDomainReturn(domain, -1);
    conn = domain->conn;

    if (conn->driver->domainHasCurrentCheckpoint) {
        int ret = conn->driver->domainHasCurrentCheckpoint(domain, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointCurrent:
 * @domain: a domain object
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Get the current checkpoint for a domain, if any.
 *
 * virDomainCheckpointFree should be used to free the resources after the
 * checkpoint object is no longer needed.
 *
 * Returns a domain checkpoint object or NULL in case of failure.  If the
 * current domain checkpoint cannot be found, then the
 * VIR_ERR_NO_DOMAIN_CHECKPOINT error is raised.
 */
virDomainCheckpointPtr
virDomainCheckpointCurrent(virDomainPtr domain,
                           unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "flags=0x%x", flags);

    virResetLastError();

    virCheckDomainReturn(domain, NULL);
    conn = domain->conn;

    if (conn->driver->domainCheckpointCurrent) {
        virDomainCheckpointPtr snap;
        snap = conn->driver->domainCheckpointCurrent(domain, flags);
        if (!snap)
            goto error;
        return snap;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virDomainCheckpointGetParent:
 * @checkpoint: a checkpoint object
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Get the parent checkpoint for @checkpoint, if any.
 *
 * virDomainCheckpointFree should be used to free the resources after the
 * checkpoint object is no longer needed.
 *
 * Returns a domain checkpoint object or NULL in case of failure.  If the
 * given checkpoint is a root (no parent), then the VIR_ERR_NO_DOMAIN_CHECKPOINT
 * error is raised.
 */
virDomainCheckpointPtr
virDomainCheckpointGetParent(virDomainCheckpointPtr checkpoint,
                             unsigned int flags)
{
    virConnectPtr conn;

    VIR_DEBUG("checkpoint=%p, flags=0x%x", checkpoint, flags);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, NULL);
    conn = checkpoint->domain->conn;

    if (conn->driver->domainCheckpointGetParent) {
        virDomainCheckpointPtr snap;
        snap = conn->driver->domainCheckpointGetParent(checkpoint, flags);
        if (!snap)
            goto error;
        return snap;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virDomainCheckpointIsCurrent:
 * @checkpoint: a checkpoint object
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Determine if the given checkpoint is the domain's current checkpoint.  See
 * also virDomainHasCurrentCheckpoint().
 *
 * Returns 1 if current, 0 if not current, or -1 on error.
 */
int
virDomainCheckpointIsCurrent(virDomainCheckpointPtr checkpoint,
                             unsigned int flags)
{
    virConnectPtr conn;

    VIR_DEBUG("checkpoint=%p, flags=0x%x", checkpoint, flags);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, -1);
    conn = checkpoint->domain->conn;

    if (conn->driver->domainCheckpointIsCurrent) {
        int ret;
        ret = conn->driver->domainCheckpointIsCurrent(checkpoint, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointHasMetadata:
 * @checkpoint: a checkpoint object
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Determine if the given checkpoint is associated with libvirt metadata
 * that would prevent the deletion of the domain.
 *
 * Returns 1 if the checkpoint has metadata, 0 if the checkpoint exists without
 * help from libvirt, or -1 on error.
 */
int
virDomainCheckpointHasMetadata(virDomainCheckpointPtr checkpoint,
                               unsigned int flags)
{
    virConnectPtr conn;

    VIR_DEBUG("checkpoint=%p, flags=0x%x", checkpoint, flags);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, -1);
    conn = checkpoint->domain->conn;

    if (conn->driver->domainCheckpointHasMetadata) {
        int ret;
        ret = conn->driver->domainCheckpointHasMetadata(checkpoint, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointDelete:
 * @checkpoint: the checkpoint to remove
 * @flags: not used yet, pass 0
 * @flags: bitwise-OR of supported virDomainCheckpointDeleteFlags
 *
 * Removes a checkpoint from the domain.
 *
 * When removing a checkpoint, the record of which portions of the
 * disk were dirtied after the checkpoint will be merged into the
 * record tracked by the parent checkpoint, if any.  Likewise, if the
 * checkpoint being deleted was the current checkpoint, the parent
 * checkpoint becomes the new current checkpoint.
 *
 * If @flags includes VIR_DOMAIN_CHECKPOINT_DELETE_METADATA_ONLY, then
 * any checkpoint metadata tracked by libvirt is removed while keeping
 * the checkpoint contents intact; if a hypervisor does not require
 * any libvirt metadata to track checkpoints, then this flag is
 * silently ignored.
 *
 * Returns 0 on success, -1 on error.
 */
int
virDomainCheckpointDelete(virDomainCheckpointPtr checkpoint,
                          unsigned int flags)
{
    virConnectPtr conn;

    VIR_DEBUG("checkpoint=%p, flags=0x%x", checkpoint, flags);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, -1);
    conn = checkpoint->domain->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    VIR_EXCLUSIVE_FLAGS_GOTO(VIR_DOMAIN_CHECKPOINT_DELETE_CHILDREN,
                             VIR_DOMAIN_CHECKPOINT_DELETE_CHILDREN_ONLY,
                             error);

    if (conn->driver->domainCheckpointDelete) {
        int ret = conn->driver->domainCheckpointDelete(checkpoint, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}


/**
 * virDomainCheckpointRef:
 * @checkpoint: the checkpoint to hold a reference on
 *
 * Increment the reference count on the checkpoint. For each
 * additional call to this method, there shall be a corresponding
 * call to virDomainCheckpointFree to release the reference count, once
 * the caller no longer needs the reference to this object.
 *
 * This method is typically useful for applications where multiple
 * threads are using a connection, and it is required that the
 * connection and domain remain open until all threads have finished
 * using the checkpoint. ie, each new thread using a checkpoint would
 * increment the reference count.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
int
virDomainCheckpointRef(virDomainCheckpointPtr checkpoint)
{
    VIR_DEBUG("checkpoint=%p, refs=%d", checkpoint,
              checkpoint ? checkpoint->parent.u.s.refs : 0);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, -1);

    virObjectRef(checkpoint);
    return 0;
}


/**
 * virDomainCheckpointFree:
 * @checkpoint: a domain checkpoint object
 *
 * Free the domain checkpoint object.  The checkpoint itself is not modified.
 * The data structure is freed and should not be used thereafter.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
int
virDomainCheckpointFree(virDomainCheckpointPtr checkpoint)
{
    VIR_DEBUG("checkpoint=%p", checkpoint);

    virResetLastError();

    virCheckDomainCheckpointReturn(checkpoint, -1);

    virObjectUnref(checkpoint);
    return 0;
}
