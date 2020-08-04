/*
 * log_daemon_dispatch.c: log management daemon dispatch
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "rpc/virnetserver.h"
#include "rpc/virnetserverclient.h"
#include "virlog.h"
#include "virstring.h"
#include "log_daemon.h"
#include "log_protocol.h"
#include "virerror.h"
#include "virthreadjob.h"
#include "virfile.h"

#define VIR_FROM_THIS VIR_FROM_RPC

VIR_LOG_INIT("logging.log_daemon_dispatch");

#include "log_daemon_dispatch_stubs.h"

static int
virLogManagerProtocolDispatchDomainOpenLogFile(virNetServerPtr server G_GNUC_UNUSED,
                                               virNetServerClientPtr client G_GNUC_UNUSED,
                                               virNetMessagePtr msg,
                                               virNetMessageErrorPtr rerr,
                                               virLogManagerProtocolDomainOpenLogFileArgs *args,
                                               virLogManagerProtocolDomainOpenLogFileRet *ret)
{
    int fd = -1;
    int rv = -1;
    off_t offset;
    ino_t inode;
    bool trunc = args->flags & VIR_LOG_MANAGER_PROTOCOL_DOMAIN_OPEN_LOG_FILE_TRUNCATE;

    if ((fd = virLogHandlerDomainOpenLogFile(virLogDaemonGetHandler(logDaemon),
                                             args->driver,
                                             (unsigned char *)args->dom.uuid,
                                             args->dom.name,
                                             args->path,
                                             trunc,
                                             &inode, &offset)) < 0)
        goto cleanup;

    ret->pos.inode = inode;
    ret->pos.offset = offset;

    if (virNetMessageAddFD(msg, fd) < 0)
        goto cleanup;

    rv = 1; /* '1' tells caller we added some FDs */

 cleanup:
    VIR_FORCE_CLOSE(fd);
    if (rv < 0)
        virNetMessageSaveError(rerr);
    return rv;
}


static int
virLogManagerProtocolDispatchDomainGetLogFilePosition(virNetServerPtr server G_GNUC_UNUSED,
                                                      virNetServerClientPtr client G_GNUC_UNUSED,
                                                      virNetMessagePtr msg G_GNUC_UNUSED,
                                                      virNetMessageErrorPtr rerr,
                                                      virLogManagerProtocolDomainGetLogFilePositionArgs *args,
                                                      virLogManagerProtocolDomainGetLogFilePositionRet *ret)
{
    int rv = -1;
    off_t offset;
    ino_t inode;

    if (virLogHandlerDomainGetLogFilePosition(virLogDaemonGetHandler(logDaemon),
                                              args->path,
                                              args->flags,
                                              &inode, &offset) < 0)
        goto cleanup;

    ret->pos.inode = inode;
    ret->pos.offset = offset;

    rv = 0;
 cleanup:

    if (rv < 0)
        virNetMessageSaveError(rerr);
    return rv;
}


static int
virLogManagerProtocolDispatchDomainReadLogFile(virNetServerPtr server G_GNUC_UNUSED,
                                               virNetServerClientPtr client G_GNUC_UNUSED,
                                               virNetMessagePtr msg G_GNUC_UNUSED,
                                               virNetMessageErrorPtr rerr,
                                               virLogManagerProtocolDomainReadLogFileArgs *args,
                                               virLogManagerProtocolDomainReadLogFileRet *ret)
{
    int rv = -1;
    char *data;

    if (args->maxlen > VIR_LOG_MANAGER_PROTOCOL_STRING_MAX) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Requested data len %llu is larger than maximum %d"),
                       (unsigned long long)args->maxlen,
                       VIR_LOG_MANAGER_PROTOCOL_STRING_MAX);
        goto cleanup;
    }

    if ((data = virLogHandlerDomainReadLogFile(virLogDaemonGetHandler(logDaemon),
                                               args->path,
                                               args->pos.inode,
                                               args->pos.offset,
                                               args->maxlen,
                                               args->flags)) == NULL)
        goto cleanup;

    ret->data = data;

    rv = 0;

 cleanup:
    if (rv < 0)
        virNetMessageSaveError(rerr);
    return rv;
}


static int
virLogManagerProtocolDispatchDomainAppendLogFile(virNetServerPtr server G_GNUC_UNUSED,
                                                 virNetServerClientPtr client G_GNUC_UNUSED,
                                                 virNetMessagePtr msg G_GNUC_UNUSED,
                                                 virNetMessageErrorPtr rerr,
                                                 virLogManagerProtocolDomainAppendLogFileArgs *args,
                                                 virLogManagerProtocolDomainAppendLogFileRet *ret)
{
    int rv;

    if ((rv = virLogHandlerDomainAppendLogFile(virLogDaemonGetHandler(logDaemon),
                                               args->driver,
                                               (unsigned char *)args->dom.uuid,
                                               args->dom.name,
                                               args->path,
                                               args->message,
                                               args->flags)) < 0) {
        virNetMessageSaveError(rerr);
        return -1;
    }

    ret->ret = rv;
    return 0;
}
