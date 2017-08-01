/*
 * libxlxml2domconfigtest.c: test conversion of domXML to
 * libxl_domain_config structure.
 *
 * Copyright (C) 2017 SUSE LINUX Products GmbH, Nuernberg, Germany.
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
 * Author: Jim Fehlig <jfehlig@suse.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include "testutils.h"

#if defined(WITH_LIBXL) && defined(WITH_YAJL) && defined(HAVE_LIBXL_DOMAIN_CONFIG_FROM_JSON)

# include "internal.h"
# include "viralloc.h"
# include "libxl/libxl_conf.h"
# include "datatypes.h"
# include "virstring.h"
# include "virmock.h"
# include "virjson.h"
# include "testutilsxen.h"

# define VIR_FROM_THIS VIR_FROM_LIBXL

static const char *abs_top_srcdir;
static virCapsPtr xencaps;

static int
testCompareXMLToDomConfig(const char *xmlfile,
                          const char *jsonfile)
{
    int ret = -1;
    libxl_domain_config actualconfig;
    libxl_domain_config expectconfig;
    xentoollog_logger *log = NULL;
    libxl_ctx *ctx = NULL;
    virPortAllocatorPtr gports = NULL;
    virDomainXMLOptionPtr xmlopt = NULL;
    virDomainDefPtr vmdef = NULL;
    char *actualjson = NULL;
    char *tempjson = NULL;
    char *expectjson = NULL;

    libxl_domain_config_init(&actualconfig);
    libxl_domain_config_init(&expectconfig);

    if (!(log = (xentoollog_logger *)xtl_createlogger_stdiostream(stderr, XTL_DEBUG, 0)))
        goto cleanup;

    if (libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, log) < 0)
        goto cleanup;

    if (!(gports = virPortAllocatorNew("vnc", 5900, 6000,
                                       VIR_PORT_ALLOCATOR_SKIP_BIND_CHECK)))
        goto cleanup;

    if (!(xmlopt = libxlCreateXMLConf()))
        goto cleanup;

    if (!(vmdef = virDomainDefParseFile(xmlfile, xencaps, xmlopt,
                                        NULL, VIR_DOMAIN_XML_INACTIVE)))
        goto cleanup;

    if (libxlBuildDomainConfig(gports, vmdef, NULL, ctx, xencaps, &actualconfig) < 0)
        goto cleanup;

    if (!(actualjson = libxl_domain_config_to_json(ctx, &actualconfig))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       "Failed to retrieve JSON doc for libxl_domain_config");
        goto cleanup;
    }

    virTestLoadFile(jsonfile, &tempjson);
    if (libxl_domain_config_from_json(ctx, &expectconfig, tempjson) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       "Failed to create libxl_domain_config from JSON doc");
        goto cleanup;
    }
    if (!(expectjson = libxl_domain_config_to_json(ctx, &expectconfig))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       "Failed to retrieve JSON doc for libxl_domain_config");
        goto cleanup;
    }

    if (virTestCompareToString(expectjson, actualjson) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(expectjson);
    VIR_FREE(actualjson);
    VIR_FREE(tempjson);
    virDomainDefFree(vmdef);
    virObjectUnref(gports);
    virObjectUnref(xmlopt);
    libxl_ctx_free(ctx);
    libxl_domain_config_dispose(&actualconfig);
    libxl_domain_config_dispose(&expectconfig);
    xtl_logger_destroy(log);
    return ret;
}


struct testInfo {
    const char *name;
};


static int
testCompareXMLToDomConfigHelper(const void *data)
{
    int ret = -1;
    const struct testInfo *info = data;
    char *xmlfile = NULL;
    char *jsonfile = NULL;

    if (virAsprintf(&xmlfile, "%s/libxlxml2domconfigdata/%s.xml",
                    abs_srcdir, info->name) < 0 ||
        virAsprintf(&jsonfile, "%s/libxlxml2domconfigdata/%s.json",
                    abs_srcdir, info->name) < 0)
        goto cleanup;

    ret = testCompareXMLToDomConfig(xmlfile, jsonfile);

 cleanup:
    VIR_FREE(xmlfile);
    VIR_FREE(jsonfile);
    return ret;
}


static int
mymain(void)
{
    int ret = 0;

    abs_top_srcdir = getenv("abs_top_srcdir");
    if (!abs_top_srcdir)
        abs_top_srcdir = abs_srcdir "/..";

    /* Set the timezone because we are mocking the time() function.
     * If we don't do that, then localtime() may return unpredictable
     * results. In order to detect things that just work by a blind
     * chance, we need to set an virtual timezone that no libvirt
     * developer resides in. */
    if (setenv("TZ", "VIR00:30", 1) < 0) {
        perror("setenv");
        return EXIT_FAILURE;
    }

    if ((xencaps = testXenCapsInit()) == NULL)
        return EXIT_FAILURE;

# define DO_TEST(name)                                                  \
    do {                                                                \
        static struct testInfo info = {                                 \
            name,                                                       \
        };                                                              \
        if (virTestRun("LibXL XML-2-JSON " name,                        \
                        testCompareXMLToDomConfigHelper, &info) < 0)    \
            ret = -1;                                                   \
    } while (0)

    DO_TEST("basic-pv");
    DO_TEST("basic-hvm");
    DO_TEST("moredevs-hvm");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain, abs_builddir "/.libs/virmocklibxl.so")

#else

int main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_LIBXL && WITH_YAJL && HAVE_LIBXL_DOMAIN_CONFIG_FROM_JSON */
