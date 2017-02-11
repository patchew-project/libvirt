#include <config.h>

#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "storage_backend_vstorage.h"
#include "virlog.h"
#include "virstring.h"
#include <mntent.h>
#include <pwd.h>
#include <grp.h>
#include "storage_util.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("storage.storage_backend_vstorage");


/**
 * @conn connection to report errors against
 * @poolobj storage pool object to build
 * @flags controls the pool formatting behaviour
 *
 * Does not support @flags, if provided an error will occur.
 *
 * Returns 0 on success, -1 on error
 */
static int
virStorageBackendVzPoolBuild(virConnectPtr conn ATTRIBUTE_UNUSED,
                             virPoolObjPtr poolobj,
                             unsigned int flags)
{
    virCheckFlags(0, -1);

    return virStorageBackendBuildLocal(poolobj);
}


static int
virStorageBackendVzPoolStart(virConnectPtr conn ATTRIBUTE_UNUSED,
                             virPoolObjPtr poolobj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    int ret = -1;
    virCommandPtr cmd = NULL;
    char *grp_name = NULL;
    char *usr_name = NULL;
    char *mode = NULL;

    /* Check the permissions */
    if (def->target.perms.mode == (mode_t) - 1)
        def->target.perms.mode = VIR_STORAGE_DEFAULT_POOL_PERM_MODE;
    if (def->target.perms.uid == (uid_t) -1)
        def->target.perms.uid = geteuid();
    if (def->target.perms.gid == (gid_t) -1)
        def->target.perms.gid = getegid();

    /* Convert ids to names because vstorage uses names */

    if (!(grp_name = virGetGroupName(def->target.perms.gid)))
        goto cleanup;

    if (!(usr_name = virGetUserName(def->target.perms.uid)))
        goto cleanup;

    if (virAsprintf(&mode, "%o", def->target.perms.mode) < 0)
        goto cleanup;

    cmd = virCommandNewArgList(VSTORAGE_MOUNT,
                               "-c", def->source.name,
                               def->target.path,
                               "-m", mode,
                               "-g", grp_name, "-u", usr_name,
                               NULL);

    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;
    ret = 0;

 cleanup:
    virCommandFree(cmd);
    VIR_FREE(mode);
    VIR_FREE(grp_name);
    VIR_FREE(usr_name);
    return ret;
}


static int
virStorageBackendVzIsMounted(virPoolObjPtr poolobj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    int ret = -1;
    FILE *mtab;
    struct mntent ent;
    char buf[1024];
    char *cluster = NULL;

    if (virAsprintf(&cluster, "vstorage://%s", pool->def->source.name) < 0)
        return -1;

    if ((mtab = fopen(_PATH_MOUNTED, "r")) == NULL) {
        virReportSystemError(errno,
                             _("cannot read mount list '%s'"),
                             _PATH_MOUNTED);
        goto cleanup;
    }

    while ((getmntent_r(mtab, &ent, buf, sizeof(buf))) != NULL) {

        if (STREQ(ent.mnt_dir, def->target.path) &&
            STREQ(ent.mnt_fsname, cluster)) {
            ret = 1;
            goto cleanup;
        }
    }

    ret = 0;

 cleanup:
    VIR_FORCE_FCLOSE(mtab);
    VIR_FREE(cluster);
    return ret;
}


static int
virStorageBackendVzPoolStop(virConnectPtr conn ATTRIBUTE_UNUSED,
                            virPoolObjPtr poolobj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    virCommandPtr cmd = NULL;
    int ret = -1;
    int rc;

    /* Short-circuit if already unmounted */
    if ((rc = virStorageBackendVzIsMounted(poolobj)) != 1)
        return rc;

    cmd = virCommandNewArgList(UMOUNT, def->target.path, NULL);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virCommandFree(cmd);
    return ret;
}


/*
 * Check whether the cluster is mounted
 */
static int
virStorageBackendVzCheck(virPoolObjPtr poolobj,
                         bool *isActive)
{
    int ret = -1;
    *isActive = false;
    if ((ret = virStorageBackendVzIsMounted(poolobj)) != 0) {
        if (ret < 0)
            return -1;
        *isActive = true;
    }

    return 0;
}


virStorageBackend virStorageBackendVstorage = {
    .type = VIR_STORAGE_POOL_VSTORAGE,

    .buildPool = virStorageBackendVzPoolBuild,
    .startPool = virStorageBackendVzPoolStart,
    .stopPool = virStorageBackendVzPoolStop,
    .deletePool = virStorageBackendDeleteLocal,
    .refreshPool = virStorageBackendRefreshLocal,
    .checkPool = virStorageBackendVzCheck,
    .buildVol = virStorageBackendVolBuildLocal,
    .buildVolFrom = virStorageBackendVolBuildFromLocal,
    .createVol = virStorageBackendVolCreateLocal,
    .refreshVol = virStorageBackendVolRefreshLocal,
    .deleteVol = virStorageBackendVolDeleteLocal,
    .resizeVol = virStorageBackendVolResizeLocal,
    .uploadVol = virStorageBackendVolUploadLocal,
    .downloadVol = virStorageBackendVolDownloadLocal,
    .wipeVol = virStorageBackendVolWipeLocal,
};
