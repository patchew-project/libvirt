/*
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

#include <stdlib.h>

#include "testutils.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

#include "storage/storage_util.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.storageutiltest");


struct testGlusterLookupParseData {
    const char *srcxml;
    const char *dstxml;
    bool netfs;
    int type;
};

static int
testGlusterLookupParse(const void *opaque)
{
    const struct testGlusterLookupParseData *data = opaque;
    virStoragePoolSourceList list = { .type = data->type,
                                      .nsources = 0,
                                      .sources = NULL
                                    };
    size_t i;
    char *srcxmldata = NULL;
    char *actual = NULL;
    int ret = -1;

    if (virTestLoadFile(data->srcxml, &srcxmldata) < 0)
        goto cleanup;

    if (virStorageUtilGlusterExtractPoolSources("testhost", srcxmldata,
                                                &list, data->netfs) < 0)
        goto cleanup;

    if (!(actual = virStoragePoolSourceListFormat(&list)))
        goto cleanup;

    ret = virTestCompareToFile(actual, data->dstxml);

 cleanup:
    VIR_FREE(srcxmldata);
    VIR_FREE(actual);

    for (i = 0; i < list.nsources; i++)
        virStoragePoolSourceClear(&list.sources[i]);
    VIR_FREE(list.sources);

    return ret;
}


static int
mymain(void)
{
    int ret = 0;

#define DO_TEST_GLUSTER_LOOKUP_FULL(testname, sffx, testnetfs, pooltype)       \
    do {                                                                       \
        struct testGlusterLookupParseData data;                                \
        data.srcxml = abs_srcdir "/virstorageutildata/"                        \
                      "gluster-parse-" testname "-src.xml";                    \
        data.dstxml = abs_srcdir "/virstorageutildata/"                        \
                      "gluster-parse-" testname "-" sffx  ".xml";              \
        data.netfs = testnetfs;                                                \
        data.type = pooltype;                                                  \
        if (virTestRun("gluster lookup " sffx " " testname,                    \
                       testGlusterLookupParse, &data) < 0)                     \
            ret = -1;                                                          \
    } while (0)

#define DO_TEST_GLUSTER_LOOKUP_NATIVE(testname)                                \
    DO_TEST_GLUSTER_LOOKUP_FULL(testname, "native", false, VIR_STORAGE_POOL_GLUSTER)
#define DO_TEST_GLUSTER_LOOKUP_NETFS(testname)                                 \
    DO_TEST_GLUSTER_LOOKUP_FULL(testname, "netfs", true, VIR_STORAGE_POOL_NETFS)

    DO_TEST_GLUSTER_LOOKUP_NATIVE("basic");
    DO_TEST_GLUSTER_LOOKUP_NETFS("basic");

#undef DO_TEST_GLUSTER_LOOKUP_NATIVE
#undef DO_TEST_GLUSTER_LOOKUP_NETFS
#undef DO_TEST_GLUSTER_LOOKUP_FULL

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIRT_TEST_MAIN(mymain)
