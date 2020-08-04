/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "virjson.h"
#include "virbuffer.h"
#include "virxml.h"
#include "testutils.h"
#include "testutilsqemu.h"
#include "qemumonitortestutils.h"
#include "qemu/qemu_migration_params.h"
#define LIBVIRT_QEMU_MIGRATION_PARAMSPRIV_H_ALLOW
#include "qemu/qemu_migration_paramspriv.h"
#include "qemu/qemu_monitor.h"

#define VIR_FROM_THIS VIR_FROM_NONE

typedef struct _qemuMigParamsData qemuMigParamsData;
struct _qemuMigParamsData {
    virDomainXMLOptionPtr xmlopt;
    const char *name;
};


static void
qemuMigParamsTestFormatXML(virBufferPtr buf,
                           qemuMigrationParamsPtr migParams)
{
    virBufferAddLit(buf, "<test>\n");
    virBufferAdjustIndent(buf, 2);

    if (migParams)
        qemuMigrationParamsFormat(buf, migParams);

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</test>\n");
}


static int
qemuMigParamsTestXML2XML(const void *opaque)
{
    const qemuMigParamsData *data = opaque;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    g_autofree char *xmlFile = NULL;
    g_autoptr(xmlDoc) doc = NULL;
    g_autoptr(xmlXPathContext) ctxt = NULL;
    g_autoptr(qemuMigrationParams) migParams = NULL;
    g_autofree char *actualXML = NULL;

    xmlFile = g_strdup_printf("%s/qemumigparamsdata/%s.xml", abs_srcdir,
                              data->name);

    if (!(doc = virXMLParseFileCtxt(xmlFile, &ctxt)))
        return -1;

    if (qemuMigrationParamsParse(ctxt, &migParams) < 0)
        return -1;

    qemuMigParamsTestFormatXML(&buf, migParams);

    if (!(actualXML = virBufferContentAndReset(&buf)))
        return -1;

    if (virTestCompareToFile(actualXML, xmlFile) < 0)
        return -1;

    return 0;
}


static int
qemuMigParamsTestXML(const void *opaque)
{
    const qemuMigParamsData *data = opaque;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    g_autofree char *replyFile = NULL;
    g_autofree char *xmlFile = NULL;
    qemuMonitorTestPtr mon = NULL;
    g_autoptr(virJSONValue) params = NULL;
    g_autoptr(qemuMigrationParams) migParams = NULL;
    g_autofree char *actualXML = NULL;
    int ret = -1;

    replyFile = g_strdup_printf("%s/qemumigparamsdata/%s.reply",
                                abs_srcdir, data->name);
    xmlFile = g_strdup_printf("%s/qemumigparamsdata/%s.xml",
                              abs_srcdir, data->name);

    if (!(mon = qemuMonitorTestNewFromFile(replyFile, data->xmlopt, true)))
        goto cleanup;

    if (qemuMonitorGetMigrationParams(qemuMonitorTestGetMonitor(mon),
                                      &params) < 0)
        goto cleanup;

    if (!(migParams = qemuMigrationParamsFromJSON(params)))
        goto cleanup;

    qemuMigParamsTestFormatXML(&buf, migParams);

    if (!(actualXML = virBufferContentAndReset(&buf)))
        goto cleanup;

    if (virTestCompareToFile(actualXML, xmlFile) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    qemuMonitorTestFree(mon);
    return ret;
}


static int
qemuMigParamsTestJSON(const void *opaque)
{
    const qemuMigParamsData *data = opaque;
    g_autofree char *replyFile = NULL;
    g_autofree char *jsonFile = NULL;
    qemuMonitorTestPtr mon = NULL;
    g_autoptr(virJSONValue) paramsIn = NULL;
    g_autoptr(virJSONValue) paramsOut = NULL;
    g_autoptr(qemuMigrationParams) migParams = NULL;
    g_autofree char *actualJSON = NULL;
    int ret = -1;

    replyFile = g_strdup_printf("%s/qemumigparamsdata/%s.reply",
                                abs_srcdir, data->name);
    jsonFile = g_strdup_printf("%s/qemumigparamsdata/%s.json",
                               abs_srcdir, data->name);

    if (!(mon = qemuMonitorTestNewFromFile(replyFile, data->xmlopt, true)))
        goto cleanup;

    if (qemuMonitorGetMigrationParams(qemuMonitorTestGetMonitor(mon),
                                      &paramsIn) < 0)
        goto cleanup;

    if (!(migParams = qemuMigrationParamsFromJSON(paramsIn)))
        goto cleanup;

    if (!(paramsOut = qemuMigrationParamsToJSON(migParams)) ||
        !(actualJSON = virJSONValueToString(paramsOut, true)))
        goto cleanup;

    if (virTestCompareToFile(actualJSON, jsonFile) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    qemuMonitorTestFree(mon);
    return ret;
}


static int
mymain(void)
{
    virQEMUDriver driver;
    int ret = 0;

    if (qemuTestDriverInit(&driver) < 0)
        return EXIT_FAILURE;

    virEventRegisterDefaultImpl();

#define DO_TEST(name) \
    do { \
        qemuMigParamsData data = { \
            driver.xmlopt, name \
        }; \
        if (virTestRun(name " (xml)", qemuMigParamsTestXML, &data) < 0) \
            ret = -1; \
        if (virTestRun(name " (json)", qemuMigParamsTestJSON, &data) < 0) \
            ret = -1; \
        if (virTestRun(name " (xml2xml)", qemuMigParamsTestXML2XML, &data) < 0) \
            ret = -1; \
    } while (0)

    DO_TEST("unsupported");
    DO_TEST("empty");
    DO_TEST("basic");
    DO_TEST("tls");
    DO_TEST("tls-enabled");
    DO_TEST("tls-hostname");

    qemuTestDriverFree(&driver);

    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
