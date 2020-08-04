/*
 * Copyright (C) Red Hat, Inc. 2019
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"
#include "storage_conf.h"
#include "storage_capabilities.h"


#define VIR_FROM_THIS VIR_FROM_NONE


struct test_virStoragePoolCapsFormatData {
    const char *filename;
    virCapsPtr driverCaps;
};

static void
test_virCapabilitiesAddFullStoragePool(virCapsPtr caps)
{
    size_t i;

    for (i = 0; i < VIR_STORAGE_POOL_LAST; i++)
        virCapabilitiesAddStoragePool(caps, i);
}


static void
test_virCapabilitiesAddFSStoragePool(virCapsPtr caps)
{
    virCapabilitiesAddStoragePool(caps, VIR_STORAGE_POOL_FS);
}


static int
test_virStoragePoolCapsFormat(const void *opaque)
{
    struct test_virStoragePoolCapsFormatData *data =
        (struct test_virStoragePoolCapsFormatData *) opaque;
    virCapsPtr driverCaps = data->driverCaps;
    g_autoptr(virStoragePoolCaps) poolCaps = NULL;
    g_autofree char *path = NULL;
    g_autofree char *poolCapsXML = NULL;


    if (!(poolCaps = virStoragePoolCapsNew(driverCaps)))
        return -1;

    path = g_strdup_printf("%s/storagepoolcapsschemadata/poolcaps-%s.xml",
                           abs_srcdir, data->filename);

    if (!(poolCapsXML = virStoragePoolCapsFormat(poolCaps)))
        return -1;

    if (virTestCompareToFile(poolCapsXML, path) < 0)
        return -1;

    return 0;
}


static int
mymain(void)
{
    int ret = 0;
    g_autoptr(virCaps) fullCaps = NULL;
    g_autoptr(virCaps) fsCaps = NULL;

#define DO_TEST(Filename, DriverCaps) \
    do { \
        struct test_virStoragePoolCapsFormatData data = \
            {.filename = Filename, .driverCaps = DriverCaps }; \
        if (virTestRun(Filename, test_virStoragePoolCapsFormat, &data) < 0) \
            ret = -1; \
    } while (0)

    if (!(fullCaps = virCapabilitiesNew(VIR_ARCH_NONE, false, false)) ||
        !(fsCaps = virCapabilitiesNew(VIR_ARCH_NONE, false, false))) {
        return -1;
    }

    test_virCapabilitiesAddFullStoragePool(fullCaps);
    test_virCapabilitiesAddFSStoragePool(fsCaps);

    DO_TEST("full", fullCaps);
    DO_TEST("fs", fsCaps);

    return ret;
}

VIR_TEST_MAIN(mymain)
