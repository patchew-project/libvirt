/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "testutils.h"

#include "virfile.h"
#include "virfilecache.h"


#define VIR_FROM_THIS VIR_FROM_NONE


struct _testFileCacheObj {
    virObject parent;
    char *data;
};
typedef struct _testFileCacheObj testFileCacheObj;
typedef testFileCacheObj *testFileCacheObjPtr;


static virClassPtr testFileCacheObjClass;


static void
testFileCacheObjDispose(void *opaque)
{
    testFileCacheObjPtr obj = opaque;
    VIR_FREE(obj->data);
}


static int
testFileCacheObjOnceInit(void)
{
    if (!VIR_CLASS_NEW(testFileCacheObj, virClassForObject()))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(testFileCacheObj);


static testFileCacheObjPtr
testFileCacheObjNew(const char *data)
{
    testFileCacheObjPtr obj;

    if (testFileCacheObjInitialize() < 0)
        return NULL;

    if (!(obj = virObjectNew(testFileCacheObjClass)))
        return NULL;

    obj->data = g_strdup(data);

    return obj;
}


struct _testFileCachePriv {
    bool dataSaved;
    const char *newData;
    const char *expectData;
};
typedef struct _testFileCachePriv testFileCachePriv;
typedef testFileCachePriv *testFileCachePrivPtr;


static bool
testFileCacheIsValid(void *data,
                     void *priv)
{
    testFileCachePrivPtr testPriv = priv;
    testFileCacheObjPtr obj = data;

    return STREQ(testPriv->expectData, obj->data);
}


static void *
testFileCacheNewData(const char *name G_GNUC_UNUSED,
                     void *priv)
{
    testFileCachePrivPtr testPriv = priv;

    return testFileCacheObjNew(testPriv->newData);
}


static void *
testFileCacheLoadFile(const char *filename,
                      const char *name G_GNUC_UNUSED,
                      void *priv G_GNUC_UNUSED,
                      bool *outdated G_GNUC_UNUSED)
{
    testFileCacheObjPtr obj;
    char *data;

    if (virFileReadAll(filename, 20, &data) < 0)
        return NULL;

    obj = testFileCacheObjNew(data);

    VIR_FREE(data);
    return obj;
}


static int
testFileCacheSaveFile(void *data G_GNUC_UNUSED,
                      const char *filename G_GNUC_UNUSED,
                      void *priv)
{
    testFileCachePrivPtr testPriv = priv;

    testPriv->dataSaved = true;

    return 0;
}


virFileCacheHandlers testFileCacheHandlers = {
    .isValid = testFileCacheIsValid,
    .newData = testFileCacheNewData,
    .loadFile = testFileCacheLoadFile,
    .saveFile = testFileCacheSaveFile
};


struct _testFileCacheData {
    virFileCachePtr cache;
    const char *name;
    const char *newData;
    const char *expectData;
    bool expectSave;
};
typedef struct _testFileCacheData testFileCacheData;
typedef testFileCacheData *testFileCacheDataPtr;


static int
testFileCache(const void *opaque)
{
    int ret = -1;
    const testFileCacheData *data = opaque;
    testFileCacheObjPtr obj = NULL;
    testFileCachePrivPtr testPriv = virFileCacheGetPriv(data->cache);

    testPriv->dataSaved = false;
    testPriv->newData = data->newData;
    testPriv->expectData = data->expectData;

    if (!(obj = virFileCacheLookup(data->cache, data->name))) {
        fprintf(stderr, "Getting cached data failed.\n");
        goto cleanup;
    }

    if (!obj->data || STRNEQ(data->expectData, obj->data)) {
        fprintf(stderr, "Expect data '%s', loaded data '%s'.\n",
                data->expectData, NULLSTR(obj->data));
        goto cleanup;
    }

    if (data->expectSave != testPriv->dataSaved) {
        fprintf(stderr, "Expect data to be saved '%s', data saved '%s'.\n",
                data->expectSave ? "yes" : "no",
                testPriv->dataSaved ? "yes" : "no");
        goto cleanup;
    }

    ret = 0;

 cleanup:
    virObjectUnref(obj);
    return ret;
}


static int
mymain(void)
{
    int ret = 0;
    testFileCachePriv testPriv = {0};
    virFileCachePtr cache = NULL;

    if (!(cache = virFileCacheNew(abs_srcdir "/virfilecachedata",
                                  "cache", &testFileCacheHandlers)))
        return EXIT_FAILURE;

    virFileCacheSetPriv(cache, &testPriv);

#define TEST_RUN(name, newData, expectData, expectSave) \
    do { \
        testFileCacheData data = { \
            cache, name, newData, expectData, expectSave \
        }; \
        if (virTestRun(name, testFileCache, &data) < 0) \
            ret = -1; \
    } while (0)

    /* The cache file name is created using:
     * '$ echo -n $TEST_NAME | sha256sum' */
    TEST_RUN("cacheValid", NULL, "aaa\n", false);
    TEST_RUN("cacheInvalid", "bbb\n", "bbb\n", true);
    TEST_RUN("cacheMissing", "ccc\n", "ccc\n", true);

    virObjectUnref(cache);

    return ret != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

VIR_TEST_MAIN_PRELOAD(mymain, VIR_TEST_MOCK("virfilecache"))
