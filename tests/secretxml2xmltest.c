#include <config.h>

#include <stdlib.h>

#include "internal.h"
#include "testutils.h"
#include "secret_conf.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static int
testCompareXMLToXMLFiles(const char *inxml,
                         const char *outxml,
                         bool expect_parse_fail)
{
    char *actual = NULL;
    int ret = -1;
    virSecretDefPtr secret = NULL;
    unsigned int parse_flags = VIR_SECRET_DEF_PARSE_VALIDATE_USAGE_ID;

    if (!(secret = virSecretDefParseFile(inxml, parse_flags))) {
        if (expect_parse_fail) {
            VIR_TEST_DEBUG("Got expected parse failure msg='%s'",
                           virGetLastErrorMessage());
            virResetLastError();
            ret = 0;
        }
        goto fail;
    }

    if (!(actual = virSecretDefFormat(secret)))
        goto fail;

    if (virTestCompareToFile(actual, outxml) < 0)
        goto fail;

    ret = 0;

 fail:
    VIR_FREE(actual);
    virSecretDefFree(secret);
    return ret;
}

struct testInfo {
    const char *name;
    bool different;
    bool expect_fail;
};

static int
testCompareXMLToXMLHelper(const void *data)
{
    int result = -1;
    char *inxml = NULL;
    char *outxml = NULL;
    const struct testInfo *info = data;

    if (virAsprintf(&inxml, "%s/secretxml2xmlin/%s.xml",
                    abs_srcdir, info->name) < 0 ||
        virAsprintf(&outxml, "%s/secretxml2xml%s/%s.xml",
                    abs_srcdir,
                    info->different ? "out" : "in",
                    info->name) < 0) {
        goto cleanup;
    }

    result = testCompareXMLToXMLFiles(inxml, outxml, info->expect_fail);

 cleanup:
    VIR_FREE(inxml);
    VIR_FREE(outxml);

    return result;
}

static int
mymain(void)
{
    int ret = 0;

#define DO_TEST_FULL(name, different, parse_fail) \
    do { \
        const struct testInfo info = {name, different, parse_fail}; \
        if (virTestRun("Secret XML->XML " name, \
                       testCompareXMLToXMLHelper, &info) < 0) \
            ret = -1; \
    } while (0)

#define DO_TEST(name) \
    DO_TEST_FULL(name, false, false)

#define DO_TEST_PARSE_FAIL(name) \
    DO_TEST_FULL(name, false, true)

    DO_TEST("ephemeral-usage-volume");
    DO_TEST("usage-volume");
    DO_TEST("usage-ceph");
    DO_TEST("usage-iscsi");
    DO_TEST("usage-tls");
    DO_TEST_PARSE_FAIL("usage-whitespace-invalid");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
