/*
 * storagebackendlinstortest.c: test for linstor storage backend
 *
 * Copyright (C) 2020-2021 Rene Peinthor
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
#include <fcntl.h>
#include <stdio.h>

#include "internal.h"
#include "testutils.h"
#define LIBVIRT_STORAGE_BACKEND_LINSTOR_PRIV_H_ALLOW
#include "storage/storage_backend_linstor_priv.h"

#define VIR_FROM_THIS VIR_FROM_NONE

struct testStoragePoolListParserData {
    const char *input_json;
    const char *poolxml;
    const char *node_name;
    int expected_return;
    uint64_t expected_capacity;
    uint64_t expected_allocation;
};

struct testVolumeDefinitionListParserData {
    const char *input_json;
    const char *poolxml;
    const char *volxml;
    int expected_return;
    uint64_t expected_capacity;
};

struct testResourceListParserData {
    const char *rsclist_json;
    const char *voldeflist_json;
    const char *poolxml;
    const char *noden_name;
    const char *rsc_filter_json;
    int expected_return;
    size_t expected_volume_count;
};

struct testResourceGroupListParserData {
    const char *input_json;
    const char *poolxml;
    const char *rsc_grp;
    int expected_return;
    const char *expected_storpools;
};

struct testResourceDeployedData {
    const char *input_json;
    const char *rscname;
    const char *nodename;
    int expected_return;
};


static int
test_resourcegroup_list_parser(const void *opaque)
{
    const struct testResourceGroupListParserData *data = opaque;
    g_autofree char *poolxml = NULL;
    g_autoptr(virStoragePoolDef) pool = NULL;
    g_autofree char *inputJson = NULL;
    g_autofree char *indata = NULL;
    virJSONValuePtr storagePoolList = NULL;

    inputJson = g_strdup_printf("%s/linstorjsondata/%s",
                              abs_srcdir, data->input_json);

    poolxml = g_strdup_printf("%s/storagepoolxml2xmlin/%s",
                              abs_srcdir, data->poolxml);

    if (!(pool = virStoragePoolDefParseFile(poolxml))) {
        return -1;
    }

    if (virTestLoadFile(inputJson, &indata) < 0)
        return -1;

    if (virStorageBackendLinstorParseResourceGroupList(data->rsc_grp,
                                                       indata,
                                                       &storagePoolList) != 0) {
        virJSONValueFree(storagePoolList);
        return -1;
    }

    if (storagePoolList == NULL) {
        return -1;
    }

    if (g_strcmp0(virJSONValueToString(storagePoolList, false),
                  data->expected_storpools) != 0) {
            virJSONValueFree(storagePoolList);
            return -1;
    }

    virJSONValueFree(storagePoolList);
    return 0;
}


static int
run_test_resourcegroup_list_parser(void)
{
    int ret = 0;

    struct testResourceGroupListParserData rscGrpTest[] = {
        { "resource-group.json", "pool-linstor.xml", "libvirtgrp", 0, "[\"thinpool\"]" },
        { NULL, NULL, NULL, 0, NULL }
    };

    /* volumedefinition list parse */
    struct testResourceGroupListParserData *test = rscGrpTest;
    {
        while (test->input_json != NULL) {
            if (virTestRun(
                        "resourcegroup_list_parser",
                        test_resourcegroup_list_parser, test) < 0)
                ret = -1;
            ++test;
        }
    }
    return ret;
}


static int
test_storagepool_list_parser(const void *opaque)
{
    const struct testStoragePoolListParserData *data = opaque;
    g_autofree char *poolxml = NULL;
    g_autoptr(virStoragePoolDef) pool = NULL;
    g_autofree char *inputJson = NULL;
    g_autofree char *indata = NULL;

    inputJson = g_strdup_printf("%s/linstorjsondata/%s",
                              abs_srcdir, data->input_json);

    poolxml = g_strdup_printf("%s/storagepoolxml2xmlin/%s",
                              abs_srcdir, data->poolxml);

    if (!(pool = virStoragePoolDefParseFile(poolxml)))
        return -1;

    if (virTestLoadFile(inputJson, &indata) < 0)
        return -1;

    if (virStorageBackendLinstorParseStoragePoolList(pool, data->node_name, indata) !=
        data->expected_return)
        return -1;

    if (data->expected_return)
        return 0;

    if (pool->capacity == data->expected_capacity &&
        pool->allocation == data->expected_allocation)
        return 0;

    return -1;
}


static int
run_test_storagepool_list_parser(void)
{
    int ret = 0;

    struct testStoragePoolListParserData storPoolTest[] = {
        { "storage-pools-ssdpool.json", "pool-linstor.xml", "redfox", 0, 3078635913216, 760423070720 },
        { "storage-pools.json", "pool-linstor.xml", "silverfox", 0, 51088015228928, 1026862166016 },
        { NULL, NULL, NULL, 0, 0, 0 }
    };
    /* volumedefinition list parse */
    struct testStoragePoolListParserData *test = storPoolTest;
    {
        while (test->input_json != NULL) {
            if (virTestRun(
                        "test_storagepool_list_parser",
                        test_storagepool_list_parser, test) < 0)
                ret = -1;
            ++test;
        }
    }
    return ret;
}


static int
test_volumedefinition_list_parser(const void *opaque)
{
    const struct testVolumeDefinitionListParserData *data = opaque;
    g_autoptr(virStoragePoolDef) pool = NULL;
    g_autoptr(virStorageVolDef) vol = NULL;
    g_autofree char *poolxml = NULL;
    g_autofree char *volxml = NULL;
    g_autofree char *inputJson = NULL;
    g_autofree char *indata = NULL;

    inputJson = g_strdup_printf("%s/linstorjsondata/%s",
                              abs_srcdir, data->input_json);

    poolxml = g_strdup_printf("%s/storagepoolxml2xmlin/%s",
                              abs_srcdir, data->poolxml);

    volxml = g_strdup_printf("%s/storagevolxml2xmlin/%s",
                             abs_srcdir, data->volxml);

    if (!(pool = virStoragePoolDefParseFile(poolxml)))
        return -1;

    if (!(vol = virStorageVolDefParseFile(pool, volxml, 0)))
        return -1;

    if (virTestLoadFile(inputJson, &indata) < 0)
        return -1;

    if (virStorageBackendLinstorParseVolumeDefinition(vol, indata) !=
        data->expected_return)
        return -1;

    if (data->expected_return)
        return 0;

    if (vol->target.capacity == data->expected_capacity)
        return 0;

    return -1;
}


static int
run_test_volumedefinition_list_parser(void)
{
    int ret = 0;

    struct testVolumeDefinitionListParserData volumeDefTest[] = {
        { "volume-definition-test2.json", "pool-linstor.xml", "vol-linstor.xml", 0, 104857600 },
        { NULL, NULL, NULL, 0, 0 }
    };
    /* volumedefinition list parse */
    struct testVolumeDefinitionListParserData *test = volumeDefTest;
    {
        while (test->input_json != NULL) {
            if (virTestRun(
                        "volumedefinition_list_parser",
                        test_volumedefinition_list_parser, test) < 0)
                ret = -1;
            ++test;
        }
    }
    return ret;
}


static int
testResourceListParser(const void *opaque)
{
    int ret = -1;
    const struct testResourceListParserData *data = opaque;
    virStoragePoolObjPtr pool = NULL;
    virStoragePoolDefPtr poolDef = NULL;
    g_autofree char *poolxml = NULL;
    g_autofree char *rscListJsonFile = NULL;
    g_autofree char *volDefListJsonFile = NULL;
    g_autofree char *rscListData = NULL;
    g_autofree char *volDefListData = NULL;
    virJSONValuePtr rscFilterArr = NULL;

    rscListJsonFile = g_strdup_printf("%s/linstorjsondata/%s",
                              abs_srcdir, data->rsclist_json);

    volDefListJsonFile = g_strdup_printf("%s/linstorjsondata/%s",
                              abs_srcdir, data->voldeflist_json);

    poolxml = g_strdup_printf("%s/storagepoolxml2xmlin/%s",
                              abs_srcdir, data->poolxml);

    rscFilterArr = virJSONValueFromString(data->rsc_filter_json);

    if (!(poolDef = virStoragePoolDefParseFile(poolxml)))
        goto cleanup;

    if (!(pool = virStoragePoolObjNew()))
        goto cleanup;

    virStoragePoolObjSetDef(pool, poolDef);

    if (virTestLoadFile(rscListJsonFile, &rscListData) < 0)
        goto cleanup;

    if (virTestLoadFile(volDefListJsonFile, &volDefListData) < 0)
        goto cleanup;

    if (virStorageBackendLinstorParseResourceList(pool,
                                                  data->noden_name,
                                                  rscFilterArr,
                                                  rscListData,
                                                  volDefListData) != data->expected_return)
        goto cleanup;

    if (data->expected_return) {
        ret = 0;
        goto cleanup;
    }

    if (data->expected_volume_count == virStoragePoolObjGetVolumesCount(pool))
        ret = 0;

 cleanup:
    virStoragePoolObjEndAPI(&pool);
    return ret;
}

static int
runTestResourceListParser(void)
{
    int ret = 0;
    struct testResourceListParserData rscListParseData[] = {
        { "resource-list-test2.json", "volume-def-list.json", "pool-linstor.xml", "linstor1", "[\"test2\"]", 0, 1 },
        { NULL, NULL, NULL, NULL, NULL, 0, 0}
    };

    struct testResourceListParserData *test = rscListParseData;
    {
        while (test->rsclist_json != NULL) {
            if (virTestRun(
                        "virStorageBackendLinstorParseResourceList",
                        testResourceListParser, test) < 0)
                ret = -1;
            ++test;
        }
    }

    return ret;
}


static int
mymain(void)
{
    int ret = 0;

    ret = run_test_resourcegroup_list_parser() ? -1 : ret;
    ret = run_test_storagepool_list_parser() ? -1 : ret;
    ret = run_test_volumedefinition_list_parser() ? -1 : ret;
    ret = runTestResourceListParser() ? -1 : ret;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
