#include <config.h>
#include <fcntl.h>
#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "datatypes.h"
#include "driver.h"
#include "secret_util.h"
#include "storage_backend_iscsi_direct.h"
#include "storage_util.h"
#include "viralloc.h"
#include "vircommand.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virobject.h"
#include "virstring.h"
#include "virtime.h"
#include "viruuid.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

#define ISCSI_DEFAULT_TARGET_PORT 3260
#define VIR_ISCSI_TEST_UNIT_TIMEOUT 30 * 1000

VIR_LOG_INIT("storage.storage_backend_iscsi_direct");

static struct iscsi_context *
virISCSIDirectCreateContext(const char* initiator_iqn)
{
    struct iscsi_context *iscsi = NULL;

    iscsi = iscsi_create_context(initiator_iqn);
    if (!iscsi)
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to create iscsi context for %s"),
                       initiator_iqn);
    return iscsi;
}

static char *
virStorageBackendISCSIDirectPortal(virStoragePoolSourcePtr source)
{
    char *portal = NULL;

    if (source->nhost != 1) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Expected exactly 1 host for the storage pool"));
        return NULL;
    }
    if (source->hosts[0].port == 0) {
        ignore_value(virAsprintf(&portal, "%s:%d",
                                 source->hosts[0].name,
                                 ISCSI_DEFAULT_TARGET_PORT));
    } else if (strchr(source->hosts[0].name, ':')) {
        ignore_value(virAsprintf(&portal, "[%s]:%d",
                                 source->hosts[0].name,
                                 source->hosts[0].port));
    } else {
        ignore_value(virAsprintf(&portal, "%s:%d",
                                 source->hosts[0].name,
                                 source->hosts[0].port));
    }
    return portal;
}

static int
virStorageBackendISCSIDirectSetAuth(struct iscsi_context *iscsi,
                                    virStoragePoolSourcePtr source)
{
    unsigned char *secret_value = NULL;
    size_t secret_size;
    virStorageAuthDefPtr authdef = source->auth;
    int ret = -1;
    virConnectPtr conn = NULL;

    if (!authdef || authdef->authType == VIR_STORAGE_AUTH_TYPE_NONE)
        return 0;

    VIR_DEBUG("username='%s' authType=%d seclookupdef.type=%d",
              authdef->username, authdef->authType, authdef->seclookupdef.type);

    if (authdef->authType != VIR_STORAGE_AUTH_TYPE_CHAP) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("iscsi-direct pool only supports 'chap' auth type"));
        return ret;
    }

    if (!(conn = virGetConnectSecret()))
        return ret;

    if (virSecretGetSecretString(conn, &authdef->seclookupdef,
                                 VIR_SECRET_USAGE_TYPE_ISCSI,
                                 &secret_value, &secret_size) < 0)
        goto cleanup;

    if (iscsi_set_initiator_username_pwd(iscsi,
                                         authdef->username,
                                         (const char *)secret_value) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to set credential: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

    ret = 0;
 cleanup:
    VIR_DISPOSE_N(secret_value, secret_size);
    virObjectUnref(conn);
    return ret;
}

static int
virISCSIDirectSetContext(struct iscsi_context *iscsi,
                         const char *target_name)
{
    if (iscsi_init_transport(iscsi, TCP_TRANSPORT) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to init transport: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    if (iscsi_set_targetname(iscsi, target_name) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to set target name: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    if (iscsi_set_session_type(iscsi, ISCSI_SESSION_NORMAL) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to set session type: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    return 0;
}

static int
virISCSIDirectConnect(struct iscsi_context *iscsi,
                      const char *portal)
{
    if (iscsi_connect_sync(iscsi, portal) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to connect: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    if (iscsi_login_sync(iscsi) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to login: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    return 0;
}

static struct scsi_reportluns_list *
virISCSIDirectReportLuns(struct iscsi_context *iscsi)
{
    struct scsi_task *task = NULL;
    struct scsi_reportluns_list *list = NULL;
    int full_size;

    if (!(task = iscsi_reportluns_sync(iscsi, 0, 16))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to reportluns: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

    full_size = scsi_datain_getfullsize(task);

    if (full_size > task->datain.size) {
        scsi_free_scsi_task(task);
        if (!(task = iscsi_reportluns_sync(iscsi, 0, full_size))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to reportluns: %s"),
                           iscsi_get_error(iscsi));
            goto cleanup;
        }
    }

    if (!(list = scsi_datain_unmarshall(task))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to unmarshall reportluns: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

 cleanup:
    scsi_free_scsi_task(task);
    return list;
}

static int
virISCSIDirectTestUnitReady(struct iscsi_context *iscsi,
                            int lun)
{
    struct scsi_task *task = NULL;
    int ret = -1;
    virTimeBackOffVar timebackoff;

    if (virTimeBackOffStart(&timebackoff, 1,
                            VIR_ISCSI_TEST_UNIT_TIMEOUT) < 0)
        goto cleanup;

    do {
        if (!(task = iscsi_testunitready_sync(iscsi, lun))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed testunitready: %s"),
                           iscsi_get_error(iscsi));
            goto cleanup;
        }

        if (task->status != SCSI_STATUS_CHECK_CONDITION ||
            task->sense.key != SCSI_SENSE_UNIT_ATTENTION ||
            task->sense.ascq != SCSI_SENSE_ASCQ_BUS_RESET)
            break;

        scsi_free_scsi_task(task);
    } while (virTimeBackOffWait(&timebackoff));

    if (task->status != SCSI_STATUS_GOOD) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed testunitready: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

    ret = 0;
 cleanup:
    scsi_free_scsi_task(task);
    return ret;
}

static int
virISCSIDirectSetVolumeAttributes(virStoragePoolObjPtr pool,
                                  virStorageVolDefPtr vol,
                                  int lun,
                                  char *portal)
{
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);

    if (virAsprintf(&vol->name, "%u", lun) < 0)
        return -1;
    if (virAsprintf(&vol->key, "ip-%s-iscsi-%s-lun-%u", portal,
                    def->source.devices[0].path, lun) < 0)
        return -1;
    if (virAsprintf(&vol->target.path, "ip-%s-iscsi-%s-lun-%u", portal,
                    def->source.devices[0].path, lun) < 0)
        return -1;
    return 0;
}

static int
virISCSIDirectSetVolumeCapacity(struct iscsi_context *iscsi,
                                virStorageVolDefPtr vol, int lun)
{
    struct scsi_task *task = NULL;
    struct scsi_inquiry_standard *inq = NULL;
    long long size = 0;
    int ret = -1;

    if (!(task = iscsi_inquiry_sync(iscsi, lun, 0, 0, 64)) ||
        task->status != SCSI_STATUS_GOOD) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to send inquiry command: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

    if (!(inq = scsi_datain_unmarshall(task))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to unmarshall reply: %s"),
                       iscsi_get_error(iscsi));
        goto cleanup;
    }

    if (inq->device_type == SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS) {
        struct scsi_readcapacity10 *rc10 = NULL;

        scsi_free_scsi_task(task);
        task = NULL;

        if (!(task = iscsi_readcapacity10_sync(iscsi, lun, 0, 0)) ||
            task->status != SCSI_STATUS_GOOD) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to get capacity of lun: %s"),
                           iscsi_get_error(iscsi));
            goto cleanup;
        }

        if (!(rc10 = scsi_datain_unmarshall(task))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to unmarshall reply: %s"),
                           iscsi_get_error(iscsi));
            goto cleanup;
        }

        size  = rc10->block_size;
        size *= rc10->lba;
        vol->target.capacity = size;
        vol->target.allocation = size;

    }

    ret = 0;
 cleanup:
    scsi_free_scsi_task(task);
    return ret;
}

static int
virISCSIDirectRefreshVol(virStoragePoolObjPtr pool,
                         struct iscsi_context *iscsi,
                         int lun,
                         char *portal)
{
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);
    virStorageVolDefPtr vol = NULL;
    int ret = -1;

    virStoragePoolObjClearVols(pool);
    if (virISCSIDirectTestUnitReady(iscsi, lun) < 0)
        goto cleanup;

    if (VIR_ALLOC(vol) < 0)
        goto cleanup;

    vol->type = VIR_STORAGE_VOL_NETWORK;

    if (virISCSIDirectSetVolumeCapacity(iscsi, vol, lun) < 0)
        goto cleanup;

    def->capacity += vol->target.capacity;
    def->allocation += vol->target.allocation;

    if (virISCSIDirectSetVolumeAttributes(pool, vol, lun, portal) < 0)
        goto cleanup;

    if (virStoragePoolObjAddVol(pool, vol) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to create volume: %d"),
                       lun);
        goto cleanup;
    }
    vol = NULL;

    ret = 0;
 cleanup:
    virStorageVolDefFree(vol);
    return ret;
}

static int
virStorageBackendISCSIDirectRefreshVols(virStoragePoolObjPtr pool,
                                        struct iscsi_context *iscsi,
                                        char *portal)
{
    struct scsi_reportluns_list *list = NULL;
    size_t i;

    if (!(list = virISCSIDirectReportLuns(iscsi)))
        return -1;
    for (i = 0; i < list->num; i++) {
        if (virISCSIDirectRefreshVol(pool, iscsi, list->luns[i], portal) < 0)
            return -1;
    }

    return 0;
}

static int
virISCSIDirectDisconnect(struct iscsi_context *iscsi)
{
    if (iscsi_logout_sync(iscsi) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to logout: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    if (iscsi_disconnect(iscsi) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to disconnect: %s"),
                       iscsi_get_error(iscsi));
        return -1;
    }
    return 0;
}

static int
virStorageBackendISCSIDirectCheckPool(virStoragePoolObjPtr pool,
                                      bool *isActive)
{
    *isActive = virStoragePoolObjIsActive(pool);
    return 0;
}

static int
virStorageBackendISCSIDirectRefreshPool(virStoragePoolObjPtr pool)
{
    virStoragePoolDefPtr def = virStoragePoolObjGetDef(pool);
    struct iscsi_context *iscsi = NULL;
    char *portal = NULL;
    int ret = -1;

    if (!(iscsi = virISCSIDirectCreateContext(def->source.initiator.iqn)))
        goto cleanup;
    if (!(portal = virStorageBackendISCSIDirectPortal(&def->source)))
        goto cleanup;
    if (virStorageBackendISCSIDirectSetAuth(iscsi, &def->source) < 0)
        goto cleanup;
    if (virISCSIDirectSetContext(iscsi, def->source.devices[0].path) < 0)
        goto cleanup;
    if (virISCSIDirectConnect(iscsi, portal) < 0)
        goto cleanup;
    if (virStorageBackendISCSIDirectRefreshVols(pool, iscsi, portal) < 0)
        goto disconect;

    ret = 0;
 disconect:
    virISCSIDirectDisconnect(iscsi);
 cleanup:
    iscsi_destroy_context(iscsi);
    VIR_FREE(portal);
    return ret;
}

virStorageBackend virStorageBackendISCSIDirect = {
    .type = VIR_STORAGE_POOL_ISCSI_DIRECT,

    .checkPool = virStorageBackendISCSIDirectCheckPool,
    .refreshPool = virStorageBackendISCSIDirectRefreshPool,
};

int
virStorageBackendISCSIDirectRegister(void)
{
    return virStorageBackendRegister(&virStorageBackendISCSIDirect);
}
