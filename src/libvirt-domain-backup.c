/*
 * libvirt-domain-backup.c: entry points for virDomainBackupPtr APIs
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
 *
 * Author: Nikolay Shirokovskiy <nshirokovskiy@virtuozzo.com>
 */

#include <config.h>

#include "datatypes.h"
#include "virlog.h"

VIR_LOG_INIT("libvirt.domain-backup");

#define VIR_FROM_THIS VIR_FROM_DOMAIN_BACKUP

int
virDomainBackupStart(virDomainPtr domain,
                     const char *xmlDesc,
                     unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "xmlDesc=%s, flags=%x", xmlDesc, flags);

    virResetLastError();

    virCheckDomainReturn(domain, -1);
    conn = domain->conn;

    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->driver->domainBackupStart) {
        if (conn->driver->domainBackupStart(domain, xmlDesc, flags) < 0)
            goto error;

        return 0;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}

int
virDomainBackupStop(virDomainPtr domain,
                    unsigned int flags)
{
    virConnectPtr conn;

    VIR_DOMAIN_DEBUG(domain, "flags=%x", flags);

    virResetLastError();

    virCheckDomainReturn(domain, -1);
    conn = domain->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->driver->domainBackupStop) {
        if (conn->driver->domainBackupStop(domain, flags) < 0)
            goto error;

        return 0;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(conn);
    return -1;
}
