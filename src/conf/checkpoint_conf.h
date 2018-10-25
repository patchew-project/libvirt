/*
 * checkpoint_conf.h: domain checkpoint XML processing
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
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
 *
 * Author: Eric Blake <eblake@redhat.com>
 */

#ifndef __CHECKPOINT_CONF_H
# define __CHECKPOINT_CONF_H

# include "internal.h"
# include "domain_conf.h"

/* Items related to checkpoint state */

typedef enum {
    VIR_DOMAIN_CHECKPOINT_TYPE_DEFAULT = 0,
    VIR_DOMAIN_CHECKPOINT_TYPE_NONE,
    VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP,

    VIR_DOMAIN_CHECKPOINT_TYPE_LAST
} virDomainCheckpointType;

/* Stores disk-checkpoint information */
typedef struct _virDomainCheckpointDiskDef virDomainCheckpointDiskDef;
typedef virDomainCheckpointDiskDef *virDomainCheckpointDiskDefPtr;
struct _virDomainCheckpointDiskDef {
    char *name;     /* name matching the <target dev='...' of the domain */
    char *node;     /* corresponding node name, internal use only */
    int idx;        /* index within checkpoint->dom->disks that matches name */
    int type;       /* virDomainCheckpointType */
    char *bitmap;   /* bitmap name, if type is bitmap */
    unsigned long long size; /* current checkpoint size in bytes */
};

/* Stores the complete checkpoint metadata */
typedef struct _virDomainCheckpointDef virDomainCheckpointDef;
typedef virDomainCheckpointDef *virDomainCheckpointDefPtr;
struct _virDomainCheckpointDef {
    /* Public XML.  */
    char *name;
    char *description;
    char *parent;
    long long creationTime; /* in seconds */

    size_t ndisks; /* should not exceed dom->ndisks */
    virDomainCheckpointDiskDef *disks;

    virDomainDefPtr dom;

    /* Internal use.  */
    bool current; /* At most one checkpoint in the list should have this set */
};

struct _virDomainCheckpointObj {
    virDomainCheckpointDefPtr def; /* non-NULL except for metaroot */

    virDomainCheckpointObjPtr parent; /* non-NULL except for metaroot, before
                                         virDomainCheckpointUpdateRelations, or
                                         after virDomainCheckpointDropParent */
    virDomainCheckpointObjPtr sibling; /* NULL if last child of parent */
    size_t nchildren;
    virDomainCheckpointObjPtr first_child; /* NULL if no children */
};

virDomainCheckpointObjListPtr virDomainCheckpointObjListNew(void);
void virDomainCheckpointObjListFree(virDomainCheckpointObjListPtr checkpoints);

typedef enum {
    VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE = 1 << 0,
    VIR_DOMAIN_CHECKPOINT_PARSE_DISKS    = 1 << 1,
    VIR_DOMAIN_CHECKPOINT_PARSE_INTERNAL = 1 << 2,
} virDomainCheckpointParseFlags;

virDomainCheckpointDefPtr virDomainCheckpointDefParseString(const char *xmlStr,
                                                            virCapsPtr caps,
                                                            virDomainXMLOptionPtr xmlopt,
                                                            unsigned int flags);
virDomainCheckpointDefPtr virDomainCheckpointDefParseNode(xmlDocPtr xml,
                                                          xmlNodePtr root,
                                                          virCapsPtr caps,
                                                          virDomainXMLOptionPtr xmlopt,
                                                          unsigned int flags);
void virDomainCheckpointDefFree(virDomainCheckpointDefPtr def);
char *virDomainCheckpointDefFormat(virDomainCheckpointDefPtr def,
                                   virCapsPtr caps,
                                   virDomainXMLOptionPtr xmlopt,
                                   unsigned int flags,
                                   bool internal);
int virDomainCheckpointAlignDisks(virDomainCheckpointDefPtr checkpoint);
virDomainCheckpointObjPtr virDomainCheckpointAssignDef(virDomainCheckpointObjListPtr checkpoints,
                                                       virDomainCheckpointDefPtr def);

virDomainCheckpointObjPtr virDomainCheckpointFindByName(virDomainCheckpointObjListPtr checkpoints,
                                                        const char *name);
void virDomainCheckpointObjListRemove(virDomainCheckpointObjListPtr checkpoints,
                                      virDomainCheckpointObjPtr checkpoint);
int virDomainCheckpointForEach(virDomainCheckpointObjListPtr checkpoints,
                               virHashIterator iter,
                               void *data);
int virDomainCheckpointForEachChild(virDomainCheckpointObjPtr checkpoint,
                                    virHashIterator iter,
                                    void *data);
int virDomainCheckpointForEachDescendant(virDomainCheckpointObjPtr checkpoint,
                                         virHashIterator iter,
                                         void *data);
int virDomainCheckpointUpdateRelations(virDomainCheckpointObjListPtr checkpoints);
void virDomainCheckpointDropParent(virDomainCheckpointObjPtr checkpoint);

# define VIR_DOMAIN_CHECKPOINT_FILTERS_METADATA \
               (VIR_DOMAIN_CHECKPOINT_LIST_METADATA     | \
                VIR_DOMAIN_CHECKPOINT_LIST_NO_METADATA)

# define VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES \
               (VIR_DOMAIN_CHECKPOINT_LIST_LEAVES       | \
                VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES)

# define VIR_DOMAIN_CHECKPOINT_FILTERS_ALL \
               (VIR_DOMAIN_CHECKPOINT_FILTERS_METADATA  | \
                VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES)

int virDomainListAllCheckpoints(virDomainCheckpointObjListPtr checkpoints,
                                virDomainCheckpointObjPtr from,
                                virDomainPtr dom,
                                virDomainCheckpointPtr **objs,
                                unsigned int flags);

int virDomainCheckpointRedefinePrep(virDomainPtr domain,
                                    virDomainObjPtr vm,
                                    virDomainCheckpointDefPtr *def,
                                    virDomainCheckpointObjPtr *checkpoint,
                                    virDomainXMLOptionPtr xmlopt,
                                    bool *update_current);

VIR_ENUM_DECL(virDomainCheckpoint)

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

VIR_ENUM_DECL(virDomainBackup)

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

#endif /* __CHECKPOINT_CONF_H */
