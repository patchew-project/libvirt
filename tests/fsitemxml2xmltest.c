#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include "internal.h"
#include "testutils.h"
#include "fs_conf.h"
#include "testutilsqemu.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static int
testCompareXMLToXMLFiles(const char *fspoolxml, const char *inxml,
                         const char *outxml, unsigned int flags)
{
    char *actual = NULL;
    int ret = -1;
    virFSPoolDefPtr fspool = NULL;
    virFSItemDefPtr dev = NULL;

    if (!(fspool = virFSPoolDefParseFile(fspoolxml)))
        goto fail;

    if (!(dev = virFSItemDefParseFile(fspool, inxml, flags)))
        goto fail;

    if (!(actual = virFSItemDefFormat(fspool, dev)))
        goto fail;

    if (virTestCompareToFile(actual, outxml) < 0)
        goto fail;

    ret = 0;

 fail:
    VIR_FREE(actual);
    virFSPoolDefFree(fspool);
    virFSItemDefFree(dev);
    return ret;
}

struct testInfo {
    const char *fspool;
    const char *name;
    unsigned int flags;
};

static int
testCompareXMLToXMLHelper(const void *data)
{
    int result = -1;
    const struct testInfo *info = data;
    char *fspoolxml = NULL;
    char *inxml = NULL;
    char *outxml = NULL;

    if (virAsprintf(&fspoolxml, "%s/fspoolxml2xmlin/%s.xml",
                    abs_srcdir, info->fspool) < 0 ||
        virAsprintf(&inxml, "%s/fsitemxml2xmlin/%s.xml",
                    abs_srcdir, info->name) < 0 ||
        virAsprintf(&outxml, "%s/fsitemxml2xmlout/%s.xml",
                    abs_srcdir, info->name) < 0) {
        goto cleanup;
    }

    result = testCompareXMLToXMLFiles(fspoolxml, inxml, outxml, info->flags);

 cleanup:
    VIR_FREE(fspoolxml);
    VIR_FREE(inxml);
    VIR_FREE(outxml);

    return result;
}


static int
mymain(void)
{
    int ret = 0;

#define DO_TEST_FULL(fspool, name, flags)                         \
    do {                                                        \
        struct testInfo info = { fspool, name, flags };           \
        if (virTestRun("FS Item XML-2-XML " name,           \
                       testCompareXMLToXMLHelper, &info) < 0)   \
            ret = -1;                                           \
    }                                                           \
    while (0);

#define DO_TEST(fspool, name) DO_TEST_FULL(fspool, name, 0)

    DO_TEST("fspool-dir", "item");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIRT_TEST_MAIN(mymain)
