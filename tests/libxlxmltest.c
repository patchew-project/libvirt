/*
 * libxlxmltest.c: Test xl.cfg(5) <-> libxl_domain_config conversion
 *
 * Copyright (C) 2007, 2010-2011, 2014 Red Hat, Inc.
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
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
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 * Author: Kiarie Kahurani <davidkiarie4@gmail.com>
 * Author: Marek Marczykowski-Górecki <marmarek@invisiblethingslab.com>
 *
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef WITH_YAJL2
# define HAVE_YAJL_V2 1
#endif
#include <libxl.h>
#include <libxl_json.h>

#include "internal.h"
#include "datatypes.h"
#include "viralloc.h"
#include "virstring.h"
#include "testutils.h"
#include "testutilsxen.h"
#include "libxl/libxl_conf.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static virCapsPtr caps;
static virDomainXMLOptionPtr xmlopt;
xentoollog_logger_stdiostream *logger;
libxl_ctx *ctx;


/*
 * This function provides a mechanism to replace variables in test
 * data files whose values are discovered at built time.
 */
static char *
testReplaceVarsXML(const char *xml)
{
    char *xmlcfgData;
    char *replacedXML;

    if (virTestLoadFile(xml, &xmlcfgData) < 0)
        return NULL;

    replacedXML = virStringReplace(xmlcfgData, "/LIBXL_FIRMWARE_DIR",
                                   LIBXL_FIRMWARE_DIR);

    /* libxl driver checks for emulator existence */
    replacedXML = virStringReplace(xmlcfgData, "/usr/lib/xen/bin/qemu-system-i386",
                                   "/bin/true");

    VIR_FREE(xmlcfgData);
    return replacedXML;
}

/*
 * Parses domXML to virDomainDef object, which is then converted to json
 * config and compared with expected config.
 */
static int
testCompareParseXML(const char *json, const char *xml, bool replaceVars)
{
    char *gotjsonData = NULL;
    virConfPtr conf = NULL;
    virConnectPtr conn = NULL;
    size_t wrote = 0;
    int ret = -1;
    virDomainDefPtr def = NULL;
    char *replacedXML = NULL;
    virPortAllocatorPtr reservedGraphicsPorts = NULL;
    libxl_domain_config d_config;
    yajl_gen gen = NULL;

    conn = virGetConnect();
    if (!conn) goto fail;

    if (replaceVars) {
        if (!(replacedXML = testReplaceVarsXML(xml)))
            goto fail;
        if (!(def = virDomainDefParseString(replacedXML, caps, xmlopt,
                                            NULL, VIR_DOMAIN_XML_INACTIVE)))
            goto fail;
    } else {
        if (!(def = virDomainDefParseFile(xml, caps, xmlopt,
                                          NULL, VIR_DOMAIN_XML_INACTIVE)))
            goto fail;
    }

    if (!(reservedGraphicsPorts = virPortAllocatorNew("VNC",
                              LIBXL_VNC_PORT_MIN,
                              LIBXL_VNC_PORT_MAX,
                              0)))
        goto fail;

    if (libxlBuildDomainConfig(reservedGraphicsPorts,
                def,
                NULL, /* channelDir, unused in current version */
                ctx,
                caps,
                &d_config))
        goto fail;

    if (!(gen = libxl_yajl_gen_alloc(NULL)))
        goto fail;

    if (libxl_domain_config_gen_json(gen, &d_config) != yajl_gen_status_ok)
        goto fail;

    if (yajl_gen_get_buf(gen,
                (const unsigned char **)&gotjsonData,
                &wrote) != yajl_gen_status_ok)
        goto fail;

    if (virTestCompareToFile(gotjsonData, json) < 0)
        goto fail;

    ret = 0;

 fail:
    VIR_FREE(replacedXML);
    // yajl_gen_free handle also gotjsonData
    if (gen)
        yajl_gen_free(gen);
    if (conf)
        virConfFree(conf);
    virObjectUnref(reservedGraphicsPorts);
    virDomainDefFree(def);
    virObjectUnref(conn);

    return ret;
}

struct testInfo {
    const char *name;
    bool replaceVars;
};

static int
testCompareHelper(const void *data)
{
    int result = -1;
    const struct testInfo *info = data;
    char *xml = NULL;
    char *json = NULL;

    if (virAsprintf(&xml, "%s/xlconfigdata/test-%s.xml",
                    abs_srcdir, info->name) < 0 ||
        virAsprintf(&json, "%s/xlconfigdata/test-%s.json",
                    abs_srcdir, info->name) < 0)
        goto cleanup;

    result = testCompareParseXML(json, xml, info->replaceVars);

 cleanup:
    VIR_FREE(xml);
    VIR_FREE(json);

    return result;
}


static int
mymain(void)
{
    int ret = 0;

    logger = xtl_createlogger_stdiostream(stderr, XTL_PROGRESS, 0);
    if (!logger)
        return EXIT_FAILURE;

    if (libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, (xentoollog_logger*)logger))
        return EXIT_AM_SKIP;

    if (!(caps = testXLInitCaps()))
        return EXIT_FAILURE;

    if (!(xmlopt = libxlCreateXMLConf()))
        return EXIT_FAILURE;

#define DO_TEST_PARSE(name, replace)                                    \
    do {                                                                \
        struct testInfo info0 = { name, replace };                   \
        if (virTestRun("Xen XML-2-json Parse  " name,                     \
                       testCompareHelper, &info0) < 0)                  \
            ret = -1;                                                   \
    } while (0)

#define DO_TEST(name)                                                   \
    do {                                                                \
        DO_TEST_PARSE(name, false);                                     \
    } while (0)

#define DO_TEST_REPLACE_VARS(name)                                      \
    do {                                                                \
        DO_TEST_PARSE(name, true);                                      \
    } while (0)

    DO_TEST_REPLACE_VARS("fullvirt-ovmf");

    DO_TEST("fullvirt-cpuid");

    virObjectUnref(caps);
    virObjectUnref(xmlopt);
    libxl_ctx_free(ctx);
    xtl_logger_destroy((xentoollog_logger*)logger);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
