/*
 * cpu_arm.c: CPU driver for arm CPUs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) Canonical Ltd. 2012
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

#include "viralloc.h"
#include "cpu.h"
#include "cpu_map.h"
#include "virstring.h"
#include "virxml.h"

#define VIR_FROM_THIS VIR_FROM_CPU

static const virArch archs[] = {
    VIR_ARCH_ARMV6L,
    VIR_ARCH_ARMV7B,
    VIR_ARCH_ARMV7L,
    VIR_ARCH_AARCH64,
};

typedef struct _virCPUarmFeature virCPUarmFeature;
typedef virCPUarmFeature *virCPUarmFeaturePtr;
struct _virCPUarmFeature {
    char *name;
};

static virCPUarmFeaturePtr
virCPUarmFeatureNew(void)
{
    virCPUarmFeaturePtr feature;

    if (VIR_ALLOC(feature) < 0)
        return NULL;

    return feature;
}

static void
virCPUarmFeatureFree(virCPUarmFeaturePtr feature)
{
    if (!feature)
        return;

    VIR_FREE(feature->name);
    VIR_FREE(feature);
}

typedef struct _virCPUarmMap virCPUarmMap;
typedef virCPUarmMap *virCPUarmMapPtr;
struct _virCPUarmMap {
    size_t nfeatures;
    virCPUarmFeaturePtr *features;
};

static virCPUarmMapPtr
virCPUarmMapNew(void)
{
    virCPUarmMapPtr map;

    if (VIR_ALLOC(map) < 0)
        return NULL;

    return map;
}

static void
virCPUarmMapFree(virCPUarmMapPtr map)
{
    size_t i;

    if (!map)
        return;

    for (i = 0; i < map->nfeatures; i++)
        virCPUarmFeatureFree(map->features[i]);
    VIR_FREE(map->features);

    VIR_FREE(map);
}

static virCPUarmFeaturePtr
virCPUarmMapFeatureFind(virCPUarmMapPtr map,
                        const char *name)
{
    size_t i;

    for (i = 0; i < map->nfeatures; i++) {
        if (STREQ(map->features[i]->name, name))
            return map->features[i];
    }

    return NULL;
}

static int
virCPUarmMapFeatureParse(xmlXPathContextPtr ctxt ATTRIBUTE_UNUSED,
                         const char *name,
                         void *data)
{
    virCPUarmMapPtr map = data;
    virCPUarmFeaturePtr feature;
    int ret = -1;

    if (!(feature = virCPUarmFeatureNew()))
        goto cleanup;

    if (VIR_STRDUP(feature->name, name) < 0)
        goto cleanup;

    if (virCPUarmMapFeatureFind(map, feature->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU feature %s already defined"), feature->name);
        goto cleanup;
    }

    if (VIR_APPEND_ELEMENT(map->features, map->nfeatures, feature) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virCPUarmFeatureFree(feature);

    return ret;
}

static virCPUarmMapPtr
virCPUarmLoadMap(void)
{
    virCPUarmMapPtr map;

    if (!(map = virCPUarmMapNew()))
        goto error;

    if (cpuMapLoad("arm", NULL, virCPUarmMapFeatureParse, NULL, map) < 0)
        goto error;

    return map;

 error:
    virCPUarmMapFree(map);

    return NULL;
}

static virCPUarmMapPtr cpuMap;

int virCPUarmDriverOnceInit(void);
VIR_ONCE_GLOBAL_INIT(virCPUarmDriver);

int
virCPUarmDriverOnceInit(void)
{
    if (!(cpuMap = virCPUarmLoadMap()))
        return -1;

    return 0;
}

static virCPUarmMapPtr
virCPUarmGetMap(void)
{
    if (virCPUarmDriverInitialize() < 0)
        return NULL;

    return cpuMap;
}

static int
virCPUarmUpdate(virCPUDefPtr guest,
                const virCPUDef *host)
{
    int ret = -1;
    virCPUDefPtr updated = NULL;

    if (guest->mode != VIR_CPU_MODE_HOST_MODEL)
        return 0;

    if (!host) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("unknown host CPU model"));
        goto cleanup;
    }

    if (!(updated = virCPUDefCopyWithoutModel(guest)))
        goto cleanup;

    updated->mode = VIR_CPU_MODE_CUSTOM;
    if (virCPUDefCopyModel(updated, host, true) < 0)
        goto cleanup;

    virCPUDefStealModel(guest, updated, false);
    guest->mode = VIR_CPU_MODE_CUSTOM;
    guest->match = VIR_CPU_MATCH_EXACT;
    ret = 0;

 cleanup:
    virCPUDefFree(updated);
    return ret;
}


static virCPUDefPtr
virCPUarmBaseline(virCPUDefPtr *cpus,
                  unsigned int ncpus ATTRIBUTE_UNUSED,
                  virDomainCapsCPUModelsPtr models ATTRIBUTE_UNUSED,
                  const char **features ATTRIBUTE_UNUSED,
                  bool migratable ATTRIBUTE_UNUSED)
{
    virCPUDefPtr cpu = NULL;

    if (VIR_ALLOC(cpu) < 0 ||
        VIR_STRDUP(cpu->model, cpus[0]->model) < 0) {
        virCPUDefFree(cpu);
        return NULL;
    }

    cpu->type = VIR_CPU_TYPE_GUEST;
    cpu->match = VIR_CPU_MATCH_EXACT;

    return cpu;
}

static virCPUCompareResult
virCPUarmCompare(virCPUDefPtr host ATTRIBUTE_UNUSED,
                 virCPUDefPtr cpu ATTRIBUTE_UNUSED,
                 bool failMessages ATTRIBUTE_UNUSED)
{
    return VIR_CPU_COMPARE_IDENTICAL;
}

static int
virCPUarmValidateFeatures(virCPUDefPtr cpu)
{
    virCPUarmMapPtr map;
    size_t i;

    if (!(map = virCPUarmGetMap()))
        return -1;

    for (i = 0; i < cpu->nfeatures; i++) {
        if (!virCPUarmMapFeatureFind(map, cpu->features[i].name)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown CPU feature: %s"),
                           cpu->features[i].name);
            return -1;
        }
    }

    return 0;
}

struct cpuArchDriver cpuDriverArm = {
    .name = "arm",
    .arch = archs,
    .narch = ARRAY_CARDINALITY(archs),
    .compare = virCPUarmCompare,
    .decode = NULL,
    .encode = NULL,
    .baseline = virCPUarmBaseline,
    .update = virCPUarmUpdate,
    .validateFeatures = virCPUarmValidateFeatures,
};
