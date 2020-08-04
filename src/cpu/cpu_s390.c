/*
 * cpu_s390.c: CPU driver for s390(x) CPUs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright IBM Corp. 2012
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "viralloc.h"
#include "virstring.h"
#include "cpu.h"


#define VIR_FROM_THIS VIR_FROM_CPU

static const virArch archs[] = { VIR_ARCH_S390, VIR_ARCH_S390X };

static virCPUCompareResult
virCPUs390Compare(virCPUDefPtr host G_GNUC_UNUSED,
                  virCPUDefPtr cpu G_GNUC_UNUSED,
                  bool failMessages G_GNUC_UNUSED)
{
    /* s390 relies on QEMU to perform all runability checking. Return
     * VIR_CPU_COMPARE_IDENTICAL to bypass Libvirt checking.
     */
    return VIR_CPU_COMPARE_IDENTICAL;
}

static int
virCPUs390Update(virCPUDefPtr guest,
                 const virCPUDef *host)
{
    g_autoptr(virCPUDef) updated = NULL;
    size_t i;

    if (guest->mode == VIR_CPU_MODE_CUSTOM) {
        if (guest->match == VIR_CPU_MATCH_MINIMUM) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("match mode %s not supported"),
                           virCPUMatchTypeToString(guest->match));
        } else {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("optional CPU features are not supported"));
        }
        return -1;
    }

    if (!host) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("unknown host CPU model"));
        return -1;
    }

    if (!(updated = virCPUDefCopyWithoutModel(guest)))
        return -1;

    updated->mode = VIR_CPU_MODE_CUSTOM;
    if (virCPUDefCopyModel(updated, host, true) < 0)
        return -1;

    for (i = 0; i < guest->nfeatures; i++) {
       if (virCPUDefUpdateFeature(updated,
                                  guest->features[i].name,
                                  guest->features[i].policy) < 0)
           return -1;
    }

    virCPUDefStealModel(guest, updated, false);
    guest->mode = VIR_CPU_MODE_CUSTOM;
    guest->match = VIR_CPU_MATCH_EXACT;

    return 0;
}


static int
virCPUs390ValidateFeatures(virCPUDefPtr cpu)
{
    size_t i;

    for (i = 0; i < cpu->nfeatures; i++) {
        if (cpu->features[i].policy == VIR_CPU_FEATURE_OPTIONAL) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("only cpu feature policies 'require' and "
                             "'disable' are supported for %s"),
                           cpu->features[i].name);
            return -1;
        }
    }

    return 0;
}


struct cpuArchDriver cpuDriverS390 = {
    .name = "s390",
    .arch = archs,
    .narch = G_N_ELEMENTS(archs),
    .compare    = virCPUs390Compare,
    .decode     = NULL,
    .encode     = NULL,
    .baseline   = NULL,
    .update     = virCPUs390Update,
    .validateFeatures = virCPUs390ValidateFeatures,
};
