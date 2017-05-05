/*
 * backup_conf.h: domain backup XML processing
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
 *
 */

#ifndef __BACKUP_CONF_H
# define __BACKUP_CONF_H

# include "internal.h"
# include "domain_conf.h"

/* Items related to backup state */

/* Stores disk-backup information */
typedef struct _virDomainBackupDiskDef virDomainBackupDiskDef;
typedef virDomainBackupDiskDef *virDomainBackupDiskDefPtr;
struct _virDomainBackupDiskDef {
    char *name;     /* name matching the <target dev='...' of the domain */
    virStorageSourcePtr target;
    virDomainDiskDefPtr vmdisk;
};

/* Stores the complete backup metadata */
typedef struct _virDomainBackupDef virDomainBackupDef;
typedef virDomainBackupDef *virDomainBackupDefPtr;
struct _virDomainBackupDef {
    /* Public XML. */
    char *name;
    char *description;
    long long creationTime; /* in seconds */

    size_t ndisks; /* should not exceed dom->ndisks */
    virDomainBackupDiskDef *disks;
};

virDomainBackupDefPtr
virDomainBackupDefParseString(const char *xmlStr,
                              virCapsPtr caps,
                              virDomainXMLOptionPtr xmlopt,
                              unsigned int flags);
virDomainBackupDefPtr
virDomainBackupDefParseNode(xmlDocPtr xml,
                            xmlNodePtr root,
                            virCapsPtr caps,
                            virDomainXMLOptionPtr xmlopt,
                            unsigned int flags);
void
virDomainBackupDefFree(virDomainBackupDefPtr def);

int
virDomainBackupDefResolveDisks(virDomainBackupDefPtr def,
                               virDomainDefPtr vmdef);

#endif /* __BACKUP_CONF_H */
