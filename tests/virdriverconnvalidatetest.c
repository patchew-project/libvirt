/*
 * Copyright (C) 2019 IBM Corporation
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
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"
#include "driver.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.driverconnvalidatetest");

struct testDriverConnValidateData {
    const char *uriPath;
    const char *entityName;
    bool privileged;
    bool expectFailure;
};


static int testDriverConnValidate(const void *args)
{
    const struct testDriverConnValidateData *data = args;

    bool ret = virConnectValidateURIPath(data->uriPath,
                                         data->entityName,
                                         data->privileged);
    if (data->expectFailure)
        ret = !ret;

    return ret ? 0 : -1;
}


static int
mymain(void)
{
    int ret = 0;
    struct testDriverConnValidateData data;

#define TEST_SUCCESS(name) \
    do  { \
        data.expectFailure = false; \
        if (virTestRun("Test conn URI path validate ok " name, \
                       testDriverConnValidate, &data) < 0) \
            ret = -1; \
    } while (0)

#define TEST_FAIL(name) \
    do  { \
        data.expectFailure = true; \
        if (virTestRun("Test conn URI path validate fail " name, \
                       testDriverConnValidate, &data) < 0) \
            ret = -1; \
    } while (0)

    /* Validation should always succeed with privileged user
     * and '/system' URI path
     */
    data.uriPath = "/system";
    data.privileged = true;

    data.entityName = "interface";
    TEST_SUCCESS("interface privileged /system");

    data.entityName = "network";
    TEST_SUCCESS("network privileged /system");

    data.entityName = "nodedev";
    TEST_SUCCESS("nodedev privileged /system");

    data.entityName = "secret";
    TEST_SUCCESS("secret privileged /system");

    data.entityName = "storage";
    TEST_SUCCESS("storage privileged /system");

    data.entityName = "qemu";
    TEST_SUCCESS("qemu privileged /system");

    data.entityName = "vbox";
    TEST_SUCCESS("vbox privileged /system");

    /* Fail URI path validation for all '/system' path with
     * unprivileged user
     */
    data.privileged = false;

    data.entityName = "interface";
    TEST_FAIL("interface unprivileged /system");

    data.entityName = "network";
    TEST_FAIL("network unprivileged /system");

    data.entityName = "nodedev";
    TEST_FAIL("nodedev unprivileged /system");

    data.entityName = "secret";
    TEST_FAIL("secret unprivileged /system");

    data.entityName = "storage";
    TEST_FAIL("storage unprivileged /system");

    data.entityName = "qemu";
    TEST_FAIL("qemu unprivileged /system");

    data.entityName = "vbox";
    TEST_FAIL("vbox unprivileged /system");

    /* Validation should always succeed with unprivileged user
     * and '/session' URI path
     */
    data.uriPath = "/session";

    data.entityName = "interface";
    TEST_SUCCESS("interface privileged /session");

    data.entityName = "network";
    TEST_SUCCESS("network privileged /session");

    data.entityName = "nodedev";
    TEST_SUCCESS("nodedev privileged /session");

    data.entityName = "secret";
    TEST_SUCCESS("secret privileged /session");

    data.entityName = "storage";
    TEST_SUCCESS("storage privileged /session");

    data.entityName = "qemu";
    TEST_SUCCESS("qemu privileged /session");

    data.entityName = "vbox";
    TEST_SUCCESS("vbox privileged /session");

    /* Fail URI path validation for all '/session' path with
     * privileged user
     */
    data.privileged = true;

    data.entityName = "interface";
    TEST_FAIL("interface privileged /session");

    data.entityName = "network";
    TEST_FAIL("network privileged /session");

    data.entityName = "nodedev";
    TEST_FAIL("nodedev privileged /session");

    data.entityName = "secret";
    TEST_FAIL("secret privileged /session");

    data.entityName = "storage";
    TEST_FAIL("storage privileged /session");

    /* ... except with qemu and vbox, where root can connect
     * with both /system and /session
     */
    data.entityName = "qemu";
    TEST_SUCCESS("qemu privileged /session");

    data.entityName = "vbox";
    TEST_SUCCESS("vbox privileged /session");

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
