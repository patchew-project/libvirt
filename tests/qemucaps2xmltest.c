/*
 * Copyright (C) 2014 Red Hat, Inc.
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

#include "testutils.h"
#include "testutilsqemu.h"
#include "qemu/qemu_capabilities.h"


#define VIR_FROM_THIS VIR_FROM_NONE


typedef struct _testQemuData testQemuData;
typedef testQemuData *testQemuDataPtr;
struct _testQemuData {
    const char *inputDir;
    const char *outputDir;
    const char *prefix;
    const char *version;
    const char *archName;
    const char *suffix;
    int ret;
};

static int
testQemuDataInit(testQemuDataPtr data)
{
    data->outputDir = abs_srcdir "/qemucaps2xmloutdata";

    data->ret = 0;

    return 0;
}

static virQEMUCapsPtr
testQemuGetCaps(char *caps)
{
    g_autoptr(virQEMUCaps) qemuCaps = NULL;
    xmlDocPtr xml;
    xmlXPathContextPtr ctxt = NULL;
    ssize_t i, n;
    xmlNodePtr *nodes = NULL;

    if (!(xml = virXMLParseStringCtxt(caps, "(test caps)", &ctxt)))
        goto error;

    if ((n = virXPathNodeSet("/qemuCaps/flag", ctxt, &nodes)) < 0) {
        fprintf(stderr, "failed to parse qemu capabilities flags");
        goto error;
    }

    if (!(qemuCaps = virQEMUCapsNew()))
        goto error;

    for (i = 0; i < n; i++) {
        char *str = virXMLPropString(nodes[i], "name");
        if (str) {
            int flag = virQEMUCapsTypeFromString(str);
            if (flag < 0) {
                fprintf(stderr, "Unknown qemu capabilities flag %s", str);
                VIR_FREE(str);
                goto error;
            }
            VIR_FREE(str);
            virQEMUCapsSet(qemuCaps, flag);
        }
    }

    VIR_FREE(nodes);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return g_steal_pointer(&qemuCaps);

 error:
    VIR_FREE(nodes);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return NULL;
}

static virCapsPtr
testGetCaps(char *capsData, const testQemuData *data)
{
    g_autoptr(virQEMUCaps) qemuCaps = NULL;
    g_autoptr(virCaps) caps = NULL;
    virArch arch = virArchFromString(data->archName);
    g_autofree char *binary = NULL;

    binary = g_strdup_printf("/usr/bin/qemu-system-%s", data->archName);

    if ((qemuCaps = testQemuGetCaps(capsData)) == NULL) {
        fprintf(stderr, "failed to parse qemu capabilities flags");
        return NULL;
    }

    caps = virCapabilitiesNew(arch, false, false);

    if (virQEMUCapsInitGuestFromBinary(caps,
                                       binary,
                                       qemuCaps,
                                       arch) < 0) {
        fprintf(stderr, "failed to create the capabilities from qemu");
        return NULL;
    }

    return g_steal_pointer(&caps);
}

static int
testQemuCapsXML(const void *opaque)
{
    int ret = -1;
    const testQemuData *data = opaque;
    char *capsFile = NULL, *xmlFile = NULL;
    char *capsData = NULL;
    char *capsXml = NULL;
    g_autoptr(virCaps) capsProvided = NULL;

    xmlFile = g_strdup_printf("%s/caps.%s.xml", data->outputDir, data->archName);

    capsFile = g_strdup_printf("%s/%s_%s.%s.%s",
                               data->inputDir, data->prefix, data->version,
                               data->archName, data->suffix);

    if (virTestLoadFile(capsFile, &capsData) < 0)
        goto cleanup;

    if (!(capsProvided = testGetCaps(capsData, data)))
        goto cleanup;

    capsXml = virCapabilitiesFormatXML(capsProvided);
    if (!capsXml)
        goto cleanup;

    if (virTestCompareToFile(capsXml, xmlFile) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(xmlFile);
    VIR_FREE(capsFile);
    VIR_FREE(capsXml);
    VIR_FREE(capsData);
    return ret;
}

static int
doCapsTest(const char *inputDir,
           const char *prefix,
           const char *version,
           const char *archName,
           const char *suffix,
           void *opaque)
{
    testQemuDataPtr data = (testQemuDataPtr) opaque;
    g_autofree char *title = NULL;

    title = g_strdup_printf("%s (%s)", version, archName);

    data->inputDir = inputDir;
    data->prefix = prefix;
    data->version = version;
    data->archName = archName;
    data->suffix = suffix;

    if (virTestRun(title, testQemuCapsXML, data) < 0)
        data->ret = -1;

    return 0;
}

static int
mymain(void)
{
    testQemuData data;

    virEventRegisterDefaultImpl();

    if (testQemuDataInit(&data) < 0)
        return EXIT_FAILURE;

    if (testQemuCapsIterate(".xml", doCapsTest, &data) < 0)
        return EXIT_FAILURE;

    return (data.ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain, VIR_TEST_MOCK("qemucaps2xml"))
