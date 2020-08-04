/*
 * Copyright (c) 2013. Doug Goldstein <cardoe@cardoe.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"

#ifdef WITH_VMWARE


# include "vmware/vmware_conf.h"

//# define VIR_FROM_THIS VIR_FROM_NONE

struct testInfo {
    const char *vmware_type;
    const char *name;
    unsigned long version;
};

static int
testVerStrParse(const void *data)
{
    const struct testInfo *info = data;
    int ret = -1;
    char *path = NULL;
    char *databuf = NULL;
    unsigned long version;
    int vmware_type;

    path = g_strdup_printf("%s/vmwareverdata/%s.txt", abs_srcdir, info->name);

    if (virTestLoadFile(path, &databuf) < 0)
        goto cleanup;

    if ((vmware_type = vmwareDriverTypeFromString(info->vmware_type)) < 0)
        goto cleanup;

    if (vmwareParseVersionStr(vmware_type, databuf, &version) < 0)
        goto cleanup;

    if (version != info->version) {
        fprintf(stderr, "%s: parsed versions do not match: got %lu, "
                "expected %lu\n", info->name, version, info->version);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(path);
    VIR_FREE(databuf);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;

# define DO_TEST(vmware_type, name, version) \
    do { \
        struct testInfo info = { \
            vmware_type, name, version \
        }; \
        if (virTestRun("VMware Version String Parsing " name, \
                       testVerStrParse, &info) < 0) \
            ret = -1; \
    } while (0)

    DO_TEST("ws", "workstation-7.0.0", 7000000);
    DO_TEST("ws", "workstation-7.0.0-with-garbage", 7000000);
    DO_TEST("fusion", "fusion-5.0.3", 5000003);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_VMWARE */
