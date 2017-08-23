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

struct _virSecretObj {
    virObjectLockable parent;
    char *configFile;
    char *base64File;
    virSecretDefPtr def;
    unsigned char *value;       /* May be NULL */
    size_t value_size;
};

static virClassPtr virSecretObjClass;
static void virSecretObjDispose(void *obj);

struct _virSecretObjList {
    virObjectLookupHash parent;
};

struct virSecretSearchData {
    int usageType;
    const char *usageID;
};


static int
virSecretObjOnceInit(void)
{
    if (!(virSecretObjClass = virClassNew(virClassForObjectLockable(),
                                          "virSecretObj",
                                          sizeof(virSecretObj),
                                          virSecretObjDispose)))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(virSecretObj)

static virSecretObjPtr
virSecretObjNew(void)
{
    virSecretObjPtr obj;

    if (virSecretObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectLockableNew(virSecretObjClass)))
        return NULL;

    virObjectLock(obj);

    return obj;
}


void
virSecretObjEndAPI(virSecretObjPtr *obj)
{
    if (!*obj)
        return;

    virObjectUnlock(*obj);
    virObjectUnref(*obj);
    *obj = NULL;
}


virSecretObjListPtr
virSecretObjListNew(void)
{
    return virObjectLookupHashNew(virClassForObjectLookupHash(), 50,
                                  VIR_OBJECT_LOOKUP_HASH_UUID);
}


static void
virSecretObjDispose(void *opaque)
{
    virSecretObjPtr obj = opaque;

    virSecretDefFree(obj->def);
    if (obj->value) {
        /* Wipe before free to ensure we don't leave a secret on the heap */
        memset(obj->value, 0, obj->value_size);
        VIR_FREE(obj->value);
    }
    VIR_FREE(obj->configFile);
    VIR_FREE(obj->base64File);
}


/**
 * virSecretObjFindByUUIDLocked:
 * @secrets: list of secret objects
 * @uuid: secret uuid to find
 *
 * This functions requires @secrets to be locked already!
 *
 * Returns: not locked, but ref'd secret object.
 */
static virSecretObjPtr
virSecretObjListFindByUUIDLocked(virSecretObjListPtr secrets,
                                 const char *uuidstr)
{
    return virObjectLookupHashFindLocked(secrets, uuidstr);
}


/**
 * virSecretObjFindByUUID:
 * @secrets: list of secret objects
 * @uuidstr: secret uuid to find
 *
 * This function locks @secrets and finds the secret object which
 * corresponds to @uuid.
 *
 * Returns: locked and ref'd secret object.
 */
virSecretObjPtr
virSecretObjListFindByUUID(virSecretObjListPtr secrets,
                           const char *uuidstr)
{
    return virObjectLookupHashFind(secrets, uuidstr);
}


static int
virSecretObjSearchName(const void *payload,
                       const void *name ATTRIBUTE_UNUSED,
                       const void *opaque)
{
    virSecretObjPtr obj = (virSecretObjPtr) payload;
    virSecretDefPtr def;
    struct virSecretSearchData *data = (struct virSecretSearchData *) opaque;
    int found = 0;

    virObjectLock(obj);
    def = obj->def;

    if (def->usage_type != data->usageType)
        goto cleanup;

    if (data->usageType != VIR_SECRET_USAGE_TYPE_NONE &&
        STREQ(def->usage_id, data->usageID))
        found = 1;

 cleanup:
    virObjectUnlock(obj);
    return found;
}


/**
 * virSecretObjFindByUsageLocked:
 * @secrets: list of secret objects
 * @usageType: secret usageType to find
 * @usageID: secret usage string
 *
 * This functions requires @secrets to be locked already!
 *
 * Returns: not locked, but ref'd secret object.
 */
static virSecretObjPtr
virSecretObjListFindByUsageLocked(virSecretObjListPtr secrets,
                                  int usageType,
                                  const char *usageID)
{
    struct virSecretSearchData data = { .usageType = usageType,
                                        .usageID = usageID };

    return virObjectLookupHashSearchUUIDLocked(secrets, virSecretObjSearchName,
                                               &data);
}


/**
 * virSecretObjFindByUsage:
 * @secrets: list of secret objects
 * @usageType: secret usageType to find
 * @usageID: secret usage string
 *
 * This function locks @secrets and finds the secret object which
 * corresponds to @usageID of @usageType.
 *
 * NB: The usageID cannot be used as a hash table key because
 *     virSecretDefParseUsage will not fill in the def->usage_id
 *     if the def->usage_type is VIR_SECRET_USAGE_TYPE_NONE, thus
 *     we cannot use def->usage_id as a key since both keys must
 *     be present in every object in order to be valid.
 *
 * Returns: locked and ref'd secret object.
 */
virSecretObjPtr
virSecretObjListFindByUsage(virSecretObjListPtr secrets,
                            int usageType,
                            const char *usageID)
{
    struct virSecretSearchData data = { .usageType = usageType,
                                        .usageID = usageID };

    return virObjectLookupHashSearchUUID(secrets, virSecretObjSearchName, &data);
}


/*
 * virSecretObjListRemove:
 * @secrets: list of secret objects
 * @obj: a locked secret object
 *
 * Remove the object from the hash table.  The caller must hold the lock
 * on the driver owning @secrets and must have also locked @secret to
 * ensure no one else is either waiting for @secret or still using it.
 */
void
virSecretObjListRemove(virSecretObjListPtr secrets,
                       virSecretObjPtr obj)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    if (!obj)
        return;

    virUUIDFormat(obj->def->uuid, uuidstr);
    virObjectLookupHashRemove(secrets, obj, uuidstr, NULL);
}


/*
 * virSecretObjListAdd:
 * @secrets: list of secret objects
 * @newdef: new secret definition
 * @configDir: directory to place secret config files
 * @oldDef: Former secret def (e.g. a reload path perhaps)
 *
 * Add the new @newdef to the secret obj table hash
 *
 * Returns: locked and ref'd secret or NULL if failure to add
 */
virSecretObjPtr
virSecretObjListAdd(virSecretObjListPtr secrets,
                    virSecretDefPtr newdef,
                    const char *configDir,
                    virSecretDefPtr *oldDef)
{
    virSecretObjPtr obj;
    virSecretDefPtr objdef;
    virSecretObjPtr ret = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virObjectRWLockWrite(secrets);

    if (oldDef)
        *oldDef = NULL;

    virUUIDFormat(newdef->uuid, uuidstr);

    /* Is there a secret already matching this UUID */
    if ((obj = virSecretObjListFindByUUIDLocked(secrets, uuidstr))) {
        objdef = obj->def;

        if (STRNEQ_NULLABLE(objdef->usage_id, newdef->usage_id)) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("a secret with UUID %s is already defined for "
                             "use with %s"),
                           uuidstr, objdef->usage_id);
            goto cleanup;
        }

        if (objdef->isprivate && !newdef->isprivate) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("cannot change private flag on existing secret"));
            goto cleanup;
        }

        if (oldDef)
            *oldDef = objdef;
        else
            virSecretDefFree(objdef);
        obj->def = newdef;
    } else {
        /* No existing secret with same UUID,
         * try look for matching usage instead */
        if ((obj = virSecretObjListFindByUsageLocked(secrets,
                                                     newdef->usage_type,
                                                     newdef->usage_id))) {
            objdef = obj->def;
            virUUIDFormat(objdef->uuid, uuidstr);
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("a secret with UUID %s already defined for "
                             "use with %s"),
                           uuidstr, newdef->usage_id);
            goto cleanup;
        }

        if (!(obj = virSecretObjNew()))
            goto cleanup;

        /* Generate the possible configFile and base64File strings
         * using the configDir, uuidstr, and appropriate suffix
         */
        if (!(obj->configFile = virFileBuildPath(configDir, uuidstr, ".xml")) ||
            !(obj->base64File = virFileBuildPath(configDir, uuidstr, ".base64")))
            goto cleanup;

        if (virObjectLookupHashAdd(secrets, obj, uuidstr, NULL) < 0)
            goto cleanup;

        obj->def = newdef;
    }

    ret = obj;
    obj = NULL;

 cleanup:
    virSecretObjEndAPI(&obj);
    virObjectRWUnlock(secrets);
    return ret;
}


static int
virSecretObjListGetHelper(void *payload,
                          const void *name ATTRIBUTE_UNUSED,
                          void *opaque)
{
    virSecretObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;
    virSecretObjListACLFilter filter = data->filter;
    char **uuids = (char **)data->elems;
    virSecretDefPtr def;

    if (data->error)
        return 0;

    if (data->maxElems >= 0 && data->nElems == data->maxElems)
        return 0;

    virObjectLock(obj);
    def = obj->def;

    if (filter && !filter(data->conn, def))
        goto cleanup;

    if (uuids) {
        char *uuidstr;

        if (VIR_ALLOC_N(uuidstr, VIR_UUID_STRING_BUFLEN) < 0) {
            data->error = true;
            goto cleanup;
        }

        virUUIDFormat(def->uuid, uuidstr);
        uuids[data->nElems++] = uuidstr;
    } else {
        data->nElems++;
    }

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virSecretObjListNumOfSecrets(virSecretObjListPtr secrets,
                             virSecretObjListACLFilter filter,
                             virConnectPtr conn)
{
    virObjectLookupHashForEachData data = {
        .conn = conn, .filter = filter, .error = false, .nElems = 0,
        .elems = NULL, .maxElems = -2 };

    return virObjectLookupHashForEachUUID(secrets, virSecretObjListGetHelper,
                                          &data);
}


#define MATCH(FLAG) (flags & (FLAG))
static bool
virSecretObjMatchFlags(virSecretObjPtr obj,
                       unsigned int flags)
{
    virSecretDefPtr def = obj->def;

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


static int
virSecretObjListExportCallback(void *payload,
                               const void *name ATTRIBUTE_UNUSED,
                               void *opaque)
{
    virSecretObjPtr obj = payload;
    virObjectLookupHashForEachDataPtr data = opaque;
    virSecretPtr *secrets = (virSecretPtr *)data->elems;
    virSecretObjListACLFilter filter = data->filter;
    virSecretDefPtr def;
    virSecretPtr secret = NULL;

    if (data->error)
        return 0;

    virObjectLock(obj);
    def = obj->def;

    if (filter && !filter(data->conn, def))
        goto cleanup;

    if (!virSecretObjMatchFlags(obj, data->flags))
        goto cleanup;

    if (!secrets) {
        data->nElems++;
        goto cleanup;
    }

    if (!(secret = virGetSecret(data->conn, def->uuid,
                                def->usage_type, def->usage_id))) {
        data->error = true;
        goto cleanup;
    }

    secrets[data->nElems++] = secret;

 cleanup:
    virObjectUnlock(obj);
    return 0;
}


int
virSecretObjListExport(virConnectPtr conn,
                       virSecretObjListPtr secretobjs,
                       virSecretPtr **secrets,
                       virSecretObjListACLFilter filter,
                       unsigned int flags)
{
    int ret;

    virObjectLookupHashForEachData data = {
        .conn = conn, .filter = filter, .error = false, .nElems = 0,
        .elems = NULL, .maxElems = 0, .flags = flags };

    if (secrets)
        data.maxElems = -1;

    ret = virObjectLookupHashForEachUUID(secretobjs,
                                         virSecretObjListExportCallback,
                                         &data);
    if (secrets)
        *secrets = (virSecretPtr *)data.elems;

    return ret;
}


int
virSecretObjListGetUUIDs(virSecretObjListPtr secrets,
                         char **uuids,
                         int maxuuids,
                         virSecretObjListACLFilter filter,
                         virConnectPtr conn)
{
    virObjectLookupHashForEachData data = {
        .conn = conn, .filter = filter, .error = false, .nElems = 0,
        .elems = (void **)uuids, .maxElems = maxuuids };

    return virObjectLookupHashForEachUUID(secrets, virSecretObjListGetHelper,
                                          &data);
}


int
virSecretObjDeleteConfig(virSecretObjPtr obj)
{
    virSecretDefPtr def = obj->def;

    if (!def->isephemeral &&
        unlink(obj->configFile) < 0 && errno != ENOENT) {
        virReportSystemError(errno, _("cannot unlink '%s'"),
                             obj->configFile);
        return -1;
    }

    return 0;
}


void
virSecretObjDeleteData(virSecretObjPtr obj)
{
    /* The configFile will already be removed, so secret won't be
     * loaded again if this fails */
    (void)unlink(obj->base64File);
}


/* Permanent secret storage */

/* Secrets are stored in virSecretDriverStatePtr->configDir.  Each secret
   has virSecretDef stored as XML in "$basename.xml".  If a value of the
   secret is defined, it is stored as base64 (with no formatting) in
   "$basename.base64".  "$basename" is in both cases the base64-encoded UUID. */
int
virSecretObjSaveConfig(virSecretObjPtr obj)
{
    char *xml = NULL;
    int ret = -1;

    if (!(xml = virSecretDefFormat(obj->def)))
        goto cleanup;

    if (virFileRewriteStr(obj->configFile, S_IRUSR | S_IWUSR, xml) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(xml);
    return ret;
}


int
virSecretObjSaveData(virSecretObjPtr obj)
{
    char *base64 = NULL;
    int ret = -1;

    if (!obj->value)
        return 0;

    if (!(base64 = virStringEncodeBase64(obj->value, obj->value_size)))
        goto cleanup;

    if (virFileRewriteStr(obj->base64File, S_IRUSR | S_IWUSR, base64) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(base64);
    return ret;
}


virSecretDefPtr
virSecretObjGetDef(virSecretObjPtr obj)
{
    return obj->def;
}


void
virSecretObjSetDef(virSecretObjPtr obj,
                   virSecretDefPtr def)
{
    obj->def = def;
}


unsigned char *
virSecretObjGetValue(virSecretObjPtr obj)
{
    virSecretDefPtr def = obj->def;
    unsigned char *ret = NULL;

    if (!obj->value) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(def->uuid, uuidstr);
        virReportError(VIR_ERR_NO_SECRET,
                       _("secret '%s' does not have a value"), uuidstr);
        goto cleanup;
    }

    if (VIR_ALLOC_N(ret, obj->value_size) < 0)
        goto cleanup;
    memcpy(ret, obj->value, obj->value_size);

 cleanup:
    return ret;
}


int
virSecretObjSetValue(virSecretObjPtr obj,
                     const unsigned char *value,
                     size_t value_size)
{
    virSecretDefPtr def = obj->def;
    unsigned char *old_value, *new_value;
    size_t old_value_size;

    if (VIR_ALLOC_N(new_value, value_size) < 0)
        return -1;

    old_value = obj->value;
    old_value_size = obj->value_size;

    memcpy(new_value, value, value_size);
    obj->value = new_value;
    obj->value_size = value_size;

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
    obj->value = old_value;
    obj->value_size = old_value_size;
    memset(new_value, 0, value_size);
    VIR_FREE(new_value);
    return -1;
}


size_t
virSecretObjGetValueSize(virSecretObjPtr obj)
{
    return obj->value_size;
}


void
virSecretObjSetValueSize(virSecretObjPtr obj,
                         size_t value_size)
{
    obj->value_size = value_size;
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
virSecretLoadValue(virSecretObjPtr obj)
{
    int ret = -1, fd = -1;
    struct stat st;
    char *contents = NULL, *value = NULL;
    size_t value_size;

    if ((fd = open(obj->base64File, O_RDONLY)) == -1) {
        if (errno == ENOENT) {
            ret = 0;
            goto cleanup;
        }
        virReportSystemError(errno, _("cannot open '%s'"),
                             obj->base64File);
        goto cleanup;
    }

    if (fstat(fd, &st) < 0) {
        virReportSystemError(errno, _("cannot stat '%s'"),
                             obj->base64File);
        goto cleanup;
    }

    if ((size_t)st.st_size != st.st_size) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("'%s' file does not fit in memory"),
                       obj->base64File);
        goto cleanup;
    }

    if (VIR_ALLOC_N(contents, st.st_size) < 0)
        goto cleanup;

    if (saferead(fd, contents, st.st_size) != st.st_size) {
        virReportSystemError(errno, _("cannot read '%s'"),
                             obj->base64File);
        goto cleanup;
    }

    VIR_FORCE_CLOSE(fd);

    if (!base64_decode_alloc(contents, st.st_size, &value, &value_size)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("invalid base64 in '%s'"),
                       obj->base64File);
        goto cleanup;
    }
    if (value == NULL)
        goto cleanup;

    obj->value = (unsigned char *)value;
    value = NULL;
    obj->value_size = value_size;

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


static virSecretObjPtr
virSecretLoad(virSecretObjListPtr secrets,
              const char *file,
              const char *path,
              const char *configDir)
{
    virSecretDefPtr def = NULL;
    virSecretObjPtr obj = NULL;

    if (!(def = virSecretDefParseFile(path)))
        goto cleanup;

    if (virSecretLoadValidateUUID(def, file) < 0)
        goto cleanup;

    if (!(obj = virSecretObjListAdd(secrets, def, configDir, NULL)))
        goto cleanup;
    def = NULL;

    if (virSecretLoadValue(obj) < 0) {
        virSecretObjListRemove(secrets, obj);
        virObjectUnref(obj);
        obj = NULL;
    }

 cleanup:
    virSecretDefFree(def);
    return obj;
}


int
virSecretLoadAllConfigs(virSecretObjListPtr secrets,
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
        virSecretObjPtr obj;

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
        virSecretObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return 0;
}
