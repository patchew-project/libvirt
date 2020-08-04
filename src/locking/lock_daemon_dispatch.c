/*
 * lock_daemon_dispatch.c: lock management daemon dispatch
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "rpc/virnetdaemon.h"
#include "rpc/virnetserverclient.h"
#include "virlog.h"
#include "virstring.h"
#include "lock_daemon.h"
#include "lock_protocol.h"
#include "virerror.h"
#include "virthreadjob.h"

#define VIR_FROM_THIS VIR_FROM_RPC

VIR_LOG_INIT("locking.lock_daemon_dispatch");

#include "lock_daemon_dispatch_stubs.h"

static int
virLockSpaceProtocolDispatchAcquireResource(virNetServerPtr server G_GNUC_UNUSED,
                                            virNetServerClientPtr client,
                                            virNetMessagePtr msg G_GNUC_UNUSED,
                                            virNetMessageErrorPtr rerr,
                                            virLockSpaceProtocolAcquireResourceArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;
    unsigned int newFlags;

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_SHARED |
                      VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_AUTOCREATE, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    if (!(lockspace = virLockDaemonFindLockSpace(lockDaemon, args->path))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Lockspace for path %s does not exist"),
                       args->path);
        goto cleanup;
    }

    newFlags = 0;
    if (flags & VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_SHARED)
        newFlags |= VIR_LOCK_SPACE_ACQUIRE_SHARED;
    if (flags & VIR_LOCK_SPACE_PROTOCOL_ACQUIRE_RESOURCE_AUTOCREATE)
        newFlags |= VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE;

    if (virLockSpaceAcquireResource(lockspace,
                                    args->name,
                                    priv->ownerPid,
                                    newFlags) < 0)
        goto cleanup;

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchCreateResource(virNetServerPtr server G_GNUC_UNUSED,
                                           virNetServerClientPtr client,
                                           virNetMessagePtr msg G_GNUC_UNUSED,
                                           virNetMessageErrorPtr rerr,
                                           virLockSpaceProtocolCreateResourceArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    if (!(lockspace = virLockDaemonFindLockSpace(lockDaemon, args->path))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Lockspace for path %s does not exist"),
                       args->path);
        goto cleanup;
    }

    if (virLockSpaceCreateResource(lockspace, args->name) < 0)
        goto cleanup;

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchDeleteResource(virNetServerPtr server G_GNUC_UNUSED,
                                           virNetServerClientPtr client,
                                           virNetMessagePtr msg G_GNUC_UNUSED,
                                           virNetMessageErrorPtr rerr,
                                           virLockSpaceProtocolDeleteResourceArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    if (!(lockspace = virLockDaemonFindLockSpace(lockDaemon, args->path))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Lockspace for path %s does not exist"),
                       args->path);
        goto cleanup;
    }

    if (virLockSpaceDeleteResource(lockspace, args->name) < 0)
        goto cleanup;

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchNew(virNetServerPtr server G_GNUC_UNUSED,
                                virNetServerClientPtr client,
                                virNetMessagePtr msg G_GNUC_UNUSED,
                                virNetMessageErrorPtr rerr,
                                virLockSpaceProtocolNewArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    if (!args->path || STREQ(args->path, "")) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("the default lockspace already exists"));
        goto cleanup;
    }

    if (virLockDaemonFindLockSpace(lockDaemon, args->path) != NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Lockspace for path %s already exists"),
                       args->path);
        goto cleanup;
    }
    virResetLastError();

    lockspace = virLockSpaceNew(args->path);
    virLockDaemonAddLockSpace(lockDaemon, args->path, lockspace);

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchRegister(virNetServerPtr server G_GNUC_UNUSED,
                                     virNetServerClientPtr client,
                                     virNetMessagePtr msg G_GNUC_UNUSED,
                                     virNetMessageErrorPtr rerr,
                                     virLockSpaceProtocolRegisterArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!args->owner.id) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    priv->ownerName = g_strdup(args->owner.name);
    memcpy(priv->ownerUUID, args->owner.uuid, VIR_UUID_BUFLEN);
    priv->ownerId = args->owner.id;
    priv->ownerPid = args->owner.pid;
    VIR_DEBUG("ownerName=%s ownerId=%d ownerPid=%lld",
              priv->ownerName, priv->ownerId, (unsigned long long)priv->ownerPid);

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchReleaseResource(virNetServerPtr server G_GNUC_UNUSED,
                                            virNetServerClientPtr client,
                                            virNetMessagePtr msg G_GNUC_UNUSED,
                                            virNetMessageErrorPtr rerr,
                                            virLockSpaceProtocolReleaseResourceArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    if (!(lockspace = virLockDaemonFindLockSpace(lockDaemon, args->path))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Lockspace for path %s does not exist"),
                       args->path);
        goto cleanup;
    }

    if (virLockSpaceReleaseResource(lockspace,
                                    args->name,
                                    priv->ownerPid) < 0)
        goto cleanup;

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchRestrict(virNetServerPtr server G_GNUC_UNUSED,
                                     virNetServerClientPtr client,
                                     virNetMessagePtr msg G_GNUC_UNUSED,
                                     virNetMessageErrorPtr rerr,
                                     virLockSpaceProtocolRestrictArgs *args)
{
    int rv = -1;
    unsigned int flags = args->flags;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);

    virMutexLock(&priv->lock);

    virCheckFlagsGoto(0, cleanup);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (!priv->ownerId) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("lock owner details have not been registered"));
        goto cleanup;
    }

    priv->restricted = true;
    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}


static int
virLockSpaceProtocolDispatchCreateLockSpace(virNetServerPtr server G_GNUC_UNUSED,
                                            virNetServerClientPtr client,
                                            virNetMessagePtr msg G_GNUC_UNUSED,
                                            virNetMessageErrorPtr rerr,
                                            virLockSpaceProtocolCreateLockSpaceArgs *args)
{
    int rv = -1;
    virLockDaemonClientPtr priv =
        virNetServerClientGetPrivateData(client);
    virLockSpacePtr lockspace;

    virMutexLock(&priv->lock);

    if (priv->restricted) {
        virReportError(VIR_ERR_OPERATION_DENIED, "%s",
                       _("lock manager connection has been restricted"));
        goto cleanup;
    }

    if (virLockDaemonFindLockSpace(lockDaemon, args->path) != NULL) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("Lockspace for path %s already exists"),
                       args->path);
        goto cleanup;
    }

    if (!(lockspace = virLockSpaceNew(args->path)))
        goto cleanup;

    if (virLockDaemonAddLockSpace(lockDaemon, args->path, lockspace) < 0) {
        virLockSpaceFree(lockspace);
        goto cleanup;
    }

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    virMutexUnlock(&priv->lock);
    return rv;
}
