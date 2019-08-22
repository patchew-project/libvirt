/*
 * backup_conf.h: domain backup XML processing
 *                 (based on domain_conf.h)
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

#pragma once

#include "internal.h"
#include "domain_conf.h"
#include "moment_conf.h"
#include "virenum.h"

/* Items related to incremental backup state */

typedef enum {
    VIR_DOMAIN_BACKUP_TYPE_DEFAULT = 0,
    VIR_DOMAIN_BACKUP_TYPE_PUSH,
    VIR_DOMAIN_BACKUP_TYPE_PULL,

    VIR_DOMAIN_BACKUP_TYPE_LAST
} virDomainBackupType;

typedef enum {
    VIR_DOMAIN_BACKUP_DISK_STATE_DEFAULT = 0, /* Initial */
    VIR_DOMAIN_BACKUP_DISK_STATE_CREATED, /* File created */
    VIR_DOMAIN_BACKUP_DISK_STATE_LABEL, /* Security labels applied */
    VIR_DOMAIN_BACKUP_DISK_STATE_READY, /* Handed to guest */
    VIR_DOMAIN_BACKUP_DISK_STATE_BITMAP, /* Associated temp bitmap created */
    VIR_DOMAIN_BACKUP_DISK_STATE_EXPORT, /* NBD export created */
    VIR_DOMAIN_BACKUP_DISK_STATE_COMPLETE, /* Push job finished */
} virDomainBackupDiskState;

/* Stores disk-backup information */
typedef struct _virDomainBackupDiskDef virDomainBackupDiskDef;
typedef virDomainBackupDiskDef *virDomainBackupDiskDefPtr;
struct _virDomainBackupDiskDef {
    char *name;     /* name matching the <target dev='...' of the domain */
    int idx;        /* index within checkpoint->dom->disks that matches name */

    /* details of target for push-mode, or of the scratch file for pull-mode */
    virStorageSourcePtr store;
    int state;      /* virDomainBackupDiskState, not stored in XML */
};

/* Stores the complete backup metadata */
typedef struct _virDomainBackupDef virDomainBackupDef;
typedef virDomainBackupDef *virDomainBackupDefPtr;
struct _virDomainBackupDef {
    /* Public XML.  */
    int type; /* virDomainBackupType */
    int id;
    char *incremental;
    virStorageNetHostDefPtr server; /* only when type == PULL */

    size_t ndisks; /* should not exceed dom->ndisks */
    virDomainBackupDiskDef *disks;
};

VIR_ENUM_DECL(virDomainBackup);

typedef enum {
    VIR_DOMAIN_BACKUP_PARSE_INTERNAL = 1 << 0,
} virDomainBackupParseFlags;

virDomainBackupDefPtr virDomainBackupDefParseString(const char *xmlStr,
                                                    virDomainXMLOptionPtr xmlopt,
                                                    unsigned int flags);
virDomainBackupDefPtr virDomainBackupDefParseNode(xmlDocPtr xml,
                                                  xmlNodePtr root,
                                                  virDomainXMLOptionPtr xmlopt,
                                                  unsigned int flags);
void virDomainBackupDefFree(virDomainBackupDefPtr def);
int virDomainBackupDefFormat(virBufferPtr buf,
                             virDomainBackupDefPtr def,
                             bool internal);
int virDomainBackupAlignDisks(virDomainBackupDefPtr backup,
                              virDomainDefPtr dom, const char *suffix);
