#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include "internal.h"
#include "testutils.h"
#include "storage_conf.h"
#include "testutilsqemu.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static int
testCompareXMLToXMLFiles(const char *inxml,
                         const char *outxml,
                         bool expect_parse_fail)
{
    char *actual = NULL;
    int ret = -1;
    virStoragePoolDefPtr dev = NULL;
    unsigned int parse_flags = VIR_STORAGE_POOL_DEF_PARSE_VALIDATE_NAME;

    if (!(dev = virStoragePoolDefParseFile(inxml, parse_flags))) {
        if (expect_parse_fail) {
            VIR_TEST_DEBUG("Got expected parse failure msg='%s'",
                           virGetLastErrorMessage());
            virResetLastError();
            ret = 0;
        }
        goto fail;
    }

    if (!(actual = virStoragePoolDefFormat(dev)))
        goto fail;

    if (virTestCompareToFile(actual, outxml) < 0)
        goto fail;

    ret = 0;

 fail:
    VIR_FREE(actual);
    virStoragePoolDefFree(dev);
    return ret;
}


typedef struct test_params {
    const char *name;
    bool expect_parse_fail;
} test_params;

static int
testCompareXMLToXMLHelper(const void *data)
{
    int result = -1;
    char *inxml = NULL;
    char *outxml = NULL;
    const test_params *tp = data;

    if (virAsprintf(&inxml, "%s/storagepoolxml2xmlin/%s.xml",
                    abs_srcdir, tp->name) < 0 ||
        virAsprintf(&outxml, "%s/storagepoolxml2xmlout/%s.xml",
                    abs_srcdir, tp->name) < 0) {
        goto cleanup;
    }

    result = testCompareXMLToXMLFiles(inxml, outxml, tp->expect_parse_fail);

 cleanup:
    VIR_FREE(inxml);
    VIR_FREE(outxml);

    return result;
}

static int
mymain(void)
{
    int ret = 0;

#define DO_TEST_FULL(name, expect_parse_fail) \
    do { \
        test_params tp = {name, expect_parse_fail}; \
        if (virTestRun("Storage Pool XML-2-XML " name, \
                       testCompareXMLToXMLHelper, &tp) < 0) \
            ret = -1; \
    } while (0)

#define DO_TEST(name) \
    DO_TEST_FULL(name, false);

#define DO_TEST_PARSE_FAIL(name) \
    DO_TEST_FULL(name, true);

    DO_TEST("pool-dir");
    DO_TEST("pool-dir-naming");
    DO_TEST_PARSE_FAIL("pool-dir-whitespace-name");
    DO_TEST("pool-fs");
    DO_TEST("pool-logical");
    DO_TEST("pool-logical-nopath");
    DO_TEST("pool-logical-create");
    DO_TEST("pool-logical-noname");
    DO_TEST("pool-disk");
    DO_TEST("pool-disk-device-nopartsep");
    DO_TEST("pool-iscsi");
    DO_TEST("pool-iscsi-auth");
    DO_TEST("pool-netfs");
    DO_TEST("pool-netfs-gluster");
    DO_TEST("pool-netfs-cifs");
    DO_TEST("pool-scsi");
    DO_TEST("pool-scsi-type-scsi-host");
    DO_TEST("pool-scsi-type-fc-host");
    DO_TEST("pool-scsi-type-fc-host-managed");
    DO_TEST("pool-mpath");
    DO_TEST("pool-iscsi-multiiqn");
    DO_TEST("pool-iscsi-vendor-product");
    DO_TEST("pool-sheepdog");
    DO_TEST("pool-gluster");
    DO_TEST("pool-gluster-sub");
    DO_TEST("pool-scsi-type-scsi-host-stable");
    DO_TEST("pool-zfs");
    DO_TEST("pool-zfs-sourcedev");
    DO_TEST("pool-rbd");
    DO_TEST("pool-vstorage");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
