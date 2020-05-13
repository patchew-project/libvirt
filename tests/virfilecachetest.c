/*
 * Copyright (C) 2017 Red Hat, Inc.
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
 */

#include <config.h>

#include "testutils.h"

#include "virfile.h"
#include "virfilecache.h"

#include <glib-object.h>

#define VIR_FROM_THIS VIR_FROM_NONE


struct _testFileCacheObj {
    GObject parent;
    char *data;
};
#define TYPE_TEST_FILE_CACHE_OBJ test_file_cache_obj_get_type()
G_DECLARE_FINAL_TYPE(testFileCacheObj, test_file_cache_obj, TEST, FILE_CACHE_OBJ, GObject);
typedef testFileCacheObj *testFileCacheObjPtr;


G_DEFINE_TYPE(testFileCacheObj, test_file_cache_obj, G_TYPE_OBJECT);


static void
testFileCacheObjFinalize(GObject *opaque)
{
    testFileCacheObjPtr obj = TEST_FILE_CACHE_OBJ(opaque);
    VIR_FREE(obj->data);

    G_OBJECT_CLASS(test_file_cache_obj_parent_class)->finalize(opaque);
}

static void
test_file_cache_obj_init(testFileCacheObj *obj G_GNUC_UNUSED)
{
}

static void
test_file_cache_obj_class_init(testFileCacheObjClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = testFileCacheObjFinalize;
}


static testFileCacheObjPtr
testFileCacheObjNew(const char *data)
{
    testFileCacheObjPtr obj = TEST_FILE_CACHE_OBJ(g_object_new(TYPE_TEST_FILE_CACHE_OBJ, NULL));

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
testFileCacheIsValid(GObject *data,
                     void *priv)
{
    testFileCachePrivPtr testPriv = priv;
    testFileCacheObjPtr obj = TEST_FILE_CACHE_OBJ(data);

    return obj && STREQ(testPriv->expectData, obj->data);
}


static GObject *
testFileCacheNewData(const char *name G_GNUC_UNUSED,
                     void *priv)
{
    testFileCachePrivPtr testPriv = priv;

    return G_OBJECT(testFileCacheObjNew(testPriv->newData));
}


static GObject *
testFileCacheLoadFile(const char *filename,
                      const char *name G_GNUC_UNUSED,
                      void *priv G_GNUC_UNUSED)
{
    testFileCacheObjPtr obj;
    char *data;

    if (virFileReadAll(filename, 20, &data) < 0)
        return NULL;

    obj = testFileCacheObjNew(data);

    VIR_FREE(data);
    return G_OBJECT(obj);
}


static int
testFileCacheSaveFile(GObject *data G_GNUC_UNUSED,
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
    const testFileCacheData *data = opaque;
    g_autoptr(testFileCacheObj) obj = NULL;
    testFileCachePrivPtr testPriv = virFileCacheGetPriv(data->cache);

    testPriv->dataSaved = false;
    testPriv->newData = data->newData;
    testPriv->expectData = data->expectData;

    if (!(obj = virFileCacheLookup(data->cache, data->name))) {
        fprintf(stderr, "Getting cached data failed.\n");
        return -1;
    }

    if (!obj->data || STRNEQ(data->expectData, obj->data)) {
        fprintf(stderr, "Expect data '%s', loaded data '%s'.\n",
                data->expectData, NULLSTR(obj->data));
        return -1;
    }

    if (data->expectSave != testPriv->dataSaved) {
        fprintf(stderr, "Expect data to be saved '%s', data saved '%s'.\n",
                data->expectSave ? "yes" : "no",
                testPriv->dataSaved ? "yes" : "no");
        return -1;
    }

    return 0;
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
