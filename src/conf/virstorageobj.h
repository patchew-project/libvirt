/*
 * virstorageobj.h: internal storage pool and volume objects handling
 *                  (derived from storage_conf.h)
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

#ifndef __VIR_STORAGEOBJ_H__
# define __VIR_STORAGEOBJ_H__

# include "internal.h"

# include "storage_conf.h"
# include "virpoolobj.h"

typedef struct _virStoragePoolObjPrivate virStoragePoolObjPrivate;
typedef virStoragePoolObjPrivate *virStoragePoolObjPrivatePtr;

typedef struct _virStorageDriverState virStorageDriverState;
typedef virStorageDriverState *virStorageDriverStatePtr;

struct _virStorageDriverState {
    virMutex lock;

    virPoolObjTablePtr pools;

    char *configDir;
    char *autostartDir;
    char *stateDir;
    bool privileged;

    /* Immutable pointer, self-locking APIs */
    virObjectEventStatePtr storageEventState;
};

const char *virStoragePoolObjPrivateGetConfigFile(virPoolObjPtr poolobj);

const char *virStoragePoolObjPrivateGetAutostartLink(virPoolObjPtr poolobj);

int virStoragePoolObjPrivateGetAutostart(virPoolObjPtr poolobj);

void virStoragePoolObjPrivateSetAutostart(virPoolObjPtr poolobj,
                                          int autostart);

int virStoragePoolObjPrivateGetAsyncjobs(virPoolObjPtr poolobj);

void virStoragePoolObjPrivateIncrAsyncjobs(virPoolObjPtr poolobj);

void virStoragePoolObjPrivateDecrAsyncjobs(virPoolObjPtr poolobj);

virPoolObjTablePtr
virStoragePoolObjPrivateGetVolumes(virPoolObjPtr poolobj);

virPoolObjPtr
virStoragePoolObjAddVolume(virPoolObjPtr poolobj, virStorageVolDefPtr voldef);

void
virStoragePoolObjRemoveVolume(virPoolObjPtr poolobj, virPoolObjPtr *volobj);

void virStoragePoolObjClearVols(virPoolObjPtr pool);

typedef bool (*virStoragePoolVolumeACLFilter)
    (virConnectPtr conn, virStoragePoolDefPtr pool, void *objdef);

int virStoragePoolObjNumOfVolumes(virPoolObjTablePtr volumes,
                                  virConnectPtr conn,
                                  virStoragePoolDefPtr pooldef,
                                  virStoragePoolVolumeACLFilter aclfilter);

int virStoragePoolObjListVolumes(virPoolObjTablePtr volumes,
                                 virConnectPtr conn,
                                 virStoragePoolDefPtr pooldef,
                                 virStoragePoolVolumeACLFilter aclfilter,
                                 char **const names,
                                 int maxnames);

int virStoragePoolObjIsDuplicate(virPoolObjTablePtr pools,
                                 virStoragePoolDefPtr def,
                                 unsigned int check_active);

virPoolObjPtr
virStoragePoolObjAdd(virPoolObjTablePtr pools,
                     virStoragePoolDefPtr def);

int virStoragePoolObjLoadAllConfigs(virPoolObjTablePtr pools,
                                    const char *configDir,
                                    const char *autostartDir);

int virStoragePoolObjLoadAllState(virPoolObjTablePtr pools,
                                  const char *stateDir);

int virStoragePoolObjSaveDef(virStorageDriverStatePtr driver,
                             virPoolObjPtr obj);

int virStoragePoolObjDeleteDef(virPoolObjPtr obj);

int virStoragePoolObjNumOfStoragePools(virPoolObjTablePtr pools,
                                       virConnectPtr conn,
                                       bool wantActive,
                                       virPoolObjACLFilter aclfilter);

int virStoragePoolObjGetNames(virPoolObjTablePtr pools,
                              virConnectPtr conn,
                              bool wantActive,
                              virPoolObjACLFilter aclfilter,
                              char **const names,
                              int maxnames);

bool virStoragePoolObjFindDuplicate(virPoolObjTablePtr pools,
                                    virConnectPtr conn,
                                    virStoragePoolDefPtr def);

int virStoragePoolObjExportList(virConnectPtr conn,
                                virPoolObjTablePtr poolobjs,
                                virStoragePoolPtr **pools,
                                virPoolObjACLFilter aclfilter,
                                unsigned int flags);

virPoolObjPtr
virStorageVolObjFindByKey(virPoolObjPtr poolobj,
                          const char *key);

virPoolObjPtr
virStorageVolObjFindByPath(virPoolObjPtr poolobj,
                           const char *path);

virPoolObjPtr
virStorageVolObjFindByName(virPoolObjPtr poolobj,
                           const char *name);

#endif /* __VIR_STORAGEOBJ_H__ */
