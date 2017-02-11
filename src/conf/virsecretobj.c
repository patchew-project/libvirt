/*
 * virsecretobj.c: internal <secret> objects handling
 *
 * Copyright (C) 2009-2016 Red Hat, Inc.
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

#include <config.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "datatypes.h"
#include "virsecretobj.h"
#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virhash.h"
#include "virlog.h"
#include "virstring.h"
#include "base64.h"

#define VIR_FROM_THIS VIR_FROM_SECRET

VIR_LOG_INIT("conf.virsecretobj");

struct _virSecretObjPrivate {
    char *configFile;
    char *base64File;
    unsigned char *value;       /* May be NULL */
    size_t value_size;
};


static void
virSecretObjPrivateFree(void *obj)
{
    virSecretObjPrivatePtr objpriv = obj;

    if (!objpriv)
        return;

    if (objpriv->value) {
        /* Wipe before free to ensure we don't leave a secret on the heap */
        memset(objpriv->value, 0, objpriv->value_size);
        VIR_FREE(objpriv->value);
    }
    VIR_FREE(objpriv->configFile);
    VIR_FREE(objpriv->base64File);
    VIR_FREE(objpriv);
}


static virSecretObjPrivatePtr
virSecretObjPrivateAlloc(const char *configDir,
                         const char *uuidstr)
{
    virSecretObjPrivatePtr objpriv = NULL;

    if (VIR_ALLOC(objpriv) < 0)
        return NULL;

    if (!(objpriv->configFile = virFileBuildPath(configDir, uuidstr, ".xml")) ||
        !(objpriv->base64File = virFileBuildPath(configDir, uuidstr,
                                                    ".base64"))) {
        virSecretObjPrivateFree(objpriv);
        return NULL;
    }

    return objpriv;
}


static int
secretAssignDef(virPoolObjPtr obj,
                void *newDef,
                void *oldDef,
                unsigned int assignFlags ATTRIBUTE_UNUSED)
{
    virSecretDefPtr objdef = virPoolObjGetDef(obj);
    virSecretDefPtr newdef = newDef;
    virSecretDefPtr *olddef = oldDef;

    if (objdef->isprivate && !newdef->isprivate) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("cannot change private flag on existing secret"));
        return -1;
    }

    /* Store away current objdef in olddef, clear it out since the SetDef
     * will attempt to free first, and replace objdef with newdef */
    if (olddef) {
        *olddef = objdef;
        virPoolObjSetDef(obj, NULL);
    }

    virPoolObjSetDef(obj, newdef);

    return 0;
}


/* virSecretObjAdd:
 * @secrets: list of secret objects
 * @def: new secret definition
 * @configDir: directory to place secret config files
 * @oldDef: Former secret def (e.g. a reload path perhaps)
 *
 * Add the new def to the secret obj table hash
 *
 * Returns: Either a pointer to a locked and ref'd secret obj that needs
 *          to use virPoolObjEndAPI when the caller is done with the object
 *          or NULL if failure to add.
 */
virPoolObjPtr
virSecretObjAdd(virPoolObjTablePtr secrets,
                virSecretDefPtr def,
                const char *configDir,
                virSecretDefPtr *oldDef)
{
    virPoolObjPtr obj = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    virSecretObjPrivatePtr objpriv = NULL;

    if (!def->usage_id || !*def->usage_id) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("secret with UUID %s does not have usage defined"),
                       uuidstr);
        return NULL;
    }

    virUUIDFormat(def->uuid, uuidstr);
    if (!(obj = virPoolObjTableAdd(secrets, uuidstr, def->usage_id,
                                   def, NULL, oldDef, virSecretDefFree,
                                   secretAssignDef, 0)))
        return NULL;

    if (!(objpriv = virPoolObjGetPrivateData(obj))) {
        if (!(objpriv = virSecretObjPrivateAlloc(configDir, uuidstr)))
            goto error;

        virPoolObjSetPrivateData(obj, objpriv, virSecretObjPrivateFree);
    }

    return obj;

 error:
    virPoolObjTableRemove(secrets, &obj);
    virPoolObjEndAPI(&obj);
    return NULL;
}


struct secretCountData {
    int count;
};

static int
secretCount(virPoolObjPtr obj ATTRIBUTE_UNUSED,
            void *opaque)
{
    struct secretCountData *data = opaque;

    data->count++;
    return 0;
}


int
virSecretObjNumOfSecrets(virPoolObjTablePtr secretobjs,
                         virConnectPtr conn,
                         virPoolObjACLFilter aclfilter)
{
    struct secretCountData data = { .count = 0 };

    if (virPoolObjTableList(secretobjs, conn, aclfilter,
                            secretCount, &data) < 0)
        return 0;

    return data.count;
}


struct secretListData {
    int nuuids;
    char **const uuids;
    int maxuuids;
};


static int secretGetUUIDs(virPoolObjPtr obj,
                          void *opaque)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);
    struct secretListData *data = opaque;

    if (data->nuuids < data->maxuuids) {
        char *uuidstr;

        if (VIR_ALLOC_N(uuidstr, VIR_UUID_STRING_BUFLEN) < 0)
            return -1;

        virUUIDFormat(def->uuid, uuidstr);
        data->uuids[data->nuuids++] = uuidstr;
    }

    return 0;
}


int
virSecretObjGetUUIDs(virPoolObjTablePtr secrets,
                     char **uuids,
                     int maxuuids,
                     virPoolObjACLFilter aclfilter,
                     virConnectPtr conn)
{
    struct secretListData data = { .nuuids = 0,
                                   .uuids = uuids,
                                   .maxuuids = maxuuids };

    if (virPoolObjTableList(secrets, conn, aclfilter,
                            secretGetUUIDs, &data) < 0)
        goto failure;

    return data.nuuids;

 failure:

    while (data.nuuids >= 0)
        VIR_FREE(data.uuids[--data.nuuids]);

    return -1;
}


#define MATCH(FLAG) (flags & (FLAG))
static bool
secretMatchFlags(virPoolObjPtr obj,
                 unsigned int flags)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);

    /* filter by whether it's ephemeral */
    if (MATCH(VIR_CONNECT_LIST_SECRETS_FILTERS_EPHEMERAL) &&
        !((MATCH(VIR_CONNECT_LIST_SECRETS_EPHEMERAL) &&
           def->isephemeral) ||
          (MATCH(VIR_CONNECT_LIST_SECRETS_NO_EPHEMERAL) &&
           !def->isephemeral)))
        return false;

    /* filter by whether it's private */
    if (MATCH(VIR_CONNECT_LIST_SECRETS_FILTERS_PRIVATE) &&
        !((MATCH(VIR_CONNECT_LIST_SECRETS_PRIVATE) &&
           def->isprivate) ||
          (MATCH(VIR_CONNECT_LIST_SECRETS_NO_PRIVATE) &&
           !def->isprivate)))
        return false;

    return true;
}
#undef MATCH


int
virSecretObjExportList(virConnectPtr conn,
                       virPoolObjTablePtr secretobjs,
                       virSecretPtr **secrets,
                       virPoolObjACLFilter aclfilter,
                       unsigned int flags)
{
    virPoolObjPtr *objs = NULL;
    size_t nobjs = 0;
    virSecretPtr *secs = NULL;

    if (virPoolObjTableCollect(secretobjs, conn, &objs, &nobjs, aclfilter,
                               secretMatchFlags, flags) < 0)
        return -1;

    if (secrets) {
        size_t i;

        if (VIR_ALLOC_N(secs, nobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nobjs; i++) {
            virSecretDefPtr def;

            virObjectLock(objs[i]);
            def = virPoolObjGetDef(objs[i]);
            secs[i] = virGetSecret(conn, def->uuid, def->usage_type,
                                   def->usage_id);
            virObjectUnlock(objs[i]);
            if (!secs[i])
                goto cleanup;
        }

        VIR_STEAL_PTR(*secrets, secs);
    }

 cleanup:
    virObjectListFree(secs);
    virObjectListFreeCount(objs, nobjs);

    return nobjs;
}


int
virSecretObjDeleteConfig(virPoolObjPtr obj)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    if (!def->isephemeral &&
        unlink(objpriv->configFile) < 0 && errno != ENOENT) {
        virReportSystemError(errno, _("cannot unlink '%s'"),
                             objpriv->configFile);
        return -1;
    }

    return 0;
}


void
virSecretObjDeleteData(virPoolObjPtr obj)
{
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    /* The configFile will already be removed, so secret won't be
     * loaded again if this fails */
    (void)unlink(objpriv->base64File);
}


/* Permanent secret storage */

/* Secrets are stored in virSecretDriverStatePtr->configDir.  Each secret
   has virSecretDef stored as XML in "$basename.xml".  If a value of the
   secret is defined, it is stored as base64 (with no formatting) in
   "$basename.base64".  "$basename" is in both cases the base64-encoded UUID. */
int
virSecretObjSaveConfig(virPoolObjPtr obj)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);
    char *xml = NULL;
    int ret = -1;

    if (!(xml = virSecretDefFormat(def)))
        goto cleanup;

    if (virFileRewriteStr(objpriv->configFile, S_IRUSR | S_IWUSR, xml) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(xml);
    return ret;
}


int
virSecretObjSaveData(virPoolObjPtr obj)
{
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);
    char *base64 = NULL;
    int ret = -1;

    if (!objpriv->value)
        return 0;

    if (!(base64 = virStringEncodeBase64(objpriv->value, objpriv->value_size)))
        goto cleanup;

    if (virFileRewriteStr(objpriv->base64File, S_IRUSR | S_IWUSR, base64) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(base64);
    return ret;
}


unsigned char *
virSecretObjGetValue(virPoolObjPtr obj)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);
    unsigned char *ret = NULL;

    if (!objpriv->value) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(def->uuid, uuidstr);
        virReportError(VIR_ERR_NO_SECRET,
                       _("secret '%s' does not have a value"), uuidstr);
        goto cleanup;
    }

    if (VIR_ALLOC_N(ret, objpriv->value_size) < 0)
        goto cleanup;
    memcpy(ret, objpriv->value, objpriv->value_size);

 cleanup:
    return ret;
}


int
virSecretObjSetValue(virPoolObjPtr obj,
                     const unsigned char *value,
                     size_t value_size)
{
    virSecretDefPtr def = virPoolObjGetDef(obj);
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);
    unsigned char *old_value, *new_value;
    size_t old_value_size;

    if (VIR_ALLOC_N(new_value, value_size) < 0)
        return -1;

    old_value = objpriv->value;
    old_value_size = objpriv->value_size;

    memcpy(new_value, value, value_size);
    objpriv->value = new_value;
    objpriv->value_size = value_size;

    if (!def->isephemeral && virSecretObjSaveData(obj) < 0)
        goto error;

    /* Saved successfully - drop old value */
    if (old_value) {
        memset(old_value, 0, old_value_size);
        VIR_FREE(old_value);
    }

    return 0;

 error:
    /* Error - restore previous state and free new value */
    objpriv->value = old_value;
    objpriv->value_size = old_value_size;
    memset(new_value, 0, value_size);
    VIR_FREE(new_value);
    return -1;
}


size_t
virSecretObjGetValueSize(virPoolObjPtr obj)
{
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->value_size;
}


void
virSecretObjSetValueSize(virPoolObjPtr obj,
                         size_t value_size)
{
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    objpriv->value_size = value_size;
}


static int
virSecretLoadValidateUUID(virSecretDefPtr def,
                          const char *file)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(def->uuid, uuidstr);

    if (!virFileMatchesNameSuffix(file, uuidstr, ".xml")) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("<uuid> does not match secret file name '%s'"),
                       file);
        return -1;
    }

    return 0;
}


static int
virSecretLoadValue(virPoolObjPtr obj)
{
    int ret = -1, fd = -1;
    struct stat st;
    char *contents = NULL, *value = NULL;
    size_t value_size;
    virSecretObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    if ((fd = open(objpriv->base64File, O_RDONLY)) == -1) {
        if (errno == ENOENT) {
            ret = 0;
            goto cleanup;
        }
        virReportSystemError(errno, _("cannot open '%s'"),
                             objpriv->base64File);
        goto cleanup;
    }

    if (fstat(fd, &st) < 0) {
        virReportSystemError(errno, _("cannot stat '%s'"),
                             objpriv->base64File);
        goto cleanup;
    }

    if ((size_t)st.st_size != st.st_size) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("'%s' file does not fit in memory"),
                       objpriv->base64File);
        goto cleanup;
    }

    if (VIR_ALLOC_N(contents, st.st_size) < 0)
        goto cleanup;

    if (saferead(fd, contents, st.st_size) != st.st_size) {
        virReportSystemError(errno, _("cannot read '%s'"),
                             objpriv->base64File);
        goto cleanup;
    }

    VIR_FORCE_CLOSE(fd);

    if (!base64_decode_alloc(contents, st.st_size, &value, &value_size)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("invalid base64 in '%s'"),
                       objpriv->base64File);
        goto cleanup;
    }
    if (value == NULL)
        goto cleanup;

    objpriv->value = (unsigned char *)value;
    value = NULL;
    objpriv->value_size = value_size;

    ret = 0;

 cleanup:
    if (value != NULL) {
        memset(value, 0, value_size);
        VIR_FREE(value);
    }
    if (contents != NULL) {
        memset(contents, 0, st.st_size);
        VIR_FREE(contents);
    }
    VIR_FORCE_CLOSE(fd);
    return ret;
}


static virPoolObjPtr
virSecretLoad(virPoolObjTablePtr secrets,
              const char *file,
              const char *path,
              const char *configDir)
{
    virSecretDefPtr def = NULL;
    virPoolObjPtr obj = NULL, ret = NULL;

    if (!(def = virSecretDefParseFile(path)))
        goto cleanup;

    if (virSecretLoadValidateUUID(def, file) < 0)
        goto cleanup;

    if (!(obj = virSecretObjAdd(secrets, def, configDir, NULL)))
        goto cleanup;
    def = NULL;

    if (virSecretLoadValue(obj) < 0)
        goto cleanup;

    VIR_STEAL_PTR(ret, obj);

 cleanup:
    if (obj)
        virPoolObjTableRemove(secrets, &obj);
    virSecretDefFree(def);
    virPoolObjEndAPI(&obj);
    return ret;
}


int
virSecretLoadAllConfigs(virPoolObjTablePtr secrets,
                        const char *configDir)
{
    DIR *dir = NULL;
    struct dirent *de;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    /* Ignore errors reported by readdir or other calls within the
     * loop (if any).  It's better to keep the secrets we managed to find. */
    while (virDirRead(dir, &de, NULL) > 0) {
        char *path;
        virPoolObjPtr obj;

        if (!virFileHasSuffix(de->d_name, ".xml"))
            continue;

        if (!(path = virFileBuildPath(configDir, de->d_name, NULL)))
            continue;

        if (!(obj = virSecretLoad(secrets, de->d_name, path, configDir))) {
            VIR_ERROR(_("Error reading secret: %s"),
                      virGetLastErrorMessage());
            VIR_FREE(path);
            continue;
        }

        VIR_FREE(path);
        virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return 0;
}
