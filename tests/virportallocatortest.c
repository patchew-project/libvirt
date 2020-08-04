/*
 * Copyright (C) 2013-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>
#include "virfile.h"
#include "testutils.h"

#if HAVE_DLFCN_H
# include <dlfcn.h>
#endif

#if defined(__linux__) && defined(RTLD_NEXT)

# include "virerror.h"
# include "viralloc.h"
# include "virlog.h"
# include "virportallocator.h"
# include "virstring.h"

# define VIR_FROM_THIS VIR_FROM_RPC

VIR_LOG_INIT("tests.portallocatortest");

static int testAllocAll(const void *args G_GNUC_UNUSED)
{
    virPortAllocatorRangePtr ports = virPortAllocatorRangeNew("test", 5900, 5909);
    int ret = -1;
    unsigned short p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0;

    if (!ports)
        return -1;

    if (virPortAllocatorAcquire(ports, &p1) < 0)
        goto cleanup;
    if (p1 != 5901) {
        VIR_TEST_DEBUG("Expected 5901, got %d", p1);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p2) < 0)
        goto cleanup;
    if (p2 != 5902) {
        VIR_TEST_DEBUG("Expected 5902, got %d", p2);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p3) < 0)
        goto cleanup;
    if (p3 != 5903) {
        VIR_TEST_DEBUG("Expected 5903, got %d", p3);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p4) < 0)
        goto cleanup;
    if (p4 != 5907) {
        VIR_TEST_DEBUG("Expected 5907, got %d", p4);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p5) < 0)
        goto cleanup;
    if (p5 != 5908) {
        VIR_TEST_DEBUG("Expected 5908, got %d", p5);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p6) < 0)
        goto cleanup;
    if (p6 != 5909) {
        VIR_TEST_DEBUG("Expected 5909, got %d", p6);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p7) == 0) {
        VIR_TEST_DEBUG("Expected error, got %d", p7);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virPortAllocatorRelease(p1);
    virPortAllocatorRelease(p2);
    virPortAllocatorRelease(p3);
    virPortAllocatorRelease(p4);
    virPortAllocatorRelease(p5);
    virPortAllocatorRelease(p6);
    virPortAllocatorRelease(p7);

    virPortAllocatorRangeFree(ports);
    return ret;
}



static int testAllocReuse(const void *args G_GNUC_UNUSED)
{
    virPortAllocatorRangePtr ports = virPortAllocatorRangeNew("test", 5900, 5910);
    int ret = -1;
    unsigned short p1 = 0, p2 = 0, p3 = 0, p4 = 0;

    if (!ports)
        return -1;

    if (virPortAllocatorAcquire(ports, &p1) < 0)
        goto cleanup;
    if (p1 != 5901) {
        VIR_TEST_DEBUG("Expected 5901, got %d", p1);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p2) < 0)
        goto cleanup;
    if (p2 != 5902) {
        VIR_TEST_DEBUG("Expected 5902, got %d", p2);
        goto cleanup;
    }

    if (virPortAllocatorAcquire(ports, &p3) < 0)
        goto cleanup;
    if (p3 != 5903) {
        VIR_TEST_DEBUG("Expected 5903, got %d", p3);
        goto cleanup;
    }

    if (virPortAllocatorRelease(p2) < 0)
        goto cleanup;

    if (virPortAllocatorAcquire(ports, &p4) < 0)
        goto cleanup;
    if (p4 != 5902) {
        VIR_TEST_DEBUG("Expected 5902, got %d", p4);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virPortAllocatorRelease(p1);
    virPortAllocatorRelease(p3);
    virPortAllocatorRelease(p4);

    virPortAllocatorRangeFree(ports);
    return ret;
}


static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("Test alloc all", testAllocAll, NULL) < 0)
        ret = -1;

    if (virTestRun("Test alloc reuse", testAllocReuse, NULL) < 0)
        ret = -1;

    g_setenv("LIBVIRT_TEST_IPV4ONLY", "really", TRUE);

    if (virTestRun("Test IPv4-only alloc all", testAllocAll, NULL) < 0)
        ret = -1;

    if (virTestRun("Test IPv4-only alloc reuse", testAllocReuse, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain, VIR_TEST_MOCK("virportallocator"))
#else /* defined(__linux__) && defined(RTLD_NEXT) */
int
main(void)
{
    return EXIT_AM_SKIP;
}
#endif
