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
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <fcntl.h>

#define VIR_FROM_THIS VIR_FROM_STORAGE

#define VIR_STORAGE_VOL_FS_OPEN_FLAGS    (VIR_STORAGE_VOL_OPEN_DEFAULT | \
                                          VIR_STORAGE_VOL_OPEN_DIR)
#define VIR_STORAGE_VOL_FS_PROBE_FLAGS   (VIR_STORAGE_VOL_FS_OPEN_FLAGS | \
                                          VIR_STORAGE_VOL_OPEN_NOERROR)



VIR_LOG_INIT("storage.storage_backend_vstorage");
/**
 * @conn connection to report errors against
 * @pool storage pool to build
 * @flags controls the pool formatting behaviour
 *
 *
 * If no flag is set, it only makes the directory;
 * If VIR_STORAGE_POOL_BUILD_NO_OVERWRITE set, it determines if
 * the directory exists and if yes - we use it. Otherwise - the new one
 * will be created.
 * If VIR_STORAGE_POOL_BUILD_OVERWRITE is set, the dircetory for pool
 * will used but the content and permissions will be updated
 *
 * Returns 0 on success, -1 on error
 */

static int
virStorageBackendVzPoolBuild(virConnectPtr conn ATTRIBUTE_UNUSED,
                                 virStoragePoolObjPtr pool,
                                 unsigned int flags)
{
    int ret = -1;
    char *parent = NULL;
    char *p = NULL;
    mode_t mode;
    unsigned int dir_create_flags;

    virCheckFlags(VIR_STORAGE_POOL_BUILD_OVERWRITE |
                  VIR_STORAGE_POOL_BUILD_NO_OVERWRITE, ret);

    VIR_EXCLUSIVE_FLAGS_GOTO(VIR_STORAGE_POOL_BUILD_OVERWRITE,
                             VIR_STORAGE_POOL_BUILD_NO_OVERWRITE,
                             error);

    if (VIR_STRDUP(parent, pool->def->target.path) < 0)
        goto error;
    if (!(p = strrchr(parent, '/'))) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("path '%s' is not absolute"),
                       pool->def->target.path);
        goto error;
    }

    if (p != parent) {
        /* assure all directories in the path prior to the final dir
         * exist, with default uid/gid/mode. */
        *p = '\0';
        if (virFileMakePath(parent) < 0) {
            virReportSystemError(errno, _("cannot create path '%s'"),
                                 parent);
            goto error;
        }
    }

    dir_create_flags = VIR_DIR_CREATE_ALLOW_EXIST;
    mode = pool->def->target.perms.mode;

    if (mode == (mode_t) -1 && !virFileExists(pool->def->target.path))
        mode = VIR_STORAGE_DEFAULT_POOL_PERM_MODE;

    /* Now create the final dir in the path with the uid/gid/mode
     * requested in the config. If the dir already exists, just set
     * the perms. */
    if (virDirCreate(pool->def->target.path,
                     mode,
                     pool->def->target.perms.uid,
                     pool->def->target.perms.gid,
                     dir_create_flags) < 0)
        goto error;

    /* Delete directory content if flag is set*/
    if (flags & VIR_STORAGE_POOL_BUILD_OVERWRITE)
       if (virFileDeleteTree(pool->def->target.path))
           goto error;

    ret = 0;

 error:
    VIR_FREE(parent);
    return ret;
}

static int
virStorageBackendVzPoolStart(virConnectPtr conn ATTRIBUTE_UNUSED,
                             virStoragePoolObjPtr pool)
{
    int ret = -1;
    virCommandPtr cmd = NULL;
    char *grp_name = NULL;
    char *usr_name = NULL;
    char *mode = NULL;

    /* Check the permissions */
    if (pool->def->target.perms.mode == (mode_t) - 1)
        pool->def->target.perms.mode = VIR_STORAGE_DEFAULT_POOL_PERM_MODE;
    if (pool->def->target.perms.uid == (uid_t) -1)
        pool->def->target.perms.uid = geteuid();
    if (pool->def->target.perms.gid == (gid_t) -1)
        pool->def->target.perms.gid = getegid();

    /* Convert ids to names because vstorage uses names */

    grp_name = virGetGroupName(pool->def->target.perms.gid);
    if (!grp_name)
        return -1;

    usr_name = virGetUserName(pool->def->target.perms.uid);
    if (!usr_name)
        return -1;

    if (virAsprintf(&mode, "%o", pool->def->target.perms.mode) < 0)
        return -1;

    cmd = virCommandNewArgList(VSTORAGE_MOUNT,
                               "-c", pool->def->source.name,
                               pool->def->target.path,
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
virStorageBackendVzIsMounted(virStoragePoolObjPtr pool)
{
    int ret = -1;
    char *src = NULL;
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

        if (STREQ(ent.mnt_dir, pool->def->target.path) &&
            STREQ(ent.mnt_fsname, cluster)) {
            ret = 1;
            goto cleanup;
        }

        VIR_FREE(src);
    }

    ret = 0;

 cleanup:
    VIR_FORCE_FCLOSE(mtab);
    VIR_FREE(src);
    VIR_FREE(cluster);
    return ret;
}

static int
virStorageBackendVzUmount(virStoragePoolObjPtr pool)
{
    virCommandPtr cmd = NULL;
    int ret = -1;
    int rc;

    /* Short-circuit if already unmounted */
    if ((rc = virStorageBackendVzIsMounted(pool)) != 1)
        return rc;

    cmd = virCommandNewArgList(UMOUNT,
                               pool->def->target.path,
                               NULL);

    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virCommandFree(cmd);
    return ret;
}

static int
virStorageBackendVzPoolStop(virConnectPtr conn ATTRIBUTE_UNUSED,
                            virStoragePoolObjPtr pool)
{
    if (virStorageBackendVzUmount(pool) < 0)
        return -1;

    return 0;
}

/**
 * @conn connection to report errors against
 * @pool storage pool to delete
 *
 * Deletes vstorage based storage pool.
 * At this moment vstorage is in umounted state - so we do not
 * need to delete volumes first.
 * Returns 0 on success, -1 on error
 */
static int
virStorageBackendVzDelete(virConnectPtr conn ATTRIBUTE_UNUSED,
                          virStoragePoolObjPtr pool,
                          unsigned int flags)
{
    virCheckFlags(0, -1);

    if (rmdir(pool->def->target.path) < 0) {
        virReportSystemError(errno,
                             _("failed to remove pool '%s'"),
                             pool->def->target.path);
        return -1;
    }

    return 0;
}

/*
 * Check wether the cluster is mounted
 */
static int
virStorageBackendVzCheck(virStoragePoolObjPtr pool,
                         bool *isActive)
{
    int ret = -1;
    *isActive = false;
    if ((ret = virStorageBackendVzIsMounted(pool)) != 0) {
        if (ret < 0)
            return -1;
        *isActive = true;
    }

    return 0;
}

/* All the underlying functions were directly copied from
 * filesystem backend with no changes.
 */

static int
virStorageBackendProbeTarget(virStorageSourcePtr target,
                             virStorageEncryptionPtr *encryption)
{
    int backingStoreFormat;
    int fd = -1;
    int ret = -1;
    int rc;
    virStorageSourcePtr meta = NULL;
    struct stat sb;

    if (encryption)
        *encryption = NULL;

    if ((rc = virStorageBackendVolOpen(target->path, &sb,
                                       VIR_STORAGE_VOL_FS_PROBE_FLAGS)) < 0)
        return rc; /* Take care to propagate rc, it is not always -1 */
    fd = rc;

    if (virStorageBackendUpdateVolTargetInfoFD(target, fd, &sb) < 0)
        goto cleanup;

    if (S_ISDIR(sb.st_mode)) {
        if (virStorageBackendIsPloopDir(target->path)) {
            if (virStorageBackendRedoPloopUpdate(target, &sb, &fd,
                                                 VIR_STORAGE_VOL_FS_PROBE_FLAGS) < 0)
                goto cleanup;
        } else {
            target->format = VIR_STORAGE_FILE_DIR;
            ret = 0;
            goto cleanup;
        }
    }

    if (!(meta = virStorageFileGetMetadataFromFD(target->path,
                                                 fd,
                                                 VIR_STORAGE_FILE_AUTO,
                                                 &backingStoreFormat)))
        goto cleanup;

    if (meta->backingStoreRaw) {
        if (!(target->backingStore = virStorageSourceNewFromBacking(meta)))
            goto cleanup;

        target->backingStore->format = backingStoreFormat;

        /* XXX: Remote storage doesn't play nicely with volumes backed by
         * remote storage. To avoid trouble, just fake the backing store is RAW
         * and put the string from the metadata as the path of the target. */
        if (!virStorageSourceIsLocalStorage(target->backingStore)) {
            virStorageSourceFree(target->backingStore);

            if (VIR_ALLOC(target->backingStore) < 0)
                goto cleanup;

            target->backingStore->type = VIR_STORAGE_TYPE_NETWORK;
            target->backingStore->path = meta->backingStoreRaw;
            meta->backingStoreRaw = NULL;
            target->backingStore->format = VIR_STORAGE_FILE_RAW;
        }

        if (target->backingStore->format == VIR_STORAGE_FILE_AUTO) {
            if ((rc = virStorageFileProbeFormat(target->backingStore->path,
                                                -1, -1)) < 0) {
                /* If the backing file is currently unavailable or is
                 * accessed via remote protocol only log an error, fake the
                 * format as RAW and continue. Returning -1 here would
                 * disable the whole storage pool, making it unavailable for
                 * even maintenance. */
                target->backingStore->format = VIR_STORAGE_FILE_RAW;
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("cannot probe backing volume format: %s"),
                               target->backingStore->path);
            } else {
                target->backingStore->format = rc;
            }
        }
    }

    target->format = meta->format;

    /* Default to success below this point */
    ret = 0;

    if (meta->capacity)
        target->capacity = meta->capacity;

    if (encryption && meta->encryption) {
        *encryption = meta->encryption;
        meta->encryption = NULL;

        /* XXX ideally we'd fill in secret UUID here
         * but we cannot guarantee 'conn' is non-NULL
         * at this point in time :-(  So we only fill
         * in secrets when someone first queries a vol
         */
    }

    virBitmapFree(target->features);
    target->features = meta->features;
    meta->features = NULL;

    if (meta->compat) {
        VIR_FREE(target->compat);
        target->compat = meta->compat;
        meta->compat = NULL;
    }

 cleanup:
    VIR_FORCE_CLOSE(fd);
    virStorageSourceFree(meta);
    return ret;

}

/**
 * The same as for fs/dir storage pools
 * Iterate over the pool's directory and enumerate all disk images
 * within it. This is non-recursive.
 */

static int
virStorageBackendVzRefresh(virConnectPtr conn ATTRIBUTE_UNUSED,
                                   virStoragePoolObjPtr pool)
{
    DIR *dir;
    struct dirent *ent;
    struct statvfs sb;
    struct stat statbuf;
    virStorageVolDefPtr vol = NULL;
    virStorageSourcePtr target = NULL;
    int direrr;
    int fd = -1, ret = -1;

    if (virDirOpen(&dir, pool->def->target.path) < 0)
        goto cleanup;

    while ((direrr = virDirRead(dir, &ent, pool->def->target.path)) > 0) {
        int err;

        if (virStringHasControlChars(ent->d_name)) {
            VIR_WARN("Ignoring file with control characters under '%s'",
                     pool->def->target.path);
            continue;
        }

        if (VIR_ALLOC(vol) < 0)
            goto cleanup;

        if (VIR_STRDUP(vol->name, ent->d_name) < 0)
            goto cleanup;

        vol->type = VIR_STORAGE_VOL_FILE;
        vol->target.format = VIR_STORAGE_FILE_RAW; /* Real value is filled in during probe */
        if (virAsprintf(&vol->target.path, "%s/%s",
                        pool->def->target.path,
                        vol->name) == -1)
            goto cleanup;

        if (VIR_STRDUP(vol->key, vol->target.path) < 0)
            goto cleanup;

        if ((err = virStorageBackendProbeTarget(&vol->target,
                                                &vol->target.encryption)) < 0) {
            if (err == -2) {
                /* Silently ignore non-regular files,
                 * eg 'lost+found', dangling symbolic link */
                virStorageVolDefFree(vol);
                vol = NULL;
                continue;
            } else if (err == -3) {
                /* The backing file is currently unavailable, its format is not
                 * explicitly specified, the probe to auto detect the format
                 * failed: continue with faked RAW format, since AUTO will
                 * break virStorageVolTargetDefFormat() generating the line
                 * <format type='...'/>. */
            } else {
                goto cleanup;
            }
        }

        /* directory based volume */
        if (vol->target.format == VIR_STORAGE_FILE_DIR)
            vol->type = VIR_STORAGE_VOL_DIR;

        if (vol->target.format == VIR_STORAGE_FILE_PLOOP)
            vol->type = VIR_STORAGE_VOL_PLOOP;

        if (vol->target.backingStore) {
            ignore_value(virStorageBackendUpdateVolTargetInfo(vol->target.backingStore,
                                                              false,
                                                              VIR_STORAGE_VOL_OPEN_DEFAULT, 0));
            /* If this failed, the backing file is currently unavailable,
             * the capacity, allocation, owner, group and mode are unknown.
             * An error message was raised, but we just continue. */
        }

        if (VIR_APPEND_ELEMENT(pool->volumes.objs, pool->volumes.count, vol) < 0)
            goto cleanup;
    }
    if (direrr < 0)
        goto cleanup;
    VIR_DIR_CLOSE(dir);
    vol = NULL;

    if (VIR_ALLOC(target))
        goto cleanup;

    if ((fd = open(pool->def->target.path, O_RDONLY)) < 0) {
        virReportSystemError(errno,
                             _("cannot open path '%s'"),
                             pool->def->target.path);
        goto cleanup;
    }

    if (fstat(fd, &statbuf) < 0) {
        virReportSystemError(errno,
                             _("cannot stat path '%s'"),
                             pool->def->target.path);
        goto cleanup;
    }

    if (virStorageBackendUpdateVolTargetInfoFD(target, fd, &statbuf) < 0)
        goto cleanup;

    /* VolTargetInfoFD doesn't update capacity correctly for the pool case */
    if (statvfs(pool->def->target.path, &sb) < 0) {
        virReportSystemError(errno,
                             _("cannot statvfs path '%s'"),
                             pool->def->target.path);
        goto cleanup;
    }

    pool->def->capacity = ((unsigned long long)sb.f_frsize *
                           (unsigned long long)sb.f_blocks);
    pool->def->available = ((unsigned long long)sb.f_bfree *
                            (unsigned long long)sb.f_frsize);
    pool->def->allocation = pool->def->capacity - pool->def->available;

    pool->def->target.perms.mode = target->perms->mode;
    pool->def->target.perms.uid = target->perms->uid;
    pool->def->target.perms.gid = target->perms->gid;
    VIR_FREE(pool->def->target.perms.label);
    if (VIR_STRDUP(pool->def->target.perms.label, target->perms->label) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_DIR_CLOSE(dir);
    VIR_FORCE_CLOSE(fd);
    virStorageVolDefFree(vol);
    virStorageSourceFree(target);
    if (ret < 0)
        virStoragePoolObjClearVols(pool);
    return ret;
}

virStorageBackend virStorageBackendVstorage = {
    .type = VIR_STORAGE_POOL_VSTORAGE,

    .buildPool = virStorageBackendVzPoolBuild,
    .startPool = virStorageBackendVzPoolStart,
    .stopPool = virStorageBackendVzPoolStop,
    .deletePool = virStorageBackendVzDelete,
    .refreshPool = virStorageBackendVzRefresh,
    .checkPool = virStorageBackendVzCheck,
};
