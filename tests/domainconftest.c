/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"

#include "domain_conf.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.domainconftest");

static virCapsPtr caps;
static virDomainXMLOptionPtr xmlopt;

struct testGetFilesystemData {
    const char *filename;
    const char *path;
    bool expectEntry;
};

static int testGetFilesystem(const void *opaque)
{
    int ret = -1;
    virDomainDefPtr def = NULL;
    char *filename = NULL;
    const struct testGetFilesystemData *data = opaque;
    virDomainFSDefPtr fsdef;

    filename = g_strdup_printf("%s/domainconfdata/%s.xml", abs_srcdir,
                               data->filename);

    if (!(def = virDomainDefParseFile(filename, xmlopt, NULL, 0)))
        goto cleanup;

    fsdef = virDomainGetFilesystemForTarget(def,
                                            data->path);
    if (!fsdef) {
        if (data->expectEntry) {
            fprintf(stderr, "Expected FS for path '%s' in '%s'\n",
                    data->path, filename);
            goto cleanup;
        }
    } else {
        if (!data->expectEntry) {
            fprintf(stderr, "Unexpected FS for path '%s' in '%s'\n",
                    data->path, filename);
            goto cleanup;
        }
    }

    ret = 0;

 cleanup:
    virDomainDefFree(def);
    VIR_FREE(filename);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;

    if ((caps = virTestGenericCapsInit()) == NULL)
        goto cleanup;

    if (!(xmlopt = virTestGenericDomainXMLConfInit()))
        goto cleanup;

#define DO_TEST_GET_FS(fspath, expect) \
    do { \
        struct testGetFilesystemData data = { \
            .filename = "getfilesystem", \
            .path = fspath, \
            .expectEntry = expect, \
        }; \
        if (virTestRun("Get FS " fspath, testGetFilesystem, &data) < 0) \
            ret = -1; \
    } while (0)

    DO_TEST_GET_FS("/", true);
    DO_TEST_GET_FS("/dev", true);
    DO_TEST_GET_FS("/dev/pts", false);
    DO_TEST_GET_FS("/doesnotexist", false);

    virObjectUnref(caps);
    virObjectUnref(xmlopt);

 cleanup:
    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
