/*
 * sysinfotest.c: Testcase(s) for virSysinfoRead
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright IBM Corp. 2012
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

#include <unistd.h>

#include "internal.h"

#include "virbuffer.h"
#include "virsysinfo.h"
#include "testutils.h"
#include "virfile.h"
#include "virstring.h"

#define LIBVIRT_VIRSYSINFOPRIV_H_ALLOW
#include "virsysinfopriv.h"

#define VIR_FROM_THIS VIR_FROM_NONE

struct testSysinfoData {
    virSysinfoDefPtr (*func)(void); /* sysinfo gathering function */
    char *decoder; /* name of dmi decoder binary/script */
    char *sysinfo; /* name of /proc/sysinfo substitute file */
    char *cpuinfo; /* name of /proc/cpuinfo substitute file */
    char *expected; /* (required) file containing output of virSysinfoFormat */
};

static int
testSysinfo(const void *data)
{
    const char *sysfsActualData;
    g_auto(virSysinfoDefPtr) ret = NULL;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    const struct testSysinfoData *testdata = data;

    virSysinfoSetup(testdata->decoder, testdata->sysinfo, testdata->cpuinfo);

    if (!testdata->expected ||
        !(ret = testdata->func()))
        return -1;

    if (virSysinfoFormat(&buf, ret) < 0)
        return -1;

    if (!(sysfsActualData = virBufferCurrentContent(&buf)))
        return -1;

    return virTestCompareToFile(sysfsActualData, testdata->expected);
}

static int
sysinfotest_run(const char *test,
                virSysinfoDefPtr (*func)(void),
                const char *decoder,
                const char *sysinfo,
                const char *cpuinfo,
                const char *expected)
{
    struct testSysinfoData testdata = { 0 };
    int ret = EXIT_FAILURE;

    testdata.func = func;

    if (decoder)
        testdata.decoder = g_strdup_printf("%s/%s", abs_srcdir, decoder);
    if (sysinfo)
        testdata.sysinfo = g_strdup_printf("%s/%s", abs_srcdir, sysinfo);
    if (cpuinfo)
        testdata.cpuinfo = g_strdup_printf("%s/%s", abs_srcdir, cpuinfo);
    if (expected)
        testdata.expected = g_strdup_printf("%s/%s", abs_srcdir, expected);

    if (virTestRun(test, testSysinfo, &testdata) < 0)
        goto error;

    ret = EXIT_SUCCESS;

 error:
    VIR_FREE(testdata.decoder);
    VIR_FREE(testdata.sysinfo);
    VIR_FREE(testdata.cpuinfo);
    VIR_FREE(testdata.expected);
    return ret;
}

#define TEST_FULL(name, func, decoder) \
    if (sysinfotest_run(name " sysinfo", func, decoder, \
                        "/sysinfodata/" name "sysinfo.data", \
                        "/sysinfodata/" name "cpuinfo.data", \
                        "/sysinfodata/" name "sysinfo.expect") != EXIT_SUCCESS) \
        ret = EXIT_FAILURE


#define TEST(name, func) \
        TEST_FULL(name, func, NULL)

static int
mymain(void)
{
    int ret = EXIT_SUCCESS;

    TEST("s390", virSysinfoReadS390);
    TEST("s390-freq", virSysinfoReadS390);
    TEST("ppc", virSysinfoReadPPC);
    TEST_FULL("x86", virSysinfoReadDMI, "/sysinfodata/x86dmidecode.sh");
    TEST("arm", virSysinfoReadARM);
    TEST("arm-rpi2", virSysinfoReadARM);
    TEST("aarch64", virSysinfoReadARM);
    TEST("aarch64-moonshot", virSysinfoReadARM);
    TEST_FULL("aarch64-gigabyte", virSysinfoReadARM,
              "/sysinfodata/aarch64-gigabytedmidecode.sh");

    return ret;
}

#undef TEST
#undef TEST_FULL

VIR_TEST_MAIN(mymain)
