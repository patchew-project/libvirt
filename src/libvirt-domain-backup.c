/*
 * libvirt-domain-backup.c: entry points for virDomainBackupPtr APIs

 * Copyright (C) 2017 Parallels International GmbH
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

VIR_LOG_INIT("libvirt.domain-backup");

#define VIR_FROM_THIS VIR_FROM_DOMAIN_BACKUP

/**
 * virDomainBackupGetName:
 * @backup: a backup object
 *
 * Get the public name for that backup
 *
 * Returns a pointer to the name or NULL, the string need not be deallocated
 * as its lifetime will be the same as the backup object.
 */
const char *
virDomainBackupGetName(virDomainBackupPtr backup)
{
    VIR_DEBUG("backup=%p", backup);

    virResetLastError();

    virCheckDomainBackupReturn(backup, NULL);

    return backup->name;
}


/**
 * virDomainBackupGetDomain:
 * @backup: a backup object
 *
 * Provides the domain pointer associated with a backup.  The
 * reference counter on the domain is not increased by this
 * call.
 *
 * WARNING: When writing libvirt bindings in other languages, do not use this
 * function.  Instead, store the domain and the backup object together.
 *
 * Returns the domain or NULL.
 */
virDomainPtr
virDomainBackupGetDomain(virDomainBackupPtr backup)
{
    VIR_DEBUG("backup=%p", backup);

    virResetLastError();

    virCheckDomainBackupReturn(backup, NULL);

    return backup->domain;
}


/**
 * virDomainBackupGetConnect:
 * @backup: a backup object
 *
 * Provides the connection pointer associated with a backup.  The
 * reference counter on the connection is not increased by this
 * call.
 *
 * WARNING: When writing libvirt bindings in other languages, do not use this
 * function.  Instead, store the connection and the backup object together.
 *
 * Returns the connection or NULL.
 */
virConnectPtr
virDomainBackupGetConnect(virDomainBackupPtr backup)
{
    VIR_DEBUG("backup=%p", backup);

    virResetLastError();

    virCheckDomainBackupReturn(backup, NULL);

    return backup->domain->conn;
}


/**
 * virDomainBackupCreateXML:
 * @domain: a domain object
 * @xmlDesc: domain backup XML description
 * @flags: reserved, must be 0
 *
 * Starts creating of the domain disks backup based on the xml description in
 * @xmlDesc. Backup is a copy of the specified domain disks at the moment of
 * operation start.
 *
 * Backup creates a blockjob for every specified disk hence the backup
 * status can be tracked thru blockjob event API and the backup progress
 * is given by per blockjob virDomainBlockJobInfo. Backup can be cancelled by
 * cancelling any of its still active blockjobs via virDomainBlockJobAbort.
 *
 * Known issues. In case libvirt connection is lost and restored back and all
 * backup blockjobs are already gone then currenly it is not possible to know
 * whether backup is completed or failed.
 *
 * Returns an (opaque) virDomainBackupPtr on success, NULL on failure.
 */
virDomainBackupPtr
virDomainBackupCreateXML(virDomainPtr domain,
                         const char *xmlDesc,
                         unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "xmlDesc=%s, flags=%x", xmlDesc, flags);

    virResetLastError();

    virCheckDomainReturn(domain, NULL);
    conn = domain->conn;

    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->driver->domainBackupCreateXML) {
        virDomainBackupPtr ret;
        ret = conn->driver->domainBackupCreateXML(domain, xmlDesc, flags);
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
 * virDomainBackupRef:
 * @backup: the backup to hold a reference on
 *
 * Increment the reference count on the backup. For each
 * additional call to this method, there shall be a corresponding
 * call to virDomainBackupFree to release the reference count, once
 * the caller no longer needs the reference to this object.
 *
 * This method is typically useful for applications where multiple
 * threads are using a connection, and it is required that the
 * connection and domain remain open until all threads have finished
 * using the backup. ie, each new thread using a backup would
 * increment the reference count.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
int
virDomainBackupRef(virDomainBackupPtr backup)
{
    VIR_DEBUG("backup=%p, refs=%d", backup,
              backup ? backup->object.u.s.refs : 0);

    virResetLastError();

    virCheckDomainBackupReturn(backup, -1);

    virObjectRef(backup);
    return 0;
}


/**
 * virDomainBackupFree:
 * @backup: a domain backup object
 *
 * Free the domain backup object.  The backup itself is not modified.
 * The data structure is freed and should not be used thereafter.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
int
virDomainBackupFree(virDomainBackupPtr backup)
{
    VIR_DEBUG("backup=%p", backup);

    virResetLastError();

    virCheckDomainBackupReturn(backup, -1);

    virObjectUnref(backup);
    return 0;
}
