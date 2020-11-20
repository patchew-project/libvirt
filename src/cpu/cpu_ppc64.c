/*
 * cpu_ppc64.c: CPU driver for 64-bit PowerPC CPUs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) IBM Corporation, 2010
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

#include "virlog.h"
#include "viralloc.h"
#include "cpu.h"
#include "virstring.h"
#include "cpu_map.h"
#include "virbuffer.h"

#define VIR_FROM_THIS VIR_FROM_CPU

VIR_LOG_INIT("cpu.cpu_ppc64");

static const virArch archs[] = { VIR_ARCH_PPC64, VIR_ARCH_PPC64LE };

typedef struct _ppc64_vendor virCPUppc64Vendor;
typedef struct _ppc64_vendor *virCPUppc64VendorPtr;
struct _ppc64_vendor {
    char *name;
};

typedef struct _ppc64_model virCPUppc64Model;
typedef struct _ppc64_model *virCPUppc64ModelPtr;
struct _ppc64_model {
    char *name;
    const virCPUppc64Vendor *vendor;
    virCPUppc64Data data;
};

typedef struct _ppc64_map virCPUppc64Map;
typedef struct _ppc64_map *virCPUppc64MapPtr;
struct _ppc64_map {
    size_t nvendors;
    virCPUppc64VendorPtr *vendors;
    size_t nmodels;
    virCPUppc64ModelPtr *models;
};

/* Convert a legacy CPU definition by transforming
 * model names to generation names:
 *   POWER7_v2.1  => POWER7
 *   POWER7_v2.3  => POWER7
 *   POWER7+_v2.1 => POWER7
 *   POWER8_v1.0  => POWER8 */
static int
virCPUppc64ConvertLegacy(virCPUDefPtr cpu)
{
    if (cpu->model &&
        (STREQ(cpu->model, "POWER7_v2.1") ||
         STREQ(cpu->model, "POWER7_v2.3") ||
         STREQ(cpu->model, "POWER7+_v2.1") ||
         STREQ(cpu->model, "POWER8_v1.0"))) {
        cpu->model[strlen("POWERx")] = 0;
    }

    return 0;
}

/* Some hosts can run guests in compatibility mode, but not all
 * host CPUs support this and not all combinations are valid.
 * This function performs the necessary checks */
static virCPUCompareResult
ppc64CheckCompatibilityMode(const char *host_model,
                            const char *compat_mode)
{
    int host;
    int compat;
    char *tmp;

    if (!compat_mode)
        return VIR_CPU_COMPARE_IDENTICAL;

    /* Valid host CPUs: POWER6, POWER7, POWER8, POWER9 */
    if (!STRPREFIX(host_model, "POWER") ||
        !(tmp = (char *) host_model + strlen("POWER")) ||
        virStrToLong_i(tmp, NULL, 10, &host) < 0 ||
        host < 6 || host > 9) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s",
                       _("Host CPU does not support compatibility modes"));
        return VIR_CPU_COMPARE_ERROR;
    }

    /* Valid compatibility modes: power6, power7, power8, power9 */
    if (!STRPREFIX(compat_mode, "power") ||
        !(tmp = (char *) compat_mode + strlen("power")) ||
        virStrToLong_i(tmp, NULL, 10, &compat) < 0 ||
        compat < 6 || compat > 9) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown compatibility mode %s"),
                       compat_mode);
        return VIR_CPU_COMPARE_ERROR;
    }

    /* Version check */
    if (compat > host)
        return VIR_CPU_COMPARE_INCOMPATIBLE;

    return VIR_CPU_COMPARE_IDENTICAL;
}

static void
ppc64DataClear(virCPUppc64Data *data)
{
    if (!data)
        return;

    g_clear_pointer(&data->pvr, g_free);
}

static int
ppc64DataCopy(virCPUppc64Data *dst, const virCPUppc64Data *src)
{
    size_t i;

    dst->pvr = g_new0(virCPUppc64PVR, src->len);
    dst->len = src->len;

    for (i = 0; i < src->len; i++) {
        dst->pvr[i].value = src->pvr[i].value;
        dst->pvr[i].mask = src->pvr[i].mask;
    }

    return 0;
}

static void
ppc64VendorFree(virCPUppc64VendorPtr vendor)
{
    if (!vendor)
        return;

    g_free(vendor->name);
    g_free(vendor);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUppc64Vendor, ppc64VendorFree);

static virCPUppc64VendorPtr
ppc64VendorFind(const virCPUppc64Map *map,
                const char *name)
{
    size_t i;

    for (i = 0; i < map->nvendors; i++) {
        if (STREQ(map->vendors[i]->name, name))
            return map->vendors[i];
    }

    return NULL;
}

static void
ppc64ModelFree(virCPUppc64ModelPtr model)
{
    if (!model)
        return;

    ppc64DataClear(&model->data);
    g_free(model->name);
    g_free(model);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUppc64Model, ppc64ModelFree);

static virCPUppc64ModelPtr
ppc64ModelCopy(const virCPUppc64Model *model)
{
    g_autoptr(virCPUppc64Model) copy = NULL;

    copy = g_new0(virCPUppc64Model, 1);
    copy->name = g_strdup(model->name);

    if (ppc64DataCopy(&copy->data, &model->data) < 0)
        return NULL;

    copy->vendor = model->vendor;

    return g_steal_pointer(&copy);
}

static virCPUppc64ModelPtr
ppc64ModelFind(const virCPUppc64Map *map,
               const char *name)
{
    size_t i;

    for (i = 0; i < map->nmodels; i++) {
        if (STREQ(map->models[i]->name, name))
            return map->models[i];
    }

    return NULL;
}

static virCPUppc64ModelPtr
ppc64ModelFindPVR(const virCPUppc64Map *map,
                  uint32_t pvr)
{
    size_t i;
    size_t j;

    for (i = 0; i < map->nmodels; i++) {
        virCPUppc64ModelPtr model = map->models[i];
        for (j = 0; j < model->data.len; j++) {
            if ((pvr & model->data.pvr[j].mask) == model->data.pvr[j].value)
                return model;
        }
    }

    return NULL;
}

static virCPUppc64ModelPtr
ppc64ModelFromCPU(const virCPUDef *cpu,
                  const virCPUppc64Map *map)
{
    virCPUppc64ModelPtr model;

    if (!cpu->model) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("no CPU model specified"));
        return NULL;
    }

    if (!(model = ppc64ModelFind(map, cpu->model))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown CPU model %s"), cpu->model);
        return NULL;
    }

    return ppc64ModelCopy(model);
}

static void
ppc64MapFree(virCPUppc64MapPtr map)
{
    size_t i;

    if (!map)
        return;

    for (i = 0; i < map->nmodels; i++)
        ppc64ModelFree(map->models[i]);
    g_free(map->models);

    for (i = 0; i < map->nvendors; i++)
        ppc64VendorFree(map->vendors[i]);
    g_free(map->vendors);

    g_free(map);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUppc64Map, ppc64MapFree);

static int
ppc64VendorParse(xmlXPathContextPtr ctxt G_GNUC_UNUSED,
                 const char *name,
                 void *data)
{
    virCPUppc64MapPtr map = data;
    g_autoptr(virCPUppc64Vendor) vendor = NULL;

    vendor = g_new0(virCPUppc64Vendor, 1);
    vendor->name = g_strdup(name);

    if (ppc64VendorFind(map, vendor->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU vendor %s already defined"), vendor->name);
        return -1;
    }

    if (VIR_APPEND_ELEMENT(map->vendors, map->nvendors, vendor) < 0)
        return -1;

    return 0;
}


static int
ppc64ModelParse(xmlXPathContextPtr ctxt,
                const char *name,
                void *data)
{
    virCPUppc64MapPtr map = data;
    g_autoptr(virCPUppc64Model) model = NULL;
    g_autofree xmlNodePtr *nodes = NULL;
    g_autofree char *vendor = NULL;
    unsigned long pvr;
    size_t i;
    int n;

    model = g_new0(virCPUppc64Model, 1);
    model->name = g_strdup(name);

    if (ppc64ModelFind(map, model->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU model %s already defined"), model->name);
        return -1;
    }

    if (virXPathBoolean("boolean(./vendor)", ctxt)) {
        vendor = virXPathString("string(./vendor/@name)", ctxt);
        if (!vendor) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Invalid vendor element in CPU model %s"),
                           model->name);
            return -1;
        }

        if (!(model->vendor = ppc64VendorFind(map, vendor))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unknown vendor %s referenced by CPU model %s"),
                           vendor, model->name);
            return -1;
        }
    }

    if ((n = virXPathNodeSet("./pvr", ctxt, &nodes)) <= 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Missing PVR information for CPU model %s"),
                       model->name);
        return -1;
    }

    model->data.pvr = g_new0(virCPUppc64PVR, n);
    model->data.len = n;

    for (i = 0; i < n; i++) {
        ctxt->node = nodes[i];

        if (virXPathULongHex("string(./@value)", ctxt, &pvr) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Missing or invalid PVR value in CPU model %s"),
                           model->name);
            return -1;
        }
        model->data.pvr[i].value = pvr;

        if (virXPathULongHex("string(./@mask)", ctxt, &pvr) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Missing or invalid PVR mask in CPU model %s"),
                           model->name);
            return -1;
        }
        model->data.pvr[i].mask = pvr;
    }

    if (VIR_APPEND_ELEMENT(map->models, map->nmodels, model) < 0)
        return -1;

    return 0;
}


static virCPUppc64MapPtr
ppc64LoadMap(void)
{
    g_autoptr(virCPUppc64Map) map = NULL;

    map = g_new0(virCPUppc64Map, 1);

    if (cpuMapLoad("ppc64", ppc64VendorParse, NULL, ppc64ModelParse, map) < 0)
        return NULL;

    return g_steal_pointer(&map);
}

static virCPUDataPtr
ppc64MakeCPUData(virArch arch,
                 virCPUppc64Data *data)
{
    g_autoptr(virCPUData) cpuData = NULL;

    cpuData = g_new0(virCPUData, 1);
    cpuData->arch = arch;

    if (ppc64DataCopy(&cpuData->data.ppc64, data) < 0)
        return NULL;

    return g_steal_pointer(&cpuData);
}

static virCPUCompareResult
ppc64Compute(virCPUDefPtr host,
             const virCPUDef *other,
             virCPUDataPtr *guestData,
             char **message)
{
    g_autoptr(virCPUppc64Map) map = NULL;
    g_autoptr(virCPUppc64Model) host_model = NULL;
    g_autoptr(virCPUppc64Model) guest_model = NULL;
    g_autoptr(virCPUDef) cpu = NULL;
    virArch arch;
    size_t i;

    /* Ensure existing configurations are handled correctly */
    if (!(cpu = virCPUDefCopy(other)) ||
        virCPUppc64ConvertLegacy(cpu) < 0)
        return VIR_CPU_COMPARE_ERROR;

    if (cpu->arch != VIR_ARCH_NONE) {
        bool found = false;

        for (i = 0; i < G_N_ELEMENTS(archs); i++) {
            if (archs[i] == cpu->arch) {
                found = true;
                break;
            }
        }

        if (!found) {
            VIR_DEBUG("CPU arch %s does not match host arch",
                      virArchToString(cpu->arch));
            if (message)
                *message = g_strdup_printf(_("CPU arch %s does not match host arch"),
                                           virArchToString(cpu->arch));

            return VIR_CPU_COMPARE_INCOMPATIBLE;
        }
        arch = cpu->arch;
    } else {
        arch = host->arch;
    }

    if (cpu->vendor &&
        (!host->vendor || STRNEQ(cpu->vendor, host->vendor))) {
        VIR_DEBUG("host CPU vendor does not match required CPU vendor %s",
                  cpu->vendor);
        if (message) {
            *message = g_strdup_printf(_("host CPU vendor does not match required "
                                         "CPU vendor %s"),
                                       cpu->vendor);
        }

        return VIR_CPU_COMPARE_INCOMPATIBLE;
    }

    if (!(map = ppc64LoadMap()))
        return VIR_CPU_COMPARE_ERROR;

    /* Host CPU information */
    if (!(host_model = ppc64ModelFromCPU(host, map)))
        return VIR_CPU_COMPARE_ERROR;

    if (cpu->type == VIR_CPU_TYPE_GUEST) {
        /* Guest CPU information */
        virCPUCompareResult tmp;
        switch (cpu->mode) {
        case VIR_CPU_MODE_HOST_MODEL:
            /* host-model only:
             * we need to take compatibility modes into account */
            tmp = ppc64CheckCompatibilityMode(host->model, cpu->model);
            if (tmp != VIR_CPU_COMPARE_IDENTICAL)
                return tmp;
            G_GNUC_FALLTHROUGH;

        case VIR_CPU_MODE_HOST_PASSTHROUGH:
            /* host-model and host-passthrough:
             * the guest CPU is the same as the host */
            guest_model = ppc64ModelCopy(host_model);
            break;

        case VIR_CPU_MODE_CUSTOM:
            /* custom:
             * look up guest CPU information */
            guest_model = ppc64ModelFromCPU(cpu, map);
            break;
        }
    } else {
        /* Other host CPU information */
        guest_model = ppc64ModelFromCPU(cpu, map);
    }

    if (!guest_model)
        return VIR_CPU_COMPARE_ERROR;

    if (STRNEQ(guest_model->name, host_model->name)) {
        VIR_DEBUG("host CPU model does not match required CPU model %s",
                  guest_model->name);
        if (message) {
            *message = g_strdup_printf(_("host CPU model does not match required "
                                         "CPU model %s"),
                                       guest_model->name);
        }

        return VIR_CPU_COMPARE_INCOMPATIBLE;
    }

    if (guestData)
        if (!(*guestData = ppc64MakeCPUData(arch, &guest_model->data)))
            return VIR_CPU_COMPARE_ERROR;

    return VIR_CPU_COMPARE_IDENTICAL;
}

static virCPUCompareResult
virCPUppc64Compare(virCPUDefPtr host,
                   virCPUDefPtr cpu,
                   bool failIncompatible)
{
    virCPUCompareResult ret;
    g_autofree char *message = NULL;

    if (!host || !host->model) {
        if (failIncompatible) {
            virReportError(VIR_ERR_CPU_INCOMPATIBLE, "%s",
                           _("unknown host CPU"));
            return VIR_CPU_COMPARE_ERROR;
        }

        VIR_WARN("unknown host CPU");
        return VIR_CPU_COMPARE_INCOMPATIBLE;
    }

    ret = ppc64Compute(host, cpu, NULL, &message);

    if (failIncompatible && ret == VIR_CPU_COMPARE_INCOMPATIBLE) {
        ret = VIR_CPU_COMPARE_ERROR;
        if (message) {
            virReportError(VIR_ERR_CPU_INCOMPATIBLE, "%s", message);
        } else {
            virReportError(VIR_ERR_CPU_INCOMPATIBLE, NULL);
        }
    }

    return ret;
}

static int
ppc64DriverDecode(virCPUDefPtr cpu,
                  const virCPUData *data,
                  virDomainCapsCPUModelsPtr models)
{
    g_autoptr(virCPUppc64Map) map = NULL;
    const virCPUppc64Model *model;

    if (!data || !(map = ppc64LoadMap()))
        return -1;

    if (!(model = ppc64ModelFindPVR(map, data->data.ppc64.pvr[0].value))) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("Cannot find CPU model with PVR 0x%08x"),
                       data->data.ppc64.pvr[0].value);
        return -1;
    }

    if (!virCPUModelIsAllowed(model->name, models)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("CPU model %s is not supported by hypervisor"),
                       model->name);
        return -1;
    }

    cpu->model = g_strdup(model->name);
    if (model->vendor)
        cpu->vendor = g_strdup(model->vendor->name);

    return 0;
}

static void
virCPUppc64DataFree(virCPUDataPtr data)
{
    if (!data)
        return;

    ppc64DataClear(&data->data.ppc64);
    g_free(data);
}


static int
virCPUppc64GetHost(virCPUDefPtr cpu,
                   virDomainCapsCPUModelsPtr models)
{
    g_autoptr(virCPUData) cpuData = NULL;
    virCPUppc64Data *data;

    if (!(cpuData = virCPUDataNew(archs[0])))
        return -1;

    data = &cpuData->data.ppc64;
    data->pvr = g_new0(virCPUppc64PVR, 1);
    data->len = 1;

#if defined(__powerpc__) || defined(__powerpc64__)
    asm("mfpvr %0"
        : "=r" (data->pvr[0].value));
#endif
    data->pvr[0].mask = 0xfffffffful;

    return ppc64DriverDecode(cpu, cpuData, models);
}


static int
virCPUppc64Update(virCPUDefPtr guest,
                  const virCPUDef *host G_GNUC_UNUSED,
                  bool relative G_GNUC_UNUSED)
{
    /*
     * - host-passthrough doesn't even get here
     * - host-model is used for host CPU running in a compatibility mode and
     *   it needs to remain unchanged
     * - custom doesn't support any optional features, there's nothing to
     *   update
     */

    if (guest->mode == VIR_CPU_MODE_CUSTOM)
        guest->match = VIR_CPU_MATCH_EXACT;

    return 0;
}

static virCPUDefPtr
virCPUppc64Baseline(virCPUDefPtr *cpus,
                    unsigned int ncpus,
                    virDomainCapsCPUModelsPtr models G_GNUC_UNUSED,
                    const char **features G_GNUC_UNUSED,
                    bool migratable G_GNUC_UNUSED)
{
    g_autoptr(virCPUppc64Map) map = NULL;
    const virCPUppc64Model *model;
    const virCPUppc64Vendor *vendor = NULL;
    g_autoptr(virCPUDef) cpu = NULL;
    size_t i;

    if (!(map = ppc64LoadMap()))
        return NULL;

    if (!(model = ppc64ModelFind(map, cpus[0]->model))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown CPU model %s"), cpus[0]->model);
        return NULL;
    }

    for (i = 0; i < ncpus; i++) {
        const virCPUppc64Vendor *vnd;

        /* Hosts running old (<= 1.2.18) versions of libvirt will report
         * strings like 'power7+' or 'power8e' instead of proper CPU model
         * names in the capabilities XML; moreover, they lack information
         * about some proper CPU models like 'POWER8'.
         * This implies two things:
         *   1) baseline among such hosts never worked
         *   2) while a few models, eg. 'POWER8_v1.0', could work on both
         *      old and new versions of libvirt, the information we have
         *      here is not enough to pick such a model
         * Hence we just compare models by name to decide whether or not
         * two hosts are compatible */
        if (STRNEQ(cpus[i]->model, model->name)) {
            virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                           _("CPUs are incompatible"));
            return NULL;
        }

        if (!cpus[i]->vendor)
            continue;

        if (!(vnd = ppc64VendorFind(map, cpus[i]->vendor))) {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("Unknown CPU vendor %s"), cpus[i]->vendor);
            return NULL;
        }

        if (model->vendor) {
            if (model->vendor != vnd) {
                virReportError(VIR_ERR_OPERATION_FAILED,
                               _("CPU vendor %s of model %s differs from "
                                 "vendor %s"),
                               model->vendor->name, model->name,
                               vnd->name);
                return NULL;
            }
        } else if (vendor) {
            if (vendor != vnd) {
                virReportError(VIR_ERR_OPERATION_FAILED, "%s",
                               _("CPU vendors do not match"));
                return NULL;
            }
        } else {
            vendor = vnd;
        }
    }

    cpu = virCPUDefNew();

    cpu->model = g_strdup(model->name);

    if (vendor)
        cpu->vendor = g_strdup(vendor->name);

    cpu->type = VIR_CPU_TYPE_GUEST;
    cpu->match = VIR_CPU_MATCH_EXACT;
    cpu->fallback = VIR_CPU_FALLBACK_FORBID;

    return g_steal_pointer(&cpu);
}

static int
virCPUppc64DriverGetModels(char ***models)
{
    g_autoptr(virCPUppc64Map) map = NULL;
    size_t i;

    if (!(map = ppc64LoadMap()))
        return -1;

    if (models) {
        *models = g_new0(char*, map->nmodels + 1);

        for (i = 0; i < map->nmodels; i++)
            (*models)[i] = g_strdup(map->models[i]->name);
    }

    return map->nmodels;
}

struct cpuArchDriver cpuDriverPPC64 = {
    .name       = "ppc64",
    .arch       = archs,
    .narch      = G_N_ELEMENTS(archs),
    .compare    = virCPUppc64Compare,
    .decode     = ppc64DriverDecode,
    .encode     = NULL,
    .dataFree   = virCPUppc64DataFree,
    .getHost    = virCPUppc64GetHost,
    .baseline   = virCPUppc64Baseline,
    .update     = virCPUppc64Update,
    .getModels  = virCPUppc64DriverGetModels,
    .convertLegacy = virCPUppc64ConvertLegacy,
};
