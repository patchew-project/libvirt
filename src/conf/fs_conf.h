/*
 * fs_conf.h: config handling for fs driver
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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

#ifndef __VIR_FS_CONF_H__
# define __VIR_FS_CONF_H__

# include "internal.h"
# include "virbitmap.h"
# include "virthread.h"
# include "virutil.h"

# include <libxml/tree.h>

# define VIR_CONNECT_LIST_FSPOOLS_FILTERS_POOL_TYPE  \
                 VIR_CONNECT_LIST_FSPOOLS_DIR

# define VIR_CONNECT_LIST_FSPOOLS_FILTERS_ACTIVE   \
                (VIR_CONNECT_LIST_FSPOOLS_ACTIVE | \
                 VIR_CONNECT_LIST_FSPOOLS_INACTIVE)

# define VIR_CONNECT_LIST_FSPOOLS_FILTERS_PERSISTENT   \
                (VIR_CONNECT_LIST_FSPOOLS_PERSISTENT | \
                 VIR_CONNECT_LIST_FSPOOLS_TRANSIENT)

# define VIR_CONNECT_LIST_FSPOOLS_FILTERS_AUTOSTART    \
                (VIR_CONNECT_LIST_FSPOOLS_AUTOSTART |  \
                 VIR_CONNECT_LIST_FSPOOLS_NO_AUTOSTART)

# define VIR_CONNECT_LIST_FSPOOLS_FILTERS_ALL                  \
                (VIR_CONNECT_LIST_FSPOOLS_FILTERS_ACTIVE     | \
                 VIR_CONNECT_LIST_FSPOOLS_FILTERS_PERSISTENT | \
                 VIR_CONNECT_LIST_FSPOOLS_FILTERS_AUTOSTART  | \
                 VIR_CONNECT_LIST_FSPOOLS_FILTERS_POOL_TYPE)

VIR_ENUM_DECL(virFSItem)
VIR_ENUM_DECL(virFS)

typedef struct _virFSPerms virFSPerms;
typedef virFSPerms *virFSPermsPtr;
struct _virFSPerms {
    mode_t mode;
    uid_t uid;
    gid_t gid;
    char *label;
};

typedef struct _virFSSourcePoolDef virFSSourcePoolDef;
struct _virFSSourcePoolDef {
    char *pool; /* pool name */
    char *item; /* item name */
    int itemtype; /* virFSItemType, internal only */
    int pooltype; /* virFSPoolType internal only */
};
typedef virFSSourcePoolDef *virFSSourcePoolDefPtr;

typedef struct _virFSSource virFSSource;
typedef virFSSource *virFSSourcePtr;

struct _virFSSource {
    int type; /* virFSType */
    char *path;
    virFSSourcePoolDefPtr srcpool;
    char *driverName;
    virFSPermsPtr perms;
    unsigned long long capacity; /* in bytes, 0 if unknown */
    unsigned long long allocation; /* in bytes, 0 if unknown */
};

typedef enum {
    VIR_FSPOOL_DIR,      /* Local directory */
    VIR_FSPOOL_LAST,
} virFSPoolType;

VIR_ENUM_DECL(virFSPool)

typedef struct _virFSItemDef virFSItemDef;
typedef virFSItemDef *virFSItemDefPtr;
struct _virFSItemDef {
    char *name;
    char *key;
    int type; /* virFSItemType */

    bool building;
    unsigned int in_use;

    virFSSource target;
};

typedef struct _virFSItemDefList virFSItemDefList;
typedef virFSItemDefList *virFSItemDefListPtr;
struct _virFSItemDefList {
    size_t count;
    virFSItemDefPtr *objs;
};

typedef struct _virFSPoolSource virFSPoolSource;
typedef virFSPoolSource *virFSPoolSourcePtr;
struct _virFSPoolSource {
    /* An optional (maybe multiple) host(s) */

    /* Or a directory */
    char *dir;

    /* Or a name */
    char *name;

    /* Product name of the source*/
    char *product;

    /* Pool type specific format such as filesystem type,
     * or lvm version, etc.
     */
    int format;
};

typedef struct _virFSPoolTarget virFSPoolTarget;
typedef virFSPoolTarget *virFSPoolTargetPtr;
struct _virFSPoolTarget {
    char *path; /* Optional local filesystem mapping */
    virFSPerms perms; /* Default permissions for volumes */
};

typedef struct _virFSPoolDef virFSPoolDef;
typedef virFSPoolDef *virFSPoolDefPtr;
struct _virFSPoolDef {
    char *name;
    unsigned char uuid[VIR_UUID_BUFLEN];
    int type; /* virFSPoolType */

    unsigned long long allocation; /* bytes */
    unsigned long long capacity; /* bytes */
    unsigned long long available; /* bytes */

    virFSPoolSource source;
    virFSPoolTarget target;
};

typedef struct _virFSPoolObj virFSPoolObj;
typedef virFSPoolObj *virFSPoolObjPtr;

struct _virFSPoolObj {
    virMutex lock;

    char *configFile;
    char *autostartLink;
    bool active;
    int autostart;
    unsigned int asyncjobs;

    virFSPoolDefPtr def;
    virFSPoolDefPtr newDef;

    virFSItemDefList items;
};

typedef struct _virFSPoolObjList virFSPoolObjList;
typedef virFSPoolObjList *virFSPoolObjListPtr;
struct _virFSPoolObjList {
    size_t count;
    virFSPoolObjPtr *objs;
};

typedef struct _virFSDriverState virFSDriverState;
typedef virFSDriverState *virFSDriverStatePtr;

struct _virFSDriverState {
    virMutex lock;

    virFSPoolObjList fspools;

    char *configDir;
    char *autostartDir;
    char *stateDir;
    bool privileged;
};

typedef struct _virFSPoolSourceList virFSPoolSourceList;
typedef virFSPoolSourceList *virFSPoolSourceListPtr;
struct _virFSPoolSourceList {
    int type;
    unsigned int nsources;
    virFSPoolSourcePtr sources;
};

typedef bool (*virFSPoolObjListFilter)(virConnectPtr conn,
                                       virFSPoolDefPtr def);

static inline int
virFSPoolObjIsActive(virFSPoolObjPtr fspool)
{
    return fspool->active;
}

int virFSPoolLoadAllConfigs(virFSPoolObjListPtr fspools,
                            const char *configDir,
                            const char *autostartDir);

int virFSPoolLoadAllState(virFSPoolObjListPtr fspools,
                          const char *stateDir);

virFSPoolObjPtr
virFSPoolLoadState(virFSPoolObjListPtr fspools,
                   const char *stateDir,
                   const char *name);
virFSPoolObjPtr
virFSPoolObjFindByUUID(virFSPoolObjListPtr fspools,
                       const unsigned char *uuid);
virFSPoolObjPtr
virFSPoolObjFindByName(virFSPoolObjListPtr fspools,
                       const char *name);

virFSItemDefPtr
virFSItemDefFindByKey(virFSPoolObjPtr fspool,
                     const char *key);
virFSItemDefPtr
virFSItemDefFindByPath(virFSPoolObjPtr fspool,
                      const char *path);
virFSItemDefPtr
virFSItemDefFindByName(virFSPoolObjPtr fspool,
                      const char *name);

void virFSPoolObjClearItems(virFSPoolObjPtr fspool);

virFSPoolDefPtr virFSPoolDefParseString(const char *xml);
virFSPoolDefPtr virFSPoolDefParseFile(const char *filename);
virFSPoolDefPtr virFSPoolDefParseNode(xmlDocPtr xml,
                                      xmlNodePtr root);
char *virFSPoolDefFormat(virFSPoolDefPtr def);

typedef enum {
    /* do not require volume capacity at all */
    VIR_ITEM_XML_PARSE_NO_CAPACITY  = 1 << 0,
    /* do not require volume capacity if the volume has a backing store */
    VIR_ITEM_XML_PARSE_OPT_CAPACITY = 1 << 1,
} virFSItemDefParseFlags;

virFSItemDefPtr
virFSItemDefParseString(virFSPoolDefPtr fspool,
                       const char *xml,
                       unsigned int flags);
virFSItemDefPtr
virFSItemDefParseFile(virFSPoolDefPtr fspool,
                     const char *filename,
                     unsigned int flags);
virFSItemDefPtr
virFSItemDefParseNode(virFSPoolDefPtr fspool,
                     xmlDocPtr xml,
                     xmlNodePtr root,
                     unsigned int flags);
char *virFSItemDefFormat(virFSPoolDefPtr fspool,
                        virFSItemDefPtr def);

virFSPoolObjPtr
virFSPoolObjAssignDef(virFSPoolObjListPtr fspools,
                      virFSPoolDefPtr def);

int virFSPoolSaveState(const char *stateFile,
                       virFSPoolDefPtr def);
int virFSPoolSaveConfig(const char *configFile,
                        virFSPoolDefPtr def);
int virFSPoolObjSaveDef(virFSDriverStatePtr driver,
                        virFSPoolObjPtr fspool,
                        virFSPoolDefPtr def);
int virFSPoolObjDeleteDef(virFSPoolObjPtr fspool);

void virFSItemDefFree(virFSItemDefPtr def);
void virFSPoolSourceClear(virFSPoolSourcePtr source);
void virFSPoolSourceFree(virFSPoolSourcePtr source);
void virFSPoolDefFree(virFSPoolDefPtr def);
void virFSPoolObjFree(virFSPoolObjPtr fspool);
void virFSPoolObjListFree(virFSPoolObjListPtr fspools);
void virFSPoolObjRemove(virFSPoolObjListPtr fspools,
                        virFSPoolObjPtr fspool);

virFSPoolSourcePtr
virFSPoolDefParseSourceString(const char *srcSpec,
                              int fspool_type);
virFSPoolSourcePtr
virFSPoolSourceListNewSource(virFSPoolSourceListPtr list);
char *virFSPoolSourceListFormat(virFSPoolSourceListPtr def);

int virFSPoolObjIsDuplicate(virFSPoolObjListPtr fspools,
                            virFSPoolDefPtr def,
                            unsigned int check_active);

char *virFSPoolGetVhbaSCSIHostParent(virConnectPtr conn,
                                     const char *name)
    ATTRIBUTE_NONNULL(1);

int virFSPoolSourceFindDuplicate(virConnectPtr conn,
                                 virFSPoolObjListPtr fspools,
                                      virFSPoolDefPtr def);

void virFSPoolObjLock(virFSPoolObjPtr obj);
void virFSPoolObjUnlock(virFSPoolObjPtr obj);

int virFSPoolObjListExport(virConnectPtr conn,
                           virFSPoolObjList fspoolobjs,
                           virFSPoolPtr **fspools,
                           virFSPoolObjListFilter filter,
                           unsigned int flags);



#endif /* __VIR_FS_CONF_H__ */
