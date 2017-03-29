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


static int
testGlusterLookupParse(const void *data)
{
    virStoragePoolSourceList list = { .type = VIR_STORAGE_POOL_GLUSTER,
                                      .nsources = 0,
                                      .sources = NULL
                                    };
    const char *testname = data;
    size_t i;
    char *srcxml = NULL;
    char *srcxmldata = NULL;
    char *destxml = NULL;
    char *actual = NULL;
    int ret = -1;

    if (virAsprintf(&srcxml,
                    "%s/virstorageutildata/gluster-parse-%s-src.xml",
                    abs_srcdir, testname) < 0 ||
        virAsprintf(&destxml,
                    "%s/virstorageutildata/gluster-parse-%s-dst.xml",
                    abs_srcdir, testname) < 0)
        goto cleanup;

    if (virTestLoadFile(srcxml, &srcxmldata) < 0)
        goto cleanup;

    if (virStorageUtilGlusterExtractPoolSources("testhost", srcxmldata,
                                                VIR_STORAGE_POOL_GLUSTER,
                                                &list) < 0)
        goto cleanup;

    if (!(actual = virStoragePoolSourceListFormat(&list)))
        goto cleanup;

    ret = virTestCompareToFile(actual, destxml);

 cleanup:
    VIR_FREE(srcxml);
    VIR_FREE(srcxmldata);
    VIR_FREE(destxml);
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

#define DO_TEST_GLUSTER_LOOKUP(testname)                                       \
    if (virTestRun("gluster lookup " testname, testGlusterLookupParse,         \
                   testname) < 0)                                              \
        ret = -1

    DO_TEST_GLUSTER_LOOKUP("basic");

#undef DO_TEST_GLUSTER_LOOKUP

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIRT_TEST_MAIN(mymain)
