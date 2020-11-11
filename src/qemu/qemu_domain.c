/*
 * qemu_domain.c: QEMU domain private state
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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

#include "qemu_domain.h"
#include "qemu_alias.h"
#include "qemu_block.h"
#include "qemu_cgroup.h"
#include "qemu_command.h"
#include "qemu_process.h"
#include "qemu_capabilities.h"
#include "qemu_hostdev.h"
#include "qemu_migration.h"
#include "qemu_migration_params.h"
#include "qemu_security.h"
#include "qemu_slirp.h"
#include "qemu_extdevice.h"
#include "qemu_blockjob.h"
#include "qemu_checkpoint.h"
#include "qemu_validate.h"
#include "qemu_namespace.h"
#include "viralloc.h"
#include "virlog.h"
#include "virerror.h"
#include "cpu/cpu.h"
#include "viruuid.h"
#include "virfile.h"
#include "domain_addr.h"
#include "domain_capabilities.h"
#include "domain_driver.h"
#include "domain_event.h"
#include "virtime.h"
#include "virnetdevopenvswitch.h"
#include "virstoragefile.h"
#include "virstring.h"
#include "virthreadjob.h"
#include "virprocess.h"
#include "vircrypto.h"
#include "virrandom.h"
#include "virsystemd.h"
#include "virsecret.h"
#include "logging/log_manager.h"
#include "locking/domain_lock.h"
#include "virdomainsnapshotobjlist.h"
#include "virdomaincheckpointobjlist.h"
#include "backup_conf.h"
#include "virutil.h"
#include "virqemu.h"

#include <sys/time.h>
#include <fcntl.h>

#define QEMU_QXL_VGAMEM_DEFAULT 16 * 1024

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_domain");


static void *
qemuJobAllocPrivate(void)
{
    return g_new0(qemuDomainJobPrivate, 1);
}


static void
qemuJobFreePrivate(void *opaque)
{
    qemuDomainJobPrivatePtr priv = opaque;

    if (!priv)
        return;

    qemuMigrationParamsFree(priv->migParams);
    VIR_FREE(priv);
}


static void
qemuJobResetPrivate(void *opaque)
{
    qemuDomainJobPrivatePtr priv = opaque;

    priv->spiceMigration = false;
    priv->spiceMigrated = false;
    priv->dumpCompleted = false;
    qemuMigrationParamsFree(priv->migParams);
    priv->migParams = NULL;
}


static int
qemuDomainObjPrivateXMLFormatNBDMigrationSource(virBufferPtr buf,
                                                virStorageSourcePtr src,
                                                virDomainXMLOptionPtr xmlopt)
{
    g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
    g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(buf);

    virBufferAsprintf(&attrBuf, " type='%s' format='%s'",
                      virStorageTypeToString(src->type),
                      virStorageFileFormatTypeToString(src->format));

    if (virDomainDiskSourceFormat(&childBuf, src, "source", 0, false,
                                  VIR_DOMAIN_DEF_FORMAT_STATUS,
                                  false, false, xmlopt) < 0)
        return -1;

    virXMLFormatElement(buf, "migrationSource", &attrBuf, &childBuf);

    return 0;
}


static int
qemuDomainObjPrivateXMLFormatNBDMigration(virBufferPtr buf,
                                          virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    size_t i;
    virDomainDiskDefPtr disk;
    qemuDomainDiskPrivatePtr diskPriv;

    for (i = 0; i < vm->def->ndisks; i++) {
        g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
        g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(buf);
        disk = vm->def->disks[i];
        diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);

        virBufferAsprintf(&attrBuf, " dev='%s' migrating='%s'",
                          disk->dst, diskPriv->migrating ? "yes" : "no");

        if (diskPriv->migrSource &&
            qemuDomainObjPrivateXMLFormatNBDMigrationSource(&childBuf,
                                                            diskPriv->migrSource,
                                                            priv->driver->xmlopt) < 0)
            return -1;

        virXMLFormatElement(buf, "disk", &attrBuf, &childBuf);
    }

    return 0;
}

static int
qemuDomainFormatJobPrivate(virBufferPtr buf,
                           qemuDomainJobObjPtr job,
                           virDomainObjPtr vm)
{
    qemuDomainJobPrivatePtr priv = job->privateData;

    if (job->asyncJob == QEMU_ASYNC_JOB_MIGRATION_OUT &&
        qemuDomainObjPrivateXMLFormatNBDMigration(buf, vm) < 0)
        return -1;

    if (priv->migParams)
        qemuMigrationParamsFormat(buf, priv->migParams);

    return 0;
}


static int
qemuDomainObjPrivateXMLParseJobNBDSource(xmlNodePtr node,
                                         xmlXPathContextPtr ctxt,
                                         virDomainDiskDefPtr disk,
                                         virDomainXMLOptionPtr xmlopt)
{
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);
    g_autofree char *format = NULL;
    g_autofree char *type = NULL;
    g_autoptr(virStorageSource) migrSource = NULL;
    xmlNodePtr sourceNode;

    ctxt->node = node;

    if (!(ctxt->node = virXPathNode("./migrationSource", ctxt)))
        return 0;

    if (!(type = virXMLPropString(ctxt->node, "type"))) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing storage source type"));
        return -1;
    }

    if (!(format = virXMLPropString(ctxt->node, "format"))) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing storage source format"));
        return -1;
    }

    if (!(migrSource = virDomainStorageSourceParseBase(type, format, NULL)))
        return -1;

    /* newer libvirt uses the <source> subelement instead of formatting the
     * source directly into <migrationSource> */
    if ((sourceNode = virXPathNode("./source", ctxt)))
        ctxt->node = sourceNode;

    if (virDomainStorageSourceParse(ctxt->node, ctxt, migrSource,
                                    VIR_DOMAIN_DEF_PARSE_STATUS, xmlopt) < 0)
        return -1;

    diskPriv->migrSource = g_steal_pointer(&migrSource);
    return 0;
}


static int
qemuDomainObjPrivateXMLParseJobNBD(virDomainObjPtr vm,
                                   xmlXPathContextPtr ctxt)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autofree xmlNodePtr *nodes = NULL;
    size_t i;
    int n;

    if ((n = virXPathNodeSet("./disk[@migrating='yes']", ctxt, &nodes)) < 0)
        return -1;

    if (n > 0) {
        if (priv->job.asyncJob != QEMU_ASYNC_JOB_MIGRATION_OUT) {
            VIR_WARN("Found disks marked for migration but we were not "
                     "migrating");
            n = 0;
        }
        for (i = 0; i < n; i++) {
            virDomainDiskDefPtr disk;
            g_autofree char *dst = NULL;

            if ((dst = virXMLPropString(nodes[i], "dev")) &&
                (disk = virDomainDiskByTarget(vm->def, dst))) {
                QEMU_DOMAIN_DISK_PRIVATE(disk)->migrating = true;

                if (qemuDomainObjPrivateXMLParseJobNBDSource(nodes[i], ctxt,
                                                             disk,
                                                             priv->driver->xmlopt) < 0)
                    return -1;
            }
        }
    }

    return 0;
}

static int
qemuDomainParseJobPrivate(xmlXPathContextPtr ctxt,
                          qemuDomainJobObjPtr job,
                          virDomainObjPtr vm)
{
    qemuDomainJobPrivatePtr priv = job->privateData;

    if (qemuDomainObjPrivateXMLParseJobNBD(vm, ctxt) < 0)
        return -1;

    if (qemuMigrationParamsParse(ctxt, &priv->migParams) < 0)
        return -1;

    return 0;
}


static qemuDomainObjPrivateJobCallbacks qemuPrivateJobCallbacks = {
    .allocJobPrivate = qemuJobAllocPrivate,
    .freeJobPrivate = qemuJobFreePrivate,
    .resetJobPrivate = qemuJobResetPrivate,
    .formatJob = qemuDomainFormatJobPrivate,
    .parseJob = qemuDomainParseJobPrivate,
};

/**
 * qemuDomainObjFromDomain:
 * @domain: Domain pointer that has to be looked up
 *
 * This function looks up @domain and returns the appropriate virDomainObjPtr
 * that has to be released by calling virDomainObjEndAPI().
 *
 * Returns the domain object with incremented reference counter which is locked
 * on success, NULL otherwise.
 */
virDomainObjPtr
qemuDomainObjFromDomain(virDomainPtr domain)
{
    virDomainObjPtr vm;
    virQEMUDriverPtr driver = domain->conn->privateData;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    vm = virDomainObjListFindByUUID(driver->domains, domain->uuid);
    if (!vm) {
        virUUIDFormat(domain->uuid, uuidstr);
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching uuid '%s' (%s)"),
                       uuidstr, domain->name);
        return NULL;
    }

    return vm;
}


struct _qemuDomainLogContext {
    GObject parent;

    int writefd;
    int readfd; /* Only used if manager == NULL */
    off_t pos;
    ino_t inode; /* Only used if manager != NULL */
    char *path;
    virLogManagerPtr manager;
};

G_DEFINE_TYPE(qemuDomainLogContext, qemu_domain_log_context, G_TYPE_OBJECT);
static virClassPtr qemuDomainSaveCookieClass;

static void qemuDomainLogContextFinalize(GObject *obj);
static void qemuDomainSaveCookieDispose(void *obj);


static int
qemuDomainOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainSaveCookie, virClassForObject()))
        return -1;

    return 0;
}

static void qemu_domain_log_context_init(qemuDomainLogContext *logctxt G_GNUC_UNUSED)
{
}

static void qemu_domain_log_context_class_init(qemuDomainLogContextClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = qemuDomainLogContextFinalize;
}

VIR_ONCE_GLOBAL_INIT(qemuDomain);

static void
qemuDomainLogContextFinalize(GObject *object)
{
    qemuDomainLogContextPtr ctxt = QEMU_DOMAIN_LOG_CONTEXT(object);
    VIR_DEBUG("ctxt=%p", ctxt);

    virLogManagerFree(ctxt->manager);
    VIR_FREE(ctxt->path);
    VIR_FORCE_CLOSE(ctxt->writefd);
    VIR_FORCE_CLOSE(ctxt->readfd);
    G_OBJECT_CLASS(qemu_domain_log_context_parent_class)->finalize(object);
}

/* qemuDomainGetMasterKeyFilePath:
 * @libDir: Directory path to domain lib files
 *
 * Generate a path to the domain master key file for libDir.
 * It's up to the caller to handle checking if path exists.
 *
 * Returns path to memory containing the name of the file. It is up to the
 * caller to free; otherwise, NULL on failure.
 */
char *
qemuDomainGetMasterKeyFilePath(const char *libDir)
{
    if (!libDir) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("invalid path for master key file"));
        return NULL;
    }
    return virFileBuildPath(libDir, "master-key.aes", NULL);
}


/* qemuDomainWriteMasterKeyFile:
 * @driver: qemu driver data
 * @vm: Pointer to the vm object
 *
 * Get the desired path to the masterKey file and store it in the path.
 *
 * Returns 0 on success, -1 on failure with error message indicating failure
 */
int
qemuDomainWriteMasterKeyFile(virQEMUDriverPtr driver,
                             virDomainObjPtr vm)
{
    g_autofree char *path = NULL;
    int fd = -1;
    int ret = -1;
    qemuDomainObjPrivatePtr priv = vm->privateData;

    /* Only gets filled in if we have the capability */
    if (!priv->masterKey)
        return 0;

    if (!(path = qemuDomainGetMasterKeyFilePath(priv->libDir)))
        return -1;

    if ((fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, 0600)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to open domain master key file for write"));
        goto cleanup;
    }

    if (safewrite(fd, priv->masterKey, priv->masterKeyLen) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to write master key file for domain"));
        goto cleanup;
    }

    if (qemuSecurityDomainSetPathLabel(driver, vm, path, false) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FORCE_CLOSE(fd);

    return ret;
}


static void
qemuDomainMasterKeyFree(qemuDomainObjPrivatePtr priv)
{
    if (!priv->masterKey)
        return;

    VIR_DISPOSE_N(priv->masterKey, priv->masterKeyLen);
}

/* qemuDomainMasterKeyReadFile:
 * @priv: pointer to domain private object
 *
 * Expected to be called during qemuProcessReconnect once the domain
 * libDir has been generated through qemuStateInitialize calling
 * virDomainObjListLoadAllConfigs which will restore the libDir path
 * to the domain private object.
 *
 * This function will get the path to the master key file and if it
 * exists, it will read the contents of the file saving it in priv->masterKey.
 *
 * Once the file exists, the validity checks may cause failures; however,
 * if the file doesn't exist or the capability doesn't exist, we just
 * return (mostly) quietly.
 *
 * Returns 0 on success or lack of capability
 *        -1 on failure with error message indicating failure
 */
int
qemuDomainMasterKeyReadFile(qemuDomainObjPrivatePtr priv)
{
    g_autofree char *path = NULL;
    int fd = -1;
    uint8_t *masterKey = NULL;
    ssize_t masterKeyLen = 0;

    /* If we don't have the capability, then do nothing. */
    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_OBJECT_SECRET))
        return 0;

    if (!(path = qemuDomainGetMasterKeyFilePath(priv->libDir)))
        return -1;

    if (!virFileExists(path)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("domain master key file doesn't exist in %s"),
                       priv->libDir);
        goto error;
    }

    if ((fd = open(path, O_RDONLY)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to open domain master key file for read"));
        goto error;
    }

    masterKey = g_new0(uint8_t, 1024);

    if ((masterKeyLen = saferead(fd, masterKey, 1024)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unable to read domain master key file"));
        goto error;
    }

    if (masterKeyLen != QEMU_DOMAIN_MASTER_KEY_LEN) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("invalid master key read, size=%zd"), masterKeyLen);
        goto error;
    }

    masterKey = g_renew(uint8_t, masterKey, masterKeyLen);

    priv->masterKey = masterKey;
    priv->masterKeyLen = masterKeyLen;

    VIR_FORCE_CLOSE(fd);

    return 0;

 error:
    if (masterKeyLen > 0)
        memset(masterKey, 0, masterKeyLen);
    VIR_FREE(masterKey);

    VIR_FORCE_CLOSE(fd);

    return -1;
}


/* qemuDomainMasterKeyRemove:
 * @priv: Pointer to the domain private object
 *
 * Remove the traces of the master key, clear the heap, clear the file,
 * delete the file.
 */
void
qemuDomainMasterKeyRemove(qemuDomainObjPrivatePtr priv)
{
    g_autofree char *path = NULL;

    if (!priv->masterKey)
        return;

    /* Clear the contents */
    qemuDomainMasterKeyFree(priv);

    /* Delete the master key file */
    path = qemuDomainGetMasterKeyFilePath(priv->libDir);
    unlink(path);
}


/* qemuDomainMasterKeyCreate:
 * @vm: Pointer to the domain object
 *
 * As long as the underlying qemu has the secret capability,
 * generate and store 'raw' in a file a random 32-byte key to
 * be used as a secret shared with qemu to share sensitive data.
 *
 * Returns: 0 on success, -1 w/ error message on failure
 */
int
qemuDomainMasterKeyCreate(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    /* If we don't have the capability, then do nothing. */
    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_OBJECT_SECRET))
        return 0;

    priv->masterKey = g_new0(uint8_t, QEMU_DOMAIN_MASTER_KEY_LEN);
    priv->masterKeyLen = QEMU_DOMAIN_MASTER_KEY_LEN;

    if (virRandomBytes(priv->masterKey, priv->masterKeyLen) < 0) {
        VIR_DISPOSE_N(priv->masterKey, priv->masterKeyLen);
        return -1;
    }

    return 0;
}


static void
qemuDomainSecretPlainClear(qemuDomainSecretPlainPtr secret)
{
    VIR_FREE(secret->username);
    VIR_DISPOSE_N(secret->secret, secret->secretlen);
}


static void
qemuDomainSecretAESClear(qemuDomainSecretAESPtr secret,
                         bool keepAlias)
{
    if (!keepAlias)
        VIR_FREE(secret->alias);

    VIR_FREE(secret->username);
    VIR_FREE(secret->iv);
    VIR_FREE(secret->ciphertext);
}


static void
qemuDomainSecretInfoClear(qemuDomainSecretInfoPtr secinfo,
                          bool keepAlias)
{
    if (!secinfo)
        return;

    switch ((qemuDomainSecretInfoType) secinfo->type) {
    case VIR_DOMAIN_SECRET_INFO_TYPE_PLAIN:
        qemuDomainSecretPlainClear(&secinfo->s.plain);
        break;

    case VIR_DOMAIN_SECRET_INFO_TYPE_AES:
        qemuDomainSecretAESClear(&secinfo->s.aes, keepAlias);
        break;

    case VIR_DOMAIN_SECRET_INFO_TYPE_LAST:
        break;
    }
}


void
qemuDomainSecretInfoFree(qemuDomainSecretInfoPtr secinfo)
{
    qemuDomainSecretInfoClear(secinfo, false);
    g_free(secinfo);
}


/**
 * qemuDomainSecretInfoDestroy:
 * @secinfo: object to destroy
 *
 * Removes any data unnecessary for further use, but keeps alias allocated.
 */
void
qemuDomainSecretInfoDestroy(qemuDomainSecretInfoPtr secinfo)
{
    qemuDomainSecretInfoClear(secinfo, true);
}


static virClassPtr qemuDomainDiskPrivateClass;
static void qemuDomainDiskPrivateDispose(void *obj);

static int
qemuDomainDiskPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainDiskPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainDiskPrivate);

static virObjectPtr
qemuDomainDiskPrivateNew(void)
{
    qemuDomainDiskPrivatePtr priv;

    if (qemuDomainDiskPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainDiskPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}

static void
qemuDomainDiskPrivateDispose(void *obj)
{
    qemuDomainDiskPrivatePtr priv = obj;

    virObjectUnref(priv->migrSource);
    VIR_FREE(priv->qomName);
    VIR_FREE(priv->nodeCopyOnRead);
    virObjectUnref(priv->blockjob);
}

static virClassPtr qemuDomainStorageSourcePrivateClass;
static void qemuDomainStorageSourcePrivateDispose(void *obj);

static int
qemuDomainStorageSourcePrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainStorageSourcePrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainStorageSourcePrivate);

virObjectPtr
qemuDomainStorageSourcePrivateNew(void)
{
    qemuDomainStorageSourcePrivatePtr priv;

    if (qemuDomainStorageSourcePrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainStorageSourcePrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainStorageSourcePrivateDispose(void *obj)
{
    qemuDomainStorageSourcePrivatePtr priv = obj;

    g_clear_pointer(&priv->secinfo, qemuDomainSecretInfoFree);
    g_clear_pointer(&priv->encinfo, qemuDomainSecretInfoFree);
    g_clear_pointer(&priv->httpcookie, qemuDomainSecretInfoFree);
    g_clear_pointer(&priv->tlsKeySecret, qemuDomainSecretInfoFree);
}


qemuDomainStorageSourcePrivatePtr
qemuDomainStorageSourcePrivateFetch(virStorageSourcePtr src)
{
    if (!src->privateData)
        src->privateData = qemuDomainStorageSourcePrivateNew();

    return QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(src);
}


static virClassPtr qemuDomainVcpuPrivateClass;
static void qemuDomainVcpuPrivateDispose(void *obj);

static int
qemuDomainVcpuPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainVcpuPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainVcpuPrivate);

static virObjectPtr
qemuDomainVcpuPrivateNew(void)
{
    qemuDomainVcpuPrivatePtr priv;

    if (qemuDomainVcpuPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainVcpuPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainVcpuPrivateDispose(void *obj)
{
    qemuDomainVcpuPrivatePtr priv = obj;

    VIR_FREE(priv->type);
    VIR_FREE(priv->alias);
    virJSONValueFree(priv->props);
    return;
}


static virClassPtr qemuDomainChrSourcePrivateClass;
static void qemuDomainChrSourcePrivateDispose(void *obj);

static int
qemuDomainChrSourcePrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainChrSourcePrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainChrSourcePrivate);

static virObjectPtr
qemuDomainChrSourcePrivateNew(void)
{
    qemuDomainChrSourcePrivatePtr priv;

    if (qemuDomainChrSourcePrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainChrSourcePrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainChrSourcePrivateDispose(void *obj)
{
    qemuDomainChrSourcePrivatePtr priv = obj;

    g_clear_pointer(&priv->secinfo, qemuDomainSecretInfoFree);
}


static virClassPtr qemuDomainVsockPrivateClass;
static void qemuDomainVsockPrivateDispose(void *obj);

static int
qemuDomainVsockPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainVsockPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainVsockPrivate);

static virObjectPtr
qemuDomainVsockPrivateNew(void)
{
    qemuDomainVsockPrivatePtr priv;

    if (qemuDomainVsockPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainVsockPrivateClass)))
        return NULL;

    priv->vhostfd = -1;

    return (virObjectPtr) priv;
}


static void
qemuDomainVsockPrivateDispose(void *obj G_GNUC_UNUSED)
{
    qemuDomainVsockPrivatePtr priv = obj;

    VIR_FORCE_CLOSE(priv->vhostfd);
}


static virClassPtr qemuDomainGraphicsPrivateClass;
static void qemuDomainGraphicsPrivateDispose(void *obj);

static int
qemuDomainGraphicsPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainGraphicsPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainGraphicsPrivate);

static virObjectPtr
qemuDomainGraphicsPrivateNew(void)
{
    qemuDomainGraphicsPrivatePtr priv;

    if (qemuDomainGraphicsPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainGraphicsPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainGraphicsPrivateDispose(void *obj)
{
    qemuDomainGraphicsPrivatePtr priv = obj;

    VIR_FREE(priv->tlsAlias);
    g_clear_pointer(&priv->secinfo, qemuDomainSecretInfoFree);
}


static virClassPtr qemuDomainNetworkPrivateClass;
static void qemuDomainNetworkPrivateDispose(void *obj);


static int
qemuDomainNetworkPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainNetworkPrivate, virClassForObject()))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(qemuDomainNetworkPrivate);


static virObjectPtr
qemuDomainNetworkPrivateNew(void)
{
    qemuDomainNetworkPrivatePtr priv;

    if (qemuDomainNetworkPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainNetworkPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainNetworkPrivateDispose(void *obj G_GNUC_UNUSED)
{
    qemuDomainNetworkPrivatePtr priv = obj;

    qemuSlirpFree(priv->slirp);
}


static virClassPtr qemuDomainFSPrivateClass;
static void qemuDomainFSPrivateDispose(void *obj);


static int
qemuDomainFSPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainFSPrivate, virClassForObject()))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(qemuDomainFSPrivate);


static virObjectPtr
qemuDomainFSPrivateNew(void)
{
    qemuDomainFSPrivatePtr priv;

    if (qemuDomainFSPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainFSPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
qemuDomainFSPrivateDispose(void *obj)
{
    qemuDomainFSPrivatePtr priv = obj;

    g_free(priv->vhostuser_fs_sock);
}

static virClassPtr qemuDomainVideoPrivateClass;
static void qemuDomainVideoPrivateDispose(void *obj);


static int
qemuDomainVideoPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(qemuDomainVideoPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(qemuDomainVideoPrivate);


static virObjectPtr
qemuDomainVideoPrivateNew(void)
{
    qemuDomainVideoPrivatePtr priv;

    if (qemuDomainVideoPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(qemuDomainVideoPrivateClass)))
        return NULL;

    priv->vhost_user_fd = -1;

    return (virObjectPtr) priv;
}


static void
qemuDomainVideoPrivateDispose(void *obj)
{
    qemuDomainVideoPrivatePtr priv = obj;

    VIR_FORCE_CLOSE(priv->vhost_user_fd);
}


/* qemuDomainSecretPlainSetup:
 * @secinfo: Pointer to secret info
 * @usageType: The virSecretUsageType
 * @username: username to use for authentication (may be NULL)
 * @seclookupdef: Pointer to seclookupdef data
 *
 * Taking a secinfo, fill in the plaintext information
 *
 * Returns 0 on success, -1 on failure with error message
 */
static int
qemuDomainSecretPlainSetup(qemuDomainSecretInfoPtr secinfo,
                           virSecretUsageType usageType,
                           const char *username,
                           virSecretLookupTypeDefPtr seclookupdef)
{
    g_autoptr(virConnect) conn = virGetConnectSecret();
    int ret = -1;

    if (!conn)
        return -1;

    secinfo->type = VIR_DOMAIN_SECRET_INFO_TYPE_PLAIN;
    secinfo->s.plain.username = g_strdup(username);

    ret = virSecretGetSecretString(conn, seclookupdef, usageType,
                                   &secinfo->s.plain.secret,
                                   &secinfo->s.plain.secretlen);

    return ret;
}


/* qemuDomainSecretAESSetup:
 * @priv: pointer to domain private object
 * @alias: alias of the secret
 * @username: username to use (may be NULL)
 * @secret: secret data
 * @secretlen: length of @secret
 *
 * Encrypts @secret for use with qemu.
 *
 * Returns qemuDomainSecretInfoPtr filled with the necessary information.
 */
static qemuDomainSecretInfoPtr
qemuDomainSecretAESSetup(qemuDomainObjPrivatePtr priv,
                         const char *alias,
                         const char *username,
                         uint8_t *secret,
                         size_t secretlen)
{
    g_autoptr(qemuDomainSecretInfo) secinfo = NULL;
    g_autofree uint8_t *raw_iv = NULL;
    size_t ivlen = QEMU_DOMAIN_AES_IV_LEN;
    g_autofree uint8_t *ciphertext = NULL;
    size_t ciphertextlen = 0;

    if (!qemuDomainSupportsEncryptedSecret(priv)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("encrypted secrets are not supported"));
        return NULL;
    }

    secinfo = g_new0(qemuDomainSecretInfo, 1);

    secinfo->type = VIR_DOMAIN_SECRET_INFO_TYPE_AES;
    secinfo->s.aes.alias = g_strdup(alias);
    secinfo->s.aes.username = g_strdup(username);

    raw_iv = g_new0(uint8_t, ivlen);

    /* Create a random initialization vector */
    if (virRandomBytes(raw_iv, ivlen) < 0)
        return NULL;

    /* Encode the IV and save that since qemu will need it */
    secinfo->s.aes.iv = g_base64_encode(raw_iv, ivlen);

    if (virCryptoEncryptData(VIR_CRYPTO_CIPHER_AES256CBC,
                             priv->masterKey, QEMU_DOMAIN_MASTER_KEY_LEN,
                             raw_iv, ivlen, secret, secretlen,
                             &ciphertext, &ciphertextlen) < 0)
        return NULL;

    /* Now encode the ciphertext and store to be passed to qemu */
    secinfo->s.aes.ciphertext = g_base64_encode(ciphertext,
                                                ciphertextlen);

    return g_steal_pointer(&secinfo);
}


/**
 * qemuDomainSecretAESSetupFromSecret:
 * @priv: pointer to domain private object
 * @srcalias: Alias of the disk/hostdev used to generate the secret alias
 * @secretuse: specific usage for the secret (may be NULL if main object is using it)
 * @usageType: The virSecretUsageType
 * @username: username to use for authentication (may be NULL)
 * @seclookupdef: Pointer to seclookupdef data
 *
 * Looks up a secret in the secret driver based on @usageType and @seclookupdef
 * and builds qemuDomainSecretInfoPtr from it. @use describes the usage of the
 * secret in case if @srcalias requires more secrets for various usage cases.
 */
static qemuDomainSecretInfoPtr
qemuDomainSecretAESSetupFromSecret(qemuDomainObjPrivatePtr priv,
                                   const char *srcalias,
                                   const char *secretuse,
                                   virSecretUsageType usageType,
                                   const char *username,
                                   virSecretLookupTypeDefPtr seclookupdef)
{
    g_autoptr(virConnect) conn = virGetConnectSecret();
    qemuDomainSecretInfoPtr secinfo;
    g_autofree char *alias = qemuAliasForSecret(srcalias, secretuse);
    uint8_t *secret = NULL;
    size_t secretlen = 0;

    if (!conn)
        return NULL;

    if (virSecretGetSecretString(conn, seclookupdef, usageType,
                                 &secret, &secretlen) < 0)
        return NULL;

    secinfo = qemuDomainSecretAESSetup(priv, alias, username, secret, secretlen);

    VIR_DISPOSE_N(secret, secretlen);

    return secinfo;
}


/**
 * qemuDomainSupportsEncryptedSecret:
 * @priv: qemu domain private data
 *
 * Returns true if libvirt can use encrypted 'secret' objects with VM which
 * @priv belongs to.
 */
bool
qemuDomainSupportsEncryptedSecret(qemuDomainObjPrivatePtr priv)
{
    return virCryptoHaveCipher(VIR_CRYPTO_CIPHER_AES256CBC) &&
           virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_OBJECT_SECRET) &&
           priv->masterKey;
}


/* qemuDomainSecretInfoNewPlain:
 * @usageType: Secret usage type
 * @username: username
 * @lookupDef: lookup def describing secret
 *
 * Helper function to create a secinfo to be used for secinfo consumers. This
 * sets up a 'plain' (unencrypted) secret for legacy consumers.
 *
 * Returns @secinfo on success, NULL on failure. Caller is responsible
 * to eventually free @secinfo.
 */
static qemuDomainSecretInfoPtr
qemuDomainSecretInfoNewPlain(virSecretUsageType usageType,
                             const char *username,
                             virSecretLookupTypeDefPtr lookupDef)
{
    qemuDomainSecretInfoPtr secinfo = NULL;

    secinfo = g_new0(qemuDomainSecretInfo, 1);

    if (qemuDomainSecretPlainSetup(secinfo, usageType, username, lookupDef) < 0) {
        g_clear_pointer(&secinfo, qemuDomainSecretInfoFree);
        return NULL;
    }

    return secinfo;
}


/**
 * qemuDomainSecretInfoTLSNew:
 * @priv: pointer to domain private object
 * @srcAlias: Alias base to use for TLS object
 * @secretUUID: Provide a secretUUID value to look up/create the secretInfo
 *
 * Using the passed @secretUUID, generate a seclookupdef that can be used
 * to generate the returned qemuDomainSecretInfoPtr for a TLS based secret.
 *
 * Returns qemuDomainSecretInfoPtr or NULL on error.
 */
qemuDomainSecretInfoPtr
qemuDomainSecretInfoTLSNew(qemuDomainObjPrivatePtr priv,
                           const char *srcAlias,
                           const char *secretUUID)
{
    virSecretLookupTypeDef seclookupdef = {0};

    if (virUUIDParse(secretUUID, seclookupdef.u.uuid) < 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("malformed TLS secret uuid '%s' provided"),
                       secretUUID);
        return NULL;
    }
    seclookupdef.type = VIR_SECRET_LOOKUP_TYPE_UUID;

    return qemuDomainSecretAESSetupFromSecret(priv, srcAlias, NULL,
                                              VIR_SECRET_USAGE_TYPE_TLS,
                                              NULL, &seclookupdef);
}


void
qemuDomainSecretDiskDestroy(virDomainDiskDefPtr disk)
{
    qemuDomainStorageSourcePrivatePtr srcPriv;
    virStorageSourcePtr n;

    for (n = disk->src; virStorageSourceIsBacking(n); n = n->backingStore) {
        if ((srcPriv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(n))) {
            qemuDomainSecretInfoDestroy(srcPriv->secinfo);
            qemuDomainSecretInfoDestroy(srcPriv->encinfo);
            qemuDomainSecretInfoDestroy(srcPriv->tlsKeySecret);
        }
    }
}


bool
qemuDomainStorageSourceHasAuth(virStorageSourcePtr src)
{
    if (!virStorageSourceIsEmpty(src) &&
        virStorageSourceGetActualType(src) == VIR_STORAGE_TYPE_NETWORK &&
        src->auth &&
        (src->protocol == VIR_STORAGE_NET_PROTOCOL_ISCSI ||
         src->protocol == VIR_STORAGE_NET_PROTOCOL_RBD))
        return true;

    return false;
}


static bool
qemuDomainDiskHasEncryptionSecret(virStorageSourcePtr src)
{
    if (!virStorageSourceIsEmpty(src) && src->encryption &&
        src->encryption->format == VIR_STORAGE_ENCRYPTION_FORMAT_LUKS &&
        src->encryption->nsecrets > 0)
        return true;

    return false;
}


static qemuDomainSecretInfoPtr
qemuDomainSecretStorageSourcePrepareCookies(qemuDomainObjPrivatePtr priv,
                                            virStorageSourcePtr src,
                                            const char *aliasprotocol)
{
    g_autofree char *secretalias = qemuAliasForSecret(aliasprotocol, "httpcookie");
    g_autofree char *cookies = qemuBlockStorageSourceGetCookieString(src);

    return qemuDomainSecretAESSetup(priv, secretalias, NULL,
                                    (uint8_t *) cookies, strlen(cookies));
}


/**
 * qemuDomainSecretStorageSourcePrepare:
 * @priv: domain private object
 * @src: storage source struct to setup
 * @authalias: prefix of the alias for secret holding authentication data
 * @encalias: prefix of the alias for secret holding encryption password
 *
 * Prepares data necessary for encryption and authentication of @src. The two
 * alias prefixes are provided since in the backing chain authentication belongs
 * to the storage protocol data whereas encryption is relevant to the format
 * driver in qemu. The two will have different node names.
 *
 * Returns 0 on success; -1 on error while reporting an libvirt error.
 */
static int
qemuDomainSecretStorageSourcePrepare(qemuDomainObjPrivatePtr priv,
                                     virStorageSourcePtr src,
                                     const char *aliasprotocol,
                                     const char *aliasformat)
{
    qemuDomainStorageSourcePrivatePtr srcPriv;
    bool iscsiHasPS = virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_ISCSI_PASSWORD_SECRET);
    bool hasAuth = qemuDomainStorageSourceHasAuth(src);
    bool hasEnc = qemuDomainDiskHasEncryptionSecret(src);

    if (!hasAuth && !hasEnc && src->ncookies == 0)
        return 0;

    if (!(src->privateData = qemuDomainStorageSourcePrivateNew()))
        return -1;

    srcPriv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(src);

    if (hasAuth) {
        virSecretUsageType usageType = VIR_SECRET_USAGE_TYPE_ISCSI;

        if (src->protocol == VIR_STORAGE_NET_PROTOCOL_RBD)
            usageType = VIR_SECRET_USAGE_TYPE_CEPH;

        if (!qemuDomainSupportsEncryptedSecret(priv) ||
            (src->protocol == VIR_STORAGE_NET_PROTOCOL_ISCSI && !iscsiHasPS)) {
            srcPriv->secinfo = qemuDomainSecretInfoNewPlain(usageType,
                                                            src->auth->username,
                                                            &src->auth->seclookupdef);
        } else {
            srcPriv->secinfo = qemuDomainSecretAESSetupFromSecret(priv, aliasprotocol,
                                                                  "auth",
                                                                  usageType,
                                                                  src->auth->username,
                                                                  &src->auth->seclookupdef);
        }

        if (!srcPriv->secinfo)
            return -1;
    }

    if (hasEnc) {
        if (!(srcPriv->encinfo = qemuDomainSecretAESSetupFromSecret(priv, aliasformat,
                                                                    "encryption",
                                                                    VIR_SECRET_USAGE_TYPE_VOLUME,
                                                                    NULL,
                                                                    &src->encryption->secrets[0]->seclookupdef)))
              return -1;
    }

    if (src->ncookies &&
        virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV) &&
        !(srcPriv->httpcookie = qemuDomainSecretStorageSourcePrepareCookies(priv,
                                                                            src,
                                                                            aliasprotocol)))
        return -1;

    return 0;
}


void
qemuDomainSecretHostdevDestroy(virDomainHostdevDefPtr hostdev)
{
    qemuDomainStorageSourcePrivatePtr srcPriv;

    if (virHostdevIsSCSIDevice(hostdev)) {
        virDomainHostdevSubsysSCSIPtr scsisrc = &hostdev->source.subsys.u.scsi;
        virDomainHostdevSubsysSCSIiSCSIPtr iscsisrc = &scsisrc->u.iscsi;

        if (scsisrc->protocol == VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_ISCSI) {
            srcPriv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(iscsisrc->src);
            if (srcPriv)
                qemuDomainSecretInfoDestroy(srcPriv->secinfo);
        }
    }
}


void
qemuDomainSecretChardevDestroy(virDomainChrSourceDefPtr dev)
{
    qemuDomainChrSourcePrivatePtr chrSourcePriv =
        QEMU_DOMAIN_CHR_SOURCE_PRIVATE(dev);

    if (!chrSourcePriv || !chrSourcePriv->secinfo)
        return;

    g_clear_pointer(&chrSourcePriv->secinfo, qemuDomainSecretInfoFree);
}


/* qemuDomainSecretChardevPrepare:
 * @cfg: Pointer to driver config object
 * @priv: pointer to domain private object
 * @chrAlias: Alias of the chr device
 * @dev: Pointer to a char source definition
 *
 * For a TCP character device, generate a qemuDomainSecretInfo to be used
 * by the command line code to generate the secret for the tls-creds to use.
 *
 * Returns 0 on success, -1 on failure
 */
int
qemuDomainSecretChardevPrepare(virQEMUDriverConfigPtr cfg,
                               qemuDomainObjPrivatePtr priv,
                               const char *chrAlias,
                               virDomainChrSourceDefPtr dev)
{
    g_autofree char *charAlias = NULL;

    if (dev->type != VIR_DOMAIN_CHR_TYPE_TCP)
        return 0;

    if (dev->data.tcp.haveTLS == VIR_TRISTATE_BOOL_YES &&
        cfg->chardevTLSx509secretUUID) {
        qemuDomainChrSourcePrivatePtr chrSourcePriv =
            QEMU_DOMAIN_CHR_SOURCE_PRIVATE(dev);

        if (!(charAlias = qemuAliasChardevFromDevAlias(chrAlias)))
            return -1;

        chrSourcePriv->secinfo =
            qemuDomainSecretInfoTLSNew(priv, charAlias,
                                       cfg->chardevTLSx509secretUUID);

        if (!chrSourcePriv->secinfo)
            return -1;
    }

    return 0;
}


static void
qemuDomainSecretGraphicsDestroy(virDomainGraphicsDefPtr graphics)
{
    qemuDomainGraphicsPrivatePtr gfxPriv = QEMU_DOMAIN_GRAPHICS_PRIVATE(graphics);

    if (!gfxPriv)
        return;

    VIR_FREE(gfxPriv->tlsAlias);
    g_clear_pointer(&gfxPriv->secinfo, qemuDomainSecretInfoFree);
}


static int
qemuDomainSecretGraphicsPrepare(virQEMUDriverConfigPtr cfg,
                                qemuDomainObjPrivatePtr priv,
                                virDomainGraphicsDefPtr graphics)
{
    virQEMUCapsPtr qemuCaps = priv->qemuCaps;
    qemuDomainGraphicsPrivatePtr gfxPriv = QEMU_DOMAIN_GRAPHICS_PRIVATE(graphics);

    if (graphics->type != VIR_DOMAIN_GRAPHICS_TYPE_VNC)
        return 0;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_TLS_CREDS_X509))
        return 0;

    if (!cfg->vncTLS)
        return 0;

    gfxPriv->tlsAlias = g_strdup("vnc-tls-creds0");

    if (cfg->vncTLSx509secretUUID) {
        gfxPriv->secinfo = qemuDomainSecretInfoTLSNew(priv, gfxPriv->tlsAlias,
                                                      cfg->vncTLSx509secretUUID);
        if (!gfxPriv->secinfo)
            return -1;
    }

    return 0;
}


/* qemuDomainSecretDestroy:
 * @vm: Domain object
 *
 * Removes all unnecessary data which was needed to generate 'secret' objects.
 */
void
qemuDomainSecretDestroy(virDomainObjPtr vm)
{
    size_t i;

    for (i = 0; i < vm->def->ndisks; i++)
        qemuDomainSecretDiskDestroy(vm->def->disks[i]);

    for (i = 0; i < vm->def->nhostdevs; i++)
        qemuDomainSecretHostdevDestroy(vm->def->hostdevs[i]);

    for (i = 0; i < vm->def->nserials; i++)
        qemuDomainSecretChardevDestroy(vm->def->serials[i]->source);

    for (i = 0; i < vm->def->nparallels; i++)
        qemuDomainSecretChardevDestroy(vm->def->parallels[i]->source);

    for (i = 0; i < vm->def->nchannels; i++)
        qemuDomainSecretChardevDestroy(vm->def->channels[i]->source);

    for (i = 0; i < vm->def->nconsoles; i++)
        qemuDomainSecretChardevDestroy(vm->def->consoles[i]->source);

    for (i = 0; i < vm->def->nsmartcards; i++) {
        if (vm->def->smartcards[i]->type ==
            VIR_DOMAIN_SMARTCARD_TYPE_PASSTHROUGH)
            qemuDomainSecretChardevDestroy(vm->def->smartcards[i]->data.passthru);
    }

    for (i = 0; i < vm->def->nrngs; i++) {
        if (vm->def->rngs[i]->backend == VIR_DOMAIN_RNG_BACKEND_EGD)
            qemuDomainSecretChardevDestroy(vm->def->rngs[i]->source.chardev);
    }

    for (i = 0; i < vm->def->nredirdevs; i++)
        qemuDomainSecretChardevDestroy(vm->def->redirdevs[i]->source);

    for (i = 0; i < vm->def->ngraphics; i++)
        qemuDomainSecretGraphicsDestroy(vm->def->graphics[i]);
}


/* qemuDomainSecretPrepare:
 * @driver: Pointer to driver object
 * @vm: Domain object
 *
 * For any objects that may require an auth/secret setup, create a
 * qemuDomainSecretInfo and save it in the appropriate place within
 * the private structures. This will be used by command line build
 * code in order to pass the secret along to qemu in order to provide
 * the necessary authentication data.
 *
 * Returns 0 on success, -1 on failure with error message set
 */
int
qemuDomainSecretPrepare(virQEMUDriverPtr driver,
                        virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    size_t i;

    /* disk and hostdev secrets are prepared when preparing internal data */

    for (i = 0; i < vm->def->nserials; i++) {
        if (qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->serials[i]->info.alias,
                                           vm->def->serials[i]->source) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->nparallels; i++) {
        if (qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->parallels[i]->info.alias,
                                           vm->def->parallels[i]->source) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->nchannels; i++) {
        if (qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->channels[i]->info.alias,
                                           vm->def->channels[i]->source) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->nconsoles; i++) {
        if (qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->consoles[i]->info.alias,
                                           vm->def->consoles[i]->source) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->nsmartcards; i++)
        if (vm->def->smartcards[i]->type ==
            VIR_DOMAIN_SMARTCARD_TYPE_PASSTHROUGH &&
            qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->smartcards[i]->info.alias,
                                           vm->def->smartcards[i]->data.passthru) < 0)
            return -1;

    for (i = 0; i < vm->def->nrngs; i++) {
        if (vm->def->rngs[i]->backend == VIR_DOMAIN_RNG_BACKEND_EGD &&
            qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->rngs[i]->info.alias,
                                           vm->def->rngs[i]->source.chardev) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->nredirdevs; i++) {
        if (qemuDomainSecretChardevPrepare(cfg, priv,
                                           vm->def->redirdevs[i]->info.alias,
                                           vm->def->redirdevs[i]->source) < 0)
            return -1;
    }

    for (i = 0; i < vm->def->ngraphics; i++) {
        if (qemuDomainSecretGraphicsPrepare(cfg, priv, vm->def->graphics[i]) < 0)
            return -1;
    }

    return 0;
}


/* This is the old way of setting up per-domain directories */
static void
qemuDomainSetPrivatePathsOld(virQEMUDriverPtr driver,
                             virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);

    if (!priv->libDir)
        priv->libDir = g_strdup_printf("%s/domain-%s", cfg->libDir, vm->def->name);

    if (!priv->channelTargetDir)
        priv->channelTargetDir = g_strdup_printf("%s/domain-%s",
                                                 cfg->channelTargetDir, vm->def->name);
}


int
qemuDomainSetPrivatePaths(virQEMUDriverPtr driver,
                          virDomainObjPtr vm)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autofree char *domname = virDomainDefGetShortName(vm->def);

    if (!domname)
        return -1;

    if (!priv->libDir)
        priv->libDir = g_strdup_printf("%s/domain-%s", cfg->libDir, domname);

    if (!priv->channelTargetDir)
        priv->channelTargetDir = g_strdup_printf("%s/domain-%s",
                                                 cfg->channelTargetDir, domname);

    return 0;
}


int
qemuDomainObjStartWorker(virDomainObjPtr dom)
{
    qemuDomainObjPrivatePtr priv = dom->privateData;

    if (!priv->eventThread) {
        g_autofree char *threadName = g_strdup_printf("vm-%s", dom->def->name);
        if (!(priv->eventThread = virEventThreadNew(threadName)))
            return -1;
    }

    return 0;
}


void
qemuDomainObjStopWorker(virDomainObjPtr dom)
{
    qemuDomainObjPrivatePtr priv = dom->privateData;
    virEventThread *eventThread;

    if (!priv->eventThread)
        return;

    /*
     * We are dropping the only reference here so that the event loop thread
     * is going to be exited synchronously. In order to avoid deadlocks we
     * need to unlock the VM so that any handler being called can finish
     * execution and thus even loop thread be finished too.
     */
    eventThread = g_steal_pointer(&priv->eventThread);
    virObjectUnlock(dom);
    g_object_unref(eventThread);
    virObjectLock(dom);
}


static void *
qemuDomainObjPrivateAlloc(void *opaque)
{
    qemuDomainObjPrivatePtr priv;

    priv = g_new0(qemuDomainObjPrivate, 1);

    if (qemuDomainObjInitJob(&priv->job, &qemuPrivateJobCallbacks) < 0) {
        virReportSystemError(errno, "%s",
                             _("Unable to init qemu driver mutexes"));
        goto error;
    }

    if (!(priv->devs = virChrdevAlloc()))
        goto error;

    if (!(priv->blockjobs = virHashNew(virObjectFreeHashData)))
        goto error;

    /* agent commands block by default, user can choose different behavior */
    priv->agentTimeout = VIR_DOMAIN_AGENT_RESPONSE_TIMEOUT_BLOCK;
    priv->migMaxBandwidth = QEMU_DOMAIN_MIG_BANDWIDTH_MAX;
    priv->driver = opaque;

    return priv;

 error:
    VIR_FREE(priv);
    return NULL;
}

/**
 * qemuDomainObjPrivateDataClear:
 * @priv: domain private data
 *
 * Clears private data entries, which are not necessary or stale if the VM is
 * not running.
 */
void
qemuDomainObjPrivateDataClear(qemuDomainObjPrivatePtr priv)
{
    g_strfreev(priv->qemuDevices);
    priv->qemuDevices = NULL;

    virCgroupFree(priv->cgroup);
    priv->cgroup = NULL;

    virPerfFree(priv->perf);
    priv->perf = NULL;

    VIR_FREE(priv->machineName);

    virObjectUnref(priv->qemuCaps);
    priv->qemuCaps = NULL;

    VIR_FREE(priv->pidfile);

    VIR_FREE(priv->libDir);
    VIR_FREE(priv->channelTargetDir);

    priv->memPrealloc = false;

    /* remove automatic pinning data */
    virBitmapFree(priv->autoNodeset);
    priv->autoNodeset = NULL;
    virBitmapFree(priv->autoCpuset);
    priv->autoCpuset = NULL;

    /* remove address data */
    virDomainPCIAddressSetFree(priv->pciaddrs);
    priv->pciaddrs = NULL;
    virDomainUSBAddressSetFree(priv->usbaddrs);
    priv->usbaddrs = NULL;

    virCPUDefFree(priv->origCPU);
    priv->origCPU = NULL;

    /* clear previously used namespaces */
    virBitmapFree(priv->namespaces);
    priv->namespaces = NULL;

    priv->rememberOwner = false;

    priv->reconnectBlockjobs = VIR_TRISTATE_BOOL_ABSENT;
    priv->allowReboot = VIR_TRISTATE_BOOL_ABSENT;

    virBitmapFree(priv->migrationCaps);
    priv->migrationCaps = NULL;

    virHashRemoveAll(priv->blockjobs);

    virObjectUnref(priv->pflash0);
    priv->pflash0 = NULL;
    virObjectUnref(priv->pflash1);
    priv->pflash1 = NULL;

    virDomainBackupDefFree(priv->backup);
    priv->backup = NULL;

    /* reset node name allocator */
    qemuDomainStorageIdReset(priv);

    priv->dbusDaemonRunning = false;

    g_strfreev(priv->dbusVMStateIds);
    priv->dbusVMStateIds = NULL;

    priv->dbusVMState = false;

    priv->inhibitDiskTransientDelete = false;
}


static void
qemuDomainObjPrivateFree(void *data)
{
    qemuDomainObjPrivatePtr priv = data;

    qemuDomainObjPrivateDataClear(priv);

    virObjectUnref(priv->monConfig);
    qemuDomainObjClearJob(&priv->job);
    VIR_FREE(priv->lockState);
    VIR_FREE(priv->origname);

    virChrdevFree(priv->devs);

    /* This should never be non-NULL if we get here, but just in case... */
    if (priv->mon) {
        VIR_ERROR(_("Unexpected QEMU monitor still active during domain deletion"));
        qemuMonitorClose(priv->mon);
    }
    if (priv->agent) {
        VIR_ERROR(_("Unexpected QEMU agent still active during domain deletion"));
        qemuAgentClose(priv->agent);
    }
    VIR_FREE(priv->cleanupCallbacks);

    g_clear_pointer(&priv->migSecinfo, qemuDomainSecretInfoFree);
    qemuDomainMasterKeyFree(priv);

    virHashFree(priv->blockjobs);

    /* This should never be non-NULL if we get here, but just in case... */
    if (priv->eventThread) {
        VIR_ERROR(_("Unexpected event thread still active during domain deletion"));
        g_object_unref(priv->eventThread);
    }

    VIR_FREE(priv);
}


static int
qemuStorageSourcePrivateDataAssignSecinfo(qemuDomainSecretInfoPtr *secinfo,
                                          char **alias)
{
    if (!*alias)
        return 0;

    if (!*secinfo) {
        *secinfo = g_new0(qemuDomainSecretInfo, 1);
        (*secinfo)->type = VIR_DOMAIN_SECRET_INFO_TYPE_AES;
    }

    if ((*secinfo)->type == VIR_DOMAIN_SECRET_INFO_TYPE_AES)
        (*secinfo)->s.aes.alias = g_steal_pointer(&*alias);

    return 0;
}


static int
qemuStorageSourcePrivateDataParse(xmlXPathContextPtr ctxt,
                                  virStorageSourcePtr src)
{
    qemuDomainStorageSourcePrivatePtr priv;
    g_autofree char *authalias = NULL;
    g_autofree char *encalias = NULL;
    g_autofree char *httpcookiealias = NULL;
    g_autofree char *tlskeyalias = NULL;

    src->nodestorage = virXPathString("string(./nodenames/nodename[@type='storage']/@name)", ctxt);
    src->nodeformat = virXPathString("string(./nodenames/nodename[@type='format']/@name)", ctxt);
    src->tlsAlias = virXPathString("string(./objects/TLSx509/@alias)", ctxt);

    if (src->sliceStorage)
        src->sliceStorage->nodename = virXPathString("string(./nodenames/nodename[@type='slice-storage']/@name)", ctxt);

    if (src->pr)
        src->pr->mgralias = virXPathString("string(./reservations/@mgralias)", ctxt);

    authalias = virXPathString("string(./objects/secret[@type='auth']/@alias)", ctxt);
    encalias = virXPathString("string(./objects/secret[@type='encryption']/@alias)", ctxt);
    httpcookiealias = virXPathString("string(./objects/secret[@type='httpcookie']/@alias)", ctxt);
    tlskeyalias = virXPathString("string(./objects/secret[@type='tlskey']/@alias)", ctxt);

    if (authalias || encalias || httpcookiealias || tlskeyalias) {
        if (!src->privateData &&
            !(src->privateData = qemuDomainStorageSourcePrivateNew()))
            return -1;

        priv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(src);

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->secinfo, &authalias) < 0)
            return -1;

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->encinfo, &encalias) < 0)
            return -1;

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->httpcookie, &httpcookiealias) < 0)
            return -1;

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->tlsKeySecret, &tlskeyalias) < 0)
            return -1;
    }

    if (virStorageSourcePrivateDataParseRelPath(ctxt, src) < 0)
        return -1;

    return 0;
}


static void
qemuStorageSourcePrivateDataFormatSecinfo(virBufferPtr buf,
                                          qemuDomainSecretInfoPtr secinfo,
                                          const char *type)
{
    if (!secinfo ||
        secinfo->type != VIR_DOMAIN_SECRET_INFO_TYPE_AES ||
        !secinfo->s.aes.alias)
        return;

    virBufferAsprintf(buf, "<secret type='%s' alias='%s'/>\n",
                      type, secinfo->s.aes.alias);
}


static int
qemuStorageSourcePrivateDataFormat(virStorageSourcePtr src,
                                   virBufferPtr buf)
{
    g_auto(virBuffer) tmp = VIR_BUFFER_INIT_CHILD(buf);
    qemuDomainStorageSourcePrivatePtr srcPriv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(src);
    g_auto(virBuffer) nodenamesChildBuf = VIR_BUFFER_INIT_CHILD(buf);

    virBufferEscapeString(&nodenamesChildBuf, "<nodename type='storage' name='%s'/>\n", src->nodestorage);
    virBufferEscapeString(&nodenamesChildBuf, "<nodename type='format' name='%s'/>\n", src->nodeformat);

    if (src->sliceStorage)
        virBufferEscapeString(&nodenamesChildBuf, "<nodename type='slice-storage' name='%s'/>\n",
                              src->sliceStorage->nodename);

    virXMLFormatElement(buf, "nodenames", NULL, &nodenamesChildBuf);

    if (src->pr)
        virBufferAsprintf(buf, "<reservations mgralias='%s'/>\n", src->pr->mgralias);

    if (virStorageSourcePrivateDataFormatRelPath(src, buf) < 0)
        return -1;

    if (srcPriv) {
        qemuStorageSourcePrivateDataFormatSecinfo(&tmp, srcPriv->secinfo, "auth");
        qemuStorageSourcePrivateDataFormatSecinfo(&tmp, srcPriv->encinfo, "encryption");
        qemuStorageSourcePrivateDataFormatSecinfo(&tmp, srcPriv->httpcookie, "httpcookie");
        qemuStorageSourcePrivateDataFormatSecinfo(&tmp, srcPriv->tlsKeySecret, "tlskey");
    }

    if (src->tlsAlias)
        virBufferAsprintf(&tmp, "<TLSx509 alias='%s'/>\n", src->tlsAlias);

    virXMLFormatElement(buf, "objects", NULL, &tmp);

    return 0;
}


static int
qemuDomainDiskPrivateParse(xmlXPathContextPtr ctxt,
                           virDomainDiskDefPtr disk)
{
    qemuDomainDiskPrivatePtr priv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    priv->qomName = virXPathString("string(./qom/@name)", ctxt);
    priv->nodeCopyOnRead = virXPathString("string(./nodenames/nodename[@type='copyOnRead']/@name)", ctxt);

    return 0;
}


static int
qemuDomainDiskPrivateFormat(virDomainDiskDefPtr disk,
                            virBufferPtr buf)
{
    qemuDomainDiskPrivatePtr priv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    virBufferEscapeString(buf, "<qom name='%s'/>\n", priv->qomName);

    if (priv->nodeCopyOnRead) {
        virBufferAddLit(buf, "<nodenames>\n");
        virBufferAdjustIndent(buf, 2);
        virBufferEscapeString(buf, "<nodename type='copyOnRead' name='%s'/>\n",
                              priv->nodeCopyOnRead);
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</nodenames>\n");
    }

    return 0;
}


static void
qemuDomainObjPrivateXMLFormatVcpus(virBufferPtr buf,
                                   virDomainDefPtr def)
{
    size_t i;
    size_t maxvcpus = virDomainDefGetVcpusMax(def);
    virDomainVcpuDefPtr vcpu;
    pid_t tid;

    virBufferAddLit(buf, "<vcpus>\n");
    virBufferAdjustIndent(buf, 2);

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(def, i);
        tid = QEMU_DOMAIN_VCPU_PRIVATE(vcpu)->tid;

        if (!vcpu->online || tid == 0)
            continue;

        virBufferAsprintf(buf, "<vcpu id='%zu' pid='%d'/>\n", i, tid);
    }

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</vcpus>\n");
}


static int
qemuDomainObjPrivateXMLFormatAutomaticPlacement(virBufferPtr buf,
                                                qemuDomainObjPrivatePtr priv)
{
    g_autofree char *nodeset = NULL;
    g_autofree char *cpuset = NULL;

    if (!priv->autoNodeset && !priv->autoCpuset)
        return 0;

    if (priv->autoNodeset &&
        !((nodeset = virBitmapFormat(priv->autoNodeset))))
        return -1;

    if (priv->autoCpuset &&
        !((cpuset = virBitmapFormat(priv->autoCpuset))))
        return -1;

    virBufferAddLit(buf, "<numad");
    virBufferEscapeString(buf, " nodeset='%s'", nodeset);
    virBufferEscapeString(buf, " cpuset='%s'", cpuset);
    virBufferAddLit(buf, "/>\n");

    return 0;
}


typedef struct qemuDomainPrivateBlockJobFormatData {
    virDomainXMLOptionPtr xmlopt;
    virBufferPtr buf;
} qemuDomainPrivateBlockJobFormatData;


static int
qemuDomainObjPrivateXMLFormatBlockjobFormatSource(virBufferPtr buf,
                                                  const char *element,
                                                  virStorageSourcePtr src,
                                                  virDomainXMLOptionPtr xmlopt,
                                                  bool chain)
{
    g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
    g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(buf);
    unsigned int xmlflags = VIR_DOMAIN_DEF_FORMAT_STATUS;

    virBufferAsprintf(&attrBuf, " type='%s' format='%s'",
                      virStorageTypeToString(src->type),
                      virStorageFileFormatTypeToString(src->format));

    if (virDomainDiskSourceFormat(&childBuf, src, "source", 0, true, xmlflags,
                                  false, false, xmlopt) < 0)
        return -1;

    if (chain &&
        virDomainDiskBackingStoreFormat(&childBuf, src, xmlopt, xmlflags) < 0)
        return -1;

    virXMLFormatElement(buf, element, &attrBuf, &childBuf);

    return 0;
}


static void
qemuDomainPrivateBlockJobFormatCommit(qemuBlockJobDataPtr job,
                                      virBufferPtr buf)
{
    g_auto(virBuffer) disabledBitmapsBuf = VIR_BUFFER_INIT_CHILD(buf);

    if (job->data.commit.base)
        virBufferAsprintf(buf, "<base node='%s'/>\n", job->data.commit.base->nodeformat);

    if (job->data.commit.top)
        virBufferAsprintf(buf, "<top node='%s'/>\n", job->data.commit.top->nodeformat);

    if (job->data.commit.topparent)
        virBufferAsprintf(buf, "<topparent node='%s'/>\n", job->data.commit.topparent->nodeformat);

    if (job->data.commit.deleteCommittedImages)
        virBufferAddLit(buf, "<deleteCommittedImages/>\n");

    virXMLFormatElement(buf, "disabledBaseBitmaps", NULL, &disabledBitmapsBuf);
}


static int
qemuDomainObjPrivateXMLFormatBlockjobIterator(void *payload,
                                              const char *name G_GNUC_UNUSED,
                                              void *opaque)
{
    struct qemuDomainPrivateBlockJobFormatData *data = opaque;
    g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
    g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(data->buf);
    g_auto(virBuffer) chainsBuf = VIR_BUFFER_INIT_CHILD(&childBuf);
    qemuBlockJobDataPtr job = payload;
    const char *state = qemuBlockjobStateTypeToString(job->state);
    const char *newstate = NULL;

    if (job->newstate != -1)
        newstate = qemuBlockjobStateTypeToString(job->newstate);

    virBufferEscapeString(&attrBuf, " name='%s'", job->name);
    virBufferEscapeString(&attrBuf, " type='%s'", qemuBlockjobTypeToString(job->type));
    virBufferEscapeString(&attrBuf, " state='%s'", state);
    virBufferEscapeString(&attrBuf, " newstate='%s'", newstate);
    if (job->brokentype != QEMU_BLOCKJOB_TYPE_NONE)
        virBufferEscapeString(&attrBuf, " brokentype='%s'", qemuBlockjobTypeToString(job->brokentype));
    if (!job->jobflagsmissing)
        virBufferAsprintf(&attrBuf, " jobflags='0x%x'", job->jobflags);
    virBufferEscapeString(&childBuf, "<errmsg>%s</errmsg>", job->errmsg);

    if (job->disk) {
        virBufferEscapeString(&childBuf, "<disk dst='%s'", job->disk->dst);
        if (job->mirrorChain)
            virBufferAddLit(&childBuf, " mirror='yes'");
        virBufferAddLit(&childBuf, "/>\n");
    } else {
        if (job->chain &&
            qemuDomainObjPrivateXMLFormatBlockjobFormatSource(&chainsBuf,
                                                              "disk",
                                                              job->chain,
                                                              data->xmlopt,
                                                              true) < 0)
            return -1;

        if (job->mirrorChain &&
            qemuDomainObjPrivateXMLFormatBlockjobFormatSource(&chainsBuf,
                                                              "mirror",
                                                              job->mirrorChain,
                                                              data->xmlopt,
                                                              true) < 0)
            return -1;

        virXMLFormatElement(&childBuf, "chains", NULL, &chainsBuf);
    }

    switch ((qemuBlockJobType) job->type) {
        case QEMU_BLOCKJOB_TYPE_PULL:
            if (job->data.pull.base)
                virBufferAsprintf(&childBuf, "<base node='%s'/>\n", job->data.pull.base->nodeformat);
            break;

        case QEMU_BLOCKJOB_TYPE_COMMIT:
        case QEMU_BLOCKJOB_TYPE_ACTIVE_COMMIT:
            qemuDomainPrivateBlockJobFormatCommit(job, &childBuf);
            break;

        case QEMU_BLOCKJOB_TYPE_CREATE:
            if (job->data.create.storage)
                virBufferAddLit(&childBuf, "<create mode='storage'/>\n");

            if (job->data.create.src &&
                qemuDomainObjPrivateXMLFormatBlockjobFormatSource(&childBuf,
                                                                  "src",
                                                                  job->data.create.src,
                                                                  data->xmlopt,
                                                                  false) < 0)
                return -1;
            break;

        case QEMU_BLOCKJOB_TYPE_COPY:
            if (job->data.copy.shallownew)
                virBufferAddLit(&attrBuf, " shallownew='yes'");
            break;

        case QEMU_BLOCKJOB_TYPE_BACKUP:
            virBufferEscapeString(&childBuf, "<bitmap name='%s'/>\n", job->data.backup.bitmap);
            if (job->data.backup.store) {
                if (qemuDomainObjPrivateXMLFormatBlockjobFormatSource(&childBuf,
                                                                      "store",
                                                                      job->data.backup.store,
                                                                      data->xmlopt,
                                                                      false) < 0)
                    return -1;
            }
            break;

        case QEMU_BLOCKJOB_TYPE_BROKEN:
        case QEMU_BLOCKJOB_TYPE_NONE:
        case QEMU_BLOCKJOB_TYPE_INTERNAL:
        case QEMU_BLOCKJOB_TYPE_LAST:
            break;
    }

    virXMLFormatElement(data->buf, "blockjob", &attrBuf, &childBuf);
    return 0;
}


static int
qemuDomainObjPrivateXMLFormatBlockjobs(virBufferPtr buf,
                                       virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
    g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(buf);
    bool bj = qemuDomainHasBlockjob(vm, false);
    struct qemuDomainPrivateBlockJobFormatData iterdata = { priv->driver->xmlopt,
                                                            &childBuf };

    virBufferAsprintf(&attrBuf, " active='%s'",
                      virTristateBoolTypeToString(virTristateBoolFromBool(bj)));

    if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV) &&
        virHashForEachSorted(priv->blockjobs,
                             qemuDomainObjPrivateXMLFormatBlockjobIterator,
                             &iterdata) < 0)
        return -1;

    virXMLFormatElement(buf, "blockjobs", &attrBuf, &childBuf);
    return 0;
}


static int
qemuDomainObjPrivateXMLFormatBackups(virBufferPtr buf,
                                     virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_auto(virBuffer) attrBuf = VIR_BUFFER_INITIALIZER;
    g_auto(virBuffer) childBuf = VIR_BUFFER_INIT_CHILD(buf);

    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_INCREMENTAL_BACKUP))
        return 0;

    if (priv->backup &&
        virDomainBackupDefFormat(&childBuf, priv->backup, true) < 0)
        return -1;

    virXMLFormatElement(buf, "backups", &attrBuf, &childBuf);
    return 0;
}


void
qemuDomainObjPrivateXMLFormatAllowReboot(virBufferPtr buf,
                                         virTristateBool allowReboot)
{
    virBufferAsprintf(buf, "<allowReboot value='%s'/>\n",
                      virTristateBoolTypeToString(allowReboot));

}


static void
qemuDomainObjPrivateXMLFormatPR(virBufferPtr buf,
                                qemuDomainObjPrivatePtr priv)
{
    if (priv->prDaemonRunning)
        virBufferAddLit(buf, "<prDaemon/>\n");
}


static bool
qemuDomainHasSlirp(virDomainObjPtr vm)
{
    size_t i;

    for (i = 0; i < vm->def->nnets; i++) {
        virDomainNetDefPtr net = vm->def->nets[i];

        if (QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp)
            return true;
    }

    return false;
}


static bool
qemuDomainGetSlirpHelperOk(virDomainObjPtr vm)
{
    size_t i;

    for (i = 0; i < vm->def->nnets; i++) {
        virDomainNetDefPtr net = vm->def->nets[i];

        /* if there is a builtin slirp, prevent slirp-helper */
        if (net->type == VIR_DOMAIN_NET_TYPE_USER &&
            !QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp)
            return false;
    }

    return true;
}


static int
qemuDomainObjPrivateXMLFormatSlirp(virBufferPtr buf,
                                   virDomainObjPtr vm)
{
    size_t i;

    if (!qemuDomainHasSlirp(vm))
        return 0;

    virBufferAddLit(buf, "<slirp>\n");
    virBufferAdjustIndent(buf, 2);

    for (i = 0; i < vm->def->nnets; i++) {
        virDomainNetDefPtr net = vm->def->nets[i];
        qemuSlirpPtr slirp = QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp;
        size_t j;

        if (!slirp)
            continue;

        virBufferAsprintf(buf, "<helper alias='%s' pid='%d'>\n",
                          net->info.alias, slirp->pid);

        virBufferAdjustIndent(buf, 2);
        for (j = 0; j < QEMU_SLIRP_FEATURE_LAST; j++) {
            if (qemuSlirpHasFeature(slirp, j)) {
                virBufferAsprintf(buf, "<feature name='%s'/>\n",
                                  qemuSlirpFeatureTypeToString(j));
            }
        }
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</helper>\n");
    }

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</slirp>\n");


    return 0;
}

static int
qemuDomainObjPrivateXMLFormat(virBufferPtr buf,
                              virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    const char *monitorpath;

    /* priv->monitor_chr is set only for qemu */
    if (priv->monConfig) {
        switch (priv->monConfig->type) {
        case VIR_DOMAIN_CHR_TYPE_UNIX:
            monitorpath = priv->monConfig->data.nix.path;
            break;
        default:
        case VIR_DOMAIN_CHR_TYPE_PTY:
            monitorpath = priv->monConfig->data.file.path;
            break;
        }

        virBufferEscapeString(buf, "<monitor path='%s'", monitorpath);
        virBufferAsprintf(buf, " type='%s'/>\n",
                          virDomainChrTypeToString(priv->monConfig->type));
    }

    if (priv->dbusDaemonRunning)
        virBufferAddLit(buf, "<dbusDaemon/>\n");

    if (priv->dbusVMState)
        virBufferAddLit(buf, "<dbusVMState/>\n");

    if (priv->namespaces) {
        ssize_t ns = -1;

        virBufferAddLit(buf, "<namespaces>\n");
        virBufferAdjustIndent(buf, 2);
        while ((ns = virBitmapNextSetBit(priv->namespaces, ns)) >= 0)
            virBufferAsprintf(buf, "<%s/>\n", qemuDomainNamespaceTypeToString(ns));
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</namespaces>\n");
    }

    qemuDomainObjPrivateXMLFormatVcpus(buf, vm->def);

    if (priv->qemuCaps) {
        size_t i;
        virBufferAddLit(buf, "<qemuCaps>\n");
        virBufferAdjustIndent(buf, 2);
        for (i = 0; i < QEMU_CAPS_LAST; i++) {
            if (virQEMUCapsGet(priv->qemuCaps, i)) {
                virBufferAsprintf(buf, "<flag name='%s'/>\n",
                                  virQEMUCapsTypeToString(i));
            }
        }
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</qemuCaps>\n");
    }

    if (priv->lockState)
        virBufferAsprintf(buf, "<lockstate>%s</lockstate>\n", priv->lockState);

    if (qemuDomainObjPrivateXMLFormatJob(buf, vm) < 0)
        return -1;

    if (priv->fakeReboot)
        virBufferAddLit(buf, "<fakereboot/>\n");

    if (priv->qemuDevices && *priv->qemuDevices) {
        char **tmp = priv->qemuDevices;
        virBufferAddLit(buf, "<devices>\n");
        virBufferAdjustIndent(buf, 2);
        while (*tmp) {
            virBufferAsprintf(buf, "<device alias='%s'/>\n", *tmp);
            tmp++;
        }
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</devices>\n");
    }

    if (qemuDomainObjPrivateXMLFormatAutomaticPlacement(buf, priv) < 0)
        return -1;

    /* Various per-domain paths */
    virBufferEscapeString(buf, "<libDir path='%s'/>\n", priv->libDir);
    virBufferEscapeString(buf, "<channelTargetDir path='%s'/>\n",
                          priv->channelTargetDir);

    virCPUDefFormatBufFull(buf, priv->origCPU, NULL);

    if (priv->chardevStdioLogd)
        virBufferAddLit(buf, "<chardevStdioLogd/>\n");

    if (priv->rememberOwner)
        virBufferAddLit(buf, "<rememberOwner/>\n");

    qemuDomainObjPrivateXMLFormatAllowReboot(buf, priv->allowReboot);

    qemuDomainObjPrivateXMLFormatPR(buf, priv);

    if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV))
        virBufferAsprintf(buf, "<nodename index='%llu'/>\n", priv->nodenameindex);

    if (priv->memPrealloc)
        virBufferAddLit(buf, "<memPrealloc/>\n");

    if (qemuDomainObjPrivateXMLFormatBlockjobs(buf, vm) < 0)
        return -1;

    if (qemuDomainObjPrivateXMLFormatSlirp(buf, vm) < 0)
        return -1;

    virBufferAsprintf(buf, "<agentTimeout>%i</agentTimeout>\n", priv->agentTimeout);

    if (qemuDomainObjPrivateXMLFormatBackups(buf, vm) < 0)
        return -1;

    return 0;
}


static int
qemuDomainObjPrivateXMLParseVcpu(xmlNodePtr node,
                                 unsigned int idx,
                                 virDomainDefPtr def)
{
    virDomainVcpuDefPtr vcpu;
    g_autofree char *idstr = NULL;
    g_autofree char *pidstr = NULL;
    unsigned int tmp;

    idstr = virXMLPropString(node, "id");

    if (idstr &&
        (virStrToLong_uip(idstr, NULL, 10, &idx) < 0)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot parse vcpu index '%s'"), idstr);
        return -1;
    }
    if (!(vcpu = virDomainDefGetVcpu(def, idx))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("invalid vcpu index '%u'"), idx);
        return -1;
    }

    if (!(pidstr = virXMLPropString(node, "pid")))
        return -1;

    if (virStrToLong_uip(pidstr, NULL, 10, &tmp) < 0)
        return -1;

    QEMU_DOMAIN_VCPU_PRIVATE(vcpu)->tid = tmp;

    return 0;
}


static int
qemuDomainObjPrivateXMLParseAutomaticPlacement(xmlXPathContextPtr ctxt,
                                               qemuDomainObjPrivatePtr priv,
                                               virQEMUDriverPtr driver)
{
    g_autoptr(virCapsHostNUMA) caps = NULL;
    g_autofree char *nodeset = NULL;
    g_autofree char *cpuset = NULL;
    int nodesetSize = 0;
    size_t i;

    nodeset = virXPathString("string(./numad/@nodeset)", ctxt);
    cpuset = virXPathString("string(./numad/@cpuset)", ctxt);

    if (!nodeset && !cpuset)
        return 0;

    if (!(caps = virQEMUDriverGetHostNUMACaps(driver)))
        return -1;

    /* Figure out how big the nodeset bitmap needs to be.
     * This is necessary because NUMA node IDs are not guaranteed to
     * start from 0 or be densely allocated */
    for (i = 0; i < caps->cells->len; i++) {
        virCapsHostNUMACellPtr cell =
            g_ptr_array_index(caps->cells, i);
        nodesetSize = MAX(nodesetSize, cell->num + 1);
    }

    if (nodeset &&
        virBitmapParse(nodeset, &priv->autoNodeset, nodesetSize) < 0)
        return -1;

    if (cpuset) {
        if (virBitmapParse(cpuset, &priv->autoCpuset, VIR_DOMAIN_CPUMASK_LEN) < 0)
            return -1;
    } else {
        /* autoNodeset is present in this case, since otherwise we wouldn't
         * reach this code */
        if (!(priv->autoCpuset = virCapabilitiesHostNUMAGetCpus(caps,
                                                                priv->autoNodeset)))
            return -1;
    }

    return 0;
}


static virStorageSourcePtr
qemuDomainObjPrivateXMLParseBlockjobChain(xmlNodePtr node,
                                          xmlXPathContextPtr ctxt,
                                          virDomainXMLOptionPtr xmlopt)

{
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    g_autofree char *format = NULL;
    g_autofree char *type = NULL;
    g_autofree char *index = NULL;
    g_autoptr(virStorageSource) src = NULL;
    xmlNodePtr sourceNode;
    unsigned int xmlflags = VIR_DOMAIN_DEF_PARSE_STATUS;

    ctxt->node = node;

    if (!(type = virXMLPropString(ctxt->node, "type")) ||
        !(format = virXMLPropString(ctxt->node, "format")) ||
        !(index = virXPathString("string(./source/@index)", ctxt)) ||
        !(sourceNode = virXPathNode("./source", ctxt))) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing job chain data"));
        return NULL;
    }

    if (!(src = virDomainStorageSourceParseBase(type, format, index)))
        return NULL;

    if (virDomainStorageSourceParse(sourceNode, ctxt, src, xmlflags, xmlopt) < 0)
        return NULL;

    if (virDomainDiskBackingStoreParse(ctxt, src, xmlflags, xmlopt) < 0)
        return NULL;

    return g_steal_pointer(&src);
}


static void
qemuDomainObjPrivateXMLParseBlockjobNodename(qemuBlockJobDataPtr job,
                                             const char *xpath,
                                             virStorageSourcePtr *src,
                                             xmlXPathContextPtr ctxt)
{
    g_autofree char *nodename = NULL;

    *src = NULL;

    if (!(nodename = virXPathString(xpath, ctxt)))
        return;

    if (job->disk &&
        (*src = virStorageSourceFindByNodeName(job->disk->src, nodename)))
        return;

    if (job->chain &&
        (*src = virStorageSourceFindByNodeName(job->chain, nodename)))
        return;

    if (job->mirrorChain &&
        (*src = virStorageSourceFindByNodeName(job->mirrorChain, nodename)))
        return;

    /* the node was in the XML but was not found in the job definitions */
    VIR_DEBUG("marking block job '%s' as invalid: node name '%s' missing",
              job->name, nodename);
    job->invalidData = true;
}


static int
qemuDomainObjPrivateXMLParseBlockjobDataCommit(qemuBlockJobDataPtr job,
                                               xmlXPathContextPtr ctxt)
{
    if (job->type == QEMU_BLOCKJOB_TYPE_COMMIT) {
        qemuDomainObjPrivateXMLParseBlockjobNodename(job,
                                                     "string(./topparent/@node)",
                                                     &job->data.commit.topparent,
                                                     ctxt);

        if (!job->data.commit.topparent)
            return -1;
    }

    qemuDomainObjPrivateXMLParseBlockjobNodename(job,
                                                 "string(./top/@node)",
                                                 &job->data.commit.top,
                                                 ctxt);
    qemuDomainObjPrivateXMLParseBlockjobNodename(job,
                                                 "string(./base/@node)",
                                                 &job->data.commit.base,
                                                 ctxt);

    if (virXPathNode("./deleteCommittedImages", ctxt))
        job->data.commit.deleteCommittedImages = true;

    if (!job->data.commit.top ||
        !job->data.commit.base)
        return -1;

    return 0;
}


static void
qemuDomainObjPrivateXMLParseBlockjobDataSpecific(qemuBlockJobDataPtr job,
                                                 xmlXPathContextPtr ctxt,
                                                 virDomainXMLOptionPtr xmlopt)
{
    g_autofree char *createmode = NULL;
    g_autofree char *shallownew = NULL;
    xmlNodePtr tmp;

    switch ((qemuBlockJobType) job->type) {
        case QEMU_BLOCKJOB_TYPE_PULL:
            qemuDomainObjPrivateXMLParseBlockjobNodename(job,
                                                         "string(./base/@node)",
                                                         &job->data.pull.base,
                                                         ctxt);
            /* base is not present if pulling everything */
            break;

        case QEMU_BLOCKJOB_TYPE_COMMIT:
        case QEMU_BLOCKJOB_TYPE_ACTIVE_COMMIT:
            if (qemuDomainObjPrivateXMLParseBlockjobDataCommit(job, ctxt) < 0)
                goto broken;

            break;

        case QEMU_BLOCKJOB_TYPE_CREATE:
            if (!(tmp = virXPathNode("./src", ctxt)) ||
                !(job->data.create.src = qemuDomainObjPrivateXMLParseBlockjobChain(tmp, ctxt, xmlopt)))
                goto broken;

            if ((createmode = virXPathString("string(./create/@mode)", ctxt))) {
                if (STRNEQ(createmode, "storage"))
                    goto broken;

                job->data.create.storage = true;
            }
            break;

        case QEMU_BLOCKJOB_TYPE_COPY:
            if ((shallownew =  virXPathString("string(./@shallownew)", ctxt))) {
                if (STRNEQ(shallownew, "yes"))
                    goto broken;

                job->data.copy.shallownew = true;
            }
            break;

        case QEMU_BLOCKJOB_TYPE_BACKUP:
            job->data.backup.bitmap =  virXPathString("string(./bitmap/@name)", ctxt);

            if (!(tmp = virXPathNode("./store", ctxt)) ||
                !(job->data.backup.store = qemuDomainObjPrivateXMLParseBlockjobChain(tmp, ctxt, xmlopt)))
                goto broken;
            break;

        case QEMU_BLOCKJOB_TYPE_BROKEN:
        case QEMU_BLOCKJOB_TYPE_NONE:
        case QEMU_BLOCKJOB_TYPE_INTERNAL:
        case QEMU_BLOCKJOB_TYPE_LAST:
            break;
    }

    return;

 broken:
    VIR_DEBUG("marking block job '%s' as invalid: malformed job data", job->name);
    job->invalidData = true;
}


static int
qemuDomainObjPrivateXMLParseBlockjobData(virDomainObjPtr vm,
                                         xmlNodePtr node,
                                         xmlXPathContextPtr ctxt,
                                         virDomainXMLOptionPtr xmlopt)
{
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    virDomainDiskDefPtr disk = NULL;
    g_autoptr(qemuBlockJobData) job = NULL;
    g_autofree char *name = NULL;
    g_autofree char *typestr = NULL;
    g_autofree char *brokentypestr = NULL;
    int type;
    g_autofree char *statestr = NULL;
    int state = QEMU_BLOCKJOB_STATE_FAILED;
    g_autofree char *diskdst = NULL;
    g_autofree char *newstatestr = NULL;
    g_autofree char *mirror = NULL;
    int newstate = -1;
    bool invalidData = false;
    xmlNodePtr tmp;
    unsigned long jobflags = 0;

    ctxt->node = node;

    if (!(name = virXPathString("string(./@name)", ctxt))) {
        VIR_WARN("malformed block job data for vm '%s'", vm->def->name);
        return 0;
    }

    /* if the job name is known we need to register such a job so that we can
     * clean it up */
    if (!(typestr = virXPathString("string(./@type)", ctxt)) ||
        (type = qemuBlockjobTypeFromString(typestr)) < 0) {
        type = QEMU_BLOCKJOB_TYPE_BROKEN;
        invalidData = true;
    }

    if (!(job = qemuBlockJobDataNew(type, name)))
        return -1;

    if ((brokentypestr = virXPathString("string(./@brokentype)", ctxt)) &&
        (job->brokentype = qemuBlockjobTypeFromString(brokentypestr)) < 0)
        job->brokentype = QEMU_BLOCKJOB_TYPE_NONE;

    if (!(statestr = virXPathString("string(./@state)", ctxt)) ||
        (state = qemuBlockjobStateTypeFromString(statestr)) < 0)
        invalidData = true;

    if ((newstatestr = virXPathString("string(./@newstate)", ctxt)) &&
        (newstate = qemuBlockjobStateTypeFromString(newstatestr)) < 0)
        invalidData = true;

    if ((diskdst = virXPathString("string(./disk/@dst)", ctxt)) &&
        !(disk = virDomainDiskByTarget(vm->def, diskdst)))
        invalidData = true;

    if ((mirror = virXPathString("string(./disk/@mirror)", ctxt)) &&
        STRNEQ(mirror, "yes"))
        invalidData = true;

    if (virXPathULongHex("string(./@jobflags)", ctxt, &jobflags) != 0)
        job->jobflagsmissing = true;

    if (!disk && !invalidData) {
        if ((tmp = virXPathNode("./chains/disk", ctxt)) &&
            !(job->chain = qemuDomainObjPrivateXMLParseBlockjobChain(tmp, ctxt, xmlopt)))
            invalidData = true;

        if ((tmp = virXPathNode("./chains/mirror", ctxt)) &&
            !(job->mirrorChain = qemuDomainObjPrivateXMLParseBlockjobChain(tmp, ctxt, xmlopt)))
            invalidData = true;
    }

    if (mirror) {
        if (disk)
            job->mirrorChain = virObjectRef(disk->mirror);
        else
            invalidData = true;
    }

    job->state = state;
    job->newstate = newstate;
    job->jobflags = jobflags;
    job->errmsg = virXPathString("string(./errmsg)", ctxt);
    job->invalidData = invalidData;
    job->disk = disk;

    qemuDomainObjPrivateXMLParseBlockjobDataSpecific(job, ctxt, xmlopt);

    if (qemuBlockJobRegister(job, vm, disk, false) < 0)
        return -1;

    return 0;
}


static int
qemuDomainObjPrivateXMLParseBlockjobs(virDomainObjPtr vm,
                                      qemuDomainObjPrivatePtr priv,
                                      xmlXPathContextPtr ctxt)
{
    g_autofree xmlNodePtr *nodes = NULL;
    ssize_t nnodes = 0;
    g_autofree char *active = NULL;
    int tmp;
    size_t i;

    if ((active = virXPathString("string(./blockjobs/@active)", ctxt)) &&
        (tmp = virTristateBoolTypeFromString(active)) > 0)
        priv->reconnectBlockjobs = tmp;

    if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV)) {
        if ((nnodes = virXPathNodeSet("./blockjobs/blockjob", ctxt, &nodes)) < 0)
            return -1;

        for (i = 0; i < nnodes; i++) {
            if (qemuDomainObjPrivateXMLParseBlockjobData(vm, nodes[i], ctxt,
                                                         priv->driver->xmlopt) < 0)
                return -1;
        }
    }

    return 0;
}


static int
qemuDomainObjPrivateXMLParseBackups(qemuDomainObjPrivatePtr priv,
                                    xmlXPathContextPtr ctxt)
{
    g_autofree xmlNodePtr *nodes = NULL;
    ssize_t nnodes = 0;

    if ((nnodes = virXPathNodeSet("./backups/domainbackup", ctxt, &nodes)) < 0)
        return -1;

    if (nnodes > 1) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("only one backup job is supported"));
        return -1;
    }

    if (nnodes == 0)
        return 0;

    if (!(priv->backup = virDomainBackupDefParseNode(ctxt->doc, nodes[0],
                                                     priv->driver->xmlopt,
                                                     VIR_DOMAIN_BACKUP_PARSE_INTERNAL)))
        return -1;

    return 0;
}


int
qemuDomainObjPrivateXMLParseAllowReboot(xmlXPathContextPtr ctxt,
                                        virTristateBool *allowReboot)
{
    int val;
    g_autofree char *valStr = NULL;

    if ((valStr = virXPathString("string(./allowReboot/@value)", ctxt))) {
        if ((val = virTristateBoolTypeFromString(valStr)) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("invalid allowReboot value '%s'"), valStr);
            return -1;
        }
        *allowReboot = val;
    }

    return 0;
}


static void
qemuDomainObjPrivateXMLParsePR(xmlXPathContextPtr ctxt,
                               bool *prDaemonRunning)
{
    *prDaemonRunning = virXPathBoolean("boolean(./prDaemon)", ctxt) > 0;
}


static int
qemuDomainObjPrivateXMLParseSlirpFeatures(xmlNodePtr featuresNode,
                                          xmlXPathContextPtr ctxt,
                                          qemuSlirpPtr slirp)
{
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    g_autofree xmlNodePtr *nodes = NULL;
    size_t i;
    int n;

    ctxt->node = featuresNode;

    if ((n = virXPathNodeSet("./feature", ctxt, &nodes)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("failed to parse slirp-helper features"));
        return -1;
    }

    for (i = 0; i < n; i++) {
        g_autofree char *str = virXMLPropString(nodes[i], "name");
        int feature;

        if (!str)
            continue;

        feature = qemuSlirpFeatureTypeFromString(str);
        if (feature < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unknown slirp feature %s"), str);
            return -1;
        }

        qemuSlirpSetFeature(slirp, feature);
    }

    return 0;
}


static int
qemuDomainObjPrivateXMLParse(xmlXPathContextPtr ctxt,
                             virDomainObjPtr vm,
                             virDomainDefParserConfigPtr config)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virQEMUDriverPtr driver = config->priv;
    char *monitorpath;
    g_autofree char *tmp = NULL;
    int n;
    size_t i;
    g_autofree xmlNodePtr *nodes = NULL;
    xmlNodePtr node = NULL;
    g_autoptr(virQEMUCaps) qemuCaps = NULL;

    if (!(priv->monConfig = virDomainChrSourceDefNew(NULL)))
        goto error;

    if (!(monitorpath =
          virXPathString("string(./monitor[1]/@path)", ctxt))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("no monitor path"));
        goto error;
    }

    tmp = virXPathString("string(./monitor[1]/@type)", ctxt);
    if (tmp)
        priv->monConfig->type = virDomainChrTypeFromString(tmp);
    else
        priv->monConfig->type = VIR_DOMAIN_CHR_TYPE_PTY;
    VIR_FREE(tmp);

    switch (priv->monConfig->type) {
    case VIR_DOMAIN_CHR_TYPE_PTY:
        priv->monConfig->data.file.path = monitorpath;
        break;
    case VIR_DOMAIN_CHR_TYPE_UNIX:
        priv->monConfig->data.nix.path = monitorpath;
        break;
    default:
        VIR_FREE(monitorpath);
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unsupported monitor type '%s'"),
                       virDomainChrTypeToString(priv->monConfig->type));
        goto error;
    }

    if (virXPathInt("string(./agentTimeout)", ctxt, &priv->agentTimeout) == -2) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to parse agent timeout"));
        goto error;
    }

    priv->dbusDaemonRunning = virXPathBoolean("boolean(./dbusDaemon)", ctxt) > 0;

    priv->dbusVMState = virXPathBoolean("boolean(./dbusVMState)", ctxt) > 0;

    if ((node = virXPathNode("./namespaces", ctxt))) {
        xmlNodePtr next;

        for (next = node->children; next; next = next->next) {
            int ns = qemuDomainNamespaceTypeFromString((const char *)next->name);

            if (ns < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("malformed namespace name: %s"),
                               next->name);
                goto error;
            }

            if (qemuDomainEnableNamespace(vm, ns) < 0)
                goto error;
        }
    }

    if (priv->namespaces &&
        virBitmapIsAllClear(priv->namespaces)) {
        virBitmapFree(priv->namespaces);
        priv->namespaces = NULL;
    }

    priv->rememberOwner = virXPathBoolean("count(./rememberOwner) > 0", ctxt);

    if ((n = virXPathNodeSet("./vcpus/vcpu", ctxt, &nodes)) < 0)
        goto error;

    for (i = 0; i < n; i++) {
        if (qemuDomainObjPrivateXMLParseVcpu(nodes[i], i, vm->def) < 0)
            goto error;
    }
    VIR_FREE(nodes);

    if ((n = virXPathNodeSet("./qemuCaps/flag", ctxt, &nodes)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("failed to parse qemu capabilities flags"));
        goto error;
    }
    if (n > 0) {
        if (!(qemuCaps = virQEMUCapsNew()))
            goto error;

        for (i = 0; i < n; i++) {
            g_autofree char *str = virXMLPropString(nodes[i], "name");
            if (str) {
                int flag = virQEMUCapsTypeFromString(str);
                if (flag < 0) {
                    virReportError(VIR_ERR_INTERNAL_ERROR,
                                   _("Unknown qemu capabilities flag %s"), str);
                    goto error;
                }
                virQEMUCapsSet(qemuCaps, flag);
            }
        }

        priv->qemuCaps = g_steal_pointer(&qemuCaps);
    }
    VIR_FREE(nodes);

    priv->lockState = virXPathString("string(./lockstate)", ctxt);

    if (qemuDomainObjPrivateXMLParseJob(vm, ctxt) < 0)
        goto error;

    priv->fakeReboot = virXPathBoolean("boolean(./fakereboot)", ctxt) == 1;

    if ((n = virXPathNodeSet("./devices/device", ctxt, &nodes)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to parse qemu device list"));
        goto error;
    }
    if (n > 0) {
        /* NULL-terminated list */
        priv->qemuDevices = g_new0(char *, n + 1);

        for (i = 0; i < n; i++) {
            priv->qemuDevices[i] = virXMLPropString(nodes[i], "alias");
            if (!priv->qemuDevices[i]) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("failed to parse qemu device list"));
                goto error;
            }
        }
    }
    VIR_FREE(nodes);

    if ((n = virXPathNodeSet("./slirp/helper", ctxt, &nodes)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to parse slirp helper list"));
        goto error;
    }
    for (i = 0; i < n; i++) {
        g_autofree char *alias = virXMLPropString(nodes[i], "alias");
        g_autofree char *pid = virXMLPropString(nodes[i], "pid");
        g_autoptr(qemuSlirp) slirp = qemuSlirpNew();
        virDomainDeviceDef dev;

        if (!alias || !pid || !slirp ||
            virStrToLong_i(pid, NULL, 10, &slirp->pid) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("failed to parse slirp helper list"));
            goto error;
        }

        if (virDomainDefFindDevice(vm->def, alias, &dev, true) < 0 ||
            dev.type != VIR_DOMAIN_DEVICE_NET)
            goto error;

        if (qemuDomainObjPrivateXMLParseSlirpFeatures(nodes[i], ctxt, slirp) < 0)
            goto error;

        QEMU_DOMAIN_NETWORK_PRIVATE(dev.data.net)->slirp = g_steal_pointer(&slirp);
    }
    VIR_FREE(nodes);

    if (qemuDomainObjPrivateXMLParseAutomaticPlacement(ctxt, priv, driver) < 0)
        goto error;

    if ((tmp = virXPathString("string(./libDir/@path)", ctxt)))
        priv->libDir = tmp;
    if ((tmp = virXPathString("string(./channelTargetDir/@path)", ctxt)))
        priv->channelTargetDir = tmp;
    tmp = NULL;

    qemuDomainSetPrivatePathsOld(driver, vm);

    if (virCPUDefParseXML(ctxt, "./cpu", VIR_CPU_TYPE_GUEST, &priv->origCPU,
                          false) < 0)
        goto error;

    priv->chardevStdioLogd = virXPathBoolean("boolean(./chardevStdioLogd)",
                                             ctxt) == 1;

    qemuDomainObjPrivateXMLParseAllowReboot(ctxt, &priv->allowReboot);

    qemuDomainObjPrivateXMLParsePR(ctxt, &priv->prDaemonRunning);

    if (qemuDomainObjPrivateXMLParseBlockjobs(vm, priv, ctxt) < 0)
        goto error;

    if (qemuDomainObjPrivateXMLParseBackups(priv, ctxt) < 0)
        goto error;

    qemuDomainStorageIdReset(priv);
    if (virXPathULongLong("string(./nodename/@index)", ctxt,
                          &priv->nodenameindex) == -2) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("failed to parse node name index"));
        goto error;
    }

    priv->memPrealloc = virXPathBoolean("boolean(./memPrealloc)", ctxt) == 1;

    return 0;

 error:
    virBitmapFree(priv->namespaces);
    priv->namespaces = NULL;
    virObjectUnref(priv->monConfig);
    priv->monConfig = NULL;
    g_strfreev(priv->qemuDevices);
    priv->qemuDevices = NULL;
    return -1;
}


static void *
qemuDomainObjPrivateXMLGetParseOpaque(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    return priv->qemuCaps;
}


virDomainXMLPrivateDataCallbacks virQEMUDriverPrivateDataCallbacks = {
    .alloc = qemuDomainObjPrivateAlloc,
    .free = qemuDomainObjPrivateFree,
    .diskNew = qemuDomainDiskPrivateNew,
    .diskParse = qemuDomainDiskPrivateParse,
    .diskFormat = qemuDomainDiskPrivateFormat,
    .vcpuNew = qemuDomainVcpuPrivateNew,
    .chrSourceNew = qemuDomainChrSourcePrivateNew,
    .vsockNew = qemuDomainVsockPrivateNew,
    .graphicsNew = qemuDomainGraphicsPrivateNew,
    .networkNew = qemuDomainNetworkPrivateNew,
    .videoNew = qemuDomainVideoPrivateNew,
    .fsNew = qemuDomainFSPrivateNew,
    .parse = qemuDomainObjPrivateXMLParse,
    .format = qemuDomainObjPrivateXMLFormat,
    .getParseOpaque = qemuDomainObjPrivateXMLGetParseOpaque,
    .storageParse = qemuStorageSourcePrivateDataParse,
    .storageFormat = qemuStorageSourcePrivateDataFormat,
};


static void
qemuDomainXmlNsDefFree(qemuDomainXmlNsDefPtr def)
{
    if (!def)
        return;

    virStringListFreeCount(def->args, def->num_args);
    virStringListFreeCount(def->env_name, def->num_env);
    virStringListFreeCount(def->env_value, def->num_env);
    virStringListFreeCount(def->capsadd, def->ncapsadd);
    virStringListFreeCount(def->capsdel, def->ncapsdel);

    VIR_FREE(def);
}


static void
qemuDomainDefNamespaceFree(void *nsdata)
{
    qemuDomainXmlNsDefPtr cmd = nsdata;

    qemuDomainXmlNsDefFree(cmd);
}


static int
qemuDomainDefNamespaceParseCommandlineArgs(qemuDomainXmlNsDefPtr nsdef,
                                           xmlXPathContextPtr ctxt)
{
    g_autofree xmlNodePtr *nodes = NULL;
    ssize_t nnodes;
    size_t i;

    if ((nnodes = virXPathNodeSet("./qemu:commandline/qemu:arg", ctxt, &nodes)) < 0)
        return -1;

    if (nnodes == 0)
        return 0;

    nsdef->args = g_new0(char *, nnodes);

    for (i = 0; i < nnodes; i++) {
        if (!(nsdef->args[nsdef->num_args++] = virXMLPropString(nodes[i], "value"))) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("No qemu command-line argument specified"));
            return -1;
        }
    }

    return 0;
}


static int
qemuDomainDefNamespaceParseCommandlineEnvNameValidate(const char *envname)
{
    if (!g_ascii_isalpha(envname[0]) && envname[0] != '_') {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Invalid environment name, it must begin with a letter or underscore"));
        return -1;
    }

    if (strspn(envname, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != strlen(envname)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Invalid environment name, it must contain only alphanumerics and underscore"));
        return -1;
    }

    return 0;
}


static int
qemuDomainDefNamespaceParseCommandlineEnv(qemuDomainXmlNsDefPtr nsdef,
                                          xmlXPathContextPtr ctxt)
{
    g_autofree xmlNodePtr *nodes = NULL;
    ssize_t nnodes;
    size_t i;

    if ((nnodes = virXPathNodeSet("./qemu:commandline/qemu:env", ctxt, &nodes)) < 0)
        return -1;

    if (nnodes == 0)
        return 0;

    nsdef->env_name = g_new0(char *, nnodes);
    nsdef->env_value = g_new0(char *, nnodes);

    for (i = 0; i < nnodes; i++) {
        if (!(nsdef->env_name[nsdef->num_env] = virXMLPropString(nodes[i], "name"))) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("No qemu environment name specified"));
            return -1;
        }

        if (qemuDomainDefNamespaceParseCommandlineEnvNameValidate(nsdef->env_name[nsdef->num_env]) < 0)
            return -1;

        nsdef->env_value[nsdef->num_env] = virXMLPropString(nodes[i], "value");
        /* a NULL value for command is allowed, since it might be empty */
        nsdef->num_env++;
    }

    return 0;
}


static int
qemuDomainDefNamespaceParseCaps(qemuDomainXmlNsDefPtr nsdef,
                                xmlXPathContextPtr ctxt)
{
    g_autofree xmlNodePtr *nodesadd = NULL;
    ssize_t nnodesadd;
    g_autofree xmlNodePtr *nodesdel = NULL;
    ssize_t nnodesdel;
    size_t i;

    if ((nnodesadd = virXPathNodeSet("./qemu:capabilities/qemu:add", ctxt, &nodesadd)) < 0 ||
        (nnodesdel = virXPathNodeSet("./qemu:capabilities/qemu:del", ctxt, &nodesdel)) < 0)
        return -1;

    if (nnodesadd > 0) {
        nsdef->capsadd = g_new0(char *, nnodesadd);

        for (i = 0; i < nnodesadd; i++) {
            if (!(nsdef->capsadd[nsdef->ncapsadd++] = virXMLPropString(nodesadd[i], "capability"))) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("missing capability name"));
                return -1;
            }
        }
    }

    if (nnodesdel > 0) {
        nsdef->capsdel = g_new0(char *, nnodesdel);

        for (i = 0; i < nnodesdel; i++) {
            if (!(nsdef->capsdel[nsdef->ncapsdel++] = virXMLPropString(nodesdel[i], "capability"))) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("missing capability name"));
                return -1;
            }
        }
    }

    return 0;
}


static int
qemuDomainDefNamespaceParse(xmlXPathContextPtr ctxt,
                            void **data)
{
    qemuDomainXmlNsDefPtr nsdata = NULL;
    int ret = -1;

    nsdata = g_new0(qemuDomainXmlNsDef, 1);

    if (qemuDomainDefNamespaceParseCommandlineArgs(nsdata, ctxt) < 0 ||
        qemuDomainDefNamespaceParseCommandlineEnv(nsdata, ctxt) < 0 ||
        qemuDomainDefNamespaceParseCaps(nsdata, ctxt) < 0)
        goto cleanup;

    if (nsdata->num_args > 0 || nsdata->num_env > 0 ||
        nsdata->ncapsadd > 0 || nsdata->ncapsdel > 0)
        *data = g_steal_pointer(&nsdata);

    ret = 0;

 cleanup:
    qemuDomainDefNamespaceFree(nsdata);
    return ret;
}


static void
qemuDomainDefNamespaceFormatXMLCommandline(virBufferPtr buf,
                                           qemuDomainXmlNsDefPtr cmd)
{
    size_t i;

    if (!cmd->num_args && !cmd->num_env)
        return;

    virBufferAddLit(buf, "<qemu:commandline>\n");
    virBufferAdjustIndent(buf, 2);

    for (i = 0; i < cmd->num_args; i++)
        virBufferEscapeString(buf, "<qemu:arg value='%s'/>\n",
                              cmd->args[i]);
    for (i = 0; i < cmd->num_env; i++) {
        virBufferAsprintf(buf, "<qemu:env name='%s'", cmd->env_name[i]);
        if (cmd->env_value[i])
            virBufferEscapeString(buf, " value='%s'", cmd->env_value[i]);
        virBufferAddLit(buf, "/>\n");
    }

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</qemu:commandline>\n");
}


static void
qemuDomainDefNamespaceFormatXMLCaps(virBufferPtr buf,
                                    qemuDomainXmlNsDefPtr xmlns)
{
    size_t i;

    if (!xmlns->ncapsadd && !xmlns->ncapsdel)
        return;

    virBufferAddLit(buf, "<qemu:capabilities>\n");
    virBufferAdjustIndent(buf, 2);

    for (i = 0; i < xmlns->ncapsadd; i++)
        virBufferEscapeString(buf, "<qemu:add capability='%s'/>\n", xmlns->capsadd[i]);

    for (i = 0; i < xmlns->ncapsdel; i++)
        virBufferEscapeString(buf, "<qemu:del capability='%s'/>\n", xmlns->capsdel[i]);

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</qemu:capabilities>\n");
}


static int
qemuDomainDefNamespaceFormatXML(virBufferPtr buf,
                                void *nsdata)
{
    qemuDomainXmlNsDefPtr cmd = nsdata;

    qemuDomainDefNamespaceFormatXMLCommandline(buf, cmd);
    qemuDomainDefNamespaceFormatXMLCaps(buf, cmd);

    return 0;
}


virXMLNamespace virQEMUDriverDomainXMLNamespace = {
    .parse = qemuDomainDefNamespaceParse,
    .free = qemuDomainDefNamespaceFree,
    .format = qemuDomainDefNamespaceFormatXML,
    .prefix = "qemu",
    .uri = "http://libvirt.org/schemas/domain/qemu/1.0",
};


static int
qemuDomainDefAddImplicitInputDevice(virDomainDef *def)
{
    if (ARCH_IS_X86(def->os.arch)) {
        if (virDomainDefMaybeAddInput(def,
                                      VIR_DOMAIN_INPUT_TYPE_MOUSE,
                                      VIR_DOMAIN_INPUT_BUS_PS2) < 0)
            return -1;

        if (virDomainDefMaybeAddInput(def,
                                      VIR_DOMAIN_INPUT_TYPE_KBD,
                                      VIR_DOMAIN_INPUT_BUS_PS2) < 0)
            return -1;
    }

    return 0;
}


static int
qemuDomainDefAddDefaultDevices(virDomainDefPtr def,
                               virQEMUCapsPtr qemuCaps)
{
    bool addDefaultUSB = true;
    int usbModel = -1; /* "default for machinetype" */
    int pciRoot;       /* index within def->controllers */
    bool addImplicitSATA = false;
    bool addPCIRoot = false;
    bool addPCIeRoot = false;
    bool addDefaultMemballoon = true;
    bool addDefaultUSBKBD = false;
    bool addDefaultUSBMouse = false;
    bool addPanicDevice = false;

    /* add implicit input devices */
    if (qemuDomainDefAddImplicitInputDevice(def) < 0)
        return -1;

    /* Add implicit PCI root controller if the machine has one */
    switch (def->os.arch) {
    case VIR_ARCH_I686:
    case VIR_ARCH_X86_64:
        if (STREQ(def->os.machine, "isapc")) {
            addDefaultUSB = false;
            break;
        }
        if (qemuDomainIsQ35(def)) {
            addPCIeRoot = true;
            addImplicitSATA = true;

            /* Prefer adding a USB3 controller if supported, fall back
             * to USB2 if there is no USB3 available, and if that's
             * unavailable don't add anything.
             */
            if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_QEMU_XHCI))
                usbModel = VIR_DOMAIN_CONTROLLER_MODEL_USB_QEMU_XHCI;
            else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_NEC_USB_XHCI))
                usbModel = VIR_DOMAIN_CONTROLLER_MODEL_USB_NEC_XHCI;
            else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_ICH9_USB_EHCI1))
                usbModel = VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_EHCI1;
            else
                addDefaultUSB = false;
            break;
        }
        if (qemuDomainIsI440FX(def))
            addPCIRoot = true;
        break;

    case VIR_ARCH_ARMV6L:
        addDefaultUSB = false;
        addDefaultMemballoon = false;
        if (STREQ(def->os.machine, "versatilepb"))
            addPCIRoot = true;
        break;

    case VIR_ARCH_ARMV7L:
    case VIR_ARCH_AARCH64:
        addDefaultUSB = false;
        addDefaultMemballoon = false;
        if (qemuDomainIsARMVirt(def))
            addPCIeRoot = virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX);
        break;

    case VIR_ARCH_PPC64:
    case VIR_ARCH_PPC64LE:
        addPCIRoot = true;
        addDefaultUSBKBD = true;
        addDefaultUSBMouse = true;
        /* For pSeries guests, the firmware provides the same
         * functionality as the pvpanic device, so automatically
         * add the definition if not already present */
        if (qemuDomainIsPSeries(def))
            addPanicDevice = true;
        break;

    case VIR_ARCH_ALPHA:
    case VIR_ARCH_PPC:
    case VIR_ARCH_PPCEMB:
    case VIR_ARCH_SH4:
    case VIR_ARCH_SH4EB:
        addPCIRoot = true;
        break;

    case VIR_ARCH_RISCV32:
    case VIR_ARCH_RISCV64:
        addDefaultUSB = false;
        if (qemuDomainIsRISCVVirt(def))
            addPCIeRoot = virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX);
        break;

    case VIR_ARCH_S390:
    case VIR_ARCH_S390X:
        addDefaultUSB = false;
        addPanicDevice = true;
        addPCIRoot = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_ZPCI);
        break;

    case VIR_ARCH_SPARC:
    case VIR_ARCH_SPARC64:
        addPCIRoot = true;
        break;

    case VIR_ARCH_ARMV7B:
    case VIR_ARCH_CRIS:
    case VIR_ARCH_ITANIUM:
    case VIR_ARCH_LM32:
    case VIR_ARCH_M68K:
    case VIR_ARCH_MICROBLAZE:
    case VIR_ARCH_MICROBLAZEEL:
    case VIR_ARCH_MIPS:
    case VIR_ARCH_MIPSEL:
    case VIR_ARCH_MIPS64:
    case VIR_ARCH_MIPS64EL:
    case VIR_ARCH_OR32:
    case VIR_ARCH_PARISC:
    case VIR_ARCH_PARISC64:
    case VIR_ARCH_PPCLE:
    case VIR_ARCH_UNICORE32:
    case VIR_ARCH_XTENSA:
    case VIR_ARCH_XTENSAEB:
    case VIR_ARCH_NONE:
    case VIR_ARCH_LAST:
    default:
        break;
    }

    if (addDefaultUSB &&
        virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_USB, 0) < 0 &&
        virDomainDefAddUSBController(def, 0, usbModel) < 0)
        return -1;

    if (addImplicitSATA &&
        virDomainDefMaybeAddController(
            def, VIR_DOMAIN_CONTROLLER_TYPE_SATA, 0, -1) < 0)
        return -1;

    pciRoot = virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0);

    /* NB: any machine that sets addPCIRoot to true must also return
     * true from the function qemuDomainSupportsPCI().
     */
    if (addPCIRoot) {
        if (pciRoot >= 0) {
            if (def->controllers[pciRoot]->model != VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT) {
                virReportError(VIR_ERR_XML_ERROR,
                               _("The PCI controller with index='0' must be "
                                 "model='pci-root' for this machine type, "
                                 "but model='%s' was found instead"),
                               virDomainControllerModelPCITypeToString(def->controllers[pciRoot]->model));
                return -1;
            }
        } else if (!virDomainDefAddController(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0,
                                              VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT)) {
            return -1;
        }
    }

    /* When a machine has a pcie-root, make sure that there is always
     * a dmi-to-pci-bridge controller added as bus 1, and a pci-bridge
     * as bus 2, so that standard PCI devices can be connected
     *
     * NB: any machine that sets addPCIeRoot to true must also return
     * true from the function qemuDomainSupportsPCI().
     */
    if (addPCIeRoot) {
        if (pciRoot >= 0) {
            if (def->controllers[pciRoot]->model != VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT) {
                virReportError(VIR_ERR_XML_ERROR,
                               _("The PCI controller with index='0' must be "
                                 "model='pcie-root' for this machine type, "
                                 "but model='%s' was found instead"),
                               virDomainControllerModelPCITypeToString(def->controllers[pciRoot]->model));
                return -1;
            }
        } else if (!virDomainDefAddController(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0,
                                             VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT)) {
            return -1;
        }
    }

    if (addDefaultMemballoon && !def->memballoon) {
        virDomainMemballoonDefPtr memballoon;
        memballoon = g_new0(virDomainMemballoonDef, 1);

        memballoon->model = VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO;
        def->memballoon = memballoon;
    }

    if (STRPREFIX(def->os.machine, "s390-virtio") &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_S390) && def->memballoon)
        def->memballoon->model = VIR_DOMAIN_MEMBALLOON_MODEL_NONE;

    if (addDefaultUSBMouse) {
        bool hasUSBTablet = false;
        size_t j;

        for (j = 0; j < def->ninputs; j++) {
            if (def->inputs[j]->type == VIR_DOMAIN_INPUT_TYPE_TABLET &&
                def->inputs[j]->bus == VIR_DOMAIN_INPUT_BUS_USB) {
                hasUSBTablet = true;
                break;
            }
        }

        /* Historically, we have automatically added USB keyboard and
         * mouse to some guests. While the former device is generally
         * safe to have, adding the latter is undesiderable if a USB
         * tablet is already present in the guest */
        if (hasUSBTablet)
            addDefaultUSBMouse = false;
    }

    if (addDefaultUSBKBD &&
        def->ngraphics > 0 &&
        virDomainDefMaybeAddInput(def,
                                  VIR_DOMAIN_INPUT_TYPE_KBD,
                                  VIR_DOMAIN_INPUT_BUS_USB) < 0)
        return -1;

    if (addDefaultUSBMouse &&
        def->ngraphics > 0 &&
        virDomainDefMaybeAddInput(def,
                                  VIR_DOMAIN_INPUT_TYPE_MOUSE,
                                  VIR_DOMAIN_INPUT_BUS_USB) < 0)
        return -1;

    if (addPanicDevice) {
        size_t j;
        for (j = 0; j < def->npanics; j++) {
            if (def->panics[j]->model == VIR_DOMAIN_PANIC_MODEL_DEFAULT ||
                (ARCH_IS_PPC64(def->os.arch) &&
                     def->panics[j]->model == VIR_DOMAIN_PANIC_MODEL_PSERIES) ||
                (ARCH_IS_S390(def->os.arch) &&
                     def->panics[j]->model == VIR_DOMAIN_PANIC_MODEL_S390))
                break;
        }

        if (j == def->npanics) {
            virDomainPanicDefPtr panic = g_new0(virDomainPanicDef, 1);

            if (VIR_APPEND_ELEMENT_COPY(def->panics,
                                        def->npanics, panic) < 0) {
                VIR_FREE(panic);
                return -1;
            }
        }
    }

    return 0;
}


/**
 * qemuDomainDefEnableDefaultFeatures:
 * @def: domain definition
 * @qemuCaps: QEMU capabilities
 *
 * Make sure that features that should be enabled by default are actually
 * enabled and configure default values related to those features.
 */
static void
qemuDomainDefEnableDefaultFeatures(virDomainDefPtr def,
                                   virQEMUCapsPtr qemuCaps)
{
    /* The virt machine type always uses GIC: if the relevant information
     * was not included in the domain XML, we need to choose a suitable
     * GIC version ourselves */
    if ((def->features[VIR_DOMAIN_FEATURE_GIC] == VIR_TRISTATE_SWITCH_ABSENT &&
         qemuDomainIsARMVirt(def)) ||
        (def->features[VIR_DOMAIN_FEATURE_GIC] == VIR_TRISTATE_SWITCH_ON &&
         def->gic_version == VIR_GIC_VERSION_NONE)) {
        virGICVersion version;

        VIR_DEBUG("Looking for usable GIC version in domain capabilities");
        for (version = VIR_GIC_VERSION_LAST - 1;
             version > VIR_GIC_VERSION_NONE;
             version--) {

            /* We want to use the highest available GIC version for guests;
             * however, the emulated GICv3 is currently lacking a MSI controller,
             * making it unsuitable for the pure PCIe topology we aim for.
             *
             * For that reason, we skip this step entirely for TCG guests,
             * and rely on the code below to pick the default version, GICv2,
             * which supports all the features we need.
             *
             * See https://bugzilla.redhat.com/show_bug.cgi?id=1414081 */
            if (version == VIR_GIC_VERSION_3 &&
                def->virtType == VIR_DOMAIN_VIRT_QEMU) {
                continue;
            }

            if (virQEMUCapsSupportsGICVersion(qemuCaps,
                                              def->virtType,
                                              version)) {
                VIR_DEBUG("Using GIC version %s",
                          virGICVersionTypeToString(version));
                def->gic_version = version;
                break;
            }
        }

        /* Use the default GIC version (GICv2) as a last-ditch attempt
         * if no match could be found above */
        if (def->gic_version == VIR_GIC_VERSION_NONE) {
            VIR_DEBUG("Using GIC version 2 (default)");
            def->gic_version = VIR_GIC_VERSION_2;
        }

        /* Even if we haven't found a usable GIC version in the domain
         * capabilities, we still want to enable this */
        def->features[VIR_DOMAIN_FEATURE_GIC] = VIR_TRISTATE_SWITCH_ON;
    }
}


static int
qemuCanonicalizeMachine(virDomainDefPtr def, virQEMUCapsPtr qemuCaps)
{
    const char *canon;

    if (!(canon = virQEMUCapsGetCanonicalMachine(qemuCaps, def->virtType,
                                                 def->os.machine)))
        return 0;

    if (STRNEQ(canon, def->os.machine)) {
        char *tmp;
        tmp = g_strdup(canon);
        VIR_FREE(def->os.machine);
        def->os.machine = tmp;
    }

    return 0;
}


static int
qemuDomainRecheckInternalPaths(virDomainDefPtr def,
                               virQEMUDriverConfigPtr cfg,
                               unsigned int flags)
{
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < def->ngraphics; ++i) {
        virDomainGraphicsDefPtr graphics = def->graphics[i];

        for (j = 0; j < graphics->nListens; ++j) {
            virDomainGraphicsListenDefPtr glisten =  &graphics->listens[j];

            /* This will happen only if we parse XML from old libvirts where
             * unix socket was available only for VNC graphics.  In this
             * particular case we should follow the behavior and if we remove
             * the auto-generated socket based on config option from qemu.conf
             * we need to change the listen type to address. */
            if (graphics->type == VIR_DOMAIN_GRAPHICS_TYPE_VNC &&
                glisten->type == VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_SOCKET &&
                glisten->socket &&
                !glisten->autoGenerated &&
                STRPREFIX(glisten->socket, cfg->libDir)) {
                if (flags & VIR_DOMAIN_DEF_PARSE_INACTIVE) {
                    VIR_FREE(glisten->socket);
                    glisten->type = VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_ADDRESS;
                } else {
                    glisten->fromConfig = true;
                }
            }
        }
    }

    return 0;
}


static int
qemuDomainDefVcpusPostParse(virDomainDefPtr def)
{
    unsigned int maxvcpus = virDomainDefGetVcpusMax(def);
    virDomainVcpuDefPtr vcpu;
    virDomainVcpuDefPtr prevvcpu;
    size_t i;
    bool has_order = false;

    /* vcpu 0 needs to be present, first, and non-hotpluggable */
    vcpu = virDomainDefGetVcpu(def, 0);
    if (!vcpu->online) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("vcpu 0 can't be offline"));
        return -1;
    }
    if (vcpu->hotpluggable == VIR_TRISTATE_BOOL_YES) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("vcpu0 can't be hotpluggable"));
        return -1;
    }
    if (vcpu->order != 0 && vcpu->order != 1) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("vcpu0 must be enabled first"));
        return -1;
    }

    if (vcpu->order != 0)
        has_order = true;

    prevvcpu = vcpu;

    /* all online vcpus or non online vcpu need to have order set */
    for (i = 1; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(def, i);

        if (vcpu->online &&
            (vcpu->order != 0) != has_order) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("all vcpus must have either set or unset order"));
            return -1;
        }

        /* few conditions for non-hotpluggable (thus online) vcpus */
        if (vcpu->hotpluggable == VIR_TRISTATE_BOOL_NO) {
            /* they can be ordered only at the beginning */
            if (prevvcpu->hotpluggable == VIR_TRISTATE_BOOL_YES) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("online non-hotpluggable vcpus need to be "
                                 "ordered prior to hotplugable vcpus"));
                return -1;
            }

            /* they need to be in order (qemu doesn't support any order yet).
             * Also note that multiple vcpus may share order on some platforms */
            if (prevvcpu->order > vcpu->order) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("online non-hotpluggable vcpus must be ordered "
                                 "in ascending order"));
                return -1;
            }
        }

        prevvcpu = vcpu;
    }

    return 0;
}


static int
qemuDomainDefSetDefaultCPU(virDomainDefPtr def,
                           virArch hostarch,
                           virQEMUCapsPtr qemuCaps)
{
    const char *model;

    if (def->cpu &&
        (def->cpu->mode != VIR_CPU_MODE_CUSTOM ||
         def->cpu->model))
        return 0;

    if (!virCPUArchIsSupported(def->os.arch))
        return 0;

    /* Default CPU model info from QEMU is usable for TCG only except for
     * x86, s390, and ppc64. */
    if (!ARCH_IS_X86(def->os.arch) &&
        !ARCH_IS_S390(def->os.arch) &&
        !ARCH_IS_PPC64(def->os.arch) &&
        def->virtType != VIR_DOMAIN_VIRT_QEMU)
        return 0;

    model = virQEMUCapsGetMachineDefaultCPU(qemuCaps, def->os.machine, def->virtType);
    if (!model) {
        VIR_DEBUG("Unknown default CPU model for domain '%s'", def->name);
        return 0;
    }

    if (STREQ(model, "host") && def->virtType != VIR_DOMAIN_VIRT_KVM) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("QEMU reports invalid default CPU model \"host\" "
                         "for non-kvm domain virt type"));
        return -1;
    }

    if (!def->cpu)
        def->cpu = virCPUDefNew();

    def->cpu->type = VIR_CPU_TYPE_GUEST;

    if (STREQ(model, "host")) {
        if (ARCH_IS_S390(def->os.arch) &&
            virQEMUCapsIsCPUModeSupported(qemuCaps, hostarch, def->virtType,
                                          VIR_CPU_MODE_HOST_MODEL,
                                          def->os.machine)) {
            def->cpu->mode = VIR_CPU_MODE_HOST_MODEL;
        } else {
            def->cpu->mode = VIR_CPU_MODE_HOST_PASSTHROUGH;
        }

        VIR_DEBUG("Setting default CPU mode for domain '%s' to %s",
                  def->name, virCPUModeTypeToString(def->cpu->mode));
    } else {
        /* We need to turn off all CPU checks when the domain is started
         * because the default CPU (e.g., qemu64) may not be runnable on any
         * host. QEMU will just disable the unavailable features and we will
         * update the CPU definition accordingly and set check to FULL when
         * starting the domain. */
        def->cpu->check = VIR_CPU_CHECK_NONE;
        def->cpu->mode = VIR_CPU_MODE_CUSTOM;
        def->cpu->match = VIR_CPU_MATCH_EXACT;
        def->cpu->fallback = VIR_CPU_FALLBACK_FORBID;
        def->cpu->model = g_strdup(model);

        VIR_DEBUG("Setting default CPU model for domain '%s' to %s",
                  def->name, model);
    }

    return 0;
}


static int
qemuDomainDefCPUPostParse(virDomainDefPtr def,
                          virQEMUCapsPtr qemuCaps)
{
    virCPUFeatureDefPtr sveFeature = NULL;
    bool sveVectorLengthsProvided = false;
    size_t i;

    if (!def->cpu)
        return 0;

    if (def->cpu->cache) {
        virCPUCacheDefPtr cache = def->cpu->cache;

        if (!ARCH_IS_X86(def->os.arch)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("CPU cache specification is not supported "
                             "for '%s' architecture"),
                           virArchToString(def->os.arch));
            return -1;
        }

        switch (cache->mode) {
        case VIR_CPU_CACHE_MODE_EMULATE:
            if (cache->level != 3) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("CPU cache mode '%s' can only be used with "
                                 "level='3'"),
                               virCPUCacheModeTypeToString(cache->mode));
                return -1;
            }
            break;

        case VIR_CPU_CACHE_MODE_PASSTHROUGH:
            if (def->cpu->mode != VIR_CPU_MODE_HOST_PASSTHROUGH) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("CPU cache mode '%s' can only be used with "
                                 "'%s' CPUs"),
                               virCPUCacheModeTypeToString(cache->mode),
                               virCPUModeTypeToString(VIR_CPU_MODE_HOST_PASSTHROUGH));
                return -1;
            }

            if (cache->level != -1) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("unsupported CPU cache level for mode '%s'"),
                               virCPUCacheModeTypeToString(cache->mode));
                return -1;
            }
            break;

        case VIR_CPU_CACHE_MODE_DISABLE:
            if (cache->level != -1) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("unsupported CPU cache level for mode '%s'"),
                               virCPUCacheModeTypeToString(cache->mode));
                return -1;
            }
            break;

        case VIR_CPU_CACHE_MODE_LAST:
            break;
        }
    }

    for (i = 0; i < def->cpu->nfeatures; i++) {
        virCPUFeatureDefPtr feature = &def->cpu->features[i];

        if (STREQ(feature->name, "sve")) {
            sveFeature = feature;
        } else if (STRPREFIX(feature->name, "sve")) {
            sveVectorLengthsProvided = true;
        }
    }

    if (sveVectorLengthsProvided) {
        if (sveFeature) {
            if (sveFeature->policy == VIR_CPU_FEATURE_DISABLE ||
                sveFeature->policy == VIR_CPU_FEATURE_FORBID) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("SVE disabled, but SVE vector lengths provided"));
                return -1;
            } else {
                sveFeature->policy = VIR_CPU_FEATURE_REQUIRE;
            }
        } else {
            if (VIR_RESIZE_N(def->cpu->features, def->cpu->nfeatures_max,
                             def->cpu->nfeatures, 1) < 0) {
                return -1;
            }

            def->cpu->features[def->cpu->nfeatures].name = g_strdup("sve");
            def->cpu->features[def->cpu->nfeatures].policy = VIR_CPU_FEATURE_REQUIRE;

            def->cpu->nfeatures++;
        }
    }

    /* Running domains were either started before QEMU_CAPS_CPU_MIGRATABLE was
     * introduced and thus we can't rely on it or they already have the
     * migratable default set. */
    if (def->id == -1 &&
        qemuCaps &&
        def->cpu->mode == VIR_CPU_MODE_HOST_PASSTHROUGH &&
        def->cpu->migratable == VIR_TRISTATE_SWITCH_ABSENT) {
        if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_CPU_MIGRATABLE))
            def->cpu->migratable = VIR_TRISTATE_SWITCH_ON;
        else if (ARCH_IS_X86(def->os.arch))
            def->cpu->migratable = VIR_TRISTATE_SWITCH_OFF;
    }

    /* Nothing to be done if only CPU topology is specified. */
    if (def->cpu->mode == VIR_CPU_MODE_CUSTOM &&
        !def->cpu->model)
        return 0;

    if (def->cpu->check != VIR_CPU_CHECK_DEFAULT)
        return 0;

    switch ((virCPUMode) def->cpu->mode) {
    case VIR_CPU_MODE_HOST_PASSTHROUGH:
        def->cpu->check = VIR_CPU_CHECK_NONE;
        break;

    case VIR_CPU_MODE_HOST_MODEL:
        def->cpu->check = VIR_CPU_CHECK_PARTIAL;
        break;

    case VIR_CPU_MODE_CUSTOM:
        /* Custom CPUs in TCG mode are not compared to host CPU by default. */
        if (def->virtType == VIR_DOMAIN_VIRT_QEMU)
            def->cpu->check = VIR_CPU_CHECK_NONE;
        else
            def->cpu->check = VIR_CPU_CHECK_PARTIAL;
        break;

    case VIR_CPU_MODE_LAST:
        break;
    }

    return 0;
}


static int
qemuDomainDefTsegPostParse(virDomainDefPtr def,
                           virQEMUCapsPtr qemuCaps)
{
    if (def->features[VIR_DOMAIN_FEATURE_SMM] != VIR_TRISTATE_SWITCH_ON)
        return 0;

    if (!def->tseg_specified)
        return 0;

    if (!qemuDomainIsQ35(def)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("SMM TSEG is only supported with q35 machine type"));
        return -1;
    }

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MCH_EXTENDED_TSEG_MBYTES)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Setting TSEG size is not supported with this QEMU binary"));
        return -1;
    }

    if (def->tseg_size & ((1 << 20) - 1)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("SMM TSEG size must be divisible by 1 MiB"));
        return -1;
    }

    return 0;
}


/**
 * qemuDomainDefNumaCPUsRectify:
 * @numa: pointer to numa definition
 * @maxCpus: number of CPUs this numa is supposed to have
 *
 * This function emulates the (to be deprecated) behavior of filling
 * up in node0 with the remaining CPUs, in case of an incomplete NUMA
 * setup, up to getVcpusMax.
 *
 * Returns: 0 on success, -1 on error
 */
int
qemuDomainDefNumaCPUsRectify(virDomainDefPtr def, virQEMUCapsPtr qemuCaps)
{
    unsigned int vcpusMax, numacpus;

    /* QEMU_CAPS_NUMA tells us if QEMU is able to handle disjointed
     * NUMA CPU ranges. The filling process will create a disjointed
     * setup in node0 most of the time. Do not proceed if QEMU
     * can't handle it.*/
    if (virDomainNumaGetNodeCount(def->numa) == 0 ||
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_NUMA))
        return 0;

    vcpusMax = virDomainDefGetVcpusMax(def);
    numacpus = virDomainNumaGetCPUCountTotal(def->numa);

    if (numacpus < vcpusMax) {
        if (virDomainNumaFillCPUsInNode(def->numa, 0, vcpusMax) < 0)
            return -1;
    }

    return 0;
}


static int
qemuDomainDefNumaCPUsPostParse(virDomainDefPtr def,
                               virQEMUCapsPtr qemuCaps)
{
    return qemuDomainDefNumaCPUsRectify(def, qemuCaps);
}


static int
qemuDomainDefTPMsPostParse(virDomainDefPtr def)
{
    virDomainTPMDefPtr proxyTPM = NULL;
    virDomainTPMDefPtr regularTPM = NULL;
    size_t i;

    for (i = 0; i < def->ntpms; i++) {
        virDomainTPMDefPtr tpm = def->tpms[i];

        /* TPM 1.2 and 2 are not compatible, so we choose a specific version here */
        if (tpm->version == VIR_DOMAIN_TPM_VERSION_DEFAULT) {
            if (tpm->model == VIR_DOMAIN_TPM_MODEL_SPAPR ||
                tpm->model == VIR_DOMAIN_TPM_MODEL_CRB)
                tpm->version = VIR_DOMAIN_TPM_VERSION_2_0;
            else
                tpm->version = VIR_DOMAIN_TPM_VERSION_1_2;
        }

        if (tpm->model == VIR_DOMAIN_TPM_MODEL_SPAPR_PROXY) {
            if (proxyTPM) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("only a single TPM Proxy device is supported"));
                return -1;
            } else {
                proxyTPM = tpm;
            }
        } else if (regularTPM) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("only a single TPM non-proxy device is supported"));
            return -1;
        } else {
            regularTPM = tpm;
        }
    }

    return 0;
}


static int
qemuDomainDefPostParseBasic(virDomainDefPtr def,
                            void *opaque G_GNUC_UNUSED)
{
    virQEMUDriverPtr driver = opaque;

    /* check for emulator and create a default one if needed */
    if (!def->emulator) {
        if (!(def->emulator = virQEMUCapsGetDefaultEmulator(
                  driver->hostarch, def->os.arch))) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("No emulator found for arch '%s'"),
                           virArchToString(def->os.arch));
            return 1;
        }
    }

    return 0;
}


static int
qemuDomainDefPostParse(virDomainDefPtr def,
                       unsigned int parseFlags,
                       void *opaque,
                       void *parseOpaque)
{
    virQEMUDriverPtr driver = opaque;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    virQEMUCapsPtr qemuCaps = parseOpaque;

    /* Note that qemuCaps may be NULL when this function is called. This
     * function shall not fail in that case. It will be re-run on VM startup
     * with the capabilities populated.
     */
    if (!qemuCaps)
        return 1;

    if (def->os.bootloader || def->os.bootloaderArgs) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("bootloader is not supported by QEMU"));
        return -1;
    }

    if (!def->os.machine) {
        const char *machine = virQEMUCapsGetPreferredMachine(qemuCaps,
                                                             def->virtType);
        if (!machine) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("could not get preferred machine for %s type=%s"),
                           def->emulator,
                           virDomainVirtTypeToString(def->virtType));
            return -1;
        }

        def->os.machine = g_strdup(machine);
    }

    qemuDomainNVRAMPathGenerate(cfg, def);

    if (qemuDomainDefAddDefaultDevices(def, qemuCaps) < 0)
        return -1;

    if (qemuCanonicalizeMachine(def, qemuCaps) < 0)
        return -1;

    if (qemuDomainDefSetDefaultCPU(def, driver->hostarch, qemuCaps) < 0)
        return -1;

    qemuDomainDefEnableDefaultFeatures(def, qemuCaps);

    if (qemuDomainRecheckInternalPaths(def, cfg, parseFlags) < 0)
        return -1;

    if (qemuSecurityVerify(driver->securityManager, def) < 0)
        return -1;

    if (qemuDomainDefVcpusPostParse(def) < 0)
        return -1;

    if (qemuDomainDefCPUPostParse(def, qemuCaps) < 0)
        return -1;

    if (qemuDomainDefTsegPostParse(def, qemuCaps) < 0)
        return -1;

    if (qemuDomainDefNumaCPUsPostParse(def, qemuCaps) < 0)
        return -1;

    if (qemuDomainDefTPMsPostParse(def) < 0)
        return -1;

    return 0;
}


int
qemuDomainValidateActualNetDef(const virDomainNetDef *net,
                               virQEMUCapsPtr qemuCaps)
{
    /*
     * Validations that can only be properly checked at runtime (after
     * an <interface type='network'> has been resolved to its actual
     * type.
     *
     * (In its current form this function can still be called before
     * the actual type has been resolved (e.g. at domain definition
     * time), but only if the validations would SUCCEED for
     * type='network'.)
     */
    char macstr[VIR_MAC_STRING_BUFLEN];
    virDomainNetType actualType = virDomainNetGetActualType(net);

    virMacAddrFormat(&net->mac, macstr);

    /* hypervisor-agnostic validation */
    if (virDomainActualNetDefValidate(net) < 0)
        return -1;

    /* QEMU-specific validation */

    /* Only tap/macvtap devices support multiqueue. */
    if (net->driver.virtio.queues > 0) {

        if (!(actualType == VIR_DOMAIN_NET_TYPE_NETWORK ||
              actualType == VIR_DOMAIN_NET_TYPE_BRIDGE ||
              actualType == VIR_DOMAIN_NET_TYPE_DIRECT ||
              actualType == VIR_DOMAIN_NET_TYPE_ETHERNET ||
              actualType == VIR_DOMAIN_NET_TYPE_VHOSTUSER)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("interface %s - multiqueue is not supported for network interfaces of type %s"),
                           macstr, virDomainNetTypeToString(actualType));
            return -1;
        }

        if (net->driver.virtio.queues > 1 &&
            actualType == VIR_DOMAIN_NET_TYPE_VHOSTUSER &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VHOSTUSER_MULTIQUEUE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("interface %s - multiqueue is not supported for network interfaces of type vhost-user with this QEMU binary"),
                           macstr);
            return -1;
        }
    }

    /*
     * Only standard tap devices support nwfilter rules, and even then only
     * when *not* connected to an OVS bridge or midonet (indicated by having
     * a <virtualport> element in the config)
     */
    if (net->filter) {
        const virNetDevVPortProfile *vport = virDomainNetGetActualVirtPortProfile(net);

        if (!(actualType == VIR_DOMAIN_NET_TYPE_NETWORK ||
              actualType == VIR_DOMAIN_NET_TYPE_BRIDGE ||
              actualType == VIR_DOMAIN_NET_TYPE_ETHERNET)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("interface %s - filterref is not supported for network interfaces of type %s"),
                           macstr, virDomainNetTypeToString(actualType));
            return -1;
        }
        if (vport && vport->virtPortType != VIR_NETDEV_VPORT_PROFILE_NONE) {
            /* currently none of the defined virtualport types support iptables */
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("interface %s - filterref is not supported for network interfaces with virtualport type %s"),
                           macstr, virNetDevVPortTypeToString(vport->virtPortType));
            return -1;
        }
    }

    if (net->backend.tap &&
        !(actualType == VIR_DOMAIN_NET_TYPE_NETWORK ||
          actualType == VIR_DOMAIN_NET_TYPE_BRIDGE ||
          actualType == VIR_DOMAIN_NET_TYPE_ETHERNET)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("interface %s - custom tap device path is not supported for network interfaces of type %s"),
                       macstr, virDomainNetTypeToString(actualType));
        return -1;
    }

    if (net->teaming.type == VIR_DOMAIN_NET_TEAMING_TYPE_TRANSIENT &&
        actualType != VIR_DOMAIN_NET_TYPE_HOSTDEV) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("interface %s - teaming transient device must be type='hostdev', not '%s'"),
                       macstr, virDomainNetTypeToString(actualType));
        return -1;
    }
    return 0;
}


int
qemuDomainValidateStorageSource(virStorageSourcePtr src,
                                virQEMUCapsPtr qemuCaps,
                                bool maskBlockdev)
{
    int actualType = virStorageSourceGetActualType(src);
    bool blockdev = virQEMUCapsGet(qemuCaps, QEMU_CAPS_BLOCKDEV);

    if (maskBlockdev)
        blockdev = false;

    if (src->format == VIR_STORAGE_FILE_COW) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                      _("'cow' storage format is not supported"));
        return -1;
    }

    if (src->format == VIR_STORAGE_FILE_DIR) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("'directory' storage format is not directly supported by QEMU, "
                         "use 'dir' disk type instead"));
        return -1;
    }

    if (src->format == VIR_STORAGE_FILE_ISO) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("storage format 'iso' is not directly supported by QEMU, "
                         "use 'raw' instead"));
        return -1;
    }

    if ((src->format == VIR_STORAGE_FILE_QCOW ||
         src->format == VIR_STORAGE_FILE_QCOW2) &&
        src->encryption &&
        (src->encryption->format == VIR_STORAGE_ENCRYPTION_FORMAT_DEFAULT ||
         src->encryption->format == VIR_STORAGE_ENCRYPTION_FORMAT_QCOW)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("old qcow/qcow2 encryption is not supported"));
            return -1;
    }

    if (src->format == VIR_STORAGE_FILE_QCOW2 &&
        src->encryption &&
        src->encryption->format == VIR_STORAGE_ENCRYPTION_FORMAT_LUKS &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_QCOW2_LUKS)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("LUKS encrypted QCOW2 images are not supported by this QEMU"));
        return -1;
    }

    if (src->format == VIR_STORAGE_FILE_FAT &&
        actualType != VIR_STORAGE_TYPE_VOLUME &&
        actualType != VIR_STORAGE_TYPE_DIR) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("storage format 'fat' is supported only with 'dir' "
                         "storage type"));
        return -1;
    }

    if (actualType == VIR_STORAGE_TYPE_DIR) {
        if (src->format > 0 &&
            src->format != VIR_STORAGE_FILE_FAT) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("storage type 'dir' requires use of storage format 'fat'"));
            return -1;
        }

        if (!src->readonly) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtual FAT storage can't be accessed in read-write mode"));
            return -1;
        }
    }

    if (src->pr &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_PR_MANAGER_HELPER)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("reservations not supported with this QEMU binary"));
        return -1;
    }

    /* Use QEMU_CAPS_ISCSI_PASSWORD_SECRET as witness that iscsi 'initiator-name'
     * option is available, it was introduced at the same time. */
    if (src->initiator.iqn &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_ISCSI_PASSWORD_SECRET)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("iSCSI initiator IQN not supported with this QEMU binary"));
        return -1;
    }

    if (src->sliceStorage) {
        /* In pre-blockdev era we can't configure the slice so we can allow them
         * only for detected backing store entries as they are populated
         * from a place that qemu would be able to read */
        if (!src->detected && !blockdev) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("storage slice is not supported by this QEMU binary"));
            return -1;
        }
    }

    if (src->sslverify != VIR_TRISTATE_BOOL_ABSENT) {
        if (actualType != VIR_STORAGE_TYPE_NETWORK ||
            (src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTPS &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_FTPS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("ssl verification is supported only with HTTPS/FTPS protocol"));
            return -1;
        }

        if (!src->detected && !blockdev) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("ssl verification setting is not supported by this QEMU binary"));
            return -1;
        }
    }

    if (src->ncookies > 0) {
        if (actualType != VIR_STORAGE_TYPE_NETWORK ||
            (src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTPS &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTP)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("http cookies are supported only with HTTP(S) protocol"));
            return -1;
        }

        if (!src->detected && !blockdev) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("http cookies are not supported by this QEMU binary"));
            return -1;
        }

        if (virStorageSourceNetCookiesValidate(src) < 0)
            return -1;
    }

    if (src->readahead > 0) {
        if (actualType != VIR_STORAGE_TYPE_NETWORK ||
            (src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTPS &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTP &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_FTP &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_FTPS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("readahead is supported only with HTTP(S)/FTP(s) protocols"));
            return -1;
        }

        if (!src->detected && !blockdev) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("readahead setting is not supported with this QEMU binary"));
            return -1;
        }
    }

    if (src->timeout > 0) {
        if (actualType != VIR_STORAGE_TYPE_NETWORK ||
            (src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTPS &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTP &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_FTP &&
             src->protocol != VIR_STORAGE_NET_PROTOCOL_FTPS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("timeout is supported only with HTTP(S)/FTP(s) protocols"));
            return -1;
        }

        if (!src->detected && !blockdev) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("timeout setting is not supported with this QEMU binary"));
            return -1;
        }
    }

    if (src->query &&
        (actualType != VIR_STORAGE_TYPE_NETWORK ||
         (src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTPS &&
          src->protocol != VIR_STORAGE_NET_PROTOCOL_HTTP))) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("query is supported only with HTTP(S) protocols"));
        return -1;
    }

    /* TFTP protocol was not supported for some time, lock it out at least with
     * -blockdev */
    if (actualType == VIR_STORAGE_TYPE_NETWORK &&
        src->protocol == VIR_STORAGE_NET_PROTOCOL_TFTP &&
        blockdev) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("'tftp' protocol is not supported with this QEMU binary"));
        return -1;
    }

    return 0;
}


/**
 * qemuDomainDefaultNetModel:
 * @def: domain definition
 * @qemuCaps: qemu capabilities
 *
 * Returns the default network model for a given domain. Note that if @qemuCaps
 * is NULL this function may return NULL if the default model depends on the
 * capabilities.
 */
static int
qemuDomainDefaultNetModel(const virDomainDef *def,
                          virQEMUCapsPtr qemuCaps)
{
    if (ARCH_IS_S390(def->os.arch))
        return VIR_DOMAIN_NET_MODEL_VIRTIO;

    if (def->os.arch == VIR_ARCH_ARMV6L ||
        def->os.arch == VIR_ARCH_ARMV7L ||
        def->os.arch == VIR_ARCH_AARCH64) {
        if (STREQ(def->os.machine, "versatilepb"))
            return VIR_DOMAIN_NET_MODEL_SMC91C111;

        if (qemuDomainIsARMVirt(def))
            return VIR_DOMAIN_NET_MODEL_VIRTIO;

        /* Incomplete. vexpress (and a few others) use this, but not all
         * arm boards */
        return VIR_DOMAIN_NET_MODEL_LAN9118;
    }

    /* virtio is a sensible default for RISC-V virt guests */
    if (qemuDomainIsRISCVVirt(def))
        return VIR_DOMAIN_NET_MODEL_VIRTIO;

    /* In all other cases the model depends on the capabilities. If they were
     * not provided don't report any default. */
    if (!qemuCaps)
        return VIR_DOMAIN_NET_MODEL_UNKNOWN;

    /* Try several network devices in turn; each of these devices is
     * less likely be supported out-of-the-box by the guest operating
     * system than the previous one */
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_RTL8139))
        return VIR_DOMAIN_NET_MODEL_RTL8139;
    else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_E1000))
        return VIR_DOMAIN_NET_MODEL_E1000;
    else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_NET))
        return VIR_DOMAIN_NET_MODEL_VIRTIO;

    /* We've had no luck detecting support for any network device,
     * but we have to return something: might as well be rtl8139 */
    return VIR_DOMAIN_NET_MODEL_RTL8139;
}


/*
 * Clear auto generated unix socket paths:
 *
 * libvirt 1.2.18 and older:
 *     {cfg->channelTargetDir}/{dom-name}.{target-name}
 *
 * libvirt 1.2.19 - 1.3.2:
 *     {cfg->channelTargetDir}/domain-{dom-name}/{target-name}
 *
 * libvirt 1.3.3 and newer:
 *     {cfg->channelTargetDir}/domain-{dom-id}-{short-dom-name}/{target-name}
 *
 * The unix socket path was stored in config XML until libvirt 1.3.0.
 * If someone specifies the same path as we generate, they shouldn't do it.
 *
 * This function clears the path for migration as well, so we need to clear
 * the path even if we are not storing it in the XML.
 */
static void
qemuDomainChrDefDropDefaultPath(virDomainChrDefPtr chr,
                                virQEMUDriverPtr driver)
{
    g_autoptr(virQEMUDriverConfig) cfg = NULL;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    g_autofree char *regexp = NULL;

    if (chr->deviceType != VIR_DOMAIN_CHR_DEVICE_TYPE_CHANNEL ||
        chr->targetType != VIR_DOMAIN_CHR_CHANNEL_TARGET_TYPE_VIRTIO ||
        chr->source->type != VIR_DOMAIN_CHR_TYPE_UNIX ||
        !chr->source->data.nix.path) {
        return;
    }

    cfg = virQEMUDriverGetConfig(driver);

    virBufferEscapeRegex(&buf, "^%s", cfg->channelTargetDir);
    virBufferAddLit(&buf, "/([^/]+\\.)|(domain-[^/]+/)");
    virBufferEscapeRegex(&buf, "%s$", chr->target.name);

    regexp = virBufferContentAndReset(&buf);

    if (virStringMatch(chr->source->data.nix.path, regexp))
        VIR_FREE(chr->source->data.nix.path);
}


static int
qemuDomainShmemDefPostParse(virDomainShmemDefPtr shm)
{
    /* This was the default since the introduction of this device. */
    if (shm->model != VIR_DOMAIN_SHMEM_MODEL_IVSHMEM_DOORBELL && !shm->size)
        shm->size = 4 << 20;

    /* Nothing more to check/change for IVSHMEM */
    if (shm->model == VIR_DOMAIN_SHMEM_MODEL_IVSHMEM)
        return 0;

    if (!shm->server.enabled) {
        if (shm->model == VIR_DOMAIN_SHMEM_MODEL_IVSHMEM_DOORBELL) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("shmem model '%s' is supported "
                             "only with server option enabled"),
                           virDomainShmemModelTypeToString(shm->model));
            return -1;
        }

        if (shm->msi.enabled) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("shmem model '%s' doesn't support "
                             "msi"),
                           virDomainShmemModelTypeToString(shm->model));
        }
    } else {
        if (shm->model == VIR_DOMAIN_SHMEM_MODEL_IVSHMEM_PLAIN) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("shmem model '%s' is supported "
                             "only with server option disabled"),
                           virDomainShmemModelTypeToString(shm->model));
            return -1;
        }

        if (shm->size) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("shmem model '%s' does not support size setting"),
                           virDomainShmemModelTypeToString(shm->model));
            return -1;
        }
        shm->msi.enabled = true;
        if (!shm->msi.ioeventfd)
            shm->msi.ioeventfd = VIR_TRISTATE_SWITCH_ON;
    }

    return 0;
}


#define QEMU_USB_XHCI_MAXPORTS 15


static int
qemuDomainControllerDefPostParse(virDomainControllerDefPtr cont,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps,
                                 unsigned int parseFlags)
{
    switch ((virDomainControllerType)cont->type) {
    case VIR_DOMAIN_CONTROLLER_TYPE_SCSI:
        /* Set the default SCSI controller model if not already set */
        if (qemuDomainSetSCSIControllerModel(def, cont, qemuCaps) < 0)
            return -1;
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_USB:
        if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_DEFAULT && qemuCaps) {
            /* Pick a suitable default model for the USB controller if none
             * has been selected by the user and we have the qemuCaps for
             * figuring out which controllers are supported.
             *
             * We rely on device availability instead of setting the model
             * unconditionally because, for some machine types, there's a
             * chance we will get away with using the legacy USB controller
             * when the relevant device is not available.
             *
             * See qemuBuildControllerDevCommandLine() */

            /* Default USB controller is piix3-uhci if available. */
            if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_PIIX3_USB_UHCI))
                cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX3_UHCI;

            if (ARCH_IS_S390(def->os.arch)) {
                if (cont->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
                    /* set the default USB model to none for s390 unless an
                     * address is found */
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_NONE;
                }
            } else if (ARCH_IS_PPC64(def->os.arch)) {
                /* To not break migration we need to set default USB controller
                 * for ppc64 to pci-ohci if we cannot change ABI of the VM.
                 * The nec-usb-xhci or qemu-xhci controller is used as default
                 * only for newly defined domains or devices. */
                if ((parseFlags & VIR_DOMAIN_DEF_PARSE_ABI_UPDATE) &&
                    virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_QEMU_XHCI)) {
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_QEMU_XHCI;
                } else if ((parseFlags & VIR_DOMAIN_DEF_PARSE_ABI_UPDATE) &&
                    virQEMUCapsGet(qemuCaps, QEMU_CAPS_NEC_USB_XHCI)) {
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_NEC_XHCI;
                } else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_PCI_OHCI)) {
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_PCI_OHCI;
                } else {
                    /* Explicitly fallback to legacy USB controller for PPC64. */
                    cont->model = -1;
                }
            } else if (def->os.arch == VIR_ARCH_AARCH64) {
                if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_QEMU_XHCI))
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_QEMU_XHCI;
                else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_NEC_USB_XHCI))
                    cont->model = VIR_DOMAIN_CONTROLLER_MODEL_USB_NEC_XHCI;
            }
        }
        /* forbid usb model 'qusb1' and 'qusb2' in this kind of hyperviosr */
        if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_QUSB1 ||
            cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_QUSB2) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("USB controller model type 'qusb1' or 'qusb2' "
                             "is not supported in %s"),
                           virDomainVirtTypeToString(def->virtType));
            return -1;
        }
        if ((cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_NEC_XHCI ||
             cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_QEMU_XHCI) &&
            cont->opts.usbopts.ports > QEMU_USB_XHCI_MAXPORTS) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("'%s' controller only supports up to '%u' ports"),
                           virDomainControllerModelUSBTypeToString(cont->model),
                           QEMU_USB_XHCI_MAXPORTS);
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_PCI:

        /* pSeries guests can have multiple pci-root controllers,
         * but other machine types only support a single one */
        if (!qemuDomainIsPSeries(def) &&
            (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT ||
             cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT) &&
            cont->idx != 0) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("pci-root and pcie-root controllers "
                             "should have index 0"));
            return -1;
        }

        if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS &&
            !qemuDomainIsI440FX(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("pci-expander-bus controllers are only supported "
                             "on 440fx-based machinetypes"));
            return -1;
        }
        if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS &&
            !qemuDomainIsQ35(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("pcie-expander-bus controllers are only supported "
                             "on q35-based machinetypes"));
            return -1;
        }

        /* if a PCI expander bus or pci-root on Pseries has a NUMA node
         * set, make sure that NUMA node is configured in the guest
         * <cpu><numa> array. NUMA cell id's in this array are numbered
         * from 0 .. size-1.
         */
        if (cont->opts.pciopts.numaNode >= 0 &&
            cont->opts.pciopts.numaNode >=
            (int)virDomainNumaGetNodeCount(def->numa)) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("%s with index %d is "
                             "configured for a NUMA node (%d) "
                             "not present in the domain's "
                             "<cpu><numa> array (%zu)"),
                           virDomainControllerModelPCITypeToString(cont->model),
                           cont->idx, cont->opts.pciopts.numaNode,
                           virDomainNumaGetNodeCount(def->numa));
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
    case VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL:
    case VIR_DOMAIN_CONTROLLER_TYPE_CCID:
    case VIR_DOMAIN_CONTROLLER_TYPE_IDE:
    case VIR_DOMAIN_CONTROLLER_TYPE_FDC:
    case VIR_DOMAIN_CONTROLLER_TYPE_XENBUS:
    case VIR_DOMAIN_CONTROLLER_TYPE_ISA:
    case VIR_DOMAIN_CONTROLLER_TYPE_LAST:
        break;
    }

    return 0;
}

static int
qemuDomainChrDefPostParse(virDomainChrDefPtr chr,
                          const virDomainDef *def,
                          virQEMUDriverPtr driver,
                          unsigned int parseFlags)
{
    /* Historically, isa-serial and the default matched, so in order to
     * maintain backwards compatibility we map them here. The actual default
     * will be picked below based on the architecture and machine type. */
    if (chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL &&
        chr->targetType == VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA) {
        chr->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE;
    }

    /* Set the default serial type */
    if (chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL &&
        chr->targetType == VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE) {
        if (ARCH_IS_X86(def->os.arch)) {
            chr->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA;
        } else if (qemuDomainIsPSeries(def)) {
            chr->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO;
        } else if (qemuDomainIsARMVirt(def) || qemuDomainIsRISCVVirt(def)) {
            chr->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM;
        } else if (ARCH_IS_S390(def->os.arch)) {
            chr->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP;
        }
    }

    /* Set the default target model */
    if (chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL &&
        chr->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_NONE) {
        switch ((virDomainChrSerialTargetType)chr->targetType) {
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA:
            chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_ISA_SERIAL;
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_USB:
            chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_USB_SERIAL;
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI:
            chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PCI_SERIAL;
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO:
            chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SPAPR_VTY;
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM:
            if (qemuDomainIsARMVirt(def)) {
                chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PL011;
            } else if (qemuDomainIsRISCVVirt(def)) {
                chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_16550A;
            }
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP:
            chr->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPCONSOLE;
            break;
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_LAST:
            /* Nothing to do */
            break;
        }
    }

    /* clear auto generated unix socket path for inactive definitions */
    if (parseFlags & VIR_DOMAIN_DEF_PARSE_INACTIVE) {
        qemuDomainChrDefDropDefaultPath(chr, driver);

        /* For UNIX chardev if no path is provided we generate one.
         * This also implies that the mode is 'bind'. */
        if (chr->source &&
            chr->source->type == VIR_DOMAIN_CHR_TYPE_UNIX &&
            !chr->source->data.nix.path) {
            chr->source->data.nix.listen = true;
        }
    }

    return 0;
}


/**
 * qemuDomainDeviceDiskDefPostParseRestoreSecAlias:
 *
 * Re-generate aliases for objects related to the storage source if they
 * were not stored in the status XML by an older libvirt.
 *
 * Note that qemuCaps should be always present for a status XML.
 */
static int
qemuDomainDeviceDiskDefPostParseRestoreSecAlias(virDomainDiskDefPtr disk,
                                                virQEMUCapsPtr qemuCaps,
                                                unsigned int parseFlags)
{
    qemuDomainStorageSourcePrivatePtr priv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(disk->src);
    bool restoreAuthSecret = false;
    bool restoreEncSecret = false;
    g_autofree char *authalias = NULL;
    g_autofree char *encalias = NULL;

    if (!(parseFlags & VIR_DOMAIN_DEF_PARSE_STATUS) ||
        !qemuCaps ||
        virStorageSourceIsEmpty(disk->src) ||
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_SECRET))
        return 0;

    /* network storage authentication secret */
    if (disk->src->auth &&
        (!priv || !priv->secinfo)) {

        /* only RBD and iSCSI (with capability) were supporting authentication
         * using secret object at the time we did not format the alias into the
         * status XML */
        if (virStorageSourceGetActualType(disk->src) == VIR_STORAGE_TYPE_NETWORK &&
            (disk->src->protocol == VIR_STORAGE_NET_PROTOCOL_RBD ||
             (disk->src->protocol == VIR_STORAGE_NET_PROTOCOL_ISCSI &&
              virQEMUCapsGet(qemuCaps, QEMU_CAPS_ISCSI_PASSWORD_SECRET))))
            restoreAuthSecret = true;
    }

    /* disk encryption secret */
    if (disk->src->encryption &&
        disk->src->encryption->format == VIR_STORAGE_ENCRYPTION_FORMAT_LUKS &&
        (!priv || !priv->encinfo))
        restoreEncSecret = true;

    if (!restoreAuthSecret && !restoreEncSecret)
        return 0;

    if (!priv) {
        if (!(disk->src->privateData = qemuDomainStorageSourcePrivateNew()))
            return -1;

        priv = QEMU_DOMAIN_STORAGE_SOURCE_PRIVATE(disk->src);
    }

    if (restoreAuthSecret) {
        authalias = g_strdup_printf("%s-secret0", disk->info.alias);

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->secinfo, &authalias) < 0)
            return -1;
    }

    if (restoreEncSecret) {
        encalias = g_strdup_printf("%s-luks-secret0", disk->info.alias);

        if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->encinfo, &encalias) < 0)
            return -1;
    }

    return 0;
}


static int
qemuDomainDeviceDiskDefPostParse(virDomainDiskDefPtr disk,
                                 virQEMUCapsPtr qemuCaps,
                                 unsigned int parseFlags)
{
    /* set default disk types and drivers */
    if (!virDomainDiskGetDriver(disk))
        virDomainDiskSetDriver(disk, "qemu");

    /* default disk format for drives */
    if (virDomainDiskGetFormat(disk) == VIR_STORAGE_FILE_NONE &&
        virDomainDiskGetType(disk) != VIR_STORAGE_TYPE_VOLUME)
        virDomainDiskSetFormat(disk, VIR_STORAGE_FILE_RAW);

    /* default disk format for mirrored drive */
    if (disk->mirror &&
        disk->mirror->format == VIR_STORAGE_FILE_NONE)
        disk->mirror->format = VIR_STORAGE_FILE_RAW;

    if (qemuDomainDeviceDiskDefPostParseRestoreSecAlias(disk, qemuCaps,
                                                        parseFlags) < 0)
        return -1;

    /* regenerate TLS alias for old status XMLs */
    if (parseFlags & VIR_DOMAIN_DEF_PARSE_STATUS &&
        disk->src->haveTLS == VIR_TRISTATE_BOOL_YES &&
        !disk->src->tlsAlias &&
        !(disk->src->tlsAlias = qemuAliasTLSObjFromSrcAlias(disk->info.alias)))
        return -1;

    return 0;
}


static int
qemuDomainDeviceNetDefPostParse(virDomainNetDefPtr net,
                                const virDomainDef *def,
                                virQEMUCapsPtr qemuCaps)
{
    if (net->type == VIR_DOMAIN_NET_TYPE_VDPA &&
        !virDomainNetGetModelString(net))
        net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
    else if (net->type != VIR_DOMAIN_NET_TYPE_HOSTDEV &&
        !virDomainNetGetModelString(net) &&
        virDomainNetResolveActualType(net) != VIR_DOMAIN_NET_TYPE_HOSTDEV)
        net->model = qemuDomainDefaultNetModel(def, qemuCaps);

    return 0;
}


static int
qemuDomainDefaultVideoDevice(const virDomainDef *def,
                          virQEMUCapsPtr qemuCaps)
{
    if (ARCH_IS_PPC64(def->os.arch))
        return VIR_DOMAIN_VIDEO_TYPE_VGA;
    if (qemuDomainIsARMVirt(def) ||
        qemuDomainIsRISCVVirt(def) ||
        ARCH_IS_S390(def->os.arch)) {
        return VIR_DOMAIN_VIDEO_TYPE_VIRTIO;
    }
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_CIRRUS_VGA))
        return VIR_DOMAIN_VIDEO_TYPE_CIRRUS;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VGA))
        return VIR_DOMAIN_VIDEO_TYPE_VGA;
    return VIR_DOMAIN_VIDEO_TYPE_DEFAULT;
}


static int
qemuDomainDeviceVideoDefPostParse(virDomainVideoDefPtr video,
                                  const virDomainDef *def,
                                  virQEMUCapsPtr qemuCaps)
{
    if (video->type == VIR_DOMAIN_VIDEO_TYPE_DEFAULT)
        video->type = qemuDomainDefaultVideoDevice(def, qemuCaps);

    if (video->type == VIR_DOMAIN_VIDEO_TYPE_QXL &&
        !video->vgamem) {
        video->vgamem = QEMU_QXL_VGAMEM_DEFAULT;
    }

    return 0;
}


static int
qemuDomainDevicePanicDefPostParse(virDomainPanicDefPtr panic,
                                  const virDomainDef *def)
{
    if (panic->model == VIR_DOMAIN_PANIC_MODEL_DEFAULT) {
        if (qemuDomainIsPSeries(def))
            panic->model = VIR_DOMAIN_PANIC_MODEL_PSERIES;
        else if (ARCH_IS_S390(def->os.arch))
            panic->model = VIR_DOMAIN_PANIC_MODEL_S390;
        else
            panic->model = VIR_DOMAIN_PANIC_MODEL_ISA;
    }

    return 0;
}


static int
qemuDomainVsockDefPostParse(virDomainVsockDefPtr vsock)
{
    if (vsock->model == VIR_DOMAIN_VSOCK_MODEL_DEFAULT)
        vsock->model = VIR_DOMAIN_VSOCK_MODEL_VIRTIO;

    return 0;
}


/**
 * qemuDomainDeviceHostdevDefPostParseRestoreSecAlias:
 *
 * Re-generate aliases for objects related to the storage source if they
 * were not stored in the status XML by an older libvirt.
 *
 * Note that qemuCaps should be always present for a status XML.
 */
static int
qemuDomainDeviceHostdevDefPostParseRestoreSecAlias(virDomainHostdevDefPtr hostdev,
                                                   virQEMUCapsPtr qemuCaps,
                                                   unsigned int parseFlags)
{
    qemuDomainStorageSourcePrivatePtr priv;
    virDomainHostdevSubsysSCSIPtr scsisrc = &hostdev->source.subsys.u.scsi;
    virDomainHostdevSubsysSCSIiSCSIPtr iscsisrc = &scsisrc->u.iscsi;
    g_autofree char *authalias = NULL;

    if (!(parseFlags & VIR_DOMAIN_DEF_PARSE_STATUS) ||
        !qemuCaps ||
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_SECRET))
        return 0;

    if (hostdev->mode != VIR_DOMAIN_HOSTDEV_MODE_SUBSYS ||
        hostdev->source.subsys.type != VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI ||
        scsisrc->protocol != VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_ISCSI ||
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_ISCSI_PASSWORD_SECRET) ||
        !qemuDomainStorageSourceHasAuth(iscsisrc->src))
        return 0;

    if (!(priv = qemuDomainStorageSourcePrivateFetch(iscsisrc->src)))
        return -1;

    if (priv->secinfo)
        return 0;

    authalias = g_strdup_printf("%s-secret0", hostdev->info->alias);

    if (qemuStorageSourcePrivateDataAssignSecinfo(&priv->secinfo, &authalias) < 0)
        return -1;

    return 0;
}


/**
 * qemuDomainDeviceHostdevDefPostParseRestoreBackendAlias:
 *
 * Re-generate backend alias if it wasn't stored in the status XML by an older
 * libvirtd.
 *
 * Note that qemuCaps should be always present for a status XML.
 */
static int
qemuDomainDeviceHostdevDefPostParseRestoreBackendAlias(virDomainHostdevDefPtr hostdev,
                                                       virQEMUCapsPtr qemuCaps,
                                                       unsigned int parseFlags)
{
    virDomainHostdevSubsysSCSIPtr scsisrc = &hostdev->source.subsys.u.scsi;
    virStorageSourcePtr src;

    if (!(parseFlags & VIR_DOMAIN_DEF_PARSE_STATUS))
        return 0;

    if (!qemuCaps ||
        hostdev->mode != VIR_DOMAIN_HOSTDEV_MODE_SUBSYS ||
        hostdev->source.subsys.type != VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI ||
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_BLOCKDEV_HOSTDEV_SCSI))
        return 0;

    switch ((virDomainHostdevSCSIProtocolType) scsisrc->protocol) {
    case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_NONE:
        if (!scsisrc->u.host.src)
            scsisrc->u.host.src = virStorageSourceNew();

        src = scsisrc->u.host.src;
        break;

    case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_ISCSI:
        src = scsisrc->u.iscsi.src;
        break;

    case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_LAST:
    default:
        virReportEnumRangeError(virDomainHostdevSCSIProtocolType, scsisrc->protocol);
        return -1;
    }

    if (!src->nodestorage)
        src->nodestorage = g_strdup_printf("libvirt-%s-backend", hostdev->info->alias);

    return 0;
}


static int
qemuDomainHostdevDefMdevPostParse(virDomainHostdevSubsysMediatedDevPtr mdevsrc,
                                  virQEMUCapsPtr qemuCaps)
{
    /* QEMU 2.12 added support for vfio-pci display type, we default to
     * 'display=off' to stay safe from future changes */
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VFIO_PCI_DISPLAY) &&
        mdevsrc->model == VIR_MDEV_MODEL_TYPE_VFIO_PCI &&
        mdevsrc->display == VIR_TRISTATE_SWITCH_ABSENT)
        mdevsrc->display = VIR_TRISTATE_SWITCH_OFF;

    return 0;
}


static int
qemuDomainHostdevDefPostParse(virDomainHostdevDefPtr hostdev,
                              virQEMUCapsPtr qemuCaps,
                              unsigned int parseFlags)
{
    virDomainHostdevSubsysPtr subsys = &hostdev->source.subsys;

    if (qemuDomainDeviceHostdevDefPostParseRestoreSecAlias(hostdev, qemuCaps,
                                                           parseFlags) < 0)
        return -1;

    if (qemuDomainDeviceHostdevDefPostParseRestoreBackendAlias(hostdev, qemuCaps,
                                                               parseFlags) < 0)
        return -1;

    if (hostdev->mode == VIR_DOMAIN_HOSTDEV_MODE_SUBSYS &&
        hostdev->source.subsys.type == VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_MDEV &&
        qemuDomainHostdevDefMdevPostParse(&subsys->u.mdev, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuDomainTPMDefPostParse(virDomainTPMDefPtr tpm,
                          virArch arch)
{
    if (tpm->model == VIR_DOMAIN_TPM_MODEL_DEFAULT) {
        if (ARCH_IS_PPC64(arch))
            tpm->model = VIR_DOMAIN_TPM_MODEL_SPAPR;
        else
            tpm->model = VIR_DOMAIN_TPM_MODEL_TIS;
    }

    return 0;
}


static int
qemuDomainMemoryDefPostParse(virDomainMemoryDefPtr mem,
                             virArch arch)
{
    /* For x86, dimm memory modules require 2MiB alignment rather than
     * the 1MiB we are using elsewhere. */
    unsigned int x86MemoryModuleSizeAlignment = 2048;
    unsigned long long maxmemkb = virMemoryMaxValue(false) >> 10;

    /* ppc64 memory module alignment is done in
     * virDomainMemoryDefPostParse(). */
    if (!ARCH_IS_PPC64(arch)) {
        mem->size = VIR_ROUND_UP(mem->size, x86MemoryModuleSizeAlignment);
        if (mem->size > maxmemkb) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("size of memory module overflowed after "
                             "alignment"));
            return -1;
        }
    }

    return 0;
}


static int
qemuDomainDeviceDefPostParse(virDomainDeviceDefPtr dev,
                             const virDomainDef *def,
                             unsigned int parseFlags,
                             void *opaque,
                             void *parseOpaque)
{
    virQEMUDriverPtr driver = opaque;
    /* Note that qemuCaps may be NULL when this function is called. This
     * function shall not fail in that case. It will be re-run on VM startup
     * with the capabilities populated. */
    virQEMUCapsPtr qemuCaps = parseOpaque;
    int ret = -1;

    switch ((virDomainDeviceType) dev->type) {
    case VIR_DOMAIN_DEVICE_NET:
        ret = qemuDomainDeviceNetDefPostParse(dev->data.net, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_DISK:
        ret = qemuDomainDeviceDiskDefPostParse(dev->data.disk, qemuCaps,
                                               parseFlags);
        break;

    case VIR_DOMAIN_DEVICE_VIDEO:
        ret = qemuDomainDeviceVideoDefPostParse(dev->data.video, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_PANIC:
        ret = qemuDomainDevicePanicDefPostParse(dev->data.panic, def);
        break;

    case VIR_DOMAIN_DEVICE_CONTROLLER:
        ret = qemuDomainControllerDefPostParse(dev->data.controller, def,
                                               qemuCaps, parseFlags);
        break;

    case VIR_DOMAIN_DEVICE_SHMEM:
        ret = qemuDomainShmemDefPostParse(dev->data.shmem);
        break;

    case VIR_DOMAIN_DEVICE_CHR:
        ret = qemuDomainChrDefPostParse(dev->data.chr, def, driver, parseFlags);
        break;

    case VIR_DOMAIN_DEVICE_VSOCK:
        ret = qemuDomainVsockDefPostParse(dev->data.vsock);
        break;

    case VIR_DOMAIN_DEVICE_HOSTDEV:
        ret = qemuDomainHostdevDefPostParse(dev->data.hostdev, qemuCaps, parseFlags);
        break;

    case VIR_DOMAIN_DEVICE_TPM:
        ret = qemuDomainTPMDefPostParse(dev->data.tpm, def->os.arch);
        break;

    case VIR_DOMAIN_DEVICE_MEMORY:
        ret = qemuDomainMemoryDefPostParse(dev->data.memory, def->os.arch);
        break;

    case VIR_DOMAIN_DEVICE_LEASE:
    case VIR_DOMAIN_DEVICE_FS:
    case VIR_DOMAIN_DEVICE_INPUT:
    case VIR_DOMAIN_DEVICE_SOUND:
    case VIR_DOMAIN_DEVICE_WATCHDOG:
    case VIR_DOMAIN_DEVICE_GRAPHICS:
    case VIR_DOMAIN_DEVICE_HUB:
    case VIR_DOMAIN_DEVICE_REDIRDEV:
    case VIR_DOMAIN_DEVICE_SMARTCARD:
    case VIR_DOMAIN_DEVICE_MEMBALLOON:
    case VIR_DOMAIN_DEVICE_NVRAM:
    case VIR_DOMAIN_DEVICE_RNG:
    case VIR_DOMAIN_DEVICE_IOMMU:
    case VIR_DOMAIN_DEVICE_AUDIO:
        ret = 0;
        break;

    case VIR_DOMAIN_DEVICE_NONE:
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unexpected VIR_DOMAIN_DEVICE_NONE"));
        break;

    case VIR_DOMAIN_DEVICE_LAST:
    default:
        virReportEnumRangeError(virDomainDeviceType, dev->type);
        break;
    }

    return ret;
}


static int
qemuDomainDefAssignAddresses(virDomainDef *def,
                             unsigned int parseFlags G_GNUC_UNUSED,
                             void *opaque,
                             void *parseOpaque)
{
    virQEMUDriverPtr driver = opaque;
    /* Note that qemuCaps may be NULL when this function is called. This
     * function shall not fail in that case. It will be re-run on VM startup
     * with the capabilities populated. */
    virQEMUCapsPtr qemuCaps = parseOpaque;
    bool newDomain = parseFlags & VIR_DOMAIN_DEF_PARSE_ABI_UPDATE;

    /* Skip address assignment if @qemuCaps is not present. In such case devices
     * which are automatically added may be missing. Additionally @qemuCaps should
     * only be missing when reloading configs, thus addresses were already
     * assigned. */
    if (!qemuCaps)
        return 1;

    return qemuDomainAssignAddresses(def, qemuCaps, driver, NULL, newDomain);
}


static int
qemuDomainPostParseDataAlloc(const virDomainDef *def,
                             unsigned int parseFlags G_GNUC_UNUSED,
                             void *opaque,
                             void **parseOpaque)
{
    virQEMUDriverPtr driver = opaque;

    if (!(*parseOpaque = virQEMUCapsCacheLookup(driver->qemuCapsCache,
                                                def->emulator)))
        return 1;

    return 0;
}


static void
qemuDomainPostParseDataFree(void *parseOpaque)
{
    virQEMUCapsPtr qemuCaps = parseOpaque;

    virObjectUnref(qemuCaps);
}


virDomainDefParserConfig virQEMUDriverDomainDefParserConfig = {
    .domainPostParseBasicCallback = qemuDomainDefPostParseBasic,
    .domainPostParseDataAlloc = qemuDomainPostParseDataAlloc,
    .domainPostParseDataFree = qemuDomainPostParseDataFree,
    .devicesPostParseCallback = qemuDomainDeviceDefPostParse,
    .domainPostParseCallback = qemuDomainDefPostParse,
    .assignAddressesCallback = qemuDomainDefAssignAddresses,
    .domainValidateCallback = qemuValidateDomainDef,
    .deviceValidateCallback = qemuValidateDomainDeviceDef,

    .features = VIR_DOMAIN_DEF_FEATURE_MEMORY_HOTPLUG |
                VIR_DOMAIN_DEF_FEATURE_OFFLINE_VCPUPIN |
                VIR_DOMAIN_DEF_FEATURE_INDIVIDUAL_VCPUS |
                VIR_DOMAIN_DEF_FEATURE_USER_ALIAS |
                VIR_DOMAIN_DEF_FEATURE_FW_AUTOSELECT |
                VIR_DOMAIN_DEF_FEATURE_NET_MODEL_STRING,
};


void
qemuDomainObjSaveStatus(virQEMUDriverPtr driver,
                        virDomainObjPtr obj)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);

    if (virDomainObjIsActive(obj)) {
        if (virDomainObjSave(obj, driver->xmlopt, cfg->stateDir) < 0)
            VIR_WARN("Failed to save status on vm %s", obj->def->name);
    }
}


void
qemuDomainSaveStatus(virDomainObjPtr obj)
{
    qemuDomainObjSaveStatus(QEMU_DOMAIN_PRIVATE(obj)->driver, obj);
}


void
qemuDomainSaveConfig(virDomainObjPtr obj)
{
    virQEMUDriverPtr driver = QEMU_DOMAIN_PRIVATE(obj)->driver;
    g_autoptr(virQEMUDriverConfig) cfg = NULL;
    virDomainDefPtr def = NULL;

    if (virDomainObjIsActive(obj))
        def = obj->newDef;
    else
        def = obj->def;

    if (!def)
        return;

    cfg = virQEMUDriverGetConfig(driver);

    if (virDomainDefSave(def, driver->xmlopt, cfg->configDir) < 0)
        VIR_WARN("Failed to save config of vm %s", obj->def->name);
}


/*
 * obj must be locked before calling
 *
 * To be called immediately before any QEMU monitor API call
 * Must have already called qemuDomainObjBeginJob() and checked
 * that the VM is still active; may not be used for nested async
 * jobs.
 *
 * To be followed with qemuDomainObjExitMonitor() once complete
 */
static int
qemuDomainObjEnterMonitorInternal(virQEMUDriverPtr driver,
                                  virDomainObjPtr obj,
                                  qemuDomainAsyncJob asyncJob)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;

    if (asyncJob != QEMU_ASYNC_JOB_NONE) {
        int ret;
        if ((ret = qemuDomainObjBeginNestedJob(driver, obj, asyncJob)) < 0)
            return ret;
        if (!virDomainObjIsActive(obj)) {
            virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                           _("domain is no longer running"));
            qemuDomainObjEndJob(driver, obj);
            return -1;
        }
    } else if (priv->job.asyncOwner == virThreadSelfID()) {
        VIR_WARN("This thread seems to be the async job owner; entering"
                 " monitor without asking for a nested job is dangerous");
    } else if (priv->job.owner != virThreadSelfID()) {
        VIR_WARN("Entering a monitor without owning a job. "
                 "Job %s owner %s (%llu)",
                 qemuDomainJobTypeToString(priv->job.active),
                 priv->job.ownerAPI, priv->job.owner);
    }

    VIR_DEBUG("Entering monitor (mon=%p vm=%p name=%s)",
              priv->mon, obj, obj->def->name);
    virObjectLock(priv->mon);
    virObjectRef(priv->mon);
    ignore_value(virTimeMillisNow(&priv->monStart));
    virObjectUnlock(obj);

    return 0;
}

static void ATTRIBUTE_NONNULL(1)
qemuDomainObjExitMonitorInternal(virQEMUDriverPtr driver,
                                 virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    bool hasRefs;

    qemuMonitorWatchDispose();
    virObjectUnref(priv->mon);

    hasRefs = !qemuMonitorWasDisposed();
    if (hasRefs)
        virObjectUnlock(priv->mon);

    virObjectLock(obj);
    VIR_DEBUG("Exited monitor (mon=%p vm=%p name=%s)",
              priv->mon, obj, obj->def->name);

    priv->monStart = 0;
    if (!hasRefs)
        priv->mon = NULL;

    if (priv->job.active == QEMU_JOB_ASYNC_NESTED)
        qemuDomainObjEndJob(driver, obj);
}

void qemuDomainObjEnterMonitor(virQEMUDriverPtr driver,
                               virDomainObjPtr obj)
{
    ignore_value(qemuDomainObjEnterMonitorInternal(driver, obj,
                                                   QEMU_ASYNC_JOB_NONE));
}

/* obj must NOT be locked before calling
 *
 * Should be paired with an earlier qemuDomainObjEnterMonitor() call
 *
 * Returns -1 if the domain is no longer alive after exiting the monitor.
 * In that case, the caller should be careful when using obj's data,
 * e.g. the live definition in vm->def has been freed by qemuProcessStop
 * and replaced by the persistent definition, so pointers stolen
 * from the live definition could no longer be valid.
 */
int qemuDomainObjExitMonitor(virQEMUDriverPtr driver,
                             virDomainObjPtr obj)
{
    qemuDomainObjExitMonitorInternal(driver, obj);
    if (!virDomainObjIsActive(obj)) {
        if (virGetLastErrorCode() == VIR_ERR_OK)
            virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                           _("domain is no longer running"));
        return -1;
    }
    return 0;
}

/*
 * obj must be locked before calling
 *
 * To be called immediately before any QEMU monitor API call.
 * Must have already either called qemuDomainObjBeginJob()
 * and checked that the VM is still active, with asyncJob of
 * QEMU_ASYNC_JOB_NONE; or already called qemuDomainObjBeginAsyncJob,
 * with the same asyncJob.
 *
 * Returns 0 if job was started, in which case this must be followed with
 * qemuDomainObjExitMonitor(); -2 if waiting for the nested job times out;
 * or -1 if the job could not be started (probably because the vm exited
 * in the meantime).
 */
int
qemuDomainObjEnterMonitorAsync(virQEMUDriverPtr driver,
                               virDomainObjPtr obj,
                               qemuDomainAsyncJob asyncJob)
{
    return qemuDomainObjEnterMonitorInternal(driver, obj, asyncJob);
}


/*
 * obj must be locked before calling
 *
 * To be called immediately before any QEMU agent API call.
 * Must have already called qemuDomainObjBeginAgentJob() and
 * checked that the VM is still active.
 *
 * To be followed with qemuDomainObjExitAgent() once complete
 */
qemuAgentPtr
qemuDomainObjEnterAgent(virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    qemuAgentPtr agent = priv->agent;

    VIR_DEBUG("Entering agent (agent=%p vm=%p name=%s)",
              priv->agent, obj, obj->def->name);

    virObjectLock(agent);
    virObjectRef(agent);
    virObjectUnlock(obj);

    return agent;
}


/* obj must NOT be locked before calling
 *
 * Should be paired with an earlier qemuDomainObjEnterAgent() call
 */
void
qemuDomainObjExitAgent(virDomainObjPtr obj, qemuAgentPtr agent)
{
    virObjectUnlock(agent);
    virObjectUnref(agent);
    virObjectLock(obj);

    VIR_DEBUG("Exited agent (agent=%p vm=%p name=%s)",
              agent, obj, obj->def->name);
}

void qemuDomainObjEnterRemote(virDomainObjPtr obj)
{
    VIR_DEBUG("Entering remote (vm=%p name=%s)",
              obj, obj->def->name);
    virObjectUnlock(obj);
}


int
qemuDomainObjExitRemote(virDomainObjPtr obj,
                        bool checkActive)
{
    virObjectLock(obj);
    VIR_DEBUG("Exited remote (vm=%p name=%s)",
              obj, obj->def->name);

    if (checkActive && !virDomainObjIsActive(obj)) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("domain '%s' is not running"),
                       obj->def->name);
        return -1;
    }

    return 0;
}


static virDomainDefPtr
qemuDomainDefFromXML(virQEMUDriverPtr driver,
                     virQEMUCapsPtr qemuCaps,
                     const char *xml)
{
    virDomainDefPtr def;

    def = virDomainDefParseString(xml, driver->xmlopt, qemuCaps,
                                  VIR_DOMAIN_DEF_PARSE_INACTIVE |
                                  VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE);

    return def;
}


virDomainDefPtr
qemuDomainDefCopy(virQEMUDriverPtr driver,
                  virQEMUCapsPtr qemuCaps,
                  virDomainDefPtr src,
                  unsigned int flags)
{
    g_autofree char *xml = NULL;

    if (!(xml = qemuDomainDefFormatXML(driver, qemuCaps, src, flags)))
        return NULL;

    return qemuDomainDefFromXML(driver, qemuCaps, xml);
}


int
qemuDomainMakeCPUMigratable(virCPUDefPtr cpu)
{
    if (cpu->mode == VIR_CPU_MODE_CUSTOM &&
        STREQ_NULLABLE(cpu->model, "Icelake-Server")) {
        /* Originally Icelake-Server CPU model contained pconfig CPU feature.
         * It was never actually enabled and thus it was removed. To enable
         * migration to QEMU 3.1.0 (with both new and old libvirt), we
         * explicitly disable pconfig in migration XML (otherwise old libvirt
         * would think it was implicitly enabled on the source). New libvirt
         * will drop it from the XML before starting the domain on new QEMU.
         */
        if (virCPUDefUpdateFeature(cpu, "pconfig", VIR_CPU_FEATURE_DISABLE) < 0)
            return -1;
    }

    return 0;
}


static int
qemuDomainDefFormatBufInternal(virQEMUDriverPtr driver,
                               virQEMUCapsPtr qemuCaps,
                               virDomainDefPtr def,
                               virCPUDefPtr origCPU,
                               unsigned int flags,
                               virBuffer *buf)
{
    int ret = -1;
    virDomainDefPtr copy = NULL;

    virCheckFlags(VIR_DOMAIN_XML_COMMON_FLAGS | VIR_DOMAIN_XML_UPDATE_CPU, -1);

    if (!(flags & (VIR_DOMAIN_XML_UPDATE_CPU | VIR_DOMAIN_XML_MIGRATABLE)))
        goto format;

    if (!(copy = virDomainDefCopy(def, driver->xmlopt, qemuCaps,
                                  flags & VIR_DOMAIN_XML_MIGRATABLE)))
        goto cleanup;

    def = copy;

    /* Update guest CPU requirements according to host CPU */
    if ((flags & VIR_DOMAIN_XML_UPDATE_CPU) &&
        def->cpu &&
        (def->cpu->mode != VIR_CPU_MODE_CUSTOM ||
         def->cpu->model)) {
        g_autoptr(virQEMUCaps) qCaps = NULL;

        if (qemuCaps) {
            qCaps = virObjectRef(qemuCaps);
        } else {
            if (!(qCaps = virQEMUCapsCacheLookupCopy(driver->qemuCapsCache,
                                                     def->virtType,
                                                     def->emulator,
                                                     def->os.machine)))
                goto cleanup;
        }

        if (virCPUUpdate(def->os.arch, def->cpu,
                         virQEMUCapsGetHostModel(qCaps, def->virtType,
                                                 VIR_QEMU_CAPS_HOST_CPU_MIGRATABLE)) < 0)
            goto cleanup;
    }

    if ((flags & VIR_DOMAIN_XML_MIGRATABLE)) {
        size_t i;
        int toremove = 0;
        virDomainControllerDefPtr usb = NULL, pci = NULL;

        /* If only the default USB controller is present, we can remove it
         * and make the XML compatible with older versions of libvirt which
         * didn't support USB controllers in the XML but always added the
         * default one to qemu anyway.
         */
        for (i = 0; i < def->ncontrollers; i++) {
            if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_USB) {
                if (usb) {
                    usb = NULL;
                    break;
                }
                usb = def->controllers[i];
            }
        }

        /* In order to maintain compatibility with version of libvirt that
         * didn't support <controller type='usb'/> (<= 0.9.4), we need to
         * drop the default USB controller, ie. a USB controller at index
         * zero with no model or with the default piix3-ohci model.
         *
         * However, we only need to do so for x86 i440fx machine types,
         * because other architectures and machine types were introduced
         * when libvirt already supported <controller type='usb'/>.
         */
        if (qemuDomainIsI440FX(def) &&
            usb && usb->idx == 0 &&
            (usb->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_DEFAULT ||
             usb->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX3_UHCI) &&
            !virDomainDeviceAliasIsUserAlias(usb->info.alias)) {
            VIR_DEBUG("Removing default USB controller from domain '%s'"
                      " for migration compatibility", def->name);
            toremove++;
        } else {
            usb = NULL;
        }

        /* Remove the default PCI controller if there is only one present
         * and its model is pci-root */
        for (i = 0; i < def->ncontrollers; i++) {
            if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI) {
                if (pci) {
                    pci = NULL;
                    break;
                }
                pci = def->controllers[i];
            }
        }

        if (pci && pci->idx == 0 &&
            pci->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT &&
            !virDomainDeviceAliasIsUserAlias(pci->info.alias) &&
            !pci->opts.pciopts.pcihole64) {
            VIR_DEBUG("Removing default pci-root from domain '%s'"
                      " for migration compatibility", def->name);
            toremove++;
        } else {
            pci = NULL;
        }

        if (toremove) {
            virDomainControllerDefPtr *controllers = def->controllers;
            int ncontrollers = def->ncontrollers;

            def->controllers = g_new0(virDomainControllerDefPtr, ncontrollers - toremove);
            def->ncontrollers = 0;

            for (i = 0; i < ncontrollers; i++) {
                if (controllers[i] != usb && controllers[i] != pci)
                    def->controllers[def->ncontrollers++] = controllers[i];
            }

            VIR_FREE(controllers);
            virDomainControllerDefFree(pci);
            virDomainControllerDefFree(usb);
        }

        /* Remove the panic device for selected models if present */
        for (i = 0; i < def->npanics; i++) {
            if (def->panics[i]->model == VIR_DOMAIN_PANIC_MODEL_S390 ||
                def->panics[i]->model == VIR_DOMAIN_PANIC_MODEL_PSERIES) {
                VIR_DELETE_ELEMENT(def->panics, i, def->npanics);
                break;
            }
        }

        for (i = 0; i < def->nchannels; i++)
            qemuDomainChrDefDropDefaultPath(def->channels[i], driver);

        for (i = 0; i < def->nserials; i++) {
            virDomainChrDefPtr serial = def->serials[i];

            /* Historically, the native console type for some machine types
             * was not set at all, which means it defaulted to ISA even
             * though that was not even remotely accurate. To ensure migration
             * towards older libvirt versions works for such guests, we switch
             * it back to the default here */
            if (flags & VIR_DOMAIN_XML_MIGRATABLE) {
                switch ((virDomainChrSerialTargetType)serial->targetType) {
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM:
                    serial->targetType = VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE;
                    serial->targetModel = VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_NONE;
                    break;
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_USB:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE:
                case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_LAST:
                    /* Nothing to do */
                    break;
                }
            }
        }

        /* Replace the CPU definition updated according to QEMU with the one
         * used for starting the domain. The updated def will be sent
         * separately for backward compatibility.
         */
        if (origCPU) {
            virCPUDefFree(def->cpu);
            if (!(def->cpu = virCPUDefCopy(origCPU)))
                goto cleanup;
        }

        if (def->cpu && qemuDomainMakeCPUMigratable(def->cpu) < 0)
            goto cleanup;
    }

 format:
    ret = virDomainDefFormatInternal(def, driver->xmlopt, buf,
                                     virDomainDefFormatConvertXMLFlags(flags));

 cleanup:
    virDomainDefFree(copy);
    return ret;
}


int
qemuDomainDefFormatBuf(virQEMUDriverPtr driver,
                       virQEMUCapsPtr qemuCaps,
                       virDomainDefPtr def,
                       unsigned int flags,
                       virBufferPtr buf)
{
    return qemuDomainDefFormatBufInternal(driver, qemuCaps, def, NULL, flags, buf);
}


static char *
qemuDomainDefFormatXMLInternal(virQEMUDriverPtr driver,
                               virQEMUCapsPtr qemuCaps,
                               virDomainDefPtr def,
                               virCPUDefPtr origCPU,
                               unsigned int flags)
{
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    if (qemuDomainDefFormatBufInternal(driver, qemuCaps, def, origCPU, flags, &buf) < 0)
        return NULL;

    return virBufferContentAndReset(&buf);
}


char *
qemuDomainDefFormatXML(virQEMUDriverPtr driver,
                       virQEMUCapsPtr qemuCaps,
                       virDomainDefPtr def,
                       unsigned int flags)
{
    return qemuDomainDefFormatXMLInternal(driver, qemuCaps, def, NULL, flags);
}


char *qemuDomainFormatXML(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          unsigned int flags)
{
    virDomainDefPtr def;
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virCPUDefPtr origCPU = NULL;

    if ((flags & VIR_DOMAIN_XML_INACTIVE) && vm->newDef) {
        def = vm->newDef;
    } else {
        def = vm->def;
        origCPU = priv->origCPU;
    }

    return qemuDomainDefFormatXMLInternal(driver, priv->qemuCaps, def, origCPU, flags);
}

char *
qemuDomainDefFormatLive(virQEMUDriverPtr driver,
                        virQEMUCapsPtr qemuCaps,
                        virDomainDefPtr def,
                        virCPUDefPtr origCPU,
                        bool inactive,
                        bool compatible)
{
    unsigned int flags = QEMU_DOMAIN_FORMAT_LIVE_FLAGS;

    if (inactive)
        flags |= VIR_DOMAIN_XML_INACTIVE;
    if (compatible)
        flags |= VIR_DOMAIN_XML_MIGRATABLE;

    return qemuDomainDefFormatXMLInternal(driver, qemuCaps, def, origCPU, flags);
}


void qemuDomainObjTaint(virQEMUDriverPtr driver,
                        virDomainObjPtr obj,
                        virDomainTaintFlags taint,
                        qemuDomainLogContextPtr logCtxt)
{
    virErrorPtr orig_err = NULL;
    g_autofree char *timestamp = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    int rc;

    if (!virDomainObjTaint(obj, taint))
        return;

    virUUIDFormat(obj->def->uuid, uuidstr);

    VIR_WARN("Domain id=%d name='%s' uuid=%s is tainted: %s",
             obj->def->id,
             obj->def->name,
             uuidstr,
             virDomainTaintTypeToString(taint));

    /* We don't care about errors logging taint info, so
     * preserve original error, and clear any error that
     * is raised */
    virErrorPreserveLast(&orig_err);

    if (!(timestamp = virTimeStringNow()))
        goto cleanup;

    if (logCtxt) {
        rc = qemuDomainLogContextWrite(logCtxt,
                                       "%s: Domain id=%d is tainted: %s\n",
                                       timestamp,
                                       obj->def->id,
                                       virDomainTaintTypeToString(taint));
    } else {
        rc = qemuDomainLogAppendMessage(driver, obj,
                                        "%s: Domain id=%d is tainted: %s\n",
                                        timestamp,
                                        obj->def->id,
                                        virDomainTaintTypeToString(taint));
    }

    if (rc < 0)
        virResetLastError();

 cleanup:
    virErrorRestore(&orig_err);
}


void qemuDomainObjCheckTaint(virQEMUDriverPtr driver,
                             virDomainObjPtr obj,
                             qemuDomainLogContextPtr logCtxt,
                             bool incomingMigration)
{
    size_t i;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    qemuDomainObjPrivatePtr priv = obj->privateData;
    bool custom_hypervisor_feat = false;

    if (driver->privileged &&
        (cfg->user == 0 ||
         cfg->group == 0))
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_HIGH_PRIVILEGES, logCtxt);

    if (priv->hookRun)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_HOOK, logCtxt);

    if (obj->def->namespaceData) {
        qemuDomainXmlNsDefPtr qemuxmlns = obj->def->namespaceData;
        if (qemuxmlns->num_args || qemuxmlns->num_env)
            qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_CUSTOM_ARGV, logCtxt);
        if (qemuxmlns->ncapsadd > 0 || qemuxmlns->ncapsdel > 0)
            custom_hypervisor_feat = true;
    }

    if (custom_hypervisor_feat ||
        (cfg->capabilityfilters && *cfg->capabilityfilters)) {
        qemuDomainObjTaint(driver, obj,
                           VIR_DOMAIN_TAINT_CUSTOM_HYPERVISOR_FEATURE, logCtxt);
    }

    if (obj->def->cpu &&
        obj->def->cpu->mode == VIR_CPU_MODE_HOST_PASSTHROUGH &&
        incomingMigration)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_HOST_CPU, logCtxt);

    for (i = 0; i < obj->def->ndisks; i++)
        qemuDomainObjCheckDiskTaint(driver, obj, obj->def->disks[i], logCtxt);

    for (i = 0; i < obj->def->nhostdevs; i++)
        qemuDomainObjCheckHostdevTaint(driver, obj, obj->def->hostdevs[i],
                                       logCtxt);

    for (i = 0; i < obj->def->nnets; i++)
        qemuDomainObjCheckNetTaint(driver, obj, obj->def->nets[i], logCtxt);

    if (obj->def->os.dtb)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_CUSTOM_DTB, logCtxt);
}


void qemuDomainObjCheckDiskTaint(virQEMUDriverPtr driver,
                                 virDomainObjPtr obj,
                                 virDomainDiskDefPtr disk,
                                 qemuDomainLogContextPtr logCtxt)
{
    if (disk->rawio == VIR_TRISTATE_BOOL_YES)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_HIGH_PRIVILEGES,
                           logCtxt);

    if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM &&
        virStorageSourceGetActualType(disk->src) == VIR_STORAGE_TYPE_BLOCK &&
        disk->src->path && virFileIsCDROM(disk->src->path) == 1)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_CDROM_PASSTHROUGH,
                           logCtxt);
}


void qemuDomainObjCheckHostdevTaint(virQEMUDriverPtr driver,
                                    virDomainObjPtr obj,
                                    virDomainHostdevDefPtr hostdev,
                                    qemuDomainLogContextPtr logCtxt)
{
    if (!virHostdevIsSCSIDevice(hostdev))
        return;

    if (hostdev->source.subsys.u.scsi.rawio == VIR_TRISTATE_BOOL_YES)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_HIGH_PRIVILEGES, logCtxt);
}


void qemuDomainObjCheckNetTaint(virQEMUDriverPtr driver,
                                virDomainObjPtr obj,
                                virDomainNetDefPtr net,
                                qemuDomainLogContextPtr logCtxt)
{
    /* script is only useful for NET_TYPE_ETHERNET (qemu) and
     * NET_TYPE_BRIDGE (xen), but could be (incorrectly) specified for
     * any interface type. In any case, it's adding user sauce into
     * the soup, so it should taint the domain.
     */
    if (net->script != NULL)
        qemuDomainObjTaint(driver, obj, VIR_DOMAIN_TAINT_SHELL_SCRIPTS, logCtxt);
}


qemuDomainLogContextPtr qemuDomainLogContextNew(virQEMUDriverPtr driver,
                                                virDomainObjPtr vm,
                                                qemuDomainLogContextMode mode)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    qemuDomainLogContextPtr ctxt = QEMU_DOMAIN_LOG_CONTEXT(g_object_new(QEMU_TYPE_DOMAIN_LOG_CONTEXT, NULL));

    VIR_DEBUG("Context new %p stdioLogD=%d", ctxt, cfg->stdioLogD);
    ctxt->writefd = -1;
    ctxt->readfd = -1;

    ctxt->path = g_strdup_printf("%s/%s.log", cfg->logDir, vm->def->name);

    if (cfg->stdioLogD) {
        ctxt->manager = virLogManagerNew(driver->privileged);
        if (!ctxt->manager)
            goto error;

        ctxt->writefd = virLogManagerDomainOpenLogFile(ctxt->manager,
                                                       "qemu",
                                                       vm->def->uuid,
                                                       vm->def->name,
                                                       ctxt->path,
                                                       0,
                                                       &ctxt->inode,
                                                       &ctxt->pos);
        if (ctxt->writefd < 0)
            goto error;
    } else {
        if ((ctxt->writefd = open(ctxt->path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR)) < 0) {
            virReportSystemError(errno, _("failed to create logfile %s"),
                                 ctxt->path);
            goto error;
        }
        if (virSetCloseExec(ctxt->writefd) < 0) {
            virReportSystemError(errno, _("failed to set close-on-exec flag on %s"),
                                 ctxt->path);
            goto error;
        }

        /* For unprivileged startup we must truncate the file since
         * we can't rely on logrotate. We don't use O_TRUNC since
         * it is better for SELinux policy if we truncate afterwards */
        if (mode == QEMU_DOMAIN_LOG_CONTEXT_MODE_START &&
            !driver->privileged &&
            ftruncate(ctxt->writefd, 0) < 0) {
            virReportSystemError(errno, _("failed to truncate %s"),
                                 ctxt->path);
            goto error;
        }

        if (mode == QEMU_DOMAIN_LOG_CONTEXT_MODE_START) {
            if ((ctxt->readfd = open(ctxt->path, O_RDONLY)) < 0) {
                virReportSystemError(errno, _("failed to open logfile %s"),
                                     ctxt->path);
                goto error;
            }
            if (virSetCloseExec(ctxt->readfd) < 0) {
                virReportSystemError(errno, _("failed to set close-on-exec flag on %s"),
                                     ctxt->path);
                goto error;
            }
        }

        if ((ctxt->pos = lseek(ctxt->writefd, 0, SEEK_END)) < 0) {
            virReportSystemError(errno, _("failed to seek in log file %s"),
                                 ctxt->path);
            goto error;
        }
    }

    return ctxt;

 error:
    g_clear_object(&ctxt);
    return NULL;
}


int qemuDomainLogContextWrite(qemuDomainLogContextPtr ctxt,
                              const char *fmt, ...)
{
    va_list argptr;
    g_autofree char *message = NULL;
    int ret = -1;

    va_start(argptr, fmt);

    message = g_strdup_vprintf(fmt, argptr);
    if (!ctxt->manager &&
        lseek(ctxt->writefd, 0, SEEK_END) < 0) {
        virReportSystemError(errno, "%s",
                             _("Unable to seek to end of domain logfile"));
        goto cleanup;
    }
    if (safewrite(ctxt->writefd, message, strlen(message)) < 0) {
        virReportSystemError(errno, "%s",
                             _("Unable to write to domain logfile"));
        goto cleanup;
    }

    ret = 0;

 cleanup:
    va_end(argptr);
    return ret;
}


ssize_t qemuDomainLogContextRead(qemuDomainLogContextPtr ctxt,
                                 char **msg)
{
    char *buf;
    size_t buflen;

    VIR_DEBUG("Context read %p manager=%p inode=%llu pos=%llu",
              ctxt, ctxt->manager,
              (unsigned long long)ctxt->inode,
              (unsigned long long)ctxt->pos);

    if (ctxt->manager) {
        buf = virLogManagerDomainReadLogFile(ctxt->manager,
                                             ctxt->path,
                                             ctxt->inode,
                                             ctxt->pos,
                                             1024 * 128,
                                             0);
        if (!buf)
            return -1;
        buflen = strlen(buf);
    } else {
        ssize_t got;

        buflen = 1024 * 128;

        /* Best effort jump to start of messages */
        ignore_value(lseek(ctxt->readfd, ctxt->pos, SEEK_SET));

        buf = g_new0(char, buflen);

        got = saferead(ctxt->readfd, buf, buflen - 1);
        if (got < 0) {
            VIR_FREE(buf);
            virReportSystemError(errno, "%s",
                                 _("Unable to read from log file"));
            return -1;
        }

        buf[got] = '\0';

        buf = g_renew(char, buf, got + 1);
        buflen = got;
    }

    *msg = buf;

    return buflen;
}


/**
 * qemuDomainLogAppendMessage:
 *
 * This is a best-effort attempt to add a log message to the qemu log file
 * either by using virtlogd or the legacy approach */
int
qemuDomainLogAppendMessage(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           const char *fmt,
                           ...)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    virLogManagerPtr manager = NULL;
    va_list ap;
    g_autofree char *path = NULL;
    int writefd = -1;
    g_autofree char *message = NULL;
    int ret = -1;

    va_start(ap, fmt);

    message = g_strdup_vprintf(fmt, ap);

    VIR_DEBUG("Append log message (vm='%s' message='%s) stdioLogD=%d",
              vm->def->name, message, cfg->stdioLogD);

    path = g_strdup_printf("%s/%s.log", cfg->logDir, vm->def->name);

    if (cfg->stdioLogD) {
        if (!(manager = virLogManagerNew(driver->privileged)))
            goto cleanup;

        if (virLogManagerDomainAppendMessage(manager, "qemu", vm->def->uuid,
                                             vm->def->name, path, message, 0) < 0)
            goto cleanup;
    } else {
        if ((writefd = open(path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR)) < 0) {
            virReportSystemError(errno, _("failed to create logfile %s"),
                                 path);
            goto cleanup;
        }

        if (safewrite(writefd, message, strlen(message)) < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    va_end(ap);
    VIR_FORCE_CLOSE(writefd);
    virLogManagerFree(manager);

    return ret;
}


int qemuDomainLogContextGetWriteFD(qemuDomainLogContextPtr ctxt)
{
    return ctxt->writefd;
}


void qemuDomainLogContextMarkPosition(qemuDomainLogContextPtr ctxt)
{
    if (ctxt->manager)
        virLogManagerDomainGetLogFilePosition(ctxt->manager,
                                              ctxt->path,
                                              0,
                                              &ctxt->inode,
                                              &ctxt->pos);
    else
        ctxt->pos = lseek(ctxt->writefd, 0, SEEK_END);
}


virLogManagerPtr qemuDomainLogContextGetManager(qemuDomainLogContextPtr ctxt)
{
    return ctxt->manager;
}


/* Locate an appropriate 'qemu-img' binary.  */
const char *
qemuFindQemuImgBinary(virQEMUDriverPtr driver)
{
    if (!driver->qemuImgBinary)
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("unable to find qemu-img"));

    return driver->qemuImgBinary;
}

int
qemuDomainSnapshotWriteMetadata(virDomainObjPtr vm,
                                virDomainMomentObjPtr snapshot,
                                virDomainXMLOptionPtr xmlopt,
                                const char *snapshotDir)
{
    g_autofree char *newxml = NULL;
    g_autofree char *snapDir = NULL;
    g_autofree char *snapFile = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    unsigned int flags = VIR_DOMAIN_SNAPSHOT_FORMAT_SECURE |
        VIR_DOMAIN_SNAPSHOT_FORMAT_INTERNAL;
    virDomainSnapshotDefPtr def = virDomainSnapshotObjGetDef(snapshot);

    if (virDomainSnapshotGetCurrent(vm->snapshots) == snapshot)
        flags |= VIR_DOMAIN_SNAPSHOT_FORMAT_CURRENT;
    virUUIDFormat(vm->def->uuid, uuidstr);
    newxml = virDomainSnapshotDefFormat(uuidstr, def, xmlopt, flags);
    if (newxml == NULL)
        return -1;

    snapDir = g_strdup_printf("%s/%s", snapshotDir, vm->def->name);
    if (virFileMakePath(snapDir) < 0) {
        virReportSystemError(errno, _("cannot create snapshot directory '%s'"),
                             snapDir);
        return -1;
    }

    snapFile = g_strdup_printf("%s/%s.xml", snapDir, def->parent.name);

    return virXMLSaveFile(snapFile, NULL, "snapshot-edit", newxml);
}


/* The domain is expected to be locked and inactive. Return -1 on normal
 * failure, 1 if we skipped a disk due to try_all.  */
static int
qemuDomainSnapshotForEachQcow2Raw(virQEMUDriverPtr driver,
                                  virDomainDefPtr def,
                                  const char *name,
                                  const char *op,
                                  bool try_all,
                                  int ndisks)
{
    const char *qemuimgbin;
    size_t i;
    bool skipped = false;

    qemuimgbin = qemuFindQemuImgBinary(driver);
    if (qemuimgbin == NULL) {
        /* qemuFindQemuImgBinary set the error */
        return -1;
    }

    for (i = 0; i < ndisks; i++) {
        g_autoptr(virCommand) cmd = virCommandNewArgList(qemuimgbin, "snapshot",
                                                         op, name, NULL);

        /* FIXME: we also need to handle LVM here */
        if (def->disks[i]->device == VIR_DOMAIN_DISK_DEVICE_DISK) {
            int format = virDomainDiskGetFormat(def->disks[i]);

            if (format > 0 && format != VIR_STORAGE_FILE_QCOW2) {
                if (try_all) {
                    /* Continue on even in the face of error, since other
                     * disks in this VM may have the same snapshot name.
                     */
                    VIR_WARN("skipping snapshot action on %s",
                             def->disks[i]->dst);
                    skipped = true;
                    continue;
                } else if (STREQ(op, "-c") && i) {
                    /* We must roll back partial creation by deleting
                     * all earlier snapshots.  */
                    qemuDomainSnapshotForEachQcow2Raw(driver, def, name,
                                                      "-d", false, i);
                }
                virReportError(VIR_ERR_OPERATION_INVALID,
                               _("Disk device '%s' does not support"
                                 " snapshotting"),
                               def->disks[i]->dst);
                return -1;
            }

            virCommandAddArg(cmd, virDomainDiskGetSource(def->disks[i]));

            if (virCommandRun(cmd, NULL) < 0) {
                if (try_all) {
                    VIR_WARN("skipping snapshot action on %s",
                             def->disks[i]->dst);
                    skipped = true;
                    continue;
                } else if (STREQ(op, "-c") && i) {
                    /* We must roll back partial creation by deleting
                     * all earlier snapshots.  */
                    qemuDomainSnapshotForEachQcow2Raw(driver, def, name,
                                                      "-d", false, i);
                }
                return -1;
            }
        }
    }

    return skipped ? 1 : 0;
}

/* The domain is expected to be locked and inactive. Return -1 on normal
 * failure, 1 if we skipped a disk due to try_all.  */
int
qemuDomainSnapshotForEachQcow2(virQEMUDriverPtr driver,
                               virDomainObjPtr vm,
                               virDomainMomentObjPtr snap,
                               const char *op,
                               bool try_all)
{
    /* Prefer action on the disks in use at the time the snapshot was
     * created; but fall back to current definition if dealing with a
     * snapshot created prior to libvirt 0.9.5.  */
    virDomainDefPtr def = snap->def->dom;

    if (!def)
        def = vm->def;
    return qemuDomainSnapshotForEachQcow2Raw(driver, def, snap->def->name,
                                             op, try_all, def->ndisks);
}

/* Discard one snapshot (or its metadata), without reparenting any children.  */
int
qemuDomainSnapshotDiscard(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          virDomainMomentObjPtr snap,
                          bool update_parent,
                          bool metadata_only)
{
    g_autofree char *snapFile = NULL;
    qemuDomainObjPrivatePtr priv;
    virDomainMomentObjPtr parentsnap = NULL;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);

    if (!metadata_only) {
        if (!virDomainObjIsActive(vm)) {
            /* Ignore any skipped disks */
            if (qemuDomainSnapshotForEachQcow2(driver, vm, snap, "-d",
                                               true) < 0)
                return -1;
        } else {
            priv = vm->privateData;
            qemuDomainObjEnterMonitor(driver, vm);
            /* we continue on even in the face of error */
            qemuMonitorDeleteSnapshot(priv->mon, snap->def->name);
            ignore_value(qemuDomainObjExitMonitor(driver, vm));
        }
    }

    snapFile = g_strdup_printf("%s/%s/%s.xml", cfg->snapshotDir, vm->def->name,
                               snap->def->name);

    if (snap == virDomainSnapshotGetCurrent(vm->snapshots)) {
        virDomainSnapshotSetCurrent(vm->snapshots, NULL);
        if (update_parent && snap->def->parent_name) {
            parentsnap = virDomainSnapshotFindByName(vm->snapshots,
                                                     snap->def->parent_name);
            if (!parentsnap) {
                VIR_WARN("missing parent snapshot matching name '%s'",
                         snap->def->parent_name);
            } else {
                virDomainSnapshotSetCurrent(vm->snapshots, parentsnap);
                if (qemuDomainSnapshotWriteMetadata(vm, parentsnap,
                                                    driver->xmlopt,
                                                    cfg->snapshotDir) < 0) {
                    VIR_WARN("failed to set parent snapshot '%s' as current",
                             snap->def->parent_name);
                    virDomainSnapshotSetCurrent(vm->snapshots, NULL);
                }
            }
        }
    }

    if (unlink(snapFile) < 0)
        VIR_WARN("Failed to unlink %s", snapFile);
    if (update_parent)
        virDomainMomentDropParent(snap);
    virDomainSnapshotObjListRemove(vm->snapshots, snap);

    return 0;
}

/* Hash iterator callback to discard multiple snapshots.  */
int qemuDomainMomentDiscardAll(void *payload,
                               const char *name G_GNUC_UNUSED,
                               void *data)
{
    virDomainMomentObjPtr moment = payload;
    virQEMUMomentRemovePtr curr = data;
    int err;

    if (!curr->found && curr->current == moment)
        curr->found = true;
    err = curr->momentDiscard(curr->driver, curr->vm, moment, false,
                              curr->metadata_only);
    if (err && !curr->err)
        curr->err = err;
    return 0;
}

int
qemuDomainSnapshotDiscardAllMetadata(virQEMUDriverPtr driver,
                                     virDomainObjPtr vm)
{
    virQEMUMomentRemove rem = {
        .driver = driver,
        .vm = vm,
        .metadata_only = true,
        .momentDiscard = qemuDomainSnapshotDiscard,
    };

    virDomainSnapshotForEach(vm->snapshots, qemuDomainMomentDiscardAll, &rem);
    virDomainSnapshotObjListRemoveAll(vm->snapshots);

    return rem.err;
}


static void
qemuDomainRemoveInactiveCommon(virQEMUDriverPtr driver,
                               virDomainObjPtr vm)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autofree char *snapDir = NULL;
    g_autofree char *chkDir = NULL;

    /* Remove any snapshot metadata prior to removing the domain */
    if (qemuDomainSnapshotDiscardAllMetadata(driver, vm) < 0) {
        VIR_WARN("unable to remove all snapshots for domain %s",
                 vm->def->name);
    } else {
        snapDir = g_strdup_printf("%s/%s", cfg->snapshotDir, vm->def->name);

        if (rmdir(snapDir) < 0 && errno != ENOENT)
            VIR_WARN("unable to remove snapshot directory %s", snapDir);
    }
    /* Remove any checkpoint metadata prior to removing the domain */
    if (qemuCheckpointDiscardAllMetadata(driver, vm) < 0) {
        VIR_WARN("unable to remove all checkpoints for domain %s",
                 vm->def->name);
    } else {
        chkDir = g_strdup_printf("%s/%s", cfg->checkpointDir,
                                 vm->def->name);
        if (rmdir(chkDir) < 0 && errno != ENOENT)
            VIR_WARN("unable to remove checkpoint directory %s", chkDir);
    }
    qemuExtDevicesCleanupHost(driver, vm->def);
}


/**
 * qemuDomainRemoveInactive:
 *
 * The caller must hold a lock to the vm.
 */
void
qemuDomainRemoveInactive(virQEMUDriverPtr driver,
                         virDomainObjPtr vm)
{
    if (vm->persistent) {
        /* Short-circuit, we don't want to remove a persistent domain */
        return;
    }

    qemuDomainRemoveInactiveCommon(driver, vm);

    virDomainObjListRemove(driver->domains, vm);
}


/**
 * qemuDomainRemoveInactiveLocked:
 *
 * The caller must hold a lock to the vm and must hold the
 * lock on driver->domains in order to call the remove obj
 * from locked list method.
 */
static void
qemuDomainRemoveInactiveLocked(virQEMUDriverPtr driver,
                               virDomainObjPtr vm)
{
    if (vm->persistent) {
        /* Short-circuit, we don't want to remove a persistent domain */
        return;
    }

    qemuDomainRemoveInactiveCommon(driver, vm);

    virDomainObjListRemoveLocked(driver->domains, vm);
}

/**
 * qemuDomainRemoveInactiveJob:
 *
 * Just like qemuDomainRemoveInactive but it tries to grab a
 * QEMU_JOB_MODIFY first. Even though it doesn't succeed in
 * grabbing the job the control carries with
 * qemuDomainRemoveInactive call.
 */
void
qemuDomainRemoveInactiveJob(virQEMUDriverPtr driver,
                            virDomainObjPtr vm)
{
    bool haveJob;

    haveJob = qemuDomainObjBeginJob(driver, vm, QEMU_JOB_MODIFY) >= 0;

    qemuDomainRemoveInactive(driver, vm);

    if (haveJob)
        qemuDomainObjEndJob(driver, vm);
}


/**
 * qemuDomainRemoveInactiveJobLocked:
 *
 * Similar to qemuDomainRemoveInactiveJob, except that the caller must
 * also hold the lock @driver->domains
 */
void
qemuDomainRemoveInactiveJobLocked(virQEMUDriverPtr driver,
                                  virDomainObjPtr vm)
{
    bool haveJob;

    haveJob = qemuDomainObjBeginJob(driver, vm, QEMU_JOB_MODIFY) >= 0;

    qemuDomainRemoveInactiveLocked(driver, vm);

    if (haveJob)
        qemuDomainObjEndJob(driver, vm);
}


void
qemuDomainSetFakeReboot(virQEMUDriverPtr driver,
                        virDomainObjPtr vm,
                        bool value)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);

    if (priv->fakeReboot == value)
        return;

    priv->fakeReboot = value;

    if (virDomainObjSave(vm, driver->xmlopt, cfg->stateDir) < 0)
        VIR_WARN("Failed to save status on vm %s", vm->def->name);
}

static void
qemuDomainCheckRemoveOptionalDisk(virQEMUDriverPtr driver,
                                  virDomainObjPtr vm,
                                  size_t diskIndex)
{
    char uuid[VIR_UUID_STRING_BUFLEN];
    virObjectEventPtr event = NULL;
    virDomainDiskDefPtr disk = vm->def->disks[diskIndex];
    const char *src = virDomainDiskGetSource(disk);

    virUUIDFormat(vm->def->uuid, uuid);

    VIR_DEBUG("Dropping disk '%s' on domain '%s' (UUID '%s') "
              "due to inaccessible source '%s'",
              disk->dst, vm->def->name, uuid, src);

    if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM ||
        disk->device == VIR_DOMAIN_DISK_DEVICE_FLOPPY) {

        event = virDomainEventDiskChangeNewFromObj(vm, src, NULL,
                                                   disk->info.alias,
                                                   VIR_DOMAIN_EVENT_DISK_CHANGE_MISSING_ON_START);
        virDomainDiskEmptySource(disk);
        /* keeping the old startup policy would be invalid for new images */
        disk->startupPolicy = VIR_DOMAIN_STARTUP_POLICY_DEFAULT;
    } else {
        event = virDomainEventDiskChangeNewFromObj(vm, src, NULL,
                                                   disk->info.alias,
                                                   VIR_DOMAIN_EVENT_DISK_DROP_MISSING_ON_START);
        virDomainDiskRemove(vm->def, diskIndex);
        virDomainDiskDefFree(disk);
    }

    virObjectEventStateQueue(driver->domainEventState, event);
}


/**
 * qemuDomainCheckDiskStartupPolicy:
 * @driver: qemu driver object
 * @vm: domain object
 * @disk: index of disk to check
 * @cold_boot: true if a new VM is being started
 *
 * This function should be called when the source storage for a disk device is
 * missing. The function checks whether the startup policy for the disk allows
 * removal of the source (or disk) according to the state of the VM.
 *
 * The function returns 0 if the source or disk was dropped and -1 if the state
 * of the VM does not allow this. This function does not report errors, but
 * clears any reported error if 0 is returned.
 */
int
qemuDomainCheckDiskStartupPolicy(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 size_t diskIndex,
                                 bool cold_boot)
{
    int startupPolicy = vm->def->disks[diskIndex]->startupPolicy;
    int device = vm->def->disks[diskIndex]->device;

    switch ((virDomainStartupPolicy) startupPolicy) {
        case VIR_DOMAIN_STARTUP_POLICY_OPTIONAL:
            /* Once started with an optional disk, qemu saves its section
             * in the migration stream, so later, when restoring from it
             * we must make sure the sections match. */
            if (!cold_boot &&
                device != VIR_DOMAIN_DISK_DEVICE_FLOPPY &&
                device != VIR_DOMAIN_DISK_DEVICE_CDROM)
                return -1;
            break;

        case VIR_DOMAIN_STARTUP_POLICY_DEFAULT:
        case VIR_DOMAIN_STARTUP_POLICY_MANDATORY:
            return -1;

        case VIR_DOMAIN_STARTUP_POLICY_REQUISITE:
            if (cold_boot)
                return -1;
            break;

        case VIR_DOMAIN_STARTUP_POLICY_LAST:
            /* this should never happen */
            break;
    }

    qemuDomainCheckRemoveOptionalDisk(driver, vm, diskIndex);
    virResetLastError();
    return 0;
}



/*
 * The vm must be locked when any of the following cleanup functions is
 * called.
 */
int
qemuDomainCleanupAdd(virDomainObjPtr vm,
                     qemuDomainCleanupCallback cb)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    size_t i;

    VIR_DEBUG("vm=%s, cb=%p", vm->def->name, cb);

    for (i = 0; i < priv->ncleanupCallbacks; i++) {
        if (priv->cleanupCallbacks[i] == cb)
            return 0;
    }

    if (VIR_RESIZE_N(priv->cleanupCallbacks,
                     priv->ncleanupCallbacks_max,
                     priv->ncleanupCallbacks, 1) < 0)
        return -1;

    priv->cleanupCallbacks[priv->ncleanupCallbacks++] = cb;
    return 0;
}

void
qemuDomainCleanupRemove(virDomainObjPtr vm,
                        qemuDomainCleanupCallback cb)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    size_t i;

    VIR_DEBUG("vm=%s, cb=%p", vm->def->name, cb);

    for (i = 0; i < priv->ncleanupCallbacks; i++) {
        if (priv->cleanupCallbacks[i] == cb)
            VIR_DELETE_ELEMENT_INPLACE(priv->cleanupCallbacks,
                                       i, priv->ncleanupCallbacks);
    }

    VIR_SHRINK_N(priv->cleanupCallbacks,
                 priv->ncleanupCallbacks_max,
                 priv->ncleanupCallbacks_max - priv->ncleanupCallbacks);
}

void
qemuDomainCleanupRun(virQEMUDriverPtr driver,
                     virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    VIR_DEBUG("driver=%p, vm=%s", driver, vm->def->name);

    /* run cleanup callbacks in reverse order */
    while (priv->ncleanupCallbacks)
        priv->cleanupCallbacks[--priv->ncleanupCallbacks](driver, vm);

    VIR_FREE(priv->cleanupCallbacks);
    priv->ncleanupCallbacks_max = 0;
}

void
qemuDomainGetImageIds(virQEMUDriverConfigPtr cfg,
                      virDomainObjPtr vm,
                      virStorageSourcePtr src,
                      virStorageSourcePtr parentSrc,
                      uid_t *uid, gid_t *gid)
{
    virSecurityLabelDefPtr vmlabel;
    virSecurityDeviceLabelDefPtr disklabel;

    if (uid)
        *uid = -1;
    if (gid)
        *gid = -1;

    if (cfg) {
        if (uid)
            *uid = cfg->user;

        if (gid)
            *gid = cfg->group;
    }

    if (vm && (vmlabel = virDomainDefGetSecurityLabelDef(vm->def, "dac")) &&
        vmlabel->label)
        virParseOwnershipIds(vmlabel->label, uid, gid);

    if (parentSrc &&
        (disklabel = virStorageSourceGetSecurityLabelDef(parentSrc, "dac")) &&
        disklabel->label)
        virParseOwnershipIds(disklabel->label, uid, gid);

    if ((disklabel = virStorageSourceGetSecurityLabelDef(src, "dac")) &&
        disklabel->label)
        virParseOwnershipIds(disklabel->label, uid, gid);
}


int
qemuDomainStorageFileInit(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          virStorageSourcePtr src,
                          virStorageSourcePtr parent)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    uid_t uid;
    gid_t gid;

    qemuDomainGetImageIds(cfg, vm, src, parent, &uid, &gid);

    if (virStorageFileInitAs(src, uid, gid) < 0)
        return -1;

    return 0;
}


char *
qemuDomainStorageAlias(const char *device, int depth)
{
    char *alias;

    device = qemuAliasDiskDriveSkipPrefix(device);

    if (!depth)
        alias = g_strdup(device);
    else
        alias = g_strdup_printf("%s.%d", device, depth);
    return alias;
}


/**
 * qemuDomainStorageSourceValidateDepth:
 * @src: storage source chain to validate
 * @add: offsets the calculated number of images
 * @diskdst: optional disk target to use in error message
 *
 * The XML parser limits the maximum element nesting to 256 layers. As libvirt
 * reports the chain into the status and in some cases the config XML we must
 * validate that any user-provided chains will not exceed the XML nesting limit
 * when formatted to the XML.
 *
 * This function validates that the storage source chain starting @src is at
 * most 200 layers deep. @add modifies the calculated value to offset the number
 * to allow checking cases when new layers are going to be added to the chain.
 *
 * Returns 0 on success and -1 if the chain is too deep. Error is reported.
 */
int
qemuDomainStorageSourceValidateDepth(virStorageSourcePtr src,
                                     int add,
                                     const char *diskdst)
{
    virStorageSourcePtr n;
    size_t nlayers = 0;

    for (n = src; virStorageSourceIsBacking(n); n = n->backingStore)
        nlayers++;

    nlayers += add;

    if (nlayers > 200) {
        if (diskdst)
            virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                           _("backing chains more than 200 layers deep are not "
                             "supported for disk '%s'"), diskdst);
        else
            virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                           _("backing chains more than 200 layers deep are not "
                             "supported"));

        return -1;
    }

    return 0;
}


/**
 * qemuDomainPrepareStorageSourceConfig:
 * @src: storage source to configure
 * @cfg: qemu driver config object
 * @qemuCaps: capabilities of qemu
 *
 * Set properties of @src based on the qemu driver config @cfg.
 *
 */
static void
qemuDomainPrepareStorageSourceConfig(virStorageSourcePtr src,
                                     virQEMUDriverConfigPtr cfg,
                                     virQEMUCapsPtr qemuCaps)
{
    if (!cfg)
        return;

    if (src->type == VIR_STORAGE_TYPE_NETWORK &&
        src->protocol == VIR_STORAGE_NET_PROTOCOL_GLUSTER &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_GLUSTER_DEBUG_LEVEL)) {
        src->debug = true;
        src->debugLevel = cfg->glusterDebugLevel;
    }
}


/**
 * qemuDomainDetermineDiskChain:
 * @driver: qemu driver object
 * @vm: domain object
 * @disk: disk definition
 * @disksrc: source to determine the chain for, may be NULL
 * @report_broken: report broken chain verbosely
 *
 * Prepares and initializes the backing chain of disk @disk. In cases where
 * a new source is to be associated with @disk the @disksrc parameter can be
 * used to override the source. If @report_broken is true missing images
 * in the backing chain are reported.
 */
int
qemuDomainDetermineDiskChain(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             virDomainDiskDefPtr disk,
                             virStorageSourcePtr disksrc,
                             bool report_broken)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    virStorageSourcePtr src; /* iterator for the backing chain declared in XML */
    virStorageSourcePtr n; /* iterator for the backing chain detected from disk */
    qemuDomainObjPrivatePtr priv = vm->privateData;
    bool blockdev = virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV);
    bool isSD = qemuDiskBusIsSD(disk->bus);
    uid_t uid;
    gid_t gid;

    if (!disksrc)
        disksrc = disk->src;

    if (virStorageSourceIsEmpty(disksrc))
        return 0;

    /* There is no need to check the backing chain for disks without backing
     * support */
    if (virStorageSourceIsLocalStorage(disksrc) &&
        disksrc->format > VIR_STORAGE_FILE_NONE &&
        disksrc->format < VIR_STORAGE_FILE_BACKING) {

        if (!virFileExists(disksrc->path)) {
            if (report_broken)
                virStorageFileReportBrokenChain(errno, disksrc, disksrc);

            return -1;
        }

        /* terminate the chain for such images as the code below would do */
        if (!disksrc->backingStore)
            disksrc->backingStore = virStorageSourceNew();

        /* host cdrom requires special treatment in qemu, so we need to check
         * whether a block device is a cdrom */
        if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM &&
            disksrc->format == VIR_STORAGE_FILE_RAW &&
            virStorageSourceIsBlockLocal(disksrc) &&
            virFileIsCDROM(disksrc->path) == 1)
            disksrc->hostcdrom = true;

        return 0;
    }

    src = disksrc;
    /* skip to the end of the chain if there is any */
    while (virStorageSourceHasBacking(src)) {
        if (report_broken) {
            int rv = virStorageFileSupportsAccess(src);

            if (rv < 0)
                return -1;

            if (rv > 0) {
                if (qemuDomainStorageFileInit(driver, vm, src, disksrc) < 0)
                    return -1;

                if (virStorageFileAccess(src, F_OK) < 0) {
                    virStorageFileReportBrokenChain(errno, src, disksrc);
                    virStorageFileDeinit(src);
                    return -1;
                }

                virStorageFileDeinit(src);
            }
        }
        src = src->backingStore;
    }

    /* We skipped to the end of the chain. Skip detection if there's the
     * terminator. (An allocated but empty backingStore) */
    if (src->backingStore) {
        if (qemuDomainStorageSourceValidateDepth(disksrc, 0, disk->dst) < 0)
            return -1;

        return 0;
    }

    qemuDomainGetImageIds(cfg, vm, src, disksrc, &uid, &gid);

    if (virStorageFileGetMetadata(src, uid, gid, report_broken) < 0)
        return -1;

    for (n = src->backingStore; virStorageSourceIsBacking(n); n = n->backingStore) {
        /* convert detected ISO format to 'raw' as qemu would not understand it */
        if (n->format == VIR_STORAGE_FILE_ISO)
            n->format = VIR_STORAGE_FILE_RAW;

        /* mask-out blockdev for 'sd' disks */
        if (qemuDomainValidateStorageSource(n, priv->qemuCaps, isSD) < 0)
            return -1;

        qemuDomainPrepareStorageSourceConfig(n, cfg, priv->qemuCaps);
        qemuDomainPrepareDiskSourceData(disk, n);

        if (blockdev && !isSD &&
            qemuDomainPrepareStorageSourceBlockdev(disk, n, priv, cfg) < 0)
            return -1;
    }

    if (qemuDomainStorageSourceValidateDepth(disksrc, 0, disk->dst) < 0)
        return -1;

    return 0;
}


/**
 * qemuDomainDiskGetTopNodename:
 *
 * @disk: disk definition object
 *
 * Returns the pointer to the node-name of the topmost layer used by @disk as
 * backend. Currently returns the nodename of the copy-on-read filter if enabled
 * or the nodename of the top image's format driver. Empty disks return NULL.
 * This must be used only when VIR_QEMU_CAPS_BLOCKDEV is enabled.
 */
const char *
qemuDomainDiskGetTopNodename(virDomainDiskDefPtr disk)
{
    qemuDomainDiskPrivatePtr priv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    if (virStorageSourceIsEmpty(disk->src))
        return NULL;

    if (disk->copy_on_read == VIR_TRISTATE_SWITCH_ON)
        return priv->nodeCopyOnRead;

    return disk->src->nodeformat;
}


/**
 * qemuDomainDiskGetBackendAlias:
 * @disk: disk definition
 * @qemuCaps: emulator capabilities
 * @backendAlias: filled with the alias of the disk storage backend
 *
 * Returns the correct alias for the disk backend. This may be the alias of
 * -drive for legacy setup or the correct node name for -blockdev setups.
 *
 * @backendAlias may be NULL on success if the backend does not exist
 * (disk is empty). Caller is responsible for freeing @backendAlias.
 *
 * Returns 0 on success, -1 on error with libvirt error reported.
 */
int
qemuDomainDiskGetBackendAlias(virDomainDiskDefPtr disk,
                              virQEMUCapsPtr qemuCaps,
                              char **backendAlias)
{
    *backendAlias = NULL;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_BLOCKDEV) ||
        qemuDiskBusIsSD(disk->bus)) {
        if (!(*backendAlias = qemuAliasDiskDriveFromDisk(disk)))
            return -1;

        return 0;
    }

    *backendAlias = g_strdup(qemuDomainDiskGetTopNodename(disk));
    return 0;
}


typedef enum {
    /* revoke access to the image instead of allowing it */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_REVOKE = 1 << 0,
    /* operate on full backing chain rather than single image */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN = 1 << 1,
    /* force permissions to read-only/read-write when allowing */
    /* currently does not properly work with QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_ONLY = 1 << 2,
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_WRITE = 1 << 3,
    /* don't revoke permissions when modification has failed */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_SKIP_REVOKE = 1 << 4,
    /* VM already has access to the source and we are just modifying it */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_MODIFY_ACCESS = 1 << 5,
    /* whether the image is the top image of the backing chain (e.g. disk source) */
    QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN_TOP = 1 << 6,
} qemuDomainStorageSourceAccessFlags;


static int
qemuDomainStorageSourceAccessModifyNVMe(virQEMUDriverPtr driver,
                                        virDomainObjPtr vm,
                                        virStorageSourcePtr src,
                                        bool revoke)
{
    bool revoke_maxmemlock = false;
    bool revoke_hostdev = false;
    int ret = -1;

    if (!virStorageSourceChainHasNVMe(src))
        return 0;

    VIR_DEBUG("Modifying access for a NVMe disk src=%p revoke=%d",
              src, revoke);

    if (revoke) {
        revoke_maxmemlock = true;
        revoke_hostdev = true;
        ret = 0;
        goto revoke;
    }

    if (qemuDomainAdjustMaxMemLock(vm, true) < 0)
        goto revoke;

    revoke_maxmemlock = true;

    if (qemuHostdevPrepareOneNVMeDisk(driver, vm->def->name, src) < 0)
        goto revoke;

    revoke_hostdev = true;

    return 0;

 revoke:
    if (revoke_maxmemlock) {
        if (qemuDomainAdjustMaxMemLock(vm, false) < 0)
            VIR_WARN("Unable to change max memlock limit");
    }

    if (revoke_hostdev)
        qemuHostdevReAttachOneNVMeDisk(driver, vm->def->name, src);

    return ret;
}


/**
 * qemuDomainStorageSourceAccessModify:
 * @driver: qemu driver struct
 * @vm: domain object
 * @src: Source to prepare
 * @flags: bitwise or of qemuDomainStorageSourceAccessFlags
 *
 * Setup the locks, cgroups and security permissions on a disk source and its
 * backing chain.
 *
 * Returns 0 on success and -1 on error. Reports libvirt error.
 */
static int
qemuDomainStorageSourceAccessModify(virQEMUDriverPtr driver,
                                    virDomainObjPtr vm,
                                    virStorageSourcePtr src,
                                    qemuDomainStorageSourceAccessFlags flags)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    const char *srcstr = NULLSTR(src->path);
    int ret = -1;
    virErrorPtr orig_err = NULL;
    bool chain = flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN;
    bool force_ro = flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_ONLY;
    bool force_rw = flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_WRITE;
    bool revoke = flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_REVOKE;
    bool chain_top = flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN_TOP;
    int rc;
    bool was_readonly = src->readonly;
    bool revoke_cgroup = false;
    bool revoke_label = false;
    bool revoke_namespace = false;
    bool revoke_nvme = false;
    bool revoke_lockspace = false;

    VIR_DEBUG("src='%s' readonly=%d force_ro=%d force_rw=%d revoke=%d chain=%d",
              NULLSTR(src->path), src->readonly, force_ro, force_rw, revoke, chain);

    if (force_ro)
        src->readonly = true;

    if (force_rw)
        src->readonly = false;

    /* just tear down the disk access */
    if (revoke) {
        virErrorPreserveLast(&orig_err);
        revoke_cgroup = true;
        revoke_label = true;
        revoke_namespace = true;
        revoke_nvme = true;
        revoke_lockspace = true;
        ret = 0;
        goto revoke;
    }

    if (virDomainLockImageAttach(driver->lockManager, cfg->uri, vm, src) < 0)
        goto revoke;

    revoke_lockspace = true;

    if (!(flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_MODIFY_ACCESS)) {
        if (qemuDomainStorageSourceAccessModifyNVMe(driver, vm, src, false) < 0)
            goto revoke;

        revoke_nvme = true;

        if (qemuDomainNamespaceSetupDisk(vm, src) < 0)
            goto revoke;

        revoke_namespace = true;
    }

    if (qemuSecuritySetImageLabel(driver, vm, src, chain, chain_top) < 0)
        goto revoke;

    revoke_label = true;

    if (chain)
        rc = qemuSetupImageChainCgroup(vm, src);
    else
        rc = qemuSetupImageCgroup(vm, src);

    if (rc < 0)
        goto revoke;

    revoke_cgroup = true;

    ret = 0;
    goto cleanup;

 revoke:
    if (flags & QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_SKIP_REVOKE)
        goto cleanup;

    if (revoke_cgroup) {
        if (chain)
            rc = qemuTeardownImageChainCgroup(vm, src);
        else
            rc = qemuTeardownImageCgroup(vm, src);

        if (rc < 0)
            VIR_WARN("Unable to tear down cgroup access on %s", srcstr);
    }

    if (revoke_label) {
        if (qemuSecurityRestoreImageLabel(driver, vm, src, chain) < 0)
            VIR_WARN("Unable to restore security label on %s", srcstr);
    }

    if (revoke_namespace) {
        if (qemuDomainNamespaceTeardownDisk(vm, src) < 0)
            VIR_WARN("Unable to remove /dev entry for %s", srcstr);
    }

    if (revoke_nvme)
        qemuDomainStorageSourceAccessModifyNVMe(driver, vm, src, true);

    if (revoke_lockspace) {
        if (virDomainLockImageDetach(driver->lockManager, vm, src) < 0)
            VIR_WARN("Unable to release lock on %s", srcstr);
    }

 cleanup:
    src->readonly = was_readonly;
    virErrorRestore(&orig_err);

    return ret;
}


int
qemuDomainStorageSourceChainAccessAllow(virQEMUDriverPtr driver,
                                        virDomainObjPtr vm,
                                        virStorageSourcePtr src)
{
    qemuDomainStorageSourceAccessFlags flags = QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN |
                                               QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN_TOP;

    return qemuDomainStorageSourceAccessModify(driver, vm, src, flags);
}


int
qemuDomainStorageSourceChainAccessRevoke(virQEMUDriverPtr driver,
                                         virDomainObjPtr vm,
                                         virStorageSourcePtr src)
{
    qemuDomainStorageSourceAccessFlags flags = QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_REVOKE |
                                               QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN |
                                               QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN_TOP;

    return qemuDomainStorageSourceAccessModify(driver, vm, src, flags);
}


/**
 * qemuDomainStorageSourceAccessRevoke:
 *
 * Revoke access to a single backing chain element. This restores the labels,
 * removes cgroup ACLs for devices and removes locks.
 */
void
qemuDomainStorageSourceAccessRevoke(virQEMUDriverPtr driver,
                                    virDomainObjPtr vm,
                                    virStorageSourcePtr elem)
{
    qemuDomainStorageSourceAccessFlags flags = QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_REVOKE;

    ignore_value(qemuDomainStorageSourceAccessModify(driver, vm, elem, flags));
}


/**
 * qemuDomainStorageSourceAccessAllow:
 * @driver: qemu driver data
 * @vm: domain object
 * @elem: source structure to set access for
 * @readonly: setup read-only access if true
 * @newSource: @elem describes a storage source which @vm can't access yet
 * @chainTop: @elem is top parent of backing chain
 *
 * Allow a VM access to a single element of a disk backing chain; this helper
 * ensures that the lock manager, cgroup device controller, and security manager
 * labelling are all aware of each new file before it is added to a chain.
 *
 * When modifying permissions of @elem which @vm can already access (is in the
 * backing chain) @newSource needs to be set to false.
 *
 * The @chainTop flag must be set if the @elem image is the topmost image of a
 * given backing chain or meant to become the topmost image (for e.g.
 * snapshots, or blockcopy or even in the end for active layer block commit,
 * where we discard the top of the backing chain so one of the intermediates
 * (the base) becomes the top of the chain).
 */
int
qemuDomainStorageSourceAccessAllow(virQEMUDriverPtr driver,
                                   virDomainObjPtr vm,
                                   virStorageSourcePtr elem,
                                   bool readonly,
                                   bool newSource,
                                   bool chainTop)
{
    qemuDomainStorageSourceAccessFlags flags = QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_SKIP_REVOKE;

    if (readonly)
        flags |= QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_ONLY;
    else
        flags |= QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_FORCE_READ_WRITE;

    if (!newSource)
        flags |= QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_MODIFY_ACCESS;

    if (chainTop)
        flags |= QEMU_DOMAIN_STORAGE_SOURCE_ACCESS_CHAIN_TOP;

    return qemuDomainStorageSourceAccessModify(driver, vm, elem, flags);
}


/*
 * Makes sure the @disk differs from @orig_disk only by the source
 * path and nothing else.  Fields that are being checked and the
 * information whether they are nullable (may not be specified) or is
 * taken from the virDomainDiskDefFormat() code.
 */
bool
qemuDomainDiskChangeSupported(virDomainDiskDefPtr disk,
                              virDomainDiskDefPtr orig_disk)
{
#define CHECK_EQ(field, field_name, nullable) \
    do { \
        if (nullable && !disk->field) \
            break; \
        if (disk->field != orig_disk->field) { \
            virReportError(VIR_ERR_OPERATION_UNSUPPORTED, \
                           _("cannot modify field '%s' of the disk"), \
                           field_name); \
            return false; \
        } \
    } while (0)

#define CHECK_STREQ_NULLABLE(field, field_name) \
    do { \
        if (!disk->field) \
            break; \
        if (STRNEQ_NULLABLE(disk->field, orig_disk->field)) { \
            virReportError(VIR_ERR_OPERATION_UNSUPPORTED, \
                           _("cannot modify field '%s' of the disk"), \
                           field_name); \
            return false; \
        } \
    } while (0)

    CHECK_EQ(device, "device", false);
    CHECK_EQ(bus, "bus", false);
    if (STRNEQ(disk->dst, orig_disk->dst)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("cannot modify field '%s' of the disk"),
                       "target");
        return false;
    }
    CHECK_EQ(tray_status, "tray", true);
    CHECK_EQ(removable, "removable", true);

    if (disk->geometry.cylinders &&
        disk->geometry.heads &&
        disk->geometry.sectors) {
        CHECK_EQ(geometry.cylinders, "geometry cylinders", false);
        CHECK_EQ(geometry.heads, "geometry heads", false);
        CHECK_EQ(geometry.sectors, "geometry sectors", false);
        CHECK_EQ(geometry.trans, "BIOS-translation-modus", true);
    }

    CHECK_EQ(blockio.logical_block_size,
             "blockio logical_block_size", false);
    CHECK_EQ(blockio.physical_block_size,
             "blockio physical_block_size", false);

    CHECK_EQ(blkdeviotune.total_bytes_sec,
             "blkdeviotune total_bytes_sec",
             true);
    CHECK_EQ(blkdeviotune.read_bytes_sec,
             "blkdeviotune read_bytes_sec",
             true);
    CHECK_EQ(blkdeviotune.write_bytes_sec,
             "blkdeviotune write_bytes_sec",
             true);
    CHECK_EQ(blkdeviotune.total_iops_sec,
             "blkdeviotune total_iops_sec",
             true);
    CHECK_EQ(blkdeviotune.read_iops_sec,
             "blkdeviotune read_iops_sec",
             true);
    CHECK_EQ(blkdeviotune.write_iops_sec,
             "blkdeviotune write_iops_sec",
             true);
    CHECK_EQ(blkdeviotune.total_bytes_sec_max,
             "blkdeviotune total_bytes_sec_max",
             true);
    CHECK_EQ(blkdeviotune.read_bytes_sec_max,
             "blkdeviotune read_bytes_sec_max",
             true);
    CHECK_EQ(blkdeviotune.write_bytes_sec_max,
             "blkdeviotune write_bytes_sec_max",
             true);
    CHECK_EQ(blkdeviotune.total_iops_sec_max,
             "blkdeviotune total_iops_sec_max",
             true);
    CHECK_EQ(blkdeviotune.read_iops_sec_max,
             "blkdeviotune read_iops_sec_max",
             true);
    CHECK_EQ(blkdeviotune.write_iops_sec_max,
             "blkdeviotune write_iops_sec_max",
             true);
    CHECK_EQ(blkdeviotune.size_iops_sec,
             "blkdeviotune size_iops_sec",
             true);
    CHECK_STREQ_NULLABLE(blkdeviotune.group_name,
                         "blkdeviotune group name");

    CHECK_STREQ_NULLABLE(serial,
                         "serial");
    CHECK_STREQ_NULLABLE(wwn,
                         "wwn");
    CHECK_STREQ_NULLABLE(vendor,
                         "vendor");
    CHECK_STREQ_NULLABLE(product,
                         "product");

    CHECK_EQ(cachemode, "cache", true);
    CHECK_EQ(error_policy, "error_policy", true);
    CHECK_EQ(rerror_policy, "rerror_policy", true);
    CHECK_EQ(iomode, "io", true);
    CHECK_EQ(ioeventfd, "ioeventfd", true);
    CHECK_EQ(event_idx, "event_idx", true);
    CHECK_EQ(copy_on_read, "copy_on_read", true);
    /* "snapshot" is a libvirt internal field and thus can be changed */
    /* startupPolicy is allowed to be updated. Therefore not checked here. */
    CHECK_EQ(transient, "transient", true);

    /* Note: For some address types the address auto generation for
     * @disk has still not happened at this point (e.g. driver
     * specific addresses) therefore we can't catch these possible
     * address modifications here. */
    if (disk->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
        !virDomainDeviceInfoAddressIsEqual(&disk->info, &orig_disk->info)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("cannot modify field '%s' of the disk"),
                       "address");
        return false;
    }

    /* device alias is checked already in virDomainDefCompatibleDevice */

    CHECK_EQ(info.bootIndex, "boot order", true);
    CHECK_EQ(rawio, "rawio", true);
    CHECK_EQ(sgio, "sgio", true);
    CHECK_EQ(discard, "discard", true);
    CHECK_EQ(iothread, "iothread", true);

    CHECK_STREQ_NULLABLE(domain_name,
                         "backenddomain");

    /* checks for fields stored in disk->src */
    /* unfortunately 'readonly' and 'shared' can't be converted to tristate
     * values thus we need to ignore the check if the new value is 'false' */
    CHECK_EQ(src->readonly, "readonly", true);
    CHECK_EQ(src->shared, "shared", true);

    if (!virStoragePRDefIsEqual(disk->src->pr,
                                orig_disk->src->pr)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("cannot modify field '%s' of the disk"),
                       "reservations");
        return false;
    }

#undef CHECK_EQ
#undef CHECK_STREQ_NULLABLE

    return true;
}


bool
qemuDomainDiskBlockJobIsActive(virDomainDiskDefPtr disk)
{
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);

    if (disk->mirror) {
        virReportError(VIR_ERR_BLOCK_COPY_ACTIVE,
                       _("disk '%s' already in active block job"),
                       disk->dst);

        return true;
    }

    if (diskPriv->blockjob &&
        qemuBlockJobIsRunning(diskPriv->blockjob)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("disk '%s' already in active block job"),
                       disk->dst);
        return true;
    }

    return false;
}


/**
 * qemuDomainHasBlockjob:
 * @vm: domain object
 * @copy_only: Reject only block copy job
 *
 * Return true if @vm has at least one disk involved in a current block
 * copy/commit/pull job. If @copy_only is true this returns true only if the
 * disk is involved in a block copy.
 * */
bool
qemuDomainHasBlockjob(virDomainObjPtr vm,
                      bool copy_only)
{
    size_t i;
    for (i = 0; i < vm->def->ndisks; i++) {
        virDomainDiskDefPtr disk = vm->def->disks[i];
        qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);

        if (!copy_only && diskPriv->blockjob &&
            qemuBlockJobIsRunning(diskPriv->blockjob))
            return true;

        if (disk->mirror && disk->mirrorJob == VIR_DOMAIN_BLOCK_JOB_TYPE_COPY)
            return true;
    }

    return false;
}


int
qemuDomainUpdateDeviceList(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           int asyncJob)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    char **aliases;
    int rc;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;
    rc = qemuMonitorGetDeviceAliases(priv->mon, &aliases);
    if (qemuDomainObjExitMonitor(driver, vm) < 0)
        return -1;
    if (rc < 0)
        return -1;

    g_strfreev(priv->qemuDevices);
    priv->qemuDevices = aliases;
    return 0;
}


int
qemuDomainUpdateMemoryDeviceInfo(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm,
                                 int asyncJob)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    GHashTable *meminfo = NULL;
    int rc;
    size_t i;

    if (vm->def->nmems == 0)
        return 0;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;

    rc = qemuMonitorGetMemoryDeviceInfo(priv->mon, &meminfo);

    if (qemuDomainObjExitMonitor(driver, vm) < 0) {
        virHashFree(meminfo);
        return -1;
    }

    /* if qemu doesn't support the info request, just carry on */
    if (rc == -2)
        return 0;

    if (rc < 0)
        return -1;

    for (i = 0; i < vm->def->nmems; i++) {
        virDomainMemoryDefPtr mem = vm->def->mems[i];
        qemuMonitorMemoryDeviceInfoPtr dimm;

        if (!mem->info.alias)
            continue;

        if (!(dimm = virHashLookup(meminfo, mem->info.alias)))
            continue;

        mem->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DIMM;
        mem->info.addr.dimm.slot = dimm->slot;
        mem->info.addr.dimm.base = dimm->address;
    }

    virHashFree(meminfo);
    return 0;
}


static bool
qemuDomainABIStabilityCheck(const virDomainDef *src,
                            const virDomainDef *dst)
{
    size_t i;

    if (src->mem.source != dst->mem.source) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Target memoryBacking source '%s' doesn't "
                         "match source memoryBacking source'%s'"),
                       virDomainMemorySourceTypeToString(dst->mem.source),
                       virDomainMemorySourceTypeToString(src->mem.source));
        return false;
    }

    for (i = 0; i < src->nmems; i++) {
        const char *srcAlias = src->mems[i]->info.alias;
        const char *dstAlias = dst->mems[i]->info.alias;

        if (STRNEQ_NULLABLE(srcAlias, dstAlias)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Target memory device alias '%s' doesn't "
                             "match source alias '%s'"),
                           NULLSTR(srcAlias), NULLSTR(dstAlias));
            return false;
        }
    }

    return true;
}


virDomainABIStability virQEMUDriverDomainABIStability = {
    .domain = qemuDomainABIStabilityCheck,
};


static bool
qemuDomainMigratableDefCheckABIStability(virQEMUDriverPtr driver,
                                         virDomainDefPtr src,
                                         virDomainDefPtr migratableSrc,
                                         virDomainDefPtr dst,
                                         virDomainDefPtr migratableDst)
{
    if (!virDomainDefCheckABIStabilityFlags(migratableSrc,
                                            migratableDst,
                                            driver->xmlopt,
                                            VIR_DOMAIN_DEF_ABI_CHECK_SKIP_VOLATILE))
        return false;

    /* Force update any skipped values from the volatile flag */
    dst->mem.cur_balloon = src->mem.cur_balloon;

    return true;
}


#define COPY_FLAGS (VIR_DOMAIN_XML_SECURE | \
                    VIR_DOMAIN_XML_MIGRATABLE)

bool
qemuDomainDefCheckABIStability(virQEMUDriverPtr driver,
                               virQEMUCapsPtr qemuCaps,
                               virDomainDefPtr src,
                               virDomainDefPtr dst)
{
    virDomainDefPtr migratableDefSrc = NULL;
    virDomainDefPtr migratableDefDst = NULL;
    bool ret = false;

    if (!(migratableDefSrc = qemuDomainDefCopy(driver, qemuCaps, src, COPY_FLAGS)) ||
        !(migratableDefDst = qemuDomainDefCopy(driver, qemuCaps, dst, COPY_FLAGS)))
        goto cleanup;

    ret = qemuDomainMigratableDefCheckABIStability(driver,
                                                   src, migratableDefSrc,
                                                   dst, migratableDefDst);

 cleanup:
    virDomainDefFree(migratableDefSrc);
    virDomainDefFree(migratableDefDst);
    return ret;
}


bool
qemuDomainCheckABIStability(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            virDomainDefPtr dst)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virDomainDefPtr migratableSrc = NULL;
    virDomainDefPtr migratableDst = NULL;
    g_autofree char *xml = NULL;
    bool ret = false;

    if (!(xml = qemuDomainFormatXML(driver, vm, COPY_FLAGS)) ||
        !(migratableSrc = qemuDomainDefFromXML(driver, priv->qemuCaps, xml)) ||
        !(migratableDst = qemuDomainDefCopy(driver, priv->qemuCaps, dst, COPY_FLAGS)))
        goto cleanup;

    ret = qemuDomainMigratableDefCheckABIStability(driver,
                                                   vm->def, migratableSrc,
                                                   dst, migratableDst);

 cleanup:
    virDomainDefFree(migratableSrc);
    virDomainDefFree(migratableDst);
    return ret;
}

#undef COPY_FLAGS


bool
qemuDomainAgentAvailable(virDomainObjPtr vm,
                         bool reportError)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (virDomainObjGetState(vm, NULL) != VIR_DOMAIN_RUNNING) {
        if (reportError) {
            virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                           _("domain is not running"));
        }
        return false;
    }
    if (priv->agentError) {
        if (reportError) {
            virReportError(VIR_ERR_AGENT_UNRESPONSIVE, "%s",
                           _("QEMU guest agent is not "
                             "available due to an error"));
        }
        return false;
    }
    if (!priv->agent) {
        if (qemuFindAgentConfig(vm->def)) {
            if (reportError) {
                virReportError(VIR_ERR_AGENT_UNRESPONSIVE, "%s",
                               _("QEMU guest agent is not connected"));
            }
            return false;
        } else {
            if (reportError) {
                virReportError(VIR_ERR_ARGUMENT_UNSUPPORTED, "%s",
                               _("QEMU guest agent is not configured"));
            }
            return false;
        }
    }
    return true;
}


static unsigned long long
qemuDomainGetMemorySizeAlignment(const virDomainDef *def)
{
    /* PPC requires the memory sizes to be rounded to 256MiB increments, so
     * round them to the size always. */
    if (ARCH_IS_PPC64(def->os.arch))
        return 256 * 1024;

    /* Align memory size. QEMU requires rounding to next 4KiB block.
     * We'll take the "traditional" path and round it to 1MiB */

    return 1024;
}


int
qemuDomainAlignMemorySizes(virDomainDefPtr def)
{
    unsigned long long maxmemkb = virMemoryMaxValue(false) >> 10;
    unsigned long long maxmemcapped = virMemoryMaxValue(true) >> 10;
    unsigned long long initialmem = 0;
    unsigned long long hotplugmem = 0;
    unsigned long long mem;
    unsigned long long align = qemuDomainGetMemorySizeAlignment(def);
    size_t ncells = virDomainNumaGetNodeCount(def->numa);
    size_t i;

    /* align NUMA cell sizes if relevant */
    for (i = 0; i < ncells; i++) {
        mem = VIR_ROUND_UP(virDomainNumaGetNodeMemorySize(def->numa, i), align);
        initialmem += mem;

        if (mem > maxmemkb) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("memory size of NUMA node '%zu' overflowed after "
                             "alignment"), i);
            return -1;
        }
        virDomainNumaSetNodeMemorySize(def->numa, i, mem);
    }

    /* Calculate hotplugmem. The memory modules are already aligned at this
     * point:
     *
     * - ppc64 mem modules are being aligned by virDomainMemoryDefPostParse();
     * - x86 mem modules are being aligned by qemuDomainMemoryDefPostParse(). */
    for (i = 0; i < def->nmems; i++)
        hotplugmem += def->mems[i]->size;

    /* Align initial memory size, if NUMA is present calculate it as total of
     * individual aligned NUMA node sizes. */
    if (initialmem == 0) {
        align = qemuDomainGetMemorySizeAlignment(def);
        initialmem = VIR_ROUND_UP(virDomainDefGetMemoryInitial(def), align);
    }

    if (initialmem > maxmemcapped) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("initial memory size overflowed after alignment"));
        return -1;
    }

    def->mem.max_memory = VIR_ROUND_UP(def->mem.max_memory, align);
    if (def->mem.max_memory > maxmemkb) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("maximum memory size overflowed after alignment"));
        return -1;
    }

    virDomainDefSetMemoryTotal(def, initialmem + hotplugmem);

    return 0;
}


/**
 * qemuDomainGetMonitor:
 * @vm: domain object
 *
 * Returns the monitor pointer corresponding to the domain object @vm.
 */
qemuMonitorPtr
qemuDomainGetMonitor(virDomainObjPtr vm)
{
    return ((qemuDomainObjPrivatePtr) vm->privateData)->mon;
}


/**
 * qemuFindAgentConfig:
 * @def: domain definition
 *
 * Returns the pointer to the channel definition that is used to access the
 * guest agent if the agent is configured or NULL otherwise.
 */
virDomainChrDefPtr
qemuFindAgentConfig(virDomainDefPtr def)
{
    size_t i;

    for (i = 0; i < def->nchannels; i++) {
        virDomainChrDefPtr channel = def->channels[i];

        if (channel->targetType != VIR_DOMAIN_CHR_CHANNEL_TARGET_TYPE_VIRTIO)
            continue;

        if (STREQ_NULLABLE(channel->target.name, "org.qemu.guest_agent.0"))
            return channel;
    }

    return NULL;
}


static bool
qemuDomainMachineIsQ35(const char *machine,
                       const virArch arch)
{
    if (!ARCH_IS_X86(arch))
        return false;

    if (STREQ(machine, "q35") ||
        STRPREFIX(machine, "pc-q35-")) {
        return true;
    }

    return false;
}


static bool
qemuDomainMachineIsI440FX(const char *machine,
                          const virArch arch)
{
    if (!ARCH_IS_X86(arch))
        return false;

    if (STREQ(machine, "pc") ||
        STRPREFIX(machine, "pc-0.") ||
        STRPREFIX(machine, "pc-1.") ||
        STRPREFIX(machine, "pc-i440fx-") ||
        STRPREFIX(machine, "rhel")) {
        return true;
    }

    return false;
}


static bool
qemuDomainMachineIsS390CCW(const char *machine,
                           const virArch arch)
{
    if (!ARCH_IS_S390(arch))
        return false;

    if (STRPREFIX(machine, "s390-ccw"))
        return true;

    return false;
}


/* You should normally avoid this function and use
 * qemuDomainIsARMVirt() instead. */
bool
qemuDomainMachineIsARMVirt(const char *machine,
                           const virArch arch)
{
    if (arch != VIR_ARCH_ARMV6L &&
        arch != VIR_ARCH_ARMV7L &&
        arch != VIR_ARCH_AARCH64) {
        return false;
    }

    if (STREQ(machine, "virt") ||
        STRPREFIX(machine, "virt-")) {
        return true;
    }

    return false;
}


static bool
qemuDomainMachineIsRISCVVirt(const char *machine,
                             const virArch arch)
{
    if (!ARCH_IS_RISCV(arch))
        return false;

    if (STREQ(machine, "virt") ||
        STRPREFIX(machine, "virt-")) {
        return true;
    }

    return false;
}


/* You should normally avoid this function and use
 * qemuDomainIsPSeries() instead. */
bool
qemuDomainMachineIsPSeries(const char *machine,
                           const virArch arch)
{
    if (!ARCH_IS_PPC64(arch))
        return false;

    if (STREQ(machine, "pseries") ||
        STRPREFIX(machine, "pseries-")) {
        return true;
    }

    return false;
}


/* You should normally avoid this function and use
 * qemuDomainHasBuiltinIDE() instead. */
bool
qemuDomainMachineHasBuiltinIDE(const char *machine,
                               const virArch arch)
{
    return qemuDomainMachineIsI440FX(machine, arch) ||
        STREQ(machine, "malta") ||
        STREQ(machine, "sun4u") ||
        STREQ(machine, "g3beige");
}


static bool
qemuDomainMachineNeedsFDC(const char *machine,
                          const virArch arch)
{
    const char *p = STRSKIP(machine, "pc-q35-");

    if (!ARCH_IS_X86(arch))
        return false;

    if (!p)
        return false;

    if (STRPREFIX(p, "1.") ||
        STREQ(p, "2.0") ||
        STREQ(p, "2.1") ||
        STREQ(p, "2.2") ||
        STREQ(p, "2.3")) {
        return false;
    }

    return true;
}


bool
qemuDomainIsQ35(const virDomainDef *def)
{
    return qemuDomainMachineIsQ35(def->os.machine, def->os.arch);
}


bool
qemuDomainIsI440FX(const virDomainDef *def)
{
    return qemuDomainMachineIsI440FX(def->os.machine, def->os.arch);
}


bool
qemuDomainIsS390CCW(const virDomainDef *def)
{
    return qemuDomainMachineIsS390CCW(def->os.machine, def->os.arch);
}


bool
qemuDomainIsARMVirt(const virDomainDef *def)
{
    return qemuDomainMachineIsARMVirt(def->os.machine, def->os.arch);
}


bool
qemuDomainIsRISCVVirt(const virDomainDef *def)
{
    return qemuDomainMachineIsRISCVVirt(def->os.machine, def->os.arch);
}


bool
qemuDomainIsPSeries(const virDomainDef *def)
{
    return qemuDomainMachineIsPSeries(def->os.machine, def->os.arch);
}


bool
qemuDomainHasPCIRoot(const virDomainDef *def)
{
    int root = virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0);

    if (root < 0)
        return false;

    if (def->controllers[root]->model != VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT)
        return false;

    return true;
}


bool
qemuDomainHasPCIeRoot(const virDomainDef *def)
{
    int root = virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0);

    if (root < 0)
        return false;

    if (def->controllers[root]->model != VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT)
        return false;

    return true;
}


bool
qemuDomainHasBuiltinIDE(const virDomainDef *def)
{
    return qemuDomainMachineHasBuiltinIDE(def->os.machine, def->os.arch);
}


bool
qemuDomainNeedsFDC(const virDomainDef *def)
{
    return qemuDomainMachineNeedsFDC(def->os.machine, def->os.arch);
}


bool
qemuDomainSupportsPCI(virDomainDefPtr def,
                      virQEMUCapsPtr qemuCaps)
{
    if (def->os.arch != VIR_ARCH_ARMV6L &&
        def->os.arch != VIR_ARCH_ARMV7L &&
        def->os.arch != VIR_ARCH_AARCH64 &&
        !ARCH_IS_RISCV(def->os.arch)) {
        return true;
    }

    if (STREQ(def->os.machine, "versatilepb"))
        return true;

    if ((qemuDomainIsARMVirt(def) ||
         qemuDomainIsRISCVVirt(def)) &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX)) {
        return true;
    }

    return false;
}


static bool
qemuCheckMemoryDimmConflict(const virDomainDef *def,
                            const virDomainMemoryDef *mem)
{
    size_t i;

    for (i = 0; i < def->nmems; i++) {
         virDomainMemoryDefPtr tmp = def->mems[i];

         if (tmp == mem ||
             tmp->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DIMM)
             continue;

         if (mem->info.addr.dimm.slot == tmp->info.addr.dimm.slot) {
             virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                            _("memory device slot '%u' is already being "
                              "used by another memory device"),
                            mem->info.addr.dimm.slot);
             return true;
         }

         if (mem->info.addr.dimm.base != 0 &&
             mem->info.addr.dimm.base == tmp->info.addr.dimm.base) {
             virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                            _("memory device base '0x%llx' is already being "
                              "used by another memory device"),
                            mem->info.addr.dimm.base);
             return true;
         }
    }

    return false;
}


static int
qemuDomainDefValidateMemoryHotplugDevice(const virDomainMemoryDef *mem,
                                         const virDomainDef *def)
{
    switch ((virDomainMemoryModel) mem->model) {
    case VIR_DOMAIN_MEMORY_MODEL_DIMM:
    case VIR_DOMAIN_MEMORY_MODEL_NVDIMM:
        if (mem->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DIMM &&
            mem->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("only 'dimm' addresses are supported for the "
                             "pc-dimm device"));
            return -1;
        }

        if (virDomainNumaGetNodeCount(def->numa) != 0) {
            if (mem->targetNode == -1) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("target NUMA node needs to be specified for "
                                 "memory device"));
                return -1;
            }
        }

        if (mem->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DIMM) {
            if (mem->info.addr.dimm.slot >= def->mem.memory_slots) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("memory device slot '%u' exceeds slots "
                                 "count '%u'"),
                               mem->info.addr.dimm.slot, def->mem.memory_slots);
                return -1;
            }


            if (qemuCheckMemoryDimmConflict(def, mem))
                return -1;
        }
        break;

    case VIR_DOMAIN_MEMORY_MODEL_NONE:
    case VIR_DOMAIN_MEMORY_MODEL_LAST:
        return -1;
    }

    return 0;
}


/**
 * qemuDomainDefValidateMemoryHotplug:
 * @def: domain definition
 * @qemuCaps: qemu capabilities object
 * @mem: definition of memory device that is to be added to @def with hotplug,
 *       NULL in case of regular VM startup
 *
 * Validates that the domain definition and memory modules have valid
 * configuration and are possibly able to accept @mem via hotplug if it's
 * non-NULL.
 *
 * Returns 0 on success; -1 and a libvirt error on error.
 */
int
qemuDomainDefValidateMemoryHotplug(const virDomainDef *def,
                                   virQEMUCapsPtr qemuCaps,
                                   const virDomainMemoryDef *mem)
{
    unsigned int nmems = def->nmems;
    unsigned long long hotplugSpace;
    unsigned long long hotplugMemory = 0;
    bool needPCDimmCap = false;
    bool needNvdimmCap = false;
    size_t i;

    hotplugSpace = def->mem.max_memory - virDomainDefGetMemoryInitial(def);

    if (mem) {
        nmems++;
        hotplugMemory = mem->size;

        if (qemuDomainDefValidateMemoryHotplugDevice(mem, def) < 0)
            return -1;
    }

    if (!virDomainDefHasMemoryHotplug(def)) {
        if (nmems) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("cannot use/hotplug a memory device when domain "
                             "'maxMemory' is not defined"));
            return -1;
        }

        return 0;
    }

    if (!ARCH_IS_PPC64(def->os.arch)) {
        /* due to guest support, qemu would silently enable NUMA with one node
         * once the memory hotplug backend is enabled. To avoid possible
         * confusion we will enforce user originated numa configuration along
         * with memory hotplug. */
        if (virDomainNumaGetNodeCount(def->numa) == 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("At least one numa node has to be configured when "
                             "enabling memory hotplug"));
            return -1;
        }
    }

    if (nmems > def->mem.memory_slots) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("memory device count '%u' exceeds slots count '%u'"),
                       nmems, def->mem.memory_slots);
        return -1;
    }

    for (i = 0; i < def->nmems; i++) {
        hotplugMemory += def->mems[i]->size;

        switch ((virDomainMemoryModel) def->mems[i]->model) {
        case VIR_DOMAIN_MEMORY_MODEL_DIMM:
            needPCDimmCap = true;
            break;

        case VIR_DOMAIN_MEMORY_MODEL_NVDIMM:
            needNvdimmCap = true;
            break;

        case VIR_DOMAIN_MEMORY_MODEL_NONE:
        case VIR_DOMAIN_MEMORY_MODEL_LAST:
            break;
        }

        /* already existing devices don't need to be checked on hotplug */
        if (!mem &&
            qemuDomainDefValidateMemoryHotplugDevice(def->mems[i], def) < 0)
            return -1;
    }

    if (needPCDimmCap &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_PC_DIMM)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("memory hotplug isn't supported by this QEMU binary"));
        return -1;
    }

    if (needNvdimmCap &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_NVDIMM)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("nvdimm isn't supported by this QEMU binary"));
        return -1;
    }

    if (hotplugMemory > hotplugSpace) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("memory device total size exceeds hotplug space"));
        return -1;
    }

    return 0;
}


/**
 * qemuDomainUpdateCurrentMemorySize:
 *
 * In case when the balloon is not present for the domain, the function
 * recalculates the maximum size to reflect possible changes.
 */
void
qemuDomainUpdateCurrentMemorySize(virDomainObjPtr vm)
{
    /* inactive domain doesn't need size update */
    if (!virDomainObjIsActive(vm))
        return;

    /* if no balloning is available, the current size equals to the current
     * full memory size */
    if (!virDomainDefHasMemballoon(vm->def))
        vm->def->mem.cur_balloon = virDomainDefGetMemoryTotal(vm->def);
}


/**
 * ppc64VFIODeviceIsNV2Bridge:
 * @device: string with the PCI device address
 *
 * This function receives a string that represents a PCI device,
 * such as '0004:04:00.0', and tells if the device is a NVLink2
 * bridge.
 */
static bool
ppc64VFIODeviceIsNV2Bridge(const char *device)
{
    const char *nvlink2Files[] = {"ibm,gpu", "ibm,nvlink",
                                  "ibm,nvlink-speed", "memory-region"};
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(nvlink2Files); i++) {
        g_autofree char *file = NULL;

        file = g_strdup_printf("/sys/bus/pci/devices/%s/of_node/%s",
                               device, nvlink2Files[i]);

        if (!virFileExists(file))
            return false;
    }

    return true;
}


/**
 * getPPC64MemLockLimitBytes:
 * @def: domain definition
 * @forceVFIO: force VFIO usage
 *
 * A PPC64 helper that calculates the memory locking limit in order for
 * the guest to operate properly.
 */
static unsigned long long
getPPC64MemLockLimitBytes(virDomainDefPtr def,
                          bool forceVFIO)
{
    unsigned long long memKB = 0;
    unsigned long long baseLimit = 0;
    unsigned long long memory = 0;
    unsigned long long maxMemory = 0;
    unsigned long long passthroughLimit = 0;
    size_t i, nPCIHostBridges = 0;
    virPCIDeviceAddressPtr pciAddr;
    bool usesVFIO = false;
    bool nvlink2Capable = false;

    for (i = 0; i < def->ncontrollers; i++) {
        virDomainControllerDefPtr cont = def->controllers[i];

        if (!virDomainControllerIsPSeriesPHB(cont))
            continue;

        nPCIHostBridges++;
    }

    for (i = 0; i < def->nhostdevs; i++) {
        virDomainHostdevDefPtr dev = def->hostdevs[i];

        if (virHostdevIsVFIODevice(dev)) {
            usesVFIO = true;

            pciAddr = &dev->source.subsys.u.pci.addr;
            if (virPCIDeviceAddressIsValid(pciAddr, false)) {
                g_autofree char *pciAddrStr = NULL;

                pciAddrStr = virPCIDeviceAddressAsString(pciAddr);
                if (ppc64VFIODeviceIsNV2Bridge(pciAddrStr)) {
                    nvlink2Capable = true;
                    break;
                }
            }
        }
    }

    if (virDomainDefHasNVMeDisk(def))
        usesVFIO = true;

    memory = virDomainDefGetMemoryTotal(def);

    if (def->mem.max_memory)
        maxMemory = def->mem.max_memory;
    else
        maxMemory = memory;

    /* baseLimit := maxMemory / 128                                  (a)
     *              + 4 MiB * #PHBs + 8 MiB                          (b)
     *
     * (a) is the hash table
     *
     * (b) is accounting for the 32-bit DMA window - it could be either the
     * KVM accelerated TCE tables for emulated devices, or the VFIO
     * userspace view. The 4 MiB per-PHB (including the default one) covers
     * a 2GiB DMA window: default is 1GiB, but it's possible it'll be
     * increased to help performance. The 8 MiB extra should be plenty for
     * the TCE table index for any reasonable number of PHBs and several
     * spapr-vlan or spapr-vscsi devices (512kB + a tiny bit each) */
    baseLimit = maxMemory / 128 +
                4096 * nPCIHostBridges +
                8192;

    /* NVLink2 support in QEMU is a special case of the passthrough
     * mechanics explained in the usesVFIO case below. The GPU RAM
     * is placed with a gap after maxMemory. The current QEMU
     * implementation puts the NVIDIA RAM above the PCI MMIO, which
     * starts at 32TiB and is the MMIO reserved for the guest main RAM.
     *
     * This window ends at 64TiB, and this is where the GPUs are being
     * placed. The next available window size is at 128TiB, and
     * 64TiB..128TiB will fit all possible NVIDIA GPUs.
     *
     * The same assumption as the most common case applies here:
     * the guest will request a 64-bit DMA window, per PHB, that is
     * big enough to map all its RAM, which is now at 128TiB due
     * to the GPUs.
     *
     * Note that the NVIDIA RAM window must be accounted for the TCE
     * table size, but *not* for the main RAM (maxMemory). This gives
     * us the following passthroughLimit for the NVLink2 case:
     *
     * passthroughLimit = maxMemory +
     *                    128TiB/512KiB * #PHBs + 8 MiB */
    if (nvlink2Capable) {
        passthroughLimit = maxMemory +
                           128 * (1ULL<<30) / 512 * nPCIHostBridges +
                           8192;
    } else if (usesVFIO || forceVFIO) {
        /* For regular (non-NVLink2 present) VFIO passthrough, the value
         * of passthroughLimit is:
         *
         * passthroughLimit := max( 2 GiB * #PHBs,                       (c)
         *                          memory                               (d)
         *                          + memory * 1/512 * #PHBs + 8 MiB )   (e)
         *
         * (c) is the pre-DDW VFIO DMA window accounting. We're allowing 2
         * GiB rather than 1 GiB
         *
         * (d) is the with-DDW (and memory pre-registration and related
         * features) DMA window accounting - assuming that we only account
         * RAM once, even if mapped to multiple PHBs
         *
         * (e) is the with-DDW userspace view and overhead for the 64-bit
         * DMA window. This is based a bit on expected guest behaviour, but
         * there really isn't a way to completely avoid that. We assume the
         * guest requests a 64-bit DMA window (per PHB) just big enough to
         * map all its RAM. 4 kiB page size gives the 1/512; it will be
         * less with 64 kiB pages, less still if the guest is mapped with
         * hugepages (unlike the default 32-bit DMA window, DDW windows
         * can use large IOMMU pages). 8 MiB is for second and further level
         * overheads, like (b) */
        passthroughLimit = MAX(2 * 1024 * 1024 * nPCIHostBridges,
                               memory +
                               memory / 512 * nPCIHostBridges + 8192);
    }

    memKB = baseLimit + passthroughLimit;

    return memKB << 10;
}


/**
 * qemuDomainGetMemLockLimitBytes:
 * @def: domain definition
 * @forceVFIO: force VFIO calculation
 *
 * Calculate the memory locking limit that needs to be set in order for
 * the guest to operate properly. The limit depends on a number of factors,
 * including certain configuration options and less immediately apparent ones
 * such as the guest architecture or the use of certain devices.
 * The @forceVFIO argument can be used to tell this function will use VFIO even
 * though @def doesn't indicates so right now.
 *
 * Returns: the memory locking limit, or 0 if setting the limit is not needed
 */
unsigned long long
qemuDomainGetMemLockLimitBytes(virDomainDefPtr def,
                               bool forceVFIO)
{
    unsigned long long memKB = 0;
    bool usesVFIO = false;
    size_t i;

    /* prefer the hard limit */
    if (virMemoryLimitIsSet(def->mem.hard_limit)) {
        memKB = def->mem.hard_limit;
        return memKB << 10;
    }

    /* If the guest wants its memory to be locked, we need to raise the memory
     * locking limit so that the OS will not refuse allocation requests;
     * however, there is no reliable way for us to figure out how much memory
     * the QEMU process will allocate for its own use, so our only way out is
     * to remove the limit altogether. Use with extreme care */
    if (def->mem.locked)
        return VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    if (ARCH_IS_PPC64(def->os.arch) && def->virtType == VIR_DOMAIN_VIRT_KVM)
        return getPPC64MemLockLimitBytes(def, forceVFIO);

    /* For device passthrough using VFIO the guest memory and MMIO memory
     * regions need to be locked persistent in order to allow DMA.
     *
     * Currently the below limit is based on assumptions about the x86 platform.
     *
     * The chosen value of 1GiB below originates from x86 systems where it was
     * used as space reserved for the MMIO region for the whole system.
     *
     * On x86_64 systems the MMIO regions of the IOMMU mapped devices don't
     * count towards the locked memory limit since the memory is owned by the
     * device. Emulated devices though do count, but the regions are usually
     * small. Although it's not guaranteed that the limit will be enough for all
     * configurations it didn't pose a problem for now.
     *
     * https://www.redhat.com/archives/libvir-list/2015-November/msg00329.html
     *
     * Note that this may not be valid for all platforms.
     */
    if (!forceVFIO) {
        for (i = 0; i < def->nhostdevs; i++) {
            if (virHostdevIsVFIODevice(def->hostdevs[i]) ||
                virHostdevIsMdevDevice(def->hostdevs[i])) {
                usesVFIO = true;
                break;
            }
        }

        if (virDomainDefHasNVMeDisk(def))
            usesVFIO = true;
    }

    if (usesVFIO || forceVFIO)
        memKB = virDomainDefGetMemoryTotal(def) + 1024 * 1024;

    return memKB << 10;
}


/**
 * qemuDomainAdjustMaxMemLock:
 * @vm: domain
 * @forceVFIO: apply VFIO requirements even if vm's def doesn't require it
 *
 * Adjust the memory locking limit for the QEMU process associated to @vm, in
 * order to comply with VFIO or architecture requirements. If @forceVFIO is
 * true then the limit is changed even if nothing in @vm's definition indicates
 * so.
 *
 * The limit will not be changed unless doing so is needed; the first time
 * the limit is changed, the original (default) limit is stored in @vm and
 * that value will be restored if qemuDomainAdjustMaxMemLock() is called once
 * memory locking is no longer required.
 *
 * Returns: 0 on success, <0 on failure
 */
int
qemuDomainAdjustMaxMemLock(virDomainObjPtr vm,
                           bool forceVFIO)
{
    unsigned long long bytes = 0;

    bytes = qemuDomainGetMemLockLimitBytes(vm->def, forceVFIO);

    if (bytes) {
        /* If this is the first time adjusting the limit, save the current
         * value so that we can restore it once memory locking is no longer
         * required. Failing to obtain the current limit is not a critical
         * failure, it just means we'll be unable to lower it later */
        if (!vm->original_memlock) {
            if (virProcessGetMaxMemLock(vm->pid, &(vm->original_memlock)) < 0)
                vm->original_memlock = 0;
        }
    } else {
        /* Once memory locking is no longer required, we can restore the
         * original, usually very low, limit */
        bytes = vm->original_memlock;
        vm->original_memlock = 0;
    }

    /* Trying to set the memory locking limit to zero is a no-op */
    if (virProcessSetMaxMemLock(vm->pid, bytes) < 0)
        return -1;

    return 0;
}


/**
 * qemuDomainAdjustMaxMemLockHostdev:
 * @vm: domain
 * @hostdev: device
 *
 * Temporarily add the hostdev to the domain definition. This is needed
 * because qemuDomainAdjustMaxMemLock() requires the hostdev to be already
 * part of the domain definition, but other functions like
 * qemuAssignDeviceHostdevAlias() expect it *not* to be there.
 * A better way to handle this would be nice
 *
 * Returns: 0 on success, <0 on failure
 */
int
qemuDomainAdjustMaxMemLockHostdev(virDomainObjPtr vm,
                                  virDomainHostdevDefPtr hostdev)
{
    int ret = 0;

    vm->def->hostdevs[vm->def->nhostdevs++] = hostdev;
    if (qemuDomainAdjustMaxMemLock(vm, false) < 0)
        ret = -1;

    vm->def->hostdevs[--(vm->def->nhostdevs)] = NULL;

    return ret;
}


/**
 * qemuDomainHasVcpuPids:
 * @vm: Domain object
 *
 * Returns true if we were able to successfully detect vCPU pids for the VM.
 */
bool
qemuDomainHasVcpuPids(virDomainObjPtr vm)
{
    size_t i;
    size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
    virDomainVcpuDefPtr vcpu;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vm->def, i);

        if (QEMU_DOMAIN_VCPU_PRIVATE(vcpu)->tid > 0)
            return true;
    }

    return false;
}


/**
 * qemuDomainGetVcpuPid:
 * @vm: domain object
 * @vcpu: cpu id
 *
 * Returns the vCPU pid. If @vcpu is offline or out of range 0 is returned.
 */
pid_t
qemuDomainGetVcpuPid(virDomainObjPtr vm,
                     unsigned int vcpuid)
{
    virDomainVcpuDefPtr vcpu = virDomainDefGetVcpu(vm->def, vcpuid);
    return QEMU_DOMAIN_VCPU_PRIVATE(vcpu)->tid;
}


/**
 * qemuDomainValidateVcpuInfo:
 *
 * Validates vcpu thread information. If vcpu thread IDs are reported by qemu,
 * this function validates that online vcpus have thread info present and
 * offline vcpus don't.
 *
 * Returns 0 on success -1 on error.
 */
int
qemuDomainValidateVcpuInfo(virDomainObjPtr vm)
{
    size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
    virDomainVcpuDefPtr vcpu;
    qemuDomainVcpuPrivatePtr vcpupriv;
    size_t i;

    if (!qemuDomainHasVcpuPids(vm))
        return 0;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vm->def, i);
        vcpupriv = QEMU_DOMAIN_VCPU_PRIVATE(vcpu);

        if (vcpu->online && vcpupriv->tid == 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("qemu didn't report thread id for vcpu '%zu'"), i);
            return -1;
        }

        if (!vcpu->online && vcpupriv->tid != 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("qemu reported thread id for inactive vcpu '%zu'"),
                           i);
            return -1;
        }
    }

    return 0;
}


bool
qemuDomainSupportsNewVcpuHotplug(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    return virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_QUERY_HOTPLUGGABLE_CPUS);
}


/**
 * qemuDomainRefreshVcpuInfo:
 * @driver: qemu driver data
 * @vm: domain object
 * @asyncJob: current asynchronous job type
 * @state: refresh vcpu state
 *
 * Updates vCPU information private data of @vm. Due to historical reasons this
 * function returns success even if some data were not reported by qemu.
 *
 * If @state is true, the vcpu state is refreshed as reported by the monitor.
 *
 * Returns 0 on success and -1 on fatal error.
 */
int
qemuDomainRefreshVcpuInfo(virQEMUDriverPtr driver,
                          virDomainObjPtr vm,
                          int asyncJob,
                          bool state)
{
    virDomainVcpuDefPtr vcpu;
    qemuDomainVcpuPrivatePtr vcpupriv;
    qemuMonitorCPUInfoPtr info = NULL;
    size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
    size_t i, j;
    bool hotplug;
    bool fast;
    bool validTIDs = true;
    int rc;
    int ret = -1;

    hotplug = qemuDomainSupportsNewVcpuHotplug(vm);
    fast = virQEMUCapsGet(QEMU_DOMAIN_PRIVATE(vm)->qemuCaps,
                          QEMU_CAPS_QUERY_CPUS_FAST);

    VIR_DEBUG("Maxvcpus %zu hotplug %d fast query %d", maxvcpus, hotplug, fast);

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;

    rc = qemuMonitorGetCPUInfo(qemuDomainGetMonitor(vm), &info, maxvcpus,
                               hotplug, fast);

    if (qemuDomainObjExitMonitor(driver, vm) < 0)
        goto cleanup;

    if (rc < 0)
        goto cleanup;

    /*
     * The query-cpus[-fast] commands return information
     * about the vCPUs, including the OS level PID that
     * is executing the vCPU.
     *
     * For KVM there is always a 1-1 mapping between
     * vCPUs and host OS PIDs.
     *
     * For TCG things are a little more complicated.
     *
     *  - In some cases the vCPUs will all have the same
     *    PID as the main emulator thread.
     *  - In some cases the first vCPU will have a distinct
     *    PID, but other vCPUs will share the emulator thread
     *
     * For MTTCG, things work the same as KVM, with each
     * vCPU getting its own PID.
     *
     * We use the Host OS PIDs for doing vCPU pinning
     * and reporting. The TCG data reporting will result
     * in bad behaviour such as pinning the wrong PID.
     * We must thus detect and discard bogus PID info
     * from TCG, while still honouring the modern MTTCG
     * impl which we can support.
     */
    for (i = 0; i < maxvcpus && validTIDs; i++) {
        if (info[i].tid == vm->pid) {
            VIR_DEBUG("vCPU[%zu] PID %llu duplicates process",
                      i, (unsigned long long)info[i].tid);
            validTIDs = false;
        }

        for (j = 0; j < i; j++) {
            if (info[i].tid != 0 && info[i].tid == info[j].tid) {
                VIR_DEBUG("vCPU[%zu] PID %llu duplicates vCPU[%zu]",
                          i, (unsigned long long)info[i].tid, j);
                validTIDs = false;
            }
        }

        if (validTIDs)
            VIR_DEBUG("vCPU[%zu] PID %llu is valid "
                      "(node=%d socket=%d die=%d core=%d thread=%d)",
                      i, (unsigned long long)info[i].tid,
                      info[i].node_id,
                      info[i].socket_id,
                      info[i].die_id,
                      info[i].core_id,
                      info[i].thread_id);
    }

    VIR_DEBUG("Extracting vCPU information validTIDs=%d", validTIDs);
    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vm->def, i);
        vcpupriv = QEMU_DOMAIN_VCPU_PRIVATE(vcpu);

        if (validTIDs)
            vcpupriv->tid = info[i].tid;

        vcpupriv->socket_id = info[i].socket_id;
        vcpupriv->core_id = info[i].core_id;
        vcpupriv->thread_id = info[i].thread_id;
        vcpupriv->node_id = info[i].node_id;
        vcpupriv->vcpus = info[i].vcpus;
        VIR_FREE(vcpupriv->type);
        vcpupriv->type = g_steal_pointer(&info[i].type);
        VIR_FREE(vcpupriv->alias);
        vcpupriv->alias = g_steal_pointer(&info[i].alias);
        virJSONValueFree(vcpupriv->props);
        vcpupriv->props = g_steal_pointer(&info[i].props);
        vcpupriv->enable_id = info[i].id;
        vcpupriv->qemu_id = info[i].qemu_id;

        if (hotplug && state) {
            vcpu->online = info[i].online;
            if (info[i].hotpluggable)
                vcpu->hotpluggable = VIR_TRISTATE_BOOL_YES;
            else
                vcpu->hotpluggable = VIR_TRISTATE_BOOL_NO;
        }
    }

    ret = 0;

 cleanup:
    qemuMonitorCPUInfoFree(info, maxvcpus);
    return ret;
}

/**
 * qemuDomainGetVcpuHalted:
 * @vm: domain object
 * @vcpu: cpu id
 *
 * Returns the vCPU halted state.
  */
bool
qemuDomainGetVcpuHalted(virDomainObjPtr vm,
                        unsigned int vcpuid)
{
    virDomainVcpuDefPtr vcpu = virDomainDefGetVcpu(vm->def, vcpuid);
    return QEMU_DOMAIN_VCPU_PRIVATE(vcpu)->halted;
}

/**
 * qemuDomainRefreshVcpuHalted:
 * @driver: qemu driver data
 * @vm: domain object
 * @asyncJob: current asynchronous job type
 *
 * Updates vCPU halted state in the private data of @vm.
 *
 * Returns 0 on success and -1 on error
 */
int
qemuDomainRefreshVcpuHalted(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            int asyncJob)
{
    virDomainVcpuDefPtr vcpu;
    qemuDomainVcpuPrivatePtr vcpupriv;
    size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
    virBitmapPtr haltedmap = NULL;
    size_t i;
    int ret = -1;
    bool fast;

    /* Not supported currently for TCG, see qemuDomainRefreshVcpuInfo */
    if (vm->def->virtType == VIR_DOMAIN_VIRT_QEMU)
        return 0;

    /* The halted state is interesting only on s390(x). On other platforms
     * the data would be stale at the time when it would be used.
     * Calling qemuMonitorGetCpuHalted() can adversely affect the running
     * VM's performance unless QEMU supports query-cpus-fast.
     */
    if (!ARCH_IS_S390(vm->def->os.arch) ||
        !virQEMUCapsGet(QEMU_DOMAIN_PRIVATE(vm)->qemuCaps,
                        QEMU_CAPS_QUERY_CPUS_FAST))
        return 0;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;

    fast = virQEMUCapsGet(QEMU_DOMAIN_PRIVATE(vm)->qemuCaps,
                          QEMU_CAPS_QUERY_CPUS_FAST);
    haltedmap = qemuMonitorGetCpuHalted(qemuDomainGetMonitor(vm), maxvcpus,
                                        fast);
    if (qemuDomainObjExitMonitor(driver, vm) < 0 || !haltedmap)
        goto cleanup;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vm->def, i);
        vcpupriv = QEMU_DOMAIN_VCPU_PRIVATE(vcpu);
        vcpupriv->halted = virTristateBoolFromBool(virBitmapIsBitSet(haltedmap,
                                                                     vcpupriv->qemu_id));
    }

    ret = 0;

 cleanup:
    virBitmapFree(haltedmap);
    return ret;
}

bool
qemuDomainSupportsNicdev(virDomainDefPtr def,
                         virDomainNetDefPtr net)
{
    /* non-virtio ARM nics require legacy -net nic */
    if (((def->os.arch == VIR_ARCH_ARMV6L) ||
        (def->os.arch == VIR_ARCH_ARMV7L) ||
        (def->os.arch == VIR_ARCH_AARCH64)) &&
        net->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_MMIO &&
        net->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI)
        return false;

    return true;
}

bool
qemuDomainNetSupportsMTU(virDomainNetType type)
{
    switch (type) {
    case VIR_DOMAIN_NET_TYPE_NETWORK:
    case VIR_DOMAIN_NET_TYPE_BRIDGE:
    case VIR_DOMAIN_NET_TYPE_ETHERNET:
    case VIR_DOMAIN_NET_TYPE_VHOSTUSER:
        return true;
    case VIR_DOMAIN_NET_TYPE_USER:
    case VIR_DOMAIN_NET_TYPE_SERVER:
    case VIR_DOMAIN_NET_TYPE_CLIENT:
    case VIR_DOMAIN_NET_TYPE_MCAST:
    case VIR_DOMAIN_NET_TYPE_INTERNAL:
    case VIR_DOMAIN_NET_TYPE_DIRECT:
    case VIR_DOMAIN_NET_TYPE_HOSTDEV:
    case VIR_DOMAIN_NET_TYPE_UDP:
    case VIR_DOMAIN_NET_TYPE_VDPA:
    case VIR_DOMAIN_NET_TYPE_LAST:
        break;
    }
    return false;
}


virDomainDiskDefPtr
qemuDomainDiskByName(virDomainDefPtr def,
                     const char *name)
{
    virDomainDiskDefPtr ret;

    if (!(ret = virDomainDiskByName(def, name, true))) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("disk '%s' not found in domain"), name);
        return NULL;
    }

    return ret;
}


/**
 * qemuDomainDefValidateDiskLunSource:
 * @src: disk source struct
 *
 * Validate whether the disk source is valid for disk device='lun'.
 *
 * Returns 0 if the configuration is valid -1 and a libvirt error if the source
 * is invalid.
 */
int
qemuDomainDefValidateDiskLunSource(const virStorageSource *src)
{
    if (virStorageSourceGetActualType(src) == VIR_STORAGE_TYPE_NETWORK) {
        if (src->protocol != VIR_STORAGE_NET_PROTOCOL_ISCSI) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("disk device='lun' is not supported "
                             "for protocol='%s'"),
                           virStorageNetProtocolTypeToString(src->protocol));
            return -1;
        }
    } else if (!virStorageSourceIsBlockLocal(src)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("disk device='lun' is only valid for block "
                         "type disk source"));
        return -1;
    }

    if (src->format != VIR_STORAGE_FILE_RAW) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("disk device 'lun' must use 'raw' format"));
        return -1;
    }

    if (src->sliceStorage) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("disk device 'lun' doesn't support storage slice"));
        return -1;
    }

    if (src->encryption &&
        src->encryption->format != VIR_STORAGE_ENCRYPTION_FORMAT_DEFAULT) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("disk device 'lun' doesn't support encryption"));
        return -1;
    }

    return 0;
}


int
qemuDomainPrepareChannel(virDomainChrDefPtr channel,
                         const char *domainChannelTargetDir)
{
    if (channel->targetType != VIR_DOMAIN_CHR_CHANNEL_TARGET_TYPE_VIRTIO ||
        channel->source->type != VIR_DOMAIN_CHR_TYPE_UNIX ||
        channel->source->data.nix.path)
        return 0;

    if (channel->target.name) {
        channel->source->data.nix.path = g_strdup_printf("%s/%s",
                                                         domainChannelTargetDir,
                                                         channel->target.name);
    } else {
        /* Generate a unique name */
        channel->source->data.nix.path = g_strdup_printf("%s/vioser-%02d-%02d-%02d.sock",
                                                         domainChannelTargetDir,
                                                         channel->info.addr.vioserial.controller,
                                                         channel->info.addr.vioserial.bus,
                                                         channel->info.addr.vioserial.port);
    }

    return 0;
}


/* qemuDomainPrepareChardevSourceTLS:
 * @source: pointer to host interface data for char devices
 * @cfg: driver configuration
 *
 * Updates host interface TLS encryption setting based on qemu.conf
 * for char devices.  This will be presented as "tls='yes|no'" in
 * live XML of a guest.
 */
void
qemuDomainPrepareChardevSourceTLS(virDomainChrSourceDefPtr source,
                                  virQEMUDriverConfigPtr cfg)
{
    if (source->type == VIR_DOMAIN_CHR_TYPE_TCP) {
        if (source->data.tcp.haveTLS == VIR_TRISTATE_BOOL_ABSENT) {
            if (cfg->chardevTLS)
                source->data.tcp.haveTLS = VIR_TRISTATE_BOOL_YES;
            else
                source->data.tcp.haveTLS = VIR_TRISTATE_BOOL_NO;
            source->data.tcp.tlsFromConfig = true;
        }
    }
}


/* qemuDomainPrepareChardevSource:
 * @def: live domain definition
 * @cfg: driver configuration
 *
 * Iterate through all devices that use virDomainChrSourceDefPtr as host
 * interface part.
 */
void
qemuDomainPrepareChardevSource(virDomainDefPtr def,
                               virQEMUDriverConfigPtr cfg)
{
    size_t i;

    for (i = 0; i < def->nserials; i++)
        qemuDomainPrepareChardevSourceTLS(def->serials[i]->source, cfg);

    for (i = 0; i < def->nparallels; i++)
        qemuDomainPrepareChardevSourceTLS(def->parallels[i]->source, cfg);

    for (i = 0; i < def->nchannels; i++)
        qemuDomainPrepareChardevSourceTLS(def->channels[i]->source, cfg);

    for (i = 0; i < def->nconsoles; i++)
        qemuDomainPrepareChardevSourceTLS(def->consoles[i]->source, cfg);

    for (i = 0; i < def->nrngs; i++)
        if (def->rngs[i]->backend == VIR_DOMAIN_RNG_BACKEND_EGD)
            qemuDomainPrepareChardevSourceTLS(def->rngs[i]->source.chardev, cfg);

    for (i = 0; i < def->nsmartcards; i++)
        if (def->smartcards[i]->type == VIR_DOMAIN_SMARTCARD_TYPE_PASSTHROUGH)
            qemuDomainPrepareChardevSourceTLS(def->smartcards[i]->data.passthru,
                                              cfg);

    for (i = 0; i < def->nredirdevs; i++)
        qemuDomainPrepareChardevSourceTLS(def->redirdevs[i]->source, cfg);
}


static int
qemuProcessPrepareStorageSourceTLSVxhs(virStorageSourcePtr src,
                                       virQEMUDriverConfigPtr cfg,
                                       qemuDomainObjPrivatePtr priv,
                                       const char *parentAlias)
{
    /* VxHS uses only client certificates and thus has no need for
     * the server-key.pem nor a secret that could be used to decrypt
     * the it, so no need to add a secinfo for a secret UUID. */
    if (src->haveTLS == VIR_TRISTATE_BOOL_ABSENT) {
        if (cfg->vxhsTLS)
            src->haveTLS = VIR_TRISTATE_BOOL_YES;
        else
            src->haveTLS = VIR_TRISTATE_BOOL_NO;
        src->tlsFromConfig = true;
    }

    if (src->haveTLS == VIR_TRISTATE_BOOL_YES) {
        src->tlsAlias = qemuAliasTLSObjFromSrcAlias(parentAlias);
        src->tlsCertdir = g_strdup(cfg->vxhsTLSx509certdir);

        if (cfg->vxhsTLSx509secretUUID) {
            qemuDomainStorageSourcePrivatePtr srcpriv = qemuDomainStorageSourcePrivateFetch(src);

            if (!(srcpriv->tlsKeySecret = qemuDomainSecretInfoTLSNew(priv, src->tlsAlias,
                                                                     cfg->vxhsTLSx509secretUUID)))
                return -1;
        }
    }

    return 0;
}


static int
qemuProcessPrepareStorageSourceTLSNBD(virStorageSourcePtr src,
                                      virQEMUDriverConfigPtr cfg,
                                      qemuDomainObjPrivatePtr priv,
                                      const char *parentAlias)
{
    if (src->haveTLS == VIR_TRISTATE_BOOL_ABSENT) {
        if (cfg->nbdTLS)
            src->haveTLS = VIR_TRISTATE_BOOL_YES;
        else
            src->haveTLS = VIR_TRISTATE_BOOL_NO;
        src->tlsFromConfig = true;
    }

    if (src->haveTLS == VIR_TRISTATE_BOOL_YES) {
        if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_NBD_TLS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("this qemu does not support TLS transport for NBD"));
            return -1;
        }

        src->tlsAlias = qemuAliasTLSObjFromSrcAlias(parentAlias);
        src->tlsCertdir = g_strdup(cfg->nbdTLSx509certdir);

        if (cfg->nbdTLSx509secretUUID) {
            qemuDomainStorageSourcePrivatePtr srcpriv = qemuDomainStorageSourcePrivateFetch(src);

            if (!(srcpriv->tlsKeySecret = qemuDomainSecretInfoTLSNew(priv, src->tlsAlias,
                                                                     cfg->nbdTLSx509secretUUID)))
                return -1;
        }
    }

    return 0;
}


/* qemuProcessPrepareStorageSourceTLS:
 * @source: source for a disk
 * @cfg: driver configuration
 * @parentAlias: alias of the parent device
 *
 * Updates host interface TLS encryption setting based on qemu.conf
 * for disk devices.  This will be presented as "tls='yes|no'" in
 * live XML of a guest.
 *
 * Returns 0 on success, -1 on bad config/failure
 */
static int
qemuDomainPrepareStorageSourceTLS(virStorageSourcePtr src,
                                  virQEMUDriverConfigPtr cfg,
                                  const char *parentAlias,
                                  qemuDomainObjPrivatePtr priv)
{
    if (virStorageSourceGetActualType(src) != VIR_STORAGE_TYPE_NETWORK)
        return 0;

    switch ((virStorageNetProtocol) src->protocol) {
    case VIR_STORAGE_NET_PROTOCOL_VXHS:
        if (qemuProcessPrepareStorageSourceTLSVxhs(src, cfg, priv, parentAlias) < 0)
            return -1;
        break;

    case VIR_STORAGE_NET_PROTOCOL_NBD:
        if (qemuProcessPrepareStorageSourceTLSNBD(src, cfg, priv, parentAlias) < 0)
            return -1;
        break;

    case VIR_STORAGE_NET_PROTOCOL_RBD:
    case VIR_STORAGE_NET_PROTOCOL_SHEEPDOG:
    case VIR_STORAGE_NET_PROTOCOL_GLUSTER:
    case VIR_STORAGE_NET_PROTOCOL_ISCSI:
    case VIR_STORAGE_NET_PROTOCOL_HTTP:
    case VIR_STORAGE_NET_PROTOCOL_HTTPS:
    case VIR_STORAGE_NET_PROTOCOL_FTP:
    case VIR_STORAGE_NET_PROTOCOL_FTPS:
    case VIR_STORAGE_NET_PROTOCOL_TFTP:
    case VIR_STORAGE_NET_PROTOCOL_SSH:
        if (src->haveTLS == VIR_TRISTATE_BOOL_YES) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("TLS transport is not supported for disk protocol '%s'"),
                           virStorageNetProtocolTypeToString(src->protocol));
            return -1;
        }
        break;

    case VIR_STORAGE_NET_PROTOCOL_NONE:
    case VIR_STORAGE_NET_PROTOCOL_LAST:
    default:
        virReportEnumRangeError(virStorageNetProtocol, src->protocol);
        return -1;
    }

    return 0;
}


void
qemuDomainPrepareShmemChardev(virDomainShmemDefPtr shmem)
{
    if (!shmem->server.enabled ||
        shmem->server.chr.data.nix.path)
        return;

    shmem->server.chr.data.nix.path = g_strdup_printf("/var/lib/libvirt/shmem-%s-sock",
                                                      shmem->name);
}


/**
 * qemuDomainVcpuHotplugIsInOrder:
 * @def: domain definition
 *
 * Returns true if online vcpus were added in order (clustered behind vcpu0
 * with increasing order).
 */
bool
qemuDomainVcpuHotplugIsInOrder(virDomainDefPtr def)
{
    size_t maxvcpus = virDomainDefGetVcpusMax(def);
    virDomainVcpuDefPtr vcpu;
    unsigned int prevorder = 0;
    size_t seenonlinevcpus = 0;
    size_t i;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(def, i);

        if (!vcpu->online)
            break;

        if (vcpu->order < prevorder)
            break;

        if (vcpu->order > prevorder)
            prevorder = vcpu->order;

        seenonlinevcpus++;
    }

    return seenonlinevcpus == virDomainDefGetVcpus(def);
}


/**
 * qemuDomainVcpuPersistOrder:
 * @def: domain definition
 *
 * Saves the order of vcpus detected from qemu to the domain definition.
 * The private data note the order only for the entry describing the
 * hotpluggable entity. This function copies the order into the definition part
 * of all sub entities.
 */
void
qemuDomainVcpuPersistOrder(virDomainDefPtr def)
{
    size_t maxvcpus = virDomainDefGetVcpusMax(def);
    virDomainVcpuDefPtr vcpu;
    qemuDomainVcpuPrivatePtr vcpupriv;
    unsigned int prevorder = 0;
    size_t i;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(def, i);
        vcpupriv = QEMU_DOMAIN_VCPU_PRIVATE(vcpu);

        if (!vcpu->online) {
            vcpu->order = 0;
        } else {
            if (vcpupriv->enable_id != 0)
                prevorder = vcpupriv->enable_id;

            vcpu->order = prevorder;
        }
    }
}


int
qemuDomainCheckMonitor(virQEMUDriverPtr driver,
                       virDomainObjPtr vm,
                       qemuDomainAsyncJob asyncJob)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    int ret;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;

    ret = qemuMonitorCheck(priv->mon);

    if (qemuDomainObjExitMonitor(driver, vm) < 0)
        return -1;

    return ret;
}


bool
qemuDomainSupportsVideoVga(virDomainVideoDefPtr video,
                           virQEMUCapsPtr qemuCaps)
{
    if (video->type == VIR_DOMAIN_VIDEO_TYPE_VIRTIO) {
        if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER) {
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VHOST_USER_VGA))
                return false;
        } else if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_VGA)) {
            return false;
        }
    }

    return true;
}


bool
qemuDomainNeedsVFIO(const virDomainDef *def)
{
    return virDomainDefHasVFIOHostdev(def) ||
        virDomainDefHasMdevHostdev(def) ||
        virDomainDefHasNVMeDisk(def);
}


/**
 * qemuDomainGetHostdevPath:
 * @dev: host device definition
 * @path: resulting path to @dev
 * @perms: Optional pointer to VIR_CGROUP_DEVICE_* perms
 *
 * For given device @dev fetch its host path and store it at
 * @path. Optionally, caller can get @perms on the path (e.g.
 * rw/ro). When called on a missing device, the function will return success
 * and store NULL at @path.
 *
 * The caller is responsible for freeing the @path when no longer
 * needed.
 *
 * Returns 0 on success, -1 otherwise.
 */
int
qemuDomainGetHostdevPath(virDomainHostdevDefPtr dev,
                         char **path,
                         int *perms)
{
    virDomainHostdevSubsysUSBPtr usbsrc = &dev->source.subsys.u.usb;
    virDomainHostdevSubsysPCIPtr pcisrc = &dev->source.subsys.u.pci;
    virDomainHostdevSubsysSCSIPtr scsisrc = &dev->source.subsys.u.scsi;
    virDomainHostdevSubsysSCSIVHostPtr hostsrc = &dev->source.subsys.u.scsi_host;
    virDomainHostdevSubsysMediatedDevPtr mdevsrc = &dev->source.subsys.u.mdev;
    g_autoptr(virUSBDevice) usb = NULL;
    g_autoptr(virSCSIDevice) scsi = NULL;
    g_autoptr(virSCSIVHostDevice) host = NULL;
    g_autofree char *tmpPath = NULL;
    int perm = 0;

    switch ((virDomainHostdevMode) dev->mode) {
    case VIR_DOMAIN_HOSTDEV_MODE_SUBSYS:
        switch ((virDomainHostdevSubsysType)dev->source.subsys.type) {
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_PCI:
            if (pcisrc->backend == VIR_DOMAIN_HOSTDEV_PCI_BACKEND_VFIO) {
                if (!(tmpPath = virPCIDeviceAddressGetIOMMUGroupDev(&pcisrc->addr)))
                    return -1;

                perm = VIR_CGROUP_DEVICE_RW;
            }
            break;

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_USB:
            if (dev->missing)
                break;
            usb = virUSBDeviceNew(usbsrc->bus,
                                  usbsrc->device,
                                  NULL);
            if (!usb)
                return -1;

            tmpPath = g_strdup(virUSBDeviceGetPath(usb));
            perm = VIR_CGROUP_DEVICE_RW;
            break;

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI:
            if (scsisrc->protocol == VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_ISCSI) {
                virDomainHostdevSubsysSCSIiSCSIPtr iscsisrc = &scsisrc->u.iscsi;
                VIR_DEBUG("Not updating /dev for hostdev iSCSI path '%s'", iscsisrc->src->path);
            } else {
                virDomainHostdevSubsysSCSIHostPtr scsihostsrc = &scsisrc->u.host;
                scsi = virSCSIDeviceNew(NULL,
                                        scsihostsrc->adapter,
                                        scsihostsrc->bus,
                                        scsihostsrc->target,
                                        scsihostsrc->unit,
                                        dev->readonly,
                                        dev->shareable);

                if (!scsi)
                    return -1;

                tmpPath = g_strdup(virSCSIDeviceGetPath(scsi));
                perm = virSCSIDeviceGetReadonly(scsi) ?
                    VIR_CGROUP_DEVICE_READ : VIR_CGROUP_DEVICE_RW;
            }
            break;

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI_HOST: {
            if (hostsrc->protocol ==
                VIR_DOMAIN_HOSTDEV_SUBSYS_SCSI_HOST_PROTOCOL_TYPE_VHOST) {
                if (!(host = virSCSIVHostDeviceNew(hostsrc->wwpn)))
                    return -1;

                tmpPath = g_strdup(virSCSIVHostDeviceGetPath(host));
                perm = VIR_CGROUP_DEVICE_RW;
            }
            break;
        }

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_MDEV:
            if (!(tmpPath = virMediatedDeviceGetIOMMUGroupDev(mdevsrc->uuidstr)))
                return -1;

            perm = VIR_CGROUP_DEVICE_RW;
            break;
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_LAST:
            break;
        }
        break;

    case VIR_DOMAIN_HOSTDEV_MODE_CAPABILITIES:
    case VIR_DOMAIN_HOSTDEV_MODE_LAST:
        /* nada */
        break;
    }

    *path = g_steal_pointer(&tmpPath);
    if (perms)
        *perms = perm;
    return 0;
}


/**
 * qemuDomainDiskLookupByNodename:
 * @def: domain definition to look for the disk
 * @nodename: block backend node name to find
 * @src: filled with the specific backing store element if provided
 *
 * Looks up the disk in the domain via @nodename and returns its definition.
 * Optionally fills @src and @idx if provided with the specific backing chain
 * element which corresponds to the node name.
 */
virDomainDiskDefPtr
qemuDomainDiskLookupByNodename(virDomainDefPtr def,
                               const char *nodename,
                               virStorageSourcePtr *src)
{
    size_t i;
    virStorageSourcePtr tmp = NULL;

    if (src)
        *src = NULL;

    for (i = 0; i < def->ndisks; i++) {
        if ((tmp = virStorageSourceFindByNodeName(def->disks[i]->src, nodename))) {
            if (src)
                *src = tmp;

            return def->disks[i];
        }

        if (def->disks[i]->mirror &&
            (tmp = virStorageSourceFindByNodeName(def->disks[i]->mirror, nodename))) {
            if (src)
                *src = tmp;

            return def->disks[i];
        }
    }

    return NULL;
}


/**
 * qemuDomainDiskBackingStoreGetName:
 *
 * Creates a name using the indexed syntax (vda[1])for the given backing store
 * entry for a disk.
 */
char *
qemuDomainDiskBackingStoreGetName(virDomainDiskDefPtr disk,
                                  unsigned int idx)
{
    if (idx)
        return g_strdup_printf("%s[%d]", disk->dst, idx);

    return g_strdup(disk->dst);
}


virStorageSourcePtr
qemuDomainGetStorageSourceByDevstr(const char *devstr,
                                   virDomainDefPtr def)
{
    virDomainDiskDefPtr disk = NULL;
    virStorageSourcePtr src = NULL;
    g_autofree char *target = NULL;
    unsigned int idx;
    size_t i;

    if (virStorageFileParseBackingStoreStr(devstr, &target, &idx) < 0) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("failed to parse block device '%s'"), devstr);
        return NULL;
    }

    for (i = 0; i < def->ndisks; i++) {
        if (STREQ(target, def->disks[i]->dst)) {
            disk = def->disks[i];
            break;
        }
    }

    if (!disk) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("failed to find disk '%s'"), target);
        return NULL;
    }

    if (idx == 0)
        return disk->src;

    if ((src = virStorageFileChainLookup(disk->src, NULL, NULL, idx, NULL)))
        return src;

    if (disk->mirror &&
        (src = virStorageFileChainLookup(disk->mirror, NULL, NULL, idx, NULL)))
        return src;

    return NULL;
}


static void
qemuDomainSaveCookieDispose(void *obj)
{
    qemuDomainSaveCookiePtr cookie = obj;

    VIR_DEBUG("cookie=%p", cookie);

    virCPUDefFree(cookie->cpu);
}


qemuDomainSaveCookiePtr
qemuDomainSaveCookieNew(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    g_autoptr(qemuDomainSaveCookie) cookie = NULL;

    if (qemuDomainInitialize() < 0)
        return NULL;

    if (!(cookie = virObjectNew(qemuDomainSaveCookieClass)))
        return NULL;

    if (priv->origCPU && !(cookie->cpu = virCPUDefCopy(vm->def->cpu)))
        return NULL;

    cookie->slirpHelper = qemuDomainGetSlirpHelperOk(vm);

    VIR_DEBUG("Save cookie %p, cpu=%p, slirpHelper=%d",
              cookie, cookie->cpu, cookie->slirpHelper);

    return g_steal_pointer(&cookie);
}


static int
qemuDomainSaveCookieParse(xmlXPathContextPtr ctxt G_GNUC_UNUSED,
                          virObjectPtr *obj)
{
    g_autoptr(qemuDomainSaveCookie) cookie = NULL;

    if (qemuDomainInitialize() < 0)
        return -1;

    if (!(cookie = virObjectNew(qemuDomainSaveCookieClass)))
        return -1;

    if (virCPUDefParseXML(ctxt, "./cpu[1]", VIR_CPU_TYPE_GUEST,
                          &cookie->cpu, false) < 0)
        return -1;

    cookie->slirpHelper = virXPathBoolean("boolean(./slirpHelper)", ctxt) > 0;

    *obj = (virObjectPtr) g_steal_pointer(&cookie);
    return 0;
}


static int
qemuDomainSaveCookieFormat(virBufferPtr buf,
                           virObjectPtr obj)
{
    qemuDomainSaveCookiePtr cookie = (qemuDomainSaveCookiePtr) obj;

    if (cookie->cpu &&
        virCPUDefFormatBufFull(buf, cookie->cpu, NULL) < 0)
        return -1;

    if (cookie->slirpHelper)
        virBufferAddLit(buf, "<slirpHelper/>\n");

    return 0;
}


virSaveCookieCallbacks virQEMUDriverDomainSaveCookie = {
    .parse = qemuDomainSaveCookieParse,
    .format = qemuDomainSaveCookieFormat,
};


/**
 * qemuDomainUpdateCPU:
 * @vm: domain which is being started
 * @cpu: CPU updated when the domain was running previously (before migration,
 *       snapshot, or save)
 * @origCPU: where to store the original CPU from vm->def in case @cpu was
 *           used instead
 *
 * Replace the CPU definition with the updated one when QEMU is new enough to
 * allow us to check extra features it is about to enable or disable when
 * starting a domain. The original CPU is stored in @origCPU.
 *
 * Returns 0 on success, -1 on error.
 */
int
qemuDomainUpdateCPU(virDomainObjPtr vm,
                    virCPUDefPtr cpu,
                    virCPUDefPtr *origCPU)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    *origCPU = NULL;

    if (!cpu || !vm->def->cpu ||
        !virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_QUERY_CPU_MODEL_EXPANSION) ||
        virCPUDefIsEqual(vm->def->cpu, cpu, false))
        return 0;

    if (!(cpu = virCPUDefCopy(cpu)))
        return -1;

    VIR_DEBUG("Replacing CPU def with the updated one");

    *origCPU = vm->def->cpu;
    vm->def->cpu = cpu;

    return 0;
}


/**
 * qemuDomainFixupCPUS:
 * @vm: domain object
 * @origCPU: original CPU used when the domain was started
 *
 * Libvirt older than 3.9.0 could have messed up the expansion of host-model
 * CPU when reconnecting to a running domain by adding features QEMU does not
 * support (such as cmt). This API fixes both the actual CPU provided by QEMU
 * (stored in the domain object) and the @origCPU used when starting the
 * domain.
 *
 * This is safe even if the original CPU definition used mode='custom' (rather
 * than host-model) since we know QEMU was able to start the domain and thus
 * the CPU definitions do not contain any features unknown to QEMU.
 *
 * This function can only be used on an active domain or when restoring a
 * domain which was running.
 *
 * Returns 0 on success, -1 on error.
 */
int
qemuDomainFixupCPUs(virDomainObjPtr vm,
                    virCPUDefPtr *origCPU)
{
    virCPUDefPtr fixedCPU = NULL;
    virCPUDefPtr fixedOrig = NULL;
    virArch arch = vm->def->os.arch;
    int ret = -1;

    if (!ARCH_IS_X86(arch))
        return 0;

    if (!vm->def->cpu ||
        vm->def->cpu->mode != VIR_CPU_MODE_CUSTOM ||
        !vm->def->cpu->model)
        return 0;

    /* Missing origCPU means QEMU created exactly the same virtual CPU which
     * we asked for or libvirt was too old to mess up the translation from
     * host-model.
     */
    if (!*origCPU)
        return 0;

    if (virCPUDefFindFeature(vm->def->cpu, "cmt") &&
        (!(fixedCPU = virCPUDefCopyWithoutModel(vm->def->cpu)) ||
         virCPUDefCopyModelFilter(fixedCPU, vm->def->cpu, false,
                                  virQEMUCapsCPUFilterFeatures, &arch) < 0))
        goto cleanup;

    if (virCPUDefFindFeature(*origCPU, "cmt") &&
        (!(fixedOrig = virCPUDefCopyWithoutModel(*origCPU)) ||
         virCPUDefCopyModelFilter(fixedOrig, *origCPU, false,
                                  virQEMUCapsCPUFilterFeatures, &arch) < 0))
        goto cleanup;

    if (fixedCPU) {
        virCPUDefFree(vm->def->cpu);
        vm->def->cpu = g_steal_pointer(&fixedCPU);
    }

    if (fixedOrig) {
        virCPUDefFree(*origCPU);
        *origCPU = g_steal_pointer(&fixedOrig);
    }

    ret = 0;

 cleanup:
    virCPUDefFree(fixedCPU);
    virCPUDefFree(fixedOrig);
    return ret;
}


char *
qemuDomainGetMachineName(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virQEMUDriverPtr driver = priv->driver;
    char *ret = NULL;

    if (vm->pid > 0) {
        ret = virSystemdGetMachineNameByPID(vm->pid);
        if (!ret)
            virResetLastError();
    }

    if (!ret)
        ret = virDomainDriverGenerateMachineName("qemu",
                                                 driver->embeddedRoot,
                                                 vm->def->id, vm->def->name,
                                                 driver->privileged);

    return ret;
}


/* Check whether the device address is using either 'ccw' or default s390
 * address format and whether that's "legal" for the current qemu and/or
 * guest os.machine type. This is the corollary to the code which doesn't
 * find the address type set using an emulator that supports either 'ccw'
 * or s390 and sets the address type based on the capabilities.
 *
 * If the address is using 'ccw' or s390 and it's not supported, generate
 * an error and return false; otherwise, return true.
 */
bool
qemuDomainCheckCCWS390AddressSupport(const virDomainDef *def,
                                     const virDomainDeviceInfo *info,
                                     virQEMUCapsPtr qemuCaps,
                                     const char *devicename)
{
    if (info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW) {
        if (!qemuDomainIsS390CCW(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("cannot use CCW address type for device "
                             "'%s' using machine type '%s'"),
                       devicename, def->os.machine);
            return false;
        } else if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CCW)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("CCW address type is not supported by "
                             "this QEMU"));
            return false;
        }
    } else if (info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_S390) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_S390)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtio S390 address type is not supported by "
                             "this QEMU"));
            return false;
        }
    }
    return true;
}


/**
 * qemuDomainPrepareDiskSourceData:
 *
 * @disk: Disk config object
 * @src: source to start from
 *
 * Prepares various aspects of a storage source belonging to a disk backing
 * chain based on the disk configuration. This function should be also called
 * for detected backing chain members.
 */
void
qemuDomainPrepareDiskSourceData(virDomainDiskDefPtr disk,
                                virStorageSourcePtr src)
{
    if (!disk)
        return;

    /* transfer properties valid only for the top level image */
    if (src == disk->src)
        src->detect_zeroes = disk->detect_zeroes;

    /* transfer properties valid for the full chain */
    src->iomode = disk->iomode;
    src->cachemode = disk->cachemode;
    src->discard = disk->discard;

    if (disk->device == VIR_DOMAIN_DISK_DEVICE_FLOPPY)
        src->floppyimg = true;
}


static void
qemuDomainPrepareDiskCachemode(virDomainDiskDefPtr disk)
{
    if (disk->cachemode == VIR_DOMAIN_DISK_CACHE_DEFAULT &&
        disk->src->shared && !disk->src->readonly)
        disk->cachemode = VIR_DOMAIN_DISK_CACHE_DISABLE;
}


static int
qemuDomainPrepareStorageSourcePR(virStorageSourcePtr src,
                                 qemuDomainObjPrivatePtr priv,
                                 const char *parentalias)
{
    if (!src->pr)
        return 0;

    if (virStoragePRDefIsManaged(src->pr)) {
        VIR_FREE(src->pr->path);
        if (!(src->pr->path = qemuDomainGetManagedPRSocketPath(priv)))
            return -1;
        src->pr->mgralias = g_strdup(qemuDomainGetManagedPRAlias());
    } else {
        if (!(src->pr->mgralias = qemuDomainGetUnmanagedPRAlias(parentalias)))
            return -1;
    }

    return 0;
}


/**
 * qemuDomainPrepareDiskSourceLegacy:
 * @disk: disk to prepare
 * @priv: VM private data
 * @cfg: qemu driver config
 *
 * Prepare any disk source relevant data for use with the -drive command line.
 */
static int
qemuDomainPrepareDiskSourceLegacy(virDomainDiskDefPtr disk,
                                  qemuDomainObjPrivatePtr priv,
                                  virQEMUDriverConfigPtr cfg)
{
    if (qemuDomainValidateStorageSource(disk->src, priv->qemuCaps, true) < 0)
        return -1;

    qemuDomainPrepareStorageSourceConfig(disk->src, cfg, priv->qemuCaps);
    qemuDomainPrepareDiskSourceData(disk, disk->src);

    if (qemuDomainSecretStorageSourcePrepare(priv, disk->src,
                                             disk->info.alias,
                                             disk->info.alias) < 0)
        return -1;

    if (qemuDomainPrepareStorageSourcePR(disk->src, priv, disk->info.alias) < 0)
        return -1;

    if (qemuDomainPrepareStorageSourceTLS(disk->src, cfg, disk->info.alias,
                                          priv) < 0)
        return -1;

    return 0;
}


int
qemuDomainPrepareStorageSourceBlockdev(virDomainDiskDefPtr disk,
                                       virStorageSourcePtr src,
                                       qemuDomainObjPrivatePtr priv,
                                       virQEMUDriverConfigPtr cfg)
{
    src->id = qemuDomainStorageIdNew(priv);

    src->nodestorage = g_strdup_printf("libvirt-%u-storage", src->id);
    src->nodeformat = g_strdup_printf("libvirt-%u-format", src->id);

    if (qemuBlockStorageSourceNeedsStorageSliceLayer(src))
        src->sliceStorage->nodename = g_strdup_printf("libvirt-%u-slice-sto", src->id);

    if (qemuDomainValidateStorageSource(src, priv->qemuCaps, false) < 0)
        return -1;

    qemuDomainPrepareStorageSourceConfig(src, cfg, priv->qemuCaps);
    qemuDomainPrepareDiskSourceData(disk, src);

    if (qemuDomainSecretStorageSourcePrepare(priv, src,
                                             src->nodestorage,
                                             src->nodeformat) < 0)
        return -1;

    if (qemuDomainPrepareStorageSourcePR(src, priv, src->nodestorage) < 0)
        return -1;

    if (qemuDomainPrepareStorageSourceTLS(src, cfg, src->nodestorage,
                                          priv) < 0)
        return -1;

    return 0;
}


static int
qemuDomainPrepareDiskSourceBlockdev(virDomainDiskDefPtr disk,
                                    qemuDomainObjPrivatePtr priv,
                                    virQEMUDriverConfigPtr cfg)
{
    qemuDomainDiskPrivatePtr diskPriv = QEMU_DOMAIN_DISK_PRIVATE(disk);
    virStorageSourcePtr n;

    if (disk->copy_on_read == VIR_TRISTATE_SWITCH_ON &&
        !diskPriv->nodeCopyOnRead)
        diskPriv->nodeCopyOnRead = g_strdup_printf("libvirt-CoR-%s", disk->dst);

    for (n = disk->src; virStorageSourceIsBacking(n); n = n->backingStore) {
        if (qemuDomainPrepareStorageSourceBlockdev(disk, n, priv, cfg) < 0)
            return -1;
    }

    return 0;
}


int
qemuDomainPrepareDiskSource(virDomainDiskDefPtr disk,
                            qemuDomainObjPrivatePtr priv,
                            virQEMUDriverConfigPtr cfg)
{
    qemuDomainPrepareDiskCachemode(disk);

    /* set default format for storage pool based disks */
    if (disk->src->type == VIR_STORAGE_TYPE_VOLUME &&
        disk->src->format <= VIR_STORAGE_FILE_NONE) {
        int actualType = virStorageSourceGetActualType(disk->src);

        if (actualType == VIR_STORAGE_TYPE_DIR)
            disk->src->format = VIR_STORAGE_FILE_FAT;
        else
            disk->src->format = VIR_STORAGE_FILE_RAW;
    }

    if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV) &&
        !qemuDiskBusIsSD(disk->bus)) {
        if (qemuDomainPrepareDiskSourceBlockdev(disk, priv, cfg) < 0)
            return -1;
    } else {
        if (qemuDomainPrepareDiskSourceLegacy(disk, priv, cfg) < 0)
            return -1;
    }

    return 0;
}


int
qemuDomainPrepareHostdev(virDomainHostdevDefPtr hostdev,
                         qemuDomainObjPrivatePtr priv)
{
    if (virHostdevIsSCSIDevice(hostdev)) {
        virDomainHostdevSubsysSCSIPtr scsisrc = &hostdev->source.subsys.u.scsi;
        virStorageSourcePtr src = NULL;

        switch ((virDomainHostdevSCSIProtocolType) scsisrc->protocol) {
        case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_NONE:
            virObjectUnref(scsisrc->u.host.src);
            scsisrc->u.host.src = virStorageSourceNew();
            src = scsisrc->u.host.src;

            src->type = VIR_STORAGE_TYPE_BLOCK;

            break;

        case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_ISCSI:
            src = scsisrc->u.iscsi.src;
            break;

        case VIR_DOMAIN_HOSTDEV_SCSI_PROTOCOL_TYPE_LAST:
        default:
            virReportEnumRangeError(virDomainHostdevSCSIProtocolType, scsisrc->protocol);
            return -1;
        }

        if (src) {
            const char *backendalias = hostdev->info->alias;

            src->readonly = hostdev->readonly;

            if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV_HOSTDEV_SCSI)) {
                src->id = qemuDomainStorageIdNew(priv);
                src->nodestorage = g_strdup_printf("libvirt-%d-backend", src->id);
                backendalias = src->nodestorage;
            }

            if (src->auth) {
                bool iscsiHasPS = virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_ISCSI_PASSWORD_SECRET);
                virSecretUsageType usageType = VIR_SECRET_USAGE_TYPE_ISCSI;
                qemuDomainStorageSourcePrivatePtr srcPriv = qemuDomainStorageSourcePrivateFetch(src);

                if (!qemuDomainSupportsEncryptedSecret(priv) || !iscsiHasPS) {
                    srcPriv->secinfo = qemuDomainSecretInfoNewPlain(usageType,
                                                                    src->auth->username,
                                                                    &src->auth->seclookupdef);
                } else {
                    srcPriv->secinfo = qemuDomainSecretAESSetupFromSecret(priv,
                                                                          backendalias,
                                                                          NULL,
                                                                          usageType,
                                                                          src->auth->username,
                                                                          &src->auth->seclookupdef);
                }

                if (!srcPriv->secinfo)
                    return -1;
            }
        }
    }

    return 0;
}


/**
 * qemuDomainDiskCachemodeFlags:
 *
 * Converts disk cachemode to the cache mode options for qemu. Returns -1 for
 * invalid @cachemode values and fills the flags and returns 0 on success.
 * Flags may be NULL.
 */
int
qemuDomainDiskCachemodeFlags(int cachemode,
                             bool *writeback,
                             bool *direct,
                             bool *noflush)
{
    bool dummy;

    if (!writeback)
        writeback = &dummy;

    if (!direct)
        direct = &dummy;

    if (!noflush)
        noflush = &dummy;

    /* Mapping of cache modes to the attributes according to qemu-options.hx
     *              │ cache.writeback   cache.direct   cache.no-flush
     * ─────────────┼─────────────────────────────────────────────────
     * writeback    │ true              false          false
     * none         │ true              true           false
     * writethrough │ false             false          false
     * directsync   │ false             true           false
     * unsafe       │ true              false          true
     */
    switch ((virDomainDiskCache) cachemode) {
    case VIR_DOMAIN_DISK_CACHE_DISABLE: /* 'none' */
        *writeback = true;
        *direct = true;
        *noflush = false;
        break;

    case VIR_DOMAIN_DISK_CACHE_WRITETHRU:
        *writeback = false;
        *direct = false;
        *noflush = false;
        break;

    case VIR_DOMAIN_DISK_CACHE_WRITEBACK:
        *writeback = true;
        *direct = false;
        *noflush = false;
        break;

    case VIR_DOMAIN_DISK_CACHE_DIRECTSYNC:
        *writeback = false;
        *direct = true;
        *noflush = false;
        break;

    case VIR_DOMAIN_DISK_CACHE_UNSAFE:
        *writeback = true;
        *direct = false;
        *noflush = true;
        break;

    case VIR_DOMAIN_DISK_CACHE_DEFAULT:
    case VIR_DOMAIN_DISK_CACHE_LAST:
    default:
        virReportEnumRangeError(virDomainDiskCache, cachemode);
        return -1;
    }

    return 0;
}


void
qemuProcessEventFree(struct qemuProcessEvent *event)
{
    if (!event)
        return;

    switch (event->eventType) {
    case QEMU_PROCESS_EVENT_GUESTPANIC:
        qemuMonitorEventPanicInfoFree(event->data);
        break;
    case QEMU_PROCESS_EVENT_RDMA_GID_STATUS_CHANGED:
        qemuMonitorEventRdmaGidStatusFree(event->data);
        break;
    case QEMU_PROCESS_EVENT_WATCHDOG:
    case QEMU_PROCESS_EVENT_DEVICE_DELETED:
    case QEMU_PROCESS_EVENT_NIC_RX_FILTER_CHANGED:
    case QEMU_PROCESS_EVENT_SERIAL_CHANGED:
    case QEMU_PROCESS_EVENT_BLOCK_JOB:
    case QEMU_PROCESS_EVENT_MONITOR_EOF:
    case QEMU_PROCESS_EVENT_GUEST_CRASHLOADED:
        VIR_FREE(event->data);
        break;
    case QEMU_PROCESS_EVENT_JOB_STATUS_CHANGE:
        virObjectUnref(event->data);
        break;
    case QEMU_PROCESS_EVENT_PR_DISCONNECT:
    case QEMU_PROCESS_EVENT_LAST:
        break;
    }
    VIR_FREE(event);
}


char *
qemuDomainGetManagedPRSocketPath(qemuDomainObjPrivatePtr priv)
{
    return g_strdup_printf("%s/%s.sock", priv->libDir,
                           qemuDomainGetManagedPRAlias());
}


/**
 * qemuDomainStorageIdNew:
 * @priv: qemu VM private data object.
 *
 * Generate a new unique id for a storage object. Useful for node name generation.
 */
unsigned int
qemuDomainStorageIdNew(qemuDomainObjPrivatePtr priv)
{
    return ++priv->nodenameindex;
}


/**
 * qemuDomainStorageIdReset:
 * @priv: qemu VM private data object.
 *
 * Resets the data for the node name generator. The node names need to be unique
 * for a single instance, so can be reset on VM shutdown.
 */
void
qemuDomainStorageIdReset(qemuDomainObjPrivatePtr priv)
{
    priv->nodenameindex = 0;
}


virDomainEventResumedDetailType
qemuDomainRunningReasonToResumeEvent(virDomainRunningReason reason)
{
    switch (reason) {
    case VIR_DOMAIN_RUNNING_RESTORED:
    case VIR_DOMAIN_RUNNING_FROM_SNAPSHOT:
        return VIR_DOMAIN_EVENT_RESUMED_FROM_SNAPSHOT;

    case VIR_DOMAIN_RUNNING_MIGRATED:
    case VIR_DOMAIN_RUNNING_MIGRATION_CANCELED:
        return VIR_DOMAIN_EVENT_RESUMED_MIGRATED;

    case VIR_DOMAIN_RUNNING_POSTCOPY:
        return VIR_DOMAIN_EVENT_RESUMED_POSTCOPY;

    case VIR_DOMAIN_RUNNING_UNKNOWN:
    case VIR_DOMAIN_RUNNING_SAVE_CANCELED:
    case VIR_DOMAIN_RUNNING_BOOTED:
    case VIR_DOMAIN_RUNNING_UNPAUSED:
    case VIR_DOMAIN_RUNNING_WAKEUP:
    case VIR_DOMAIN_RUNNING_CRASHED:
    case VIR_DOMAIN_RUNNING_LAST:
        break;
    }

    return VIR_DOMAIN_EVENT_RESUMED_UNPAUSED;
}


/* qemuDomainIsUsingNoShutdown:
 * @priv: Domain private data
 *
 * We can receive an event when QEMU stops. If we use no-shutdown, then
 * we can watch for this event and do a soft/warm reboot.
 *
 * Returns: @true when -no-shutdown either should be or was added to the
 * command line.
 */
bool
qemuDomainIsUsingNoShutdown(qemuDomainObjPrivatePtr priv)
{
    return priv->allowReboot == VIR_TRISTATE_BOOL_YES;
}


bool
qemuDomainDiskIsMissingLocalOptional(virDomainDiskDefPtr disk)
{
    return disk->startupPolicy == VIR_DOMAIN_STARTUP_POLICY_OPTIONAL &&
           virStorageSourceIsLocalStorage(disk->src) && disk->src->path &&
           !virFileExists(disk->src->path);
}


void
qemuDomainNVRAMPathFormat(virQEMUDriverConfigPtr cfg,
                            virDomainDefPtr def,
                            char **path)
{
    *path = g_strdup_printf("%s/%s_VARS.fd", cfg->nvramDir, def->name);
}


void
qemuDomainNVRAMPathGenerate(virQEMUDriverConfigPtr cfg,
                            virDomainDefPtr def)
{
    if (virDomainDefHasOldStyleROUEFI(def) &&
        !def->os.loader->nvram)
        qemuDomainNVRAMPathFormat(cfg, def, &def->os.loader->nvram);
}


virDomainEventSuspendedDetailType
qemuDomainPausedReasonToSuspendedEvent(virDomainPausedReason reason)
{
    switch (reason) {
    case VIR_DOMAIN_PAUSED_MIGRATION:
        return VIR_DOMAIN_EVENT_SUSPENDED_MIGRATED;

    case VIR_DOMAIN_PAUSED_FROM_SNAPSHOT:
        return VIR_DOMAIN_EVENT_SUSPENDED_FROM_SNAPSHOT;

    case VIR_DOMAIN_PAUSED_POSTCOPY_FAILED:
        return VIR_DOMAIN_EVENT_SUSPENDED_POSTCOPY_FAILED;

    case VIR_DOMAIN_PAUSED_POSTCOPY:
        return VIR_DOMAIN_EVENT_SUSPENDED_POSTCOPY;

    case VIR_DOMAIN_PAUSED_UNKNOWN:
    case VIR_DOMAIN_PAUSED_USER:
    case VIR_DOMAIN_PAUSED_SAVE:
    case VIR_DOMAIN_PAUSED_DUMP:
    case VIR_DOMAIN_PAUSED_IOERROR:
    case VIR_DOMAIN_PAUSED_WATCHDOG:
    case VIR_DOMAIN_PAUSED_SHUTTING_DOWN:
    case VIR_DOMAIN_PAUSED_SNAPSHOT:
    case VIR_DOMAIN_PAUSED_CRASHED:
    case VIR_DOMAIN_PAUSED_STARTING_UP:
    case VIR_DOMAIN_PAUSED_LAST:
        break;
    }

    return VIR_DOMAIN_EVENT_SUSPENDED_PAUSED;
}


static int
qemuDomainDefHasManagedPRBlockjobIterator(void *payload,
                                          const char *name G_GNUC_UNUSED,
                                          void *opaque)
{
    qemuBlockJobDataPtr job = payload;
    bool *hasPR = opaque;

    if (job->disk)
        return 0;

    if ((job->chain && virStorageSourceChainHasManagedPR(job->chain)) ||
        (job->mirrorChain && virStorageSourceChainHasManagedPR(job->mirrorChain)))
        *hasPR = true;

    return 0;
}


/**
 * qemuDomainDefHasManagedPR:
 * @vm: domain object
 *
 * @vm must be an active VM. Returns true if @vm has any storage source with
 * managed persistent reservations.
 */
bool
qemuDomainDefHasManagedPR(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    bool jobPR = false;

    if (virDomainDefHasManagedPR(vm->def))
        return true;

    virHashForEach(priv->blockjobs, qemuDomainDefHasManagedPRBlockjobIterator, &jobPR);

    return jobPR;
}


/**
 * qemuDomainSupportsCheckpointsBlockjobs:
 * @vm: domain object
 *
 * Checks whether a block job is supported in possible combination with
 * checkpoints (qcow2 bitmaps). Returns -1 if unsupported and reports an error
 * 0 in case everything is supported.
 */
int
qemuDomainSupportsCheckpointsBlockjobs(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_INCREMENTAL_BACKUP) &&
        virDomainListCheckpoints(vm->checkpoints, NULL, NULL, NULL, 0) > 0) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("cannot perform block operations while checkpoint exists"));
        return -1;
    }

    return 0;
}

/**
 * qemuDomainInitializePflashStorageSource:
 *
 * This helper converts the specification of the source of the 'loader' in case
 * PFLASH is required to virStorageSources in case QEMU_CAPS_BLOCKDEV is present.
 *
 * This helper is used in the intermediate state when we don't support full
 * backing chains for pflash drives in the XML.
 *
 * The nodenames used here have a different prefix to allow for a later
 * conversion. The prefixes are 'libvirt-pflash0-storage',
 * 'libvirt-pflash0-format' for pflash0 and 'libvirt-pflash1-storage' and
 * 'libvirt-pflash1-format' for pflash1.
 */
int
qemuDomainInitializePflashStorageSource(virDomainObjPtr vm)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virDomainDefPtr def = vm->def;
    g_autoptr(virStorageSource) pflash0 = NULL;
    g_autoptr(virStorageSource) pflash1 = NULL;

    if (!virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV))
        return 0;

    if (!virDomainDefHasOldStyleUEFI(def))
        return 0;

    pflash0 = virStorageSourceNew();
    pflash0->type = VIR_STORAGE_TYPE_FILE;
    pflash0->format = VIR_STORAGE_FILE_RAW;
    pflash0->path = g_strdup(def->os.loader->path);
    pflash0->readonly = def->os.loader->readonly;
    pflash0->nodeformat = g_strdup("libvirt-pflash0-format");
    pflash0->nodestorage = g_strdup("libvirt-pflash0-storage");


    if (def->os.loader->nvram) {
        pflash1 = virStorageSourceNew();
        pflash1->type = VIR_STORAGE_TYPE_FILE;
        pflash1->format = VIR_STORAGE_FILE_RAW;
        pflash1->path = g_strdup(def->os.loader->nvram);
        pflash1->readonly = false;
        pflash1->nodeformat = g_strdup("libvirt-pflash1-format");
        pflash1->nodestorage = g_strdup("libvirt-pflash1-storage");
    }

    priv->pflash0 = g_steal_pointer(&pflash0);
    priv->pflash1 = g_steal_pointer(&pflash1);

    return 0;
}


/**
 * qemuDomainDiskBlockJobIsSupported:
 *
 * Returns true if block jobs are supported on @disk by @vm or false and reports
 * an error otherwise.
 *
 * Note that this does not verify whether other block jobs are running etc.
 */
bool
qemuDomainDiskBlockJobIsSupported(virDomainObjPtr vm,
                                  virDomainDiskDefPtr disk)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (virQEMUCapsGet(priv->qemuCaps, QEMU_CAPS_BLOCKDEV) &&
        qemuDiskBusIsSD(disk->bus)) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("block jobs are not supported on disk '%s' using bus 'sd'"),
                       disk->dst);
        return false;
    }

    if (disk->transient) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("block jobs are not supported on transient disk '%s'"),
                       disk->dst);
        return false;
    }

    return true;
}


int
virQEMUFileOpenAs(uid_t fallback_uid,
                  gid_t fallback_gid,
                  bool dynamicOwnership,
                  const char *path,
                  int oflags,
                  bool *needUnlink)
{
    struct stat sb;
    bool is_reg = true;
    bool need_unlink = false;
    unsigned int vfoflags = 0;
    int fd = -1;
    int path_shared = virFileIsSharedFS(path);
    uid_t uid = geteuid();
    gid_t gid = getegid();

    /* path might be a pre-existing block dev, in which case
     * we need to skip the create step, and also avoid unlink
     * in the failure case */
    if (oflags & O_CREAT) {
        need_unlink = true;

        /* Don't force chown on network-shared FS
         * as it is likely to fail. */
        if (path_shared <= 0 || dynamicOwnership)
            vfoflags |= VIR_FILE_OPEN_FORCE_OWNER;

        if (stat(path, &sb) == 0) {
            /* It already exists, we don't want to delete it on error */
            need_unlink = false;

            is_reg = !!S_ISREG(sb.st_mode);
            /* If the path is regular file which exists
             * already and dynamic_ownership is off, we don't
             * want to change its ownership, just open it as-is */
            if (is_reg && !dynamicOwnership) {
                uid = sb.st_uid;
                gid = sb.st_gid;
            }
        }
    }

    /* First try creating the file as root */
    if (!is_reg) {
        if ((fd = open(path, oflags & ~O_CREAT)) < 0) {
            fd = -errno;
            goto error;
        }
    } else {
        if ((fd = virFileOpenAs(path, oflags, S_IRUSR | S_IWUSR, uid, gid,
                                vfoflags | VIR_FILE_OPEN_NOFORK)) < 0) {
            /* If we failed as root, and the error was permission-denied
               (EACCES or EPERM), assume it's on a network-connected share
               where root access is restricted (eg, root-squashed NFS). If the
               qemu user is non-root, just set a flag to
               bypass security driver shenanigans, and retry the operation
               after doing setuid to qemu user */
            if ((fd != -EACCES && fd != -EPERM) || fallback_uid == geteuid())
                goto error;

            /* On Linux we can also verify the FS-type of the directory. */
            switch (path_shared) {
                case 1:
                    /* it was on a network share, so we'll continue
                     * as outlined above
                     */
                    break;

                case -1:
                    virReportSystemError(-fd, oflags & O_CREAT
                                         ? _("Failed to create file "
                                             "'%s': couldn't determine fs type")
                                         : _("Failed to open file "
                                             "'%s': couldn't determine fs type"),
                                         path);
                    goto cleanup;

                case 0:
                default:
                    /* local file - log the error returned by virFileOpenAs */
                    goto error;
            }

            /* If we created the file above, then we need to remove it;
             * otherwise, the next attempt to create will fail. If the
             * file had already existed before we got here, then we also
             * don't want to delete it and allow the following to succeed
             * or fail based on existing protections
             */
            if (need_unlink)
                unlink(path);

            /* Retry creating the file as qemu user */

            /* Since we're passing different modes... */
            vfoflags |= VIR_FILE_OPEN_FORCE_MODE;

            if ((fd = virFileOpenAs(path, oflags,
                                    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
                                    fallback_uid, fallback_gid,
                                    vfoflags | VIR_FILE_OPEN_FORK)) < 0) {
                virReportSystemError(-fd, oflags & O_CREAT
                                     ? _("Error from child process creating '%s'")
                                     : _("Error from child process opening '%s'"),
                                     path);
                goto cleanup;
            }
        }
    }
 cleanup:
    if (needUnlink)
        *needUnlink = need_unlink;
    return fd;

 error:
    virReportSystemError(-fd, oflags & O_CREAT
                         ? _("Failed to create file '%s'")
                         : _("Failed to open file '%s'"),
                         path);
    goto cleanup;
}


/**
 * qemuDomainOpenFile:
 * @driver: driver object
 * @vm: domain object
 * @path: path to file to open
 * @oflags: flags for opening/creation of the file
 * @needUnlink: set to true if file was created by this function
 *
 * Internal function to properly create or open existing files, with
 * ownership affected by qemu driver setup and domain DAC label.
 *
 * Returns the file descriptor on success and negative errno on failure.
 *
 * This function should not be used on storage sources. Use
 * qemuDomainStorageFileInit and storage driver APIs if possible.
 **/
int
qemuDomainOpenFile(virQEMUDriverPtr driver,
                   virDomainObjPtr vm,
                   const char *path,
                   int oflags,
                   bool *needUnlink)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    uid_t user = cfg->user;
    gid_t group = cfg->group;
    bool dynamicOwnership = cfg->dynamicOwnership;
    virSecurityLabelDefPtr seclabel;

    /* TODO: Take imagelabel into account? */
    if (vm &&
        (seclabel = virDomainDefGetSecurityLabelDef(vm->def, "dac")) != NULL &&
        seclabel->label != NULL &&
        (virParseOwnershipIds(seclabel->label, &user, &group) < 0))
        return -1;

    return virQEMUFileOpenAs(user, group, dynamicOwnership,
                             path, oflags, needUnlink);
}


int
qemuDomainFileWrapperFDClose(virDomainObjPtr vm,
                             virFileWrapperFdPtr fd)
{
    int ret;

    /* virFileWrapperFd uses iohelper to write data onto disk.
     * However, iohelper calls fdatasync() which may take ages to
     * finish. Therefore, we shouldn't be waiting with the domain
     * object locked. */

    /* XXX Currently, this function is intended for *Save() only
     * as restore needs some reworking before it's ready for
     * this. */

    virObjectUnlock(vm);
    ret = virFileWrapperFdClose(fd);
    virObjectLock(vm);
    if (!virDomainObjIsActive(vm)) {
        if (virGetLastErrorCode() == VIR_ERR_OK)
            virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                           _("domain is no longer running"));
        ret = -1;
    }
    return ret;
}


/**
 * qemuDomainInterfaceSetDefaultQDisc:
 * @driver: QEMU driver
 * @net: domain interface
 *
 * Set the noqueue qdisc on @net if running as privileged. The
 * noqueue qdisc is a lockless transmit and thus faster than the
 * default pfifo_fast (at least in theory). But we can modify
 * root qdisc only if we have CAP_NET_ADMIN.
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int
qemuDomainInterfaceSetDefaultQDisc(virQEMUDriverPtr driver,
                                   virDomainNetDefPtr net)
{
    virDomainNetType actualType = virDomainNetGetActualType(net);

    if (!driver->privileged || !net->ifname)
        return 0;

    /* We want only those types which are represented as TAP
     * devices in the host. */
    if (actualType == VIR_DOMAIN_NET_TYPE_ETHERNET ||
        actualType == VIR_DOMAIN_NET_TYPE_NETWORK ||
        actualType == VIR_DOMAIN_NET_TYPE_BRIDGE ||
        actualType == VIR_DOMAIN_NET_TYPE_DIRECT) {
        if (virNetDevSetRootQDisc(net->ifname, "noqueue") < 0)
            return -1;
    }

    return 0;
}
