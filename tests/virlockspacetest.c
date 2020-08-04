/*
 * Copyright (C) 2011, 2013, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

#include "testutils.h"
#include "virerror.h"
#include "viralloc.h"
#include "virfile.h"
#include "virlog.h"
#include "virutil.h"

#include "virlockspace.h"

#define VIR_FROM_THIS VIR_FROM_RPC

VIR_LOG_INIT("tests.lockspacetest");

#define LOCKSPACE_DIR abs_builddir "/virlockspacedata"

static int testLockSpaceCreate(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLifecycle(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, "foo") < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLockExcl(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(), 0) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(), 0) == 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") == 0)
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLockExclAuto(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLockShr(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_SHARED) < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(), 0) == 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_SHARED) < 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") == 0)
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") == 0)
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLockShrAuto(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(LOCKSPACE_DIR)))
        goto cleanup;

    if (!virFileIsDir(LOCKSPACE_DIR))
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, "foo") < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_SHARED |
                                    VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE) == 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, "foo", geteuid(),
                                    VIR_LOCK_SPACE_ACQUIRE_SHARED |
                                    VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, "foo", geteuid()) < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}


static int testLockSpaceResourceLockPath(const void *args G_GNUC_UNUSED)
{
    virLockSpacePtr lockspace;
    int ret = -1;

    rmdir(LOCKSPACE_DIR);

    if (!(lockspace = virLockSpaceNew(NULL)))
        goto cleanup;

    if (g_mkdir(LOCKSPACE_DIR, 0700) < 0)
        goto cleanup;

    if (virLockSpaceCreateResource(lockspace, LOCKSPACE_DIR "/foo") < 0)
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, LOCKSPACE_DIR "/foo", geteuid(), 0) < 0)
        goto cleanup;

    if (!virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    if (virLockSpaceAcquireResource(lockspace, LOCKSPACE_DIR "/foo", geteuid(), 0) == 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, LOCKSPACE_DIR "/foo") == 0)
        goto cleanup;

    if (virLockSpaceReleaseResource(lockspace, LOCKSPACE_DIR "/foo", geteuid()) < 0)
        goto cleanup;

    if (virLockSpaceDeleteResource(lockspace, LOCKSPACE_DIR "/foo") < 0)
        goto cleanup;

    if (virFileExists(LOCKSPACE_DIR "/foo"))
        goto cleanup;

    ret = 0;

 cleanup:
    virLockSpaceFree(lockspace);
    rmdir(LOCKSPACE_DIR);
    return ret;
}



static int
mymain(void)
{
    int ret = 0;

#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif /* WIN32 */

    if (virTestRun("Lockspace creation", testLockSpaceCreate, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res lifecycle", testLockSpaceResourceLifecycle, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res lock excl", testLockSpaceResourceLockExcl, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res lock shr", testLockSpaceResourceLockShr, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res lock excl auto", testLockSpaceResourceLockExclAuto, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res lock shr auto", testLockSpaceResourceLockShrAuto, NULL) < 0)
        ret = -1;

    if (virTestRun("Lockspace res full path", testLockSpaceResourceLockPath, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
