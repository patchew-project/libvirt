/*
 * backup_conf.h: domain backup XML processing
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

#ifndef __BACKUP_CONF_H
# define __BACKUP_CONF_H

# include "internal.h"
# include "domain_conf.h"

typedef struct _virDomainBackupDiskDef virDomainBackupDiskDef;
typedef virDomainBackupDiskDef *virDomainBackupDiskDefPtr;
struct _virDomainBackupDiskDef {
    char *name;     /* name matching the <target dev='...' of the domain */
    int present;    /* enum virTristateBool */
    int idx;        /* index within dom->disks that matches name */
    virStorageSourcePtr src;
};

typedef enum {
    VIR_DOMAIN_BACKUP_ADDRESS_IP,
    VIR_DOMAIN_BACKUP_ADDRESS_UNIX,

    VIR_DOMAIN_BACKUP_ADDRESS_LAST,
} virDomainBackupAddressType;

VIR_ENUM_DECL(virDomainBackupAddress)

typedef struct _virDomainBackupAddressDef virDomainBackupAddressDef;
typedef virDomainBackupAddressDef *virDomainBackupAddressDefPtr;
struct _virDomainBackupAddressDef {
    union {
        struct {
            char *host;
            int port;
        } ip;
        struct {
            char *path;
        } socket;
    } data;
    int type; /* virDomainBackupAddress */
};

/* Stores the complete backup metadata */
typedef struct _virDomainBackupDef virDomainBackupDef;
typedef virDomainBackupDef *virDomainBackupDefPtr;
struct _virDomainBackupDef {
    virDomainBackupAddressDef address;

    size_t ndisks;
    virDomainBackupDiskDef *disks;

    virDomainDefPtr dom;
};

virDomainBackupDefPtr virDomainBackupDefParseString(const char *xmlStr,
                                                    virCapsPtr caps,
                                                    virDomainXMLOptionPtr xmlopt,
                                                    unsigned int flags);

virDomainBackupDefPtr virDomainBackupDefParseNode(xmlDocPtr xml,
                                                  xmlNodePtr root,
                                                  virCapsPtr caps,
                                                  virDomainXMLOptionPtr xmlopt,
                                                  unsigned int flags);

void virDomainBackupDefFree(virDomainBackupDefPtr def);

#endif /* __BACKUP_CONF_H */
