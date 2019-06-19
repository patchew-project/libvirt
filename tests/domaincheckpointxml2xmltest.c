#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include "testutils.h"

#ifdef WITH_QEMU

# include "internal.h"
# include "qemu/qemu_conf.h"
# include "qemu/qemu_domain.h"
# include "checkpoint_conf.h"
# include "testutilsqemu.h"
# include "virstring.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static virQEMUDriver driver;

enum {
    TEST_INTERNAL = 1 << 0, /* Test use of INTERNAL parse/format flag */
    TEST_REDEFINE = 1 << 1, /* Test use of REDEFINE parse flag */
    TEST_PARENT = 1 << 2, /* hard-code parent after parse */
    TEST_VDA_BITMAP = 1 << 3, /* hard-code disk vda after parse */
    TEST_SIZE = 1 << 4, /* Test use of SIZE format flag */
};

static int
testCompareXMLToXMLFiles(const char *inxml,
                         const char *outxml,
                         unsigned int flags)
{
    char *inXmlData = NULL;
    char *outXmlData = NULL;
    char *actual = NULL;
    int ret = -1;
    virDomainCheckpointDefPtr def = NULL;
    unsigned int parseflags = 0;
    unsigned int formatflags = VIR_DOMAIN_CHECKPOINT_FORMAT_SECURE;
    bool cur = false;

    if (flags & TEST_INTERNAL) {
        parseflags |= VIR_DOMAIN_CHECKPOINT_PARSE_INTERNAL;
        formatflags |= VIR_DOMAIN_CHECKPOINT_FORMAT_INTERNAL;
    }

    if (flags & TEST_REDEFINE)
        parseflags |= VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE;

    if (virTestLoadFile(inxml, &inXmlData) < 0)
        goto cleanup;

    if (virTestLoadFile(outxml, &outXmlData) < 0)
        goto cleanup;

    if (!(def = virDomainCheckpointDefParseString(inXmlData, driver.caps,
                                                  driver.xmlopt, &cur,
                                                  parseflags)))
        goto cleanup;
    if (cur) {
        if (!(flags & TEST_REDEFINE))
            goto cleanup;
        formatflags |= VIR_DOMAIN_CHECKPOINT_FORMAT_CURRENT;
    }
    if (flags & TEST_PARENT) {
        if (def->parent.parent_name)
            goto cleanup;
        if (VIR_STRDUP(def->parent.parent_name, "1525111885") < 0)
            goto cleanup;
    }
    if (flags & TEST_VDA_BITMAP) {
        virDomainCheckpointDiskDefPtr disk;

        if (VIR_EXPAND_N(def->disks, def->ndisks, 1) < 0)
            goto cleanup;
        disk = &def->disks[0];
        if (disk->bitmap)
            goto cleanup;
        if (!disk->name) {
            disk->type = VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP;
            if (VIR_STRDUP(disk->name, "vda") < 0)
                goto cleanup;
        } else if (STRNEQ(disk->name, "vda")) {
            goto cleanup;
        }
        if (VIR_STRDUP(disk->bitmap, def->parent.name) < 0)
            goto cleanup;
    }
    if (flags & TEST_SIZE) {
        def->disks[0].size = 1048576;
        formatflags |= VIR_DOMAIN_CHECKPOINT_FORMAT_SIZE;
    }

    /* Parsing XML does not populate the domain definition; work
     * around that by not requesting domain on output */
    if (!def->parent.dom)
        formatflags |= VIR_DOMAIN_CHECKPOINT_FORMAT_NO_DOMAIN;

    if (!(actual = virDomainCheckpointDefFormat(def, driver.caps,
                                                driver.xmlopt,
                                                formatflags)))
        goto cleanup;

    if (STRNEQ(outXmlData, actual)) {
        virTestDifferenceFull(stderr, outXmlData, outxml, actual, inxml);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(inXmlData);
    VIR_FREE(outXmlData);
    VIR_FREE(actual);
    virObjectUnref(def);
    return ret;
}

struct testInfo {
    const char *inxml;
    const char *outxml;
    long long creationTime;
    unsigned int flags;
};
static long long mocktime;

static int
testCheckpointPostParse(virDomainMomentDefPtr def)
{
    if (!mocktime)
        return 0;
    if (def->creationTime)
        return -1;
    def->creationTime = mocktime;
    if (!def->name &&
        virAsprintf(&def->name, "%lld", def->creationTime) < 0)
        return -1;
    return 0;
}

static int
testCompareXMLToXMLHelper(const void *data)
{
    const struct testInfo *info = data;

    mocktime = info->creationTime;
    return testCompareXMLToXMLFiles(info->inxml, info->outxml, info->flags);
}


static int
mymain(void)
{
    int ret = 0;

    if (qemuTestDriverInit(&driver) < 0)
        return EXIT_FAILURE;

    virDomainXMLOptionSetMomentPostParse(driver.xmlopt,
                                         testCheckpointPostParse);

# define DO_TEST(prefix, name, inpath, outpath, time, flags) \
    do { \
        const struct testInfo info = {abs_srcdir "/" inpath "/" name ".xml", \
                                      abs_srcdir "/" outpath "/" name ".xml", \
                                      time, flags}; \
        if (virTestRun("CHECKPOINT XML-2-XML " prefix " " name, \
                       testCompareXMLToXMLHelper, &info) < 0) \
            ret = -1; \
    } while (0)

# define DO_TEST_INOUT(name, time, flags) \
    DO_TEST("in->out", name, \
            "domaincheckpointxml2xmlin", \
            "domaincheckpointxml2xmlout", \
            time, flags)
# define DO_TEST_OUT(name, flags) \
    DO_TEST("out->out", name, \
            "domaincheckpointxml2xmlout", \
            "domaincheckpointxml2xmlout", \
            0, flags | TEST_REDEFINE)

    /* Unset or set all envvars here that are copied in qemudBuildCommandLine
     * using ADD_ENV_COPY, otherwise these tests may fail due to unexpected
     * values for these envvars */
    setenv("PATH", "/bin", 1);

    /* Tests of internal state saving - the <active> element is not
     * permitted or exposed to user XML, so the files are named to
     * skip schema validation */
    DO_TEST_OUT("internal-active-invalid", TEST_INTERNAL);
    DO_TEST_OUT("internal-inactive-invalid", TEST_INTERNAL);
    /* Test a normal user redefine */
    DO_TEST_OUT("redefine", 0);

    /* Tests of valid user input, and resulting output */
    DO_TEST_INOUT("empty", 1525889631, TEST_VDA_BITMAP);
    DO_TEST_INOUT("sample", 1525889631, TEST_PARENT | TEST_VDA_BITMAP);
    DO_TEST_INOUT("size", 1553648510,
                  TEST_PARENT | TEST_VDA_BITMAP | TEST_SIZE);

    qemuTestDriverFree(&driver);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_QEMU */
