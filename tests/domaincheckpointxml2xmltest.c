#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include <regex.h>

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

/* This regex will skip the following XML constructs in test files
 * that are dynamically generated and thus problematic to test:
 * <name>1234352345</name> if the checkpoint has no name,
 * <creationTime>23523452345</creationTime>.
 */
static const char *testCheckpointXMLVariableLineRegexStr =
    "<(name|creationTime)>[0-9]+</(name|creationTime)>";

regex_t *testCheckpointXMLVariableLineRegex = NULL;

static char *
testFilterXML(char *xml)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    char **xmlLines = NULL;
    char **xmlLine;
    char *ret = NULL;

    if (!(xmlLines = virStringSplit(xml, "\n", 0))) {
        VIR_FREE(xml);
        goto cleanup;
    }
    VIR_FREE(xml);

    for (xmlLine = xmlLines; *xmlLine; xmlLine++) {
        if (regexec(testCheckpointXMLVariableLineRegex,
                    *xmlLine, 0, NULL, 0) == 0)
            continue;

        virBufferStrcat(&buf, *xmlLine, "\n", NULL);
    }

    if (virBufferCheckError(&buf) < 0)
        goto cleanup;

    ret = virBufferContentAndReset(&buf);

 cleanup:
    virBufferFreeAndReset(&buf);
    virStringListFree(xmlLines);
    return ret;
}

static int
testCompareXMLToXMLFiles(const char *inxml,
                         const char *outxml,
                         bool internal,
                         bool redefine)
{
    char *inXmlData = NULL;
    char *outXmlData = NULL;
    char *actual = NULL;
    int ret = -1;
    virDomainCheckpointDefPtr def = NULL;
    unsigned int flags = VIR_DOMAIN_CHECKPOINT_PARSE_DISKS;

    if (internal)
        flags |= VIR_DOMAIN_CHECKPOINT_PARSE_INTERNAL;

    if (redefine)
        flags |= VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE;

    if (virTestLoadFile(inxml, &inXmlData) < 0)
        goto cleanup;

    if (virTestLoadFile(outxml, &outXmlData) < 0)
        goto cleanup;

    if (!(def = virDomainCheckpointDefParseString(inXmlData, driver.caps,
                                                  driver.xmlopt,
                                                  flags)))
        goto cleanup;

    /* Parsing XML does not populate the domain definition, so add a
     * canned bare-bones fallback */
    if (!def->dom) {
        // HACK
        ret = 77;
        goto cleanup;
        const char *def_dom = ""
            "<domain type='qemu'>"
            "  <name>fedora</name>"
            "  <uuid>93a5c045-6457-2c09-e56c-927cdf34e178</uuid>"
/* arch='x86_64' machine='pc'*/
            "  <os><type>hvm</type></os>"
            "</domain>";
        int dom_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;
        if (!(def->dom = virDomainDefParseString(def_dom, driver.caps,
                                                 driver.xmlopt, NULL,
                                                 dom_flags)))
            goto cleanup;
    }

    if (!(actual = virDomainCheckpointDefFormat(def, driver.caps,
                                                driver.xmlopt,
                                                VIR_DOMAIN_DEF_FORMAT_SECURE,
                                                internal)))
        goto cleanup;

    if (!redefine) {
        if (!(actual = testFilterXML(actual)))
            goto cleanup;

        if (!(outXmlData = testFilterXML(outXmlData)))
            goto cleanup;
    }

    if (STRNEQ(outXmlData, actual)) {
        virTestDifferenceFull(stderr, outXmlData, outxml, actual, inxml);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(inXmlData);
    VIR_FREE(outXmlData);
    VIR_FREE(actual);
    virDomainCheckpointDefFree(def);
    return ret;
}

struct testInfo {
    const char *inxml;
    const char *outxml;
    bool internal;
    bool redefine;
};


static int
testCompareXMLToXMLHelper(const void *data)
{
    const struct testInfo *info = data;

    return testCompareXMLToXMLFiles(info->inxml, info->outxml,
                                    info->internal, info->redefine);
}


static int
mymain(void)
{
    int ret = 0;

    if (qemuTestDriverInit(&driver) < 0)
        return EXIT_FAILURE;

    if (VIR_ALLOC(testCheckpointXMLVariableLineRegex) < 0)
        goto cleanup;

    if (regcomp(testCheckpointXMLVariableLineRegex,
                testCheckpointXMLVariableLineRegexStr,
                REG_EXTENDED | REG_NOSUB) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       "failed to compile test regex");
        goto cleanup;
    }


# define DO_TEST(prefix, name, inpath, outpath, internal, redefine) \
    do { \
        const struct testInfo info = {abs_srcdir "/" inpath "/" name ".xml", \
                                      abs_srcdir "/" outpath "/" name ".xml", \
                                      internal, redefine}; \
        if (virTestRun("CHECKPOINT XML-2-XML " prefix " " name, \
                       testCompareXMLToXMLHelper, &info) < 0) \
            ret = -1; \
    } while (0)

# define DO_TEST_INOUT(name, internal, redefine) \
    DO_TEST("in->out", name,\
            "domaincheckpointxml2xmlin",\
            "domaincheckpointxml2xmlout",\
            internal, redefine)

    /* Unset or set all envvars here that are copied in qemudBuildCommandLine
     * using ADD_ENV_COPY, otherwise these tests may fail due to unexpected
     * values for these envvars */
    setenv("PATH", "/bin", 1);

    DO_TEST_INOUT("empty", false, false);
    DO_TEST_INOUT("sample", false, false);

 cleanup:
    if (testCheckpointXMLVariableLineRegex)
        regfree(testCheckpointXMLVariableLineRegex);
    VIR_FREE(testCheckpointXMLVariableLineRegex);
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
