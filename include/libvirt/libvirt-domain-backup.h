/*
 * libvirt-domain-backup.h
 * Summary: APIs for management of domain backups
 * Description: Provides APIs for the management of domain backups
 * Author: Nikolay Shirokovskiy <nshirokovskiy@virtuozzo.com>
 *
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

#ifndef __VIR_LIBVIRT_DOMAIN_BACKUP_H__
# define __VIR_LIBVIRT_DOMAIN_BACKUP_H__

# ifndef __VIR_LIBVIRT_H_INCLUDES__
#  error "Don't include this file directly, only use libvirt/libvirt.h"
# endif

/**
 * virDomainBackup:
 *
 * a virDomainBackup is a private structure representing a backup of
 * a domain.
 */
typedef struct _virDomainBackup virDomainBackup;

/**
 * virDomainBackupPtr:
 *
 * a virDomainBackupPtr is pointer to a virDomainBackup private structure,
 * and is the type used to reference a domain backup in the API.
 */
typedef virDomainBackup *virDomainBackupPtr;

const char *virDomainBackupGetName(virDomainBackupPtr backup);
virDomainPtr virDomainBackupGetDomain(virDomainBackupPtr backup);
virConnectPtr virDomainBackupGetConnect(virDomainBackupPtr backup);

/* Take a backup of the current VM state */
virDomainBackupPtr virDomainBackupCreateXML(virDomainPtr domain,
                                            const char *xmlDesc,
                                            unsigned int flags);

int virDomainBackupRef(virDomainBackupPtr backup);
int virDomainBackupFree(virDomainBackupPtr backup);

#endif /* __VIR_LIBVIRT_DOMAIN_BACKUP_H__ */
