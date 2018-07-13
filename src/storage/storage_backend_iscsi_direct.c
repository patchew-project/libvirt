#include <config.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "datatypes.h"
#include "driver.h"
#include "storage_backend_iscsi_direct.h"
#include "storage_util.h"
#include "virlog.h"
#include "virobject.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("storage.storage_backend_iscsi_direct");


static int
virStorageBackendISCSIDirectCheckPool(virStoragePoolObjPtr pool ATTRIBUTE_UNUSED,
                                      bool *isActive ATTRIBUTE_UNUSED)
{
    return 0;
}

static int
virStorageBackendISCSIDirectRefreshPool(virStoragePoolObjPtr pool ATTRIBUTE_UNUSED)
{
    return 0;
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
