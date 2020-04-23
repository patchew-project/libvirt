/*
 * qemu_validate.c: QEMU general validation functions
 *
 * Copyright IBM Corp, 2020
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

#include "qemu_validate.h"
#include "qemu_block.h"
#include "qemu_command.h"
#include "qemu_domain.h"
#include "domain_conf.h"
#include "virlog.h"
#include "virutil.h"

#define VIR_FROM_THIS VIR_FROM_QEMU
#define QEMU_MAX_VCPUS_WITHOUT_EIM 255

VIR_LOG_INIT("qemu.qemu_validate");


static int
qemuValidateDomainDefPSeriesFeature(const virDomainDef *def,
                                    virQEMUCapsPtr qemuCaps,
                                    int feature)
{
    const char *str;

    if (def->features[feature] != VIR_TRISTATE_SWITCH_ABSENT &&
        !qemuDomainIsPSeries(def)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("The '%s' feature is not supported for "
                         "architecture '%s' or machine type '%s'"),
                       virDomainFeatureTypeToString(feature),
                       virArchToString(def->os.arch),
                       def->os.machine);
        return -1;
    }

    if (def->features[feature] == VIR_TRISTATE_SWITCH_ABSENT)
        return 0;

    switch (feature) {
    case VIR_DOMAIN_FEATURE_HPT:
        if (def->features[feature] != VIR_TRISTATE_SWITCH_ON)
            break;

        if (def->hpt_resizing != VIR_DOMAIN_HPT_RESIZING_NONE) {
            if (!virQEMUCapsGet(qemuCaps,
                                QEMU_CAPS_MACHINE_PSERIES_RESIZE_HPT)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("HTP resizing is not supported by this "
                                "QEMU binary"));
                return -1;
            }

            str = virDomainHPTResizingTypeToString(def->hpt_resizing);
            if (!str) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("Invalid setting for HPT resizing"));
                return -1;
            }
        }

        if (def->hpt_maxpagesize > 0 &&
            !virQEMUCapsGet(qemuCaps,
                            QEMU_CAPS_MACHINE_PSERIES_CAP_HPT_MAX_PAGE_SIZE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Configuring the page size for HPT guests "
                             "is not supported by this QEMU binary"));
            return -1;
        }
        break;

    case VIR_DOMAIN_FEATURE_HTM:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_HTM)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("HTM configuration is not supported by this "
                             "QEMU binary"));
            return -1;
        }

        str = virTristateSwitchTypeToString(def->features[feature]);
        if (!str) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid setting for HTM state"));
            return -1;
        }

        break;

    case VIR_DOMAIN_FEATURE_NESTED_HV:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_NESTED_HV)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Nested HV configuration is not supported by "
                             "this QEMU binary"));
            return -1;
        }

        str = virTristateSwitchTypeToString(def->features[feature]);
        if (!str) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid setting for nested HV state"));
            return -1;
        }

        break;

    case VIR_DOMAIN_FEATURE_CCF_ASSIST:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_CCF_ASSIST)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("ccf-assist configuration is not supported by "
                           "this QEMU binary"));
            return -1;
        }

        str = virTristateSwitchTypeToString(def->features[feature]);
        if (!str) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid setting for ccf-assist state"));
            return -1;
        }

        break;

    case VIR_DOMAIN_FEATURE_CFPC:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_CFPC)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("cfpc configuration is not supported by "
                             "this QEMU binary"));
            return -1;
        }

        break;

    case VIR_DOMAIN_FEATURE_SBBC:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_SBBC)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("sbbc configuration is not supported by "
                             "this QEMU binary"));
            return -1;
        }

        break;

    case VIR_DOMAIN_FEATURE_IBS:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_PSERIES_CAP_IBS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("ibs configuration is not supported by "
                             "this QEMU binary"));
            return -1;
        }

        break;
    }

    return 0;
}


static int
qemuValidateDomainDefFeatures(const virDomainDef *def,
                              virQEMUCapsPtr qemuCaps)
{
    size_t i;

    for (i = 0; i < VIR_DOMAIN_FEATURE_LAST; i++) {
        const char *featureName = virDomainFeatureTypeToString(i);

        switch ((virDomainFeature) i) {
        case VIR_DOMAIN_FEATURE_IOAPIC:
            if (def->features[i] != VIR_DOMAIN_IOAPIC_NONE) {
                if (!ARCH_IS_X86(def->os.arch)) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("The '%s' feature is not supported for "
                                     "architecture '%s' or machine type '%s'"),
                                   featureName,
                                   virArchToString(def->os.arch),
                                   def->os.machine);
                    return -1;
                }

                if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_KERNEL_IRQCHIP)) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                                   _("I/O APIC tuning is not supported by "
                                     "this QEMU binary"));
                    return -1;
                }

                switch ((virDomainIOAPIC) def->features[i]) {
                case VIR_DOMAIN_IOAPIC_QEMU:
                    if (!virQEMUCapsGet(qemuCaps,
                                        QEMU_CAPS_MACHINE_KERNEL_IRQCHIP_SPLIT)) {
                        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                                       _("split I/O APIC is not supported by this "
                                         "QEMU binary"));
                        return -1;
                    }
                    break;
                case VIR_DOMAIN_IOAPIC_KVM:
                case VIR_DOMAIN_IOAPIC_NONE:
                case VIR_DOMAIN_IOAPIC_LAST:
                    break;
                }
            }
            break;

        case VIR_DOMAIN_FEATURE_HPT:
        case VIR_DOMAIN_FEATURE_HTM:
        case VIR_DOMAIN_FEATURE_NESTED_HV:
        case VIR_DOMAIN_FEATURE_CCF_ASSIST:
        case VIR_DOMAIN_FEATURE_CFPC:
        case VIR_DOMAIN_FEATURE_SBBC:
        case VIR_DOMAIN_FEATURE_IBS:
            if (qemuValidateDomainDefPSeriesFeature(def, qemuCaps, i) < 0)
                return -1;
            break;

        case VIR_DOMAIN_FEATURE_GIC:
            if (def->features[i] == VIR_TRISTATE_SWITCH_ON &&
                !qemuDomainIsARMVirt(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("The '%s' feature is not supported for "
                                 "architecture '%s' or machine type '%s'"),
                               featureName,
                               virArchToString(def->os.arch),
                               def->os.machine);
                return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_SMM:
            if (def->features[i] != VIR_TRISTATE_SWITCH_ABSENT &&
                !virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_SMM_OPT)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("smm is not available with this QEMU binary"));
                return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_KVM:
            if (def->kvm_features[VIR_DOMAIN_KVM_DEDICATED] == VIR_TRISTATE_SWITCH_ON &&
                (!def->cpu || def->cpu->mode != VIR_CPU_MODE_HOST_PASSTHROUGH)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("kvm-hint-dedicated=on is only applicable "
                                 "for cpu host-passthrough"));
                return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_VMPORT:
            if (def->features[i] != VIR_TRISTATE_SWITCH_ABSENT &&
                !virQEMUCapsSupportsVmport(qemuCaps, def)) {

                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("vmport is not available "
                                 "with this QEMU binary"));
                return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_VMCOREINFO:
            if (def->features[i] == VIR_TRISTATE_SWITCH_ON &&
                !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VMCOREINFO)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                              _("vmcoreinfo is not available "
                                "with this QEMU binary"));
                return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_APIC:
            /* The kvm_pv_eoi feature is x86-only. */
            if (def->features[i] != VIR_TRISTATE_SWITCH_ABSENT &&
                def->apic_eoi != VIR_TRISTATE_SWITCH_ABSENT &&
                !ARCH_IS_X86(def->os.arch)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("The 'eoi' attribute of the '%s' feature "
                                 "is not supported for architecture '%s' or "
                                 "machine type '%s'"),
                                 featureName,
                                 virArchToString(def->os.arch),
                                 def->os.machine);
                 return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_PVSPINLOCK:
            if (def->features[i] != VIR_TRISTATE_SWITCH_ABSENT &&
                !ARCH_IS_X86(def->os.arch)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("The '%s' feature is not supported for "
                                 "architecture '%s' or machine type '%s'"),
                                 featureName,
                                 virArchToString(def->os.arch),
                                 def->os.machine);
                 return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_HYPERV:
            if (def->features[i] != VIR_TRISTATE_SWITCH_ABSENT &&
                !ARCH_IS_X86(def->os.arch) && !qemuDomainIsARMVirt(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Hyperv features are not supported for "
                                 "architecture '%s' or machine type '%s'"),
                                 virArchToString(def->os.arch),
                                 def->os.machine);
                 return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_PMU:
            if (def->features[i] == VIR_TRISTATE_SWITCH_OFF &&
                ARCH_IS_PPC64(def->os.arch)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("PMU is always enabled for architecture '%s'"),
                                 virArchToString(def->os.arch));
                 return -1;
            }
            break;

        case VIR_DOMAIN_FEATURE_XEN:
        case VIR_DOMAIN_FEATURE_ACPI:
        case VIR_DOMAIN_FEATURE_PAE:
        case VIR_DOMAIN_FEATURE_HAP:
        case VIR_DOMAIN_FEATURE_VIRIDIAN:
        case VIR_DOMAIN_FEATURE_PRIVNET:
        case VIR_DOMAIN_FEATURE_CAPABILITIES:
        case VIR_DOMAIN_FEATURE_MSRS:
        case VIR_DOMAIN_FEATURE_LAST:
            break;
        }
    }

    return 0;
}


static int
qemuValidateDomainDefClockTimers(const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    size_t i;

    for (i = 0; i < def->clock.ntimers; i++) {
        virDomainTimerDefPtr timer = def->clock.timers[i];

        switch ((virDomainTimerNameType)timer->name) {
        case VIR_DOMAIN_TIMER_NAME_PLATFORM:
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unsupported timer type (name) '%s'"),
                           virDomainTimerNameTypeToString(timer->name));
            return -1;

        case VIR_DOMAIN_TIMER_NAME_TSC:
        case VIR_DOMAIN_TIMER_NAME_KVMCLOCK:
        case VIR_DOMAIN_TIMER_NAME_HYPERVCLOCK:
        case VIR_DOMAIN_TIMER_NAME_LAST:
            break;

        case VIR_DOMAIN_TIMER_NAME_RTC:
            switch (timer->track) {
            case -1: /* unspecified - use hypervisor default */
            case VIR_DOMAIN_TIMER_TRACK_GUEST:
            case VIR_DOMAIN_TIMER_TRACK_WALL:
                break;
            case VIR_DOMAIN_TIMER_TRACK_BOOT:
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("unsupported rtc timer track '%s'"),
                               virDomainTimerTrackTypeToString(timer->track));
                return -1;
            }

            switch (timer->tickpolicy) {
            case -1:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DELAY:
                /* This is the default - missed ticks delivered when
                   next scheduled, at normal rate */
                break;
            case VIR_DOMAIN_TIMER_TICKPOLICY_CATCHUP:
                /* deliver ticks at a faster rate until caught up */
                break;
            case VIR_DOMAIN_TIMER_TICKPOLICY_MERGE:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DISCARD:
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("unsupported rtc timer tickpolicy '%s'"),
                               virDomainTimerTickpolicyTypeToString(
                                   timer->tickpolicy));
                return -1;
            }
            break;

        case VIR_DOMAIN_TIMER_NAME_PIT:
            switch (timer->tickpolicy) {
            case -1:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DELAY:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DISCARD:
                break;
            case VIR_DOMAIN_TIMER_TICKPOLICY_CATCHUP:
                if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_KVM_PIT_TICK_POLICY)) {
                    /* can't catchup if we don't have kvm-pit */
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("unsupported pit tickpolicy '%s'"),
                                   virDomainTimerTickpolicyTypeToString(
                                       timer->tickpolicy));
                    return -1;
                }
                break;
            case VIR_DOMAIN_TIMER_TICKPOLICY_MERGE:
                /* no way to support this mode for pit in qemu */
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("unsupported pit tickpolicy '%s'"),
                               virDomainTimerTickpolicyTypeToString(
                                   timer->tickpolicy));
                return -1;
            }
            break;

        case VIR_DOMAIN_TIMER_NAME_HPET:
            /* no hpet timer available. The only possible action
              is to raise an error if present="yes" */
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_NO_HPET) &&
                timer->present == 1) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               "%s", _("hpet timer is not supported"));
                return -1;
            }
            break;

        case VIR_DOMAIN_TIMER_NAME_ARMVTIMER:
            if (def->virtType != VIR_DOMAIN_VIRT_KVM ||
                !qemuDomainIsARMVirt(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Configuring the '%s' timer is not supported "
                                 "for virtType=%s arch=%s machine=%s guests"),
                               virDomainTimerNameTypeToString(timer->name),
                               virDomainVirtTypeToString(def->virtType),
                               virArchToString(def->os.arch),
                               def->os.machine);
                return -1;
            }
            if (timer->present == 0) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("The '%s' timer can't be disabled"),
                               virDomainTimerNameTypeToString(timer->name));
                return -1;
            }
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CPU_KVM_NO_ADJVTIME)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Configuring the '%s' timer is not supported "
                                 "with this QEMU binary"),
                               virDomainTimerNameTypeToString(timer->name));
                return -1;
            }

            switch (timer->tickpolicy) {
            case -1:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DELAY:
            case VIR_DOMAIN_TIMER_TICKPOLICY_DISCARD:
                break;
            case VIR_DOMAIN_TIMER_TICKPOLICY_CATCHUP:
            case VIR_DOMAIN_TIMER_TICKPOLICY_MERGE:
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("The '%s' timer does not support tickpolicy '%s'"),
                               virDomainTimerNameTypeToString(timer->name),
                               virDomainTimerTickpolicyTypeToString(timer->tickpolicy));
                return -1;
            }
            break;
        }
    }

    return 0;
}


static int
qemuValidateDomainDefPM(const virDomainDef *def,
                        virQEMUCapsPtr qemuCaps)
{
    bool q35Dom = qemuDomainIsQ35(def);

    if (def->pm.s3) {
        bool q35ICH9_S3 = q35Dom &&
                          virQEMUCapsGet(qemuCaps, QEMU_CAPS_ICH9_DISABLE_S3);

        if (!q35ICH9_S3 && !virQEMUCapsGet(qemuCaps, QEMU_CAPS_PIIX_DISABLE_S3)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           "%s", _("setting ACPI S3 not supported"));
            return -1;
        }
    }

    if (def->pm.s4) {
        bool q35ICH9_S4 = q35Dom &&
                          virQEMUCapsGet(qemuCaps, QEMU_CAPS_ICH9_DISABLE_S4);

        if (!q35ICH9_S4 && !virQEMUCapsGet(qemuCaps, QEMU_CAPS_PIIX_DISABLE_S4)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           "%s", _("setting ACPI S4 not supported"));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainDefBoot(const virDomainDef *def,
                          virQEMUCapsPtr qemuCaps)
{
    if (def->os.bios.rt_set) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_REBOOT_TIMEOUT)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("reboot timeout is not supported "
                             "by this QEMU binary"));
            return -1;
        }
    }

    if (def->os.bm_timeout_set) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPLASH_TIMEOUT)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("splash timeout is not supported "
                             "by this QEMU binary"));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainCpuCount(const virDomainDef *def, virQEMUCapsPtr qemuCaps)
{
    unsigned int maxCpus = virQEMUCapsGetMachineMaxCpus(qemuCaps, def->virtType,
                                                        def->os.machine);

    if (virDomainDefGetVcpus(def) == 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Domain requires at least 1 vCPU"));
        return -1;
    }

    if (maxCpus > 0 && virDomainDefGetVcpusMax(def) > maxCpus) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Maximum CPUs greater than specified machine "
                         "type limit %u"), maxCpus);
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDefMemory(const virDomainDef *def,
                            virQEMUCapsPtr qemuCaps)
{
    const long system_page_size = virGetSystemPageSizeKB();
    const virDomainMemtune *mem = &def->mem;

    if (mem->nhugepages == 0)
        return 0;

    if (mem->allocation == VIR_DOMAIN_MEMORY_ALLOCATION_ONDEMAND) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("hugepages are not allowed with memory "
                         "allocation ondemand"));
        return -1;
    }

    if (mem->source == VIR_DOMAIN_MEMORY_SOURCE_ANONYMOUS) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("hugepages are not allowed with anonymous "
                         "memory source"));
        return -1;
    }

    if (mem->source == VIR_DOMAIN_MEMORY_SOURCE_MEMFD &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_MEMORY_MEMFD_HUGETLB)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("hugepages is not supported with memfd memory source"));
        return -1;
    }

    /* We can't guarantee any other mem.access
     * if no guest NUMA nodes are defined. */
    if (mem->hugepages[0].size != system_page_size &&
        virDomainNumaGetNodeCount(def->numa) == 0 &&
        mem->access != VIR_DOMAIN_MEMORY_ACCESS_DEFAULT &&
        mem->access != VIR_DOMAIN_MEMORY_ACCESS_PRIVATE) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("memory access mode '%s' not supported "
                         "without guest numa node"),
                       virDomainMemoryAccessTypeToString(mem->access));
        return -1;
    }

    if (mem->nosharepages && !virQEMUCapsGet(qemuCaps, QEMU_CAPS_MEM_MERGE)) {
         virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                        _("disable shared memory is not available "
                          "with this QEMU binary"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDefNuma(const virDomainDef *def,
                          virQEMUCapsPtr qemuCaps)
{
    const long system_page_size = virGetSystemPageSizeKB();
    size_t ncells = virDomainNumaGetNodeCount(def->numa);
    size_t i;
    bool hasMemoryCap = virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_MEMORY_RAM) ||
                        virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_MEMORY_FILE) ||
                        virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_MEMORY_MEMFD);

    if (virDomainNumatuneHasPerNodeBinding(def->numa) && !hasMemoryCap) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Per-node memory binding is not supported "
                         "with this QEMU"));
        return -1;
    }

    if (def->mem.nhugepages &&
        def->mem.hugepages[0].size != system_page_size &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_MEMORY_FILE)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("huge pages per NUMA node are not "
                         "supported with this QEMU"));
        return -1;
    }

    for (i = 0; i < ncells; i++) {
        g_autofree char * cpumask = NULL;

        if (!hasMemoryCap &&
            virDomainNumaGetNodeMemoryAccessMode(def->numa, i)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Shared memory mapping is not supported "
                             "with this QEMU"));
            return -1;
        }

        if (!(cpumask = virBitmapFormat(virDomainNumaGetNodeCpumask(def->numa, i))))
            return -1;

        if (strchr(cpumask, ',') &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_NUMA)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("disjoint NUMA cpu ranges are not supported "
                             "with this QEMU"));
            return -1;
        }

    }

    if (virDomainNumaNodesDistancesAreBeingSet(def->numa) &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_NUMA_DIST)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("setting NUMA distances is not "
                         "supported with this qemu"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDefConsole(const virDomainDef *def,
                             virQEMUCapsPtr qemuCaps)
{
    size_t i;

    /* Explicit console devices */
    for (i = 0; i < def->nconsoles; i++) {
        virDomainChrDefPtr console = def->consoles[i];

        switch (console->targetType) {
        case VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_SCLP:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_SCLPCONSOLE)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("sclpconsole is not supported in this QEMU binary"));
                return -1;
            }
            break;

        case VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_SCLPLM:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_SCLPLMCONSOLE)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("sclplmconsole is not supported in this QEMU binary"));
                return -1;
            }
            break;

        case VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_VIRTIO:
        case VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_SERIAL:
            break;

        default:
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unsupported console target type %s"),
                           NULLSTR(virDomainChrConsoleTargetTypeToString(console->targetType)));
            return -1;
        }
    }

    return 0;
}


/**
 * qemuValidateDefGetVcpuHotplugGranularity:
 * @def: domain definition
 *
 * With QEMU 2.7 and newer, vCPUs can only be hotplugged in groups that
 * respect the guest's hotplug granularity; because of that, QEMU will
 * not allow guests to start unless the initial number of vCPUs is a
 * multiple of the hotplug granularity.
 *
 * Returns the vCPU hotplug granularity.
 */
static unsigned int
qemuValidateDefGetVcpuHotplugGranularity(const virDomainDef *def)
{
    /* If the guest CPU topology has not been configured, assume we
     * can hotplug vCPUs one at a time */
    if (!def->cpu || def->cpu->sockets == 0)
        return 1;

    /* For pSeries guests, hotplug can only be performed one core
     * at a time, so the vCPU hotplug granularity is the number
     * of threads per core */
    if (qemuDomainIsPSeries(def))
        return def->cpu->threads;

    /* In all other cases, we can hotplug vCPUs one at a time */
    return 1;
}


int
qemuValidateDomainDef(const virDomainDef *def,
                      void *opaque)
{
    virQEMUDriverPtr driver = opaque;
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autoptr(virQEMUCaps) qemuCaps = NULL;
    size_t i;

    if (!(qemuCaps = virQEMUCapsCacheLookup(driver->qemuCapsCache,
                                            def->emulator)))
        return -1;

    if (def->os.type != VIR_DOMAIN_OSTYPE_HVM) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Emulator '%s' does not support os type '%s'"),
                       def->emulator, virDomainOSTypeToString(def->os.type));
        return -1;
    }

    if (!virQEMUCapsIsArchSupported(qemuCaps, def->os.arch)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Emulator '%s' does not support arch '%s'"),
                       def->emulator, virArchToString(def->os.arch));
        return -1;
    }

    if (!virQEMUCapsIsVirtTypeSupported(qemuCaps, def->virtType)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Emulator '%s' does not support virt type '%s'"),
                       def->emulator, virDomainVirtTypeToString(def->virtType));
        return -1;
    }

    if (qemuCaps &&
        !virQEMUCapsIsMachineSupported(qemuCaps, def->virtType, def->os.machine)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Emulator '%s' does not support machine type '%s'"),
                       def->emulator, def->os.machine);
        return -1;
    }

    if (def->mem.min_guarantee) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Parameter 'min_guarantee' not supported by QEMU."));
        return -1;
    }

    /* On x86, UEFI requires ACPI */
    if ((def->os.firmware == VIR_DOMAIN_OS_DEF_FIRMWARE_EFI ||
         virDomainDefHasOldStyleUEFI(def)) &&
        ARCH_IS_X86(def->os.arch) &&
        def->features[VIR_DOMAIN_FEATURE_ACPI] != VIR_TRISTATE_SWITCH_ON) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("UEFI requires ACPI on this architecture"));
        return -1;
    }

    /* On aarch64, ACPI requires UEFI */
    if (def->features[VIR_DOMAIN_FEATURE_ACPI] == VIR_TRISTATE_SWITCH_ON &&
        def->os.arch == VIR_ARCH_AARCH64 &&
        (def->os.firmware != VIR_DOMAIN_OS_DEF_FIRMWARE_EFI &&
         !virDomainDefHasOldStyleUEFI(def))) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("ACPI requires UEFI on this architecture"));
        return -1;
    }

    if (def->os.loader &&
        def->os.loader->secure == VIR_TRISTATE_BOOL_YES) {
        /* These are the QEMU implementation limitations. But we
         * have to live with them for now. */

        if (!qemuDomainIsQ35(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Secure boot is supported with q35 machine types only"));
            return -1;
        }

        /* Now, technically it is possible to have secure boot on
         * 32bits too, but that would require some -cpu xxx magic
         * too. Not worth it unless we are explicitly asked. */
        if (def->os.arch != VIR_ARCH_X86_64) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Secure boot is supported for x86_64 architecture only"));
            return -1;
        }

        /* SMM will be enabled by qemuFirmwareFillDomain() if needed. */
        if (def->os.firmware == VIR_DOMAIN_OS_DEF_FIRMWARE_NONE &&
            def->features[VIR_DOMAIN_FEATURE_SMM] != VIR_TRISTATE_SWITCH_ON) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Secure boot requires SMM feature enabled"));
            return -1;
        }
    }

    if (def->genidRequested &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VMGENID)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("this QEMU does not support the 'genid' capability"));
        return -1;
    }

    /* Serial graphics adapter */
    if (def->os.bios.useserial == VIR_TRISTATE_BOOL_YES) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SGA)) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("qemu does not support SGA"));
            return -1;
        }
        if (!def->nserials) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("need at least one serial port to use SGA"));
            return -1;
        }
    }

    if (qemuValidateDomainDefClockTimers(def, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainDefPM(def, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainDefBoot(def, qemuCaps) < 0)
        return -1;

    /* QEMU 2.7 (detected via the availability of query-hotpluggable-cpus)
     * enforces stricter rules than previous versions when it comes to guest
     * CPU topology. Verify known constraints are respected */
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_QUERY_HOTPLUGGABLE_CPUS)) {
        unsigned int topologycpus;
        unsigned int granularity;
        unsigned int numacpus;

        /* Starting from QEMU 2.5, max vCPU count and overall vCPU topology
         * must agree. We only actually enforce this with QEMU 2.7+, due
         * to the capability check above */
        if (virDomainDefGetVcpusTopology(def, &topologycpus) == 0) {
            if (topologycpus != virDomainDefGetVcpusMax(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("CPU topology doesn't match maximum vcpu count"));
                return -1;
            }

            numacpus = virDomainNumaGetCPUCountTotal(def->numa);
            if ((numacpus != 0) && (topologycpus != numacpus)) {
                VIR_WARN("CPU topology doesn't match numa CPU count; "
                         "partial NUMA mapping is obsoleted and will "
                         "be removed in future");
            }
        }

        /* vCPU hotplug granularity must be respected */
        granularity = qemuValidateDefGetVcpuHotplugGranularity(def);
        if ((virDomainDefGetVcpus(def) % granularity) != 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("vCPUs count must be a multiple of the vCPU "
                             "hotplug granularity (%u)"),
                           granularity);
            return -1;
        }
    }

    if (qemuValidateDomainCpuCount(def, qemuCaps) < 0)
        return -1;

    if (ARCH_IS_X86(def->os.arch) &&
        virDomainDefGetVcpusMax(def) > QEMU_MAX_VCPUS_WITHOUT_EIM) {
        if (!qemuDomainIsQ35(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("more than %d vCPUs are only supported on "
                             "q35-based machine types"),
                           QEMU_MAX_VCPUS_WITHOUT_EIM);
            return -1;
        }
        if (!def->iommu || def->iommu->eim != VIR_TRISTATE_SWITCH_ON) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("more than %d vCPUs require extended interrupt "
                             "mode enabled on the iommu device"),
                           QEMU_MAX_VCPUS_WITHOUT_EIM);
            return -1;
        }
    }

    if (def->nresctrls &&
        def->virtType != VIR_DOMAIN_VIRT_KVM) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("cachetune is only supported for KVM domains"));
        return -1;
    }

    if (qemuValidateDomainDefFeatures(def, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainDefMemory(def, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainDefNuma(def, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainDefConsole(def, qemuCaps) < 0)
        return -1;

    if (cfg->vncTLS && cfg->vncTLSx509secretUUID &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_TLS_CREDS_X509)) {
        for (i = 0; i < def->ngraphics; i++) {
            if (def->graphics[i]->type == VIR_DOMAIN_GRAPHICS_TYPE_VNC) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("encrypted VNC TLS keys are not supported with "
                                 "this QEMU binary"));
                return -1;
            }
        }
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefZPCIAddress(virDomainDeviceInfoPtr info,
                                       virQEMUCapsPtr qemuCaps)
{
    if (!virZPCIDeviceAddressIsEmpty(&info->addr.pci.zpci) &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_ZPCI)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       "%s",
                       _("This QEMU binary doesn't support zPCI"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefAddress(const virDomainDeviceDef *dev,
                                   virQEMUCapsPtr qemuCaps)
{
    virDomainDeviceInfoPtr info;

    if (!(info = virDomainDeviceGetInfo((virDomainDeviceDef *)dev)))
        return 0;

    switch ((virDomainDeviceAddressType) info->type) {
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI:
        return qemuValidateDomainDeviceDefZPCIAddress(info, qemuCaps);

    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE:
        /* Address validation might happen before we have had a chance to
         * automatically assign addresses to devices for which the user
         * didn't specify one themselves */
        break;

    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO: {
        virDomainDeviceSpaprVioAddressPtr addr = &(info->addr.spaprvio);

        if (addr->has_reg && addr->reg > 0xffffffff) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("spapr-vio reg='0x%llx' exceeds maximum "
                             "possible value (0xffffffff)"),
                           addr->reg);
            return -1;
        }

        break;
        }

    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DRIVE:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCID:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_USB:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_S390:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_MMIO:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_ISA:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DIMM:
    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_UNASSIGNED:
        /* No validation for these address types yet */
        break;

    case VIR_DOMAIN_DEVICE_ADDRESS_TYPE_LAST:
    default:
        virReportEnumRangeError(virDomainDeviceAddressType, info->type);
        return -1;
    }

    return 0;
}


static bool
qemuValidateNetSupportsCoalesce(virDomainNetType type)
{
    switch (type) {
    case VIR_DOMAIN_NET_TYPE_NETWORK:
    case VIR_DOMAIN_NET_TYPE_BRIDGE:
        return true;
    case VIR_DOMAIN_NET_TYPE_VHOSTUSER:
    case VIR_DOMAIN_NET_TYPE_ETHERNET:
    case VIR_DOMAIN_NET_TYPE_DIRECT:
    case VIR_DOMAIN_NET_TYPE_HOSTDEV:
    case VIR_DOMAIN_NET_TYPE_USER:
    case VIR_DOMAIN_NET_TYPE_SERVER:
    case VIR_DOMAIN_NET_TYPE_CLIENT:
    case VIR_DOMAIN_NET_TYPE_MCAST:
    case VIR_DOMAIN_NET_TYPE_INTERNAL:
    case VIR_DOMAIN_NET_TYPE_UDP:
    case VIR_DOMAIN_NET_TYPE_LAST:
        break;
    }
    return false;
}


static int
qemuValidateDomainVirtioOptions(const virDomainVirtioOptions *virtio,
                                virQEMUCapsPtr qemuCaps)
{
    if (!virtio)
        return 0;

    if (virtio->iommu != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_PCI_IOMMU_PLATFORM)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("the iommu setting is not supported "
                         "with this QEMU binary"));
        return -1;
    }

    if (virtio->ats != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_PCI_ATS)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("the ats setting is not supported with this "
                         "QEMU binary"));
        return -1;
    }

    if (virtio->packed != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_PACKED_QUEUES)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("the packed setting is not supported with this "
                             "QEMU binary"));
            return -1;
        }
    return 0;
}


static int
qemuValidateDomainDeviceDefNetwork(const virDomainNetDef *net,
                                   virQEMUCapsPtr qemuCaps)
{
    bool hasIPv4 = false;
    bool hasIPv6 = false;
    size_t i;

    if (net->type == VIR_DOMAIN_NET_TYPE_USER) {
        if (net->guestIP.nroutes) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid attempt to set network interface "
                             "guest-side IP route, not supported by QEMU"));
            return -1;
        }

        for (i = 0; i < net->guestIP.nips; i++) {
            const virNetDevIPAddr *ip = net->guestIP.ips[i];

            if (VIR_SOCKET_ADDR_VALID(&net->guestIP.ips[i]->peer)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("Invalid attempt to set peer IP for guest"));
                return -1;
            }

            if (VIR_SOCKET_ADDR_IS_FAMILY(&ip->address, AF_INET)) {
                if (hasIPv4) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                                   _("Only one IPv4 address per "
                                     "interface is allowed"));
                    return -1;
                }
                hasIPv4 = true;

                if (ip->prefix > 0 &&
                    (ip->prefix < 4 || ip->prefix > 27)) {
                    virReportError(VIR_ERR_XML_ERROR, "%s",
                                   _("invalid prefix, must be in range of 4-27"));
                    return -1;
                }
            }

            if (VIR_SOCKET_ADDR_IS_FAMILY(&ip->address, AF_INET6)) {
                if (hasIPv6) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                                   _("Only one IPv6 address per "
                                     "interface is allowed"));
                    return -1;
                }
                hasIPv6 = true;

                if (ip->prefix > 120) {
                    virReportError(VIR_ERR_XML_ERROR, "%s",
                                   _("prefix too long"));
                    return -1;
                }
            }
        }
    } else if (net->guestIP.nroutes || net->guestIP.nips) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Invalid attempt to set network interface "
                         "guest-side IP route and/or address info, "
                         "not supported by QEMU"));
        return -1;
    }

    if (virDomainNetIsVirtioModel(net)) {
        if (net->driver.virtio.rx_queue_size & (net->driver.virtio.rx_queue_size - 1)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("rx_queue_size has to be a power of two"));
            return -1;
        }
        if (net->driver.virtio.tx_queue_size & (net->driver.virtio.tx_queue_size - 1)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("tx_queue_size has to be a power of two"));
            return -1;
        }
        if (qemuValidateDomainVirtioOptions(net->virtio, qemuCaps) < 0)
            return -1;
    }

    if (net->mtu &&
        !qemuDomainNetSupportsMTU(net->type)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("setting MTU on interface type %s is not supported yet"),
                       virDomainNetTypeToString(net->type));
        return -1;
    }

    if (net->teaming.type != VIR_DOMAIN_NET_TEAMING_TYPE_NONE &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_NET_FAILOVER)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("virtio-net failover (teaming) is not supported with this QEMU binary"));
        return -1;
    }
    if (net->teaming.type == VIR_DOMAIN_NET_TEAMING_TYPE_PERSISTENT
        && !virDomainNetIsVirtioModel(net)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("virtio-net teaming persistent interface must be <model type='virtio'/>, not '%s'"),
                       virDomainNetGetModelString(net));
        return -1;
    }
    if (net->teaming.type == VIR_DOMAIN_NET_TEAMING_TYPE_TRANSIENT &&
        net->type != VIR_DOMAIN_NET_TYPE_HOSTDEV &&
        net->type != VIR_DOMAIN_NET_TYPE_NETWORK) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("virtio-net teaming transient interface must be type='hostdev', not '%s'"),
                       virDomainNetTypeToString(net->type));
        return -1;
    }

   if (net->coalesce && !qemuValidateNetSupportsCoalesce(net->type)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("coalesce settings on interface type %s are not supported"),
                       virDomainNetTypeToString(net->type));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainChrSourceReconnectDef(const virDomainChrSourceReconnectDef *def)
{
    if (def->enabled == VIR_TRISTATE_BOOL_YES &&
        def->timeout == 0) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("chardev reconnect source timeout cannot be '0'"));
        return -1;
    }

    return 0;
}


static int
qemuValidateChrSerialTargetTypeToAddressType(int targetType)
{
    switch ((virDomainChrSerialTargetType)targetType) {
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA:
        return VIR_DOMAIN_DEVICE_ADDRESS_TYPE_ISA;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_USB:
        return VIR_DOMAIN_DEVICE_ADDRESS_TYPE_USB;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI:
        return VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO:
        return VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_LAST:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE:
        break;
    }

    return VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE;
}


static int
qemuValidateChrSerialTargetModelToTargetType(int targetModel)
{
    switch ((virDomainChrSerialTargetModel) targetModel) {
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_ISA_SERIAL:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_USB_SERIAL:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_USB;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PCI_SERIAL:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SPAPR_VTY:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PL011:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_16550A:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPCONSOLE:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPLMCONSOLE:
        return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP;
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_NONE:
    case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_LAST:
        break;
    }

    return VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE;
}


static int
qemuValidateDomainChrTargetDef(const virDomainChrDef *chr)
{
    int expected;

    switch ((virDomainChrDeviceType)chr->deviceType) {
    case VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL:

        /* Validate target type */
        switch ((virDomainChrSerialTargetType)chr->targetType) {
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_ISA:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_USB:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO:

            expected = qemuValidateChrSerialTargetTypeToAddressType(chr->targetType);

            if (chr->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
                chr->info.type != expected) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Target type '%s' requires address type '%s'"),
                               virDomainChrSerialTargetTypeToString(chr->targetType),
                               virDomainDeviceAddressTypeToString(expected));
                return -1;
            }
            break;

        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP:
            if (chr->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Target type '%s' cannot have an "
                                 "associated address"),
                               virDomainChrSerialTargetTypeToString(chr->targetType));
                return -1;
            }
            break;

        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_NONE:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_LAST:
            break;
        }

        /* Validate target model */
        switch ((virDomainChrSerialTargetModel) chr->targetModel) {
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_ISA_SERIAL:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_USB_SERIAL:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PCI_SERIAL:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SPAPR_VTY:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PL011:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPCONSOLE:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPLMCONSOLE:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_16550A:

            expected = qemuValidateChrSerialTargetModelToTargetType(chr->targetModel);

            if (chr->targetType != expected) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Target model '%s' requires target type '%s'"),
                               virDomainChrSerialTargetModelTypeToString(chr->targetModel),
                               virDomainChrSerialTargetTypeToString(expected));
                return -1;
            }
            break;

        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_NONE:
        case VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_LAST:
            break;
        }
        break;

    case VIR_DOMAIN_CHR_DEVICE_TYPE_CONSOLE:
    case VIR_DOMAIN_CHR_DEVICE_TYPE_PARALLEL:
    case VIR_DOMAIN_CHR_DEVICE_TYPE_CHANNEL:
    case VIR_DOMAIN_CHR_DEVICE_TYPE_LAST:
        /* Nothing to do */
        break;
    }

    return 0;
}


static int
qemuValidateDomainChrSourceDef(const virDomainChrSourceDef *def,
                               virQEMUCapsPtr qemuCaps)
{
    switch ((virDomainChrType)def->type) {
    case VIR_DOMAIN_CHR_TYPE_TCP:
        if (qemuValidateDomainChrSourceReconnectDef(&def->data.tcp.reconnect) < 0)
            return -1;
        break;

    case VIR_DOMAIN_CHR_TYPE_UNIX:
        if (qemuValidateDomainChrSourceReconnectDef(&def->data.nix.reconnect) < 0)
            return -1;
        break;

    case VIR_DOMAIN_CHR_TYPE_FILE:
        if (def->data.file.append != VIR_TRISTATE_SWITCH_ABSENT &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_CHARDEV_FILE_APPEND)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("append not supported in this QEMU binary"));
            return -1;
        }
        break;

    case VIR_DOMAIN_CHR_TYPE_NULL:
    case VIR_DOMAIN_CHR_TYPE_VC:
    case VIR_DOMAIN_CHR_TYPE_PTY:
    case VIR_DOMAIN_CHR_TYPE_DEV:
    case VIR_DOMAIN_CHR_TYPE_PIPE:
    case VIR_DOMAIN_CHR_TYPE_STDIO:
    case VIR_DOMAIN_CHR_TYPE_UDP:
    case VIR_DOMAIN_CHR_TYPE_SPICEVMC:
    case VIR_DOMAIN_CHR_TYPE_SPICEPORT:
    case VIR_DOMAIN_CHR_TYPE_NMDM:
    case VIR_DOMAIN_CHR_TYPE_LAST:
        break;
    }

    if (def->logfile) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CHARDEV_LOGFILE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("logfile not supported in this QEMU binary"));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainChrDef(const virDomainChrDef *dev,
                         const virDomainDef *def,
                         virQEMUCapsPtr qemuCaps)
{
    if (qemuValidateDomainChrSourceDef(dev->source, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainChrTargetDef(dev) < 0)
        return -1;

    if (dev->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_PARALLEL &&
        (ARCH_IS_S390(def->os.arch) || qemuDomainIsPSeries(def))) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("parallel ports are not supported"));
            return -1;
    }

    if (dev->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL) {
        bool isCompatible = true;

        if (dev->targetType == VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SYSTEM) {
            if (dev->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_PL011 &&
                !qemuDomainIsARMVirt(def)) {
                isCompatible = false;
            }
            if (dev->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_16550A &&
                !qemuDomainIsRISCVVirt(def)) {
                isCompatible = false;
            }
        }

        if (!qemuDomainIsPSeries(def) &&
            (dev->targetType == VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SPAPR_VIO ||
             dev->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SPAPR_VTY)) {
            isCompatible = false;
        }

        if (!ARCH_IS_S390(def->os.arch) &&
            (dev->targetType == VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_SCLP ||
             dev->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPCONSOLE ||
             dev->targetModel == VIR_DOMAIN_CHR_SERIAL_TARGET_MODEL_SCLPLMCONSOLE)) {
            isCompatible = false;
        }

        if (!isCompatible) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Serial device with target type '%s' and "
                             "target model '%s' not compatible with guest "
                             "architecture or machine type"),
                           virDomainChrSerialTargetTypeToString(dev->targetType),
                           virDomainChrSerialTargetModelTypeToString(dev->targetModel));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainSmartcardDef(const virDomainSmartcardDef *def,
                               virQEMUCapsPtr qemuCaps)
{
    switch (def->type) {
    case VIR_DOMAIN_SMARTCARD_TYPE_HOST:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CCID_EMULATED)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("this QEMU binary lacks smartcard host "
                             "mode support"));
            return -1;
        }
        break;

    case VIR_DOMAIN_SMARTCARD_TYPE_HOST_CERTIFICATES:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CCID_EMULATED)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("this QEMU binary lacks smartcard host "
                             "mode support"));
            return -1;
        }
        break;

    case VIR_DOMAIN_SMARTCARD_TYPE_PASSTHROUGH:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_CCID_PASSTHRU)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("this QEMU binary lacks smartcard "
                             "passthrough mode support"));
            return -1;
        }
        break;

    default:
        virReportEnumRangeError(virDomainSmartcardType, def->type);
        return -1;
    }

    if (def->type == VIR_DOMAIN_SMARTCARD_TYPE_PASSTHROUGH &&
        qemuValidateDomainChrSourceDef(def->data.passthru, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuValidateDomainRNGDef(const virDomainRNGDef *def,
                         virQEMUCapsPtr qemuCaps)
{
    if (def->backend == VIR_DOMAIN_RNG_BACKEND_EGD &&
        qemuValidateDomainChrSourceDef(def->source.chardev, qemuCaps) < 0)
        return -1;

    if (qemuValidateDomainVirtioOptions(def->virtio, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuValidateDomainRedirdevDef(const virDomainRedirdevDef *def,
                              virQEMUCapsPtr qemuCaps)
{
    if (qemuValidateDomainChrSourceDef(def->source, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuValidateDomainWatchdogDef(const virDomainWatchdogDef *dev,
                              const virDomainDef *def)
{
    switch ((virDomainWatchdogModel) dev->model) {
    case VIR_DOMAIN_WATCHDOG_MODEL_I6300ESB:
        if (dev->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
            dev->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("%s model of watchdog can go only on PCI bus"),
                           virDomainWatchdogModelTypeToString(dev->model));
            return -1;
        }
        break;

    case VIR_DOMAIN_WATCHDOG_MODEL_IB700:
        if (dev->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
            dev->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_ISA) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("%s model of watchdog can go only on ISA bus"),
                           virDomainWatchdogModelTypeToString(dev->model));
            return -1;
        }
        break;

    case VIR_DOMAIN_WATCHDOG_MODEL_DIAG288:
        if (dev->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("%s model of watchdog is virtual and cannot go on any bus."),
                           virDomainWatchdogModelTypeToString(dev->model));
            return -1;
        }
        if (!(ARCH_IS_S390(def->os.arch))) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("%s model of watchdog is allowed for s390 and s390x only"),
                           virDomainWatchdogModelTypeToString(dev->model));
            return -1;
        }
        break;

    case VIR_DOMAIN_WATCHDOG_MODEL_LAST:
        break;
    }

    return 0;
}


static int
qemuValidateDomainMdevDefVFIOPCI(const virDomainHostdevDef *hostdev,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    const virDomainHostdevSubsysMediatedDev *dev;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VFIO_PCI)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("VFIO PCI device assignment is not "
                         "supported by this version of QEMU"));
        return -1;
    }

    /* VFIO-PCI does not support boot */
    if (hostdev->info->bootIndex) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("booting from assigned devices is not "
                         "supported by mediated devices of "
                         "model vfio-pci"));
        return -1;
    }

    dev = &hostdev->source.subsys.u.mdev;
    if (dev->display == VIR_TRISTATE_SWITCH_ABSENT)
        return 0;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_VFIO_PCI_DISPLAY)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("display property of device vfio-pci is "
                         "not supported by this version of QEMU"));
        return -1;
    }

    if (dev->model != VIR_MDEV_MODEL_TYPE_VFIO_PCI) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("<hostdev> attribute 'display' is only supported"
                         " with model='vfio-pci'"));

        return -1;
    }

    if (dev->display == VIR_TRISTATE_SWITCH_ON) {
        if (def->ngraphics == 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("graphics device is needed for attribute value "
                             "'display=on' in <hostdev>"));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainMdevDefVFIOAP(const virDomainHostdevDef *hostdev,
                                const virDomainDef *def,
                                virQEMUCapsPtr qemuCaps)
{
    size_t i;
    bool vfioap_found = false;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VFIO_AP)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("VFIO AP device assignment is not "
                         "supported by this version of QEMU"));
        return -1;
    }

    /* VFIO-AP does not support boot */
    if (hostdev->info->bootIndex) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("booting from assigned devices is not "
                         "supported by mediated devices of "
                         "model vfio-ap"));
        return -1;
    }

    /* VFIO-AP is restricted to a single mediated device only */
    for (i = 0; i < def->nhostdevs; i++) {
        virDomainHostdevDefPtr hdev = def->hostdevs[i];

        if (virHostdevIsMdevDevice(hdev) &&
            hdev->source.subsys.u.mdev.model == VIR_MDEV_MODEL_TYPE_VFIO_AP) {
            if (vfioap_found) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("Only one hostdev of model vfio-ap is "
                                 "supported"));
                return -1;
            }
            vfioap_found = true;
        }
    }

    return 0;
}


static int
qemuValidateDomainMdevDef(const virDomainHostdevDef *hostdev,
                          const virDomainDef *def,
                          virQEMUCapsPtr qemuCaps)
{
    const virDomainHostdevSubsysMediatedDev *mdevsrc;

    mdevsrc = &hostdev->source.subsys.u.mdev;
    switch ((virMediatedDeviceModelType) mdevsrc->model) {
    case VIR_MDEV_MODEL_TYPE_VFIO_PCI:
        return qemuValidateDomainMdevDefVFIOPCI(hostdev, def, qemuCaps);
    case VIR_MDEV_MODEL_TYPE_VFIO_AP:
        return qemuValidateDomainMdevDefVFIOAP(hostdev, def, qemuCaps);
    case VIR_MDEV_MODEL_TYPE_VFIO_CCW:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VFIO_CCW)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("VFIO CCW device assignment is not "
                             "supported by this version of QEMU"));
            return -1;
        }
        break;
    case VIR_MDEV_MODEL_TYPE_LAST:
    default:
        virReportEnumRangeError(virMediatedDeviceModelType,
                                mdevsrc->model);
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefHostdev(const virDomainHostdevDef *hostdev,
                                   const virDomainDef *def,
                                   virQEMUCapsPtr qemuCaps)
{
    int backend;

    /* forbid capabilities mode hostdev in this kind of hypervisor */
    if (hostdev->mode == VIR_DOMAIN_HOSTDEV_MODE_CAPABILITIES) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("hostdev mode 'capabilities' is not "
                         "supported in %s"),
                       virDomainVirtTypeToString(def->virtType));
        return -1;
    }

    if (hostdev->mode == VIR_DOMAIN_HOSTDEV_MODE_SUBSYS) {
        switch ((virDomainHostdevSubsysType) hostdev->source.subsys.type) {
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_USB:
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI:
            break;

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_PCI:
            backend = hostdev->source.subsys.u.pci.backend;

            if (backend == VIR_DOMAIN_HOSTDEV_PCI_BACKEND_VFIO) {
                if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VFIO_PCI)) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                                   _("VFIO PCI device assignment is not "
                                     "supported by this version of qemu"));
                    return -1;
                }
            }
            break;

        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_SCSI_HOST:
            if (hostdev->info->bootIndex) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("booting from assigned devices is not "
                                 "supported by vhost SCSI devices"));
                return -1;
            }
            break;
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_MDEV:
            return qemuValidateDomainMdevDef(hostdev, def, qemuCaps);
        case VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_LAST:
        default:
            virReportEnumRangeError(virDomainHostdevSubsysType,
                                    hostdev->source.subsys.type);
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefVideo(const virDomainVideoDef *video,
                                 virQEMUCapsPtr qemuCaps)
{
    /* there's no properties to validate for NONE video devices */
    if (video->type == VIR_DOMAIN_VIDEO_TYPE_NONE)
        return 0;

    if (!video->primary &&
        video->type != VIR_DOMAIN_VIDEO_TYPE_QXL &&
        video->type != VIR_DOMAIN_VIDEO_TYPE_VIRTIO) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("video type '%s' is only valid as primary "
                         "video device"),
                       virDomainVideoTypeToString(video->type));
        return -1;
    }

    if (video->accel && video->accel->accel2d == VIR_TRISTATE_SWITCH_ON) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("qemu does not support the accel2d setting"));
        return -1;
    }

    if (video->type == VIR_DOMAIN_VIDEO_TYPE_QXL) {
        if (video->vram > (UINT_MAX / 1024)) {
            virReportError(VIR_ERR_OVERFLOW,
                           _("value for 'vram' must be less than '%u'"),
                           UINT_MAX / 1024);
            return -1;
        }
        if (video->ram > (UINT_MAX / 1024)) {
            virReportError(VIR_ERR_OVERFLOW,
                           _("value for 'ram' must be less than '%u'"),
                           UINT_MAX / 1024);
            return -1;
        }
        if (video->vgamem) {
            if (video->vgamem < 1024) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("value for 'vgamem' must be at least 1 MiB "
                                 "(1024 KiB)"));
                return -1;
            }

            if (video->vgamem != VIR_ROUND_UP_POWER_OF_TWO(video->vgamem)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("value for 'vgamem' must be power of two"));
                return -1;
            }
        }
    }

    if (video->type != VIR_DOMAIN_VIDEO_TYPE_VGA &&
        video->type != VIR_DOMAIN_VIDEO_TYPE_QXL &&
        video->type != VIR_DOMAIN_VIDEO_TYPE_VIRTIO &&
        video->type != VIR_DOMAIN_VIDEO_TYPE_BOCHS) {
        if (video->res) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("model resolution is not supported"));
            return -1;
        }
    }

    if (video->type == VIR_DOMAIN_VIDEO_TYPE_VGA ||
        video->type == VIR_DOMAIN_VIDEO_TYPE_VMVGA) {
        if (video->vram && video->vram < 1024) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           "%s", _("value for 'vram' must be at least "
                                   "1 MiB (1024 KiB)"));
            return -1;
        }
    }

    if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER) {
        if (video->type == VIR_DOMAIN_VIDEO_TYPE_VIRTIO &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VHOST_USER_GPU)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("this QEMU does not support 'vhost-user' video device"));
            return -1;
        }
    } else if (video->accel) {
        if (video->accel->accel3d == VIR_TRISTATE_SWITCH_ON &&
            (video->type != VIR_DOMAIN_VIDEO_TYPE_VIRTIO ||
             !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_GPU_VIRGL))) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("%s 3d acceleration is not supported"),
                           virDomainVideoTypeToString(video->type));
            return -1;
        }
    }

    if (qemuValidateDomainVirtioOptions(video->virtio, qemuCaps) < 0)
        return -1;

    return 0;
}


int
qemuValidateDomainDeviceDefDisk(const virDomainDiskDef *disk,
                                virQEMUCapsPtr qemuCaps)
{
    const char *driverName = virDomainDiskGetDriver(disk);
    virStorageSourcePtr n;
    int idx;
    int partition;

    if (disk->src->shared && !disk->src->readonly &&
        !qemuBlockStorageSourceSupportsConcurrentAccess(disk->src)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("shared access for disk '%s' requires use of "
                         "supported storage format"), disk->dst);
        return -1;
    }

    if (disk->copy_on_read == VIR_TRISTATE_SWITCH_ON) {
        if (disk->src->readonly) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("copy_on_read is not compatible with read-only disk '%s'"),
                           disk->dst);
            return -1;
        }

        if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM ||
            disk->device == VIR_DOMAIN_DISK_DEVICE_FLOPPY) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("copy_on_read is not supported with removable disk '%s'"),
                           disk->dst);
            return -1;
        }
    }

    if (disk->geometry.cylinders > 0 &&
        disk->geometry.heads > 0 &&
        disk->geometry.sectors > 0) {
        if (disk->bus == VIR_DOMAIN_DISK_BUS_USB ||
            disk->bus == VIR_DOMAIN_DISK_BUS_SD) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("CHS geometry can not be set for '%s' bus"),
                           virDomainDiskBusTypeToString(disk->bus));
            return -1;
        }

        if (disk->geometry.trans != VIR_DOMAIN_DISK_TRANS_DEFAULT &&
            disk->bus != VIR_DOMAIN_DISK_BUS_IDE) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("CHS translation mode can only be set for 'ide' bus not '%s'"),
                           virDomainDiskBusTypeToString(disk->bus));
            return -1;
        }
    }

    if (disk->serial && disk->bus == VIR_DOMAIN_DISK_BUS_SD) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Serial property not supported for drive bus '%s'"),
                       virDomainDiskBusTypeToString(disk->bus));
        return -1;
    }

    if (driverName && STRNEQ(driverName, "qemu")) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("unsupported driver name '%s' for disk '%s'"),
                       driverName, disk->dst);
        return -1;
    }

    if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM &&
        disk->bus == VIR_DOMAIN_DISK_BUS_VIRTIO) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("disk type 'virtio' of '%s' does not support ejectable media"),
                       disk->dst);
        return -1;
    }

    if (virDiskNameParse(disk->dst, &idx, &partition) < 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("invalid disk target '%s'"), disk->dst);
        return -1;
    }

    if (partition != 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("invalid disk target '%s', partitions can't appear in disk targets"),
                       disk->dst);
        return -1;
    }

    for (n = disk->src; virStorageSourceIsBacking(n); n = n->backingStore) {
        if (qemuDomainValidateStorageSource(n, qemuCaps) < 0)
            return -1;
    }

    if (disk->bus == VIR_DOMAIN_DISK_BUS_VIRTIO &&
        qemuValidateDomainVirtioOptions(disk->virtio, qemuCaps) < 0) {
        return -1;
    }

    return 0;
}


/**
 * @qemuCaps: QEMU capabilities
 * @model: SCSI model to check
 *
 * Using the @qemuCaps, let's ensure the provided @model can be supported
 *
 * Returns true if acceptable, false otherwise with error message set.
 */
static bool
qemuValidateCheckSCSIControllerModel(virQEMUCapsPtr qemuCaps,
                                    int model)
{
    switch ((virDomainControllerModelSCSI) model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSILOGIC:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_LSI)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support "
                             "the LSI 53C895A SCSI controller"));
            return false;
        }
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_SCSI:
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_TRANSITIONAL:
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_NON_TRANSITIONAL:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_SCSI)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support "
                             "virtio scsi controller"));
            return false;
        }
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI:
        /*TODO: need checking work here if necessary */
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1068:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MPTSAS1068)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support "
                             "the LSI SAS1068 (MPT Fusion) controller"));
            return false;
        }
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1078:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MEGASAS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support "
                             "the LSI SAS1078 (MegaRAID) controller"));
            return false;
        }
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_AUTO:
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_BUSLOGIC:
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VMPVSCSI:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Unsupported controller model: %s"),
                       virDomainControllerModelSCSITypeToString(model));
        return false;
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LAST:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unexpected SCSI controller model %d"),
                       model);
        return false;
    }

    return true;
}



static int
qemuValidateDomainDeviceDefControllerSATA(const virDomainControllerDef *controller,
                                          const virDomainDef *def,
                                          virQEMUCapsPtr qemuCaps)
{
    /* first SATA controller on Q35 machines is implicit */
    if (controller->idx == 0 && qemuDomainIsQ35(def))
        return 0;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_ICH9_AHCI)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("SATA is not supported with this QEMU binary"));
        return -1;
    }
    return 0;
}


static int
qemuValidateDomainDeviceDefControllerIDE(const virDomainControllerDef *controller,
                                         const virDomainDef *def)
{
    /* first IDE controller is implicit on various machines */
    if (controller->idx == 0 && qemuDomainHasBuiltinIDE(def))
        return 0;

    /* Since we currently only support the integrated IDE
     * controller on various boards, if we ever get to here, it's
     * because some other machinetype had an IDE controller
     * specified, or one with a single IDE controller had multiple
     * IDE controllers specified.
     */
    if (qemuDomainHasBuiltinIDE(def))
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Only a single IDE controller is supported "
                         "for this machine type"));
    else
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("IDE controllers are unsupported for "
                         "this QEMU binary or machine type"));
    return -1;
}


/* qemuValidateCheckSCSIControllerIOThreads:
 * @controller: Pointer to controller def
 * @def: Pointer to domain def
 *
 * If this controller definition has iothreads set, let's make sure the
 * configuration is right before adding to the command line
 *
 * Returns true if either supported or there are no iothreads for controller;
 * otherwise, returns false if configuration is not quite right.
 */
static bool
qemuValidateCheckSCSIControllerIOThreads(const virDomainControllerDef *controller,
                                         const virDomainDef *def)
{
    if (!controller->iothread)
        return true;

    if (controller->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
        controller->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI &&
        controller->info.type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW) {
       virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("virtio-scsi IOThreads only available for virtio "
                         "pci and virtio ccw controllers"));
       return false;
    }

    /* Can we find the controller iothread in the iothreadid list? */
    if (!virDomainIOThreadIDFind(def, controller->iothread)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("controller iothread '%u' not defined in iothreadid"),
                       controller->iothread);
        return false;
    }

    return true;
}


static int
qemuValidateDomainDeviceDefControllerSCSI(const virDomainControllerDef *controller,
                                          const virDomainDef *def)
{
    switch ((virDomainControllerModelSCSI) controller->model) {
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_SCSI:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_TRANSITIONAL:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_NON_TRANSITIONAL:
            if (!qemuValidateCheckSCSIControllerIOThreads(controller, def))
                return -1;
            break;

        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_AUTO:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_BUSLOGIC:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSILOGIC:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1068:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VMPVSCSI:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1078:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_DEFAULT:
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LAST:
            break;
    }

    return 0;
}


/**
 * virValidateControllerPCIModelNameToQEMUCaps:
 * @modelName: model name
 *
 * Maps model names for PCI controllers (virDomainControllerPCIModelName)
 * to the QEMU capabilities required to use them (virQEMUCapsFlags).
 *
 * Returns: the QEMU capability itself (>0) on success; 0 if no QEMU
 *          capability is needed; <0 on error.
 */
static int
virValidateControllerPCIModelNameToQEMUCaps(int modelName)
{
    switch ((virDomainControllerPCIModelName) modelName) {
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCI_BRIDGE:
        return QEMU_CAPS_DEVICE_PCI_BRIDGE;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_I82801B11_BRIDGE:
        return QEMU_CAPS_DEVICE_DMI_TO_PCI_BRIDGE;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_IOH3420:
        return QEMU_CAPS_DEVICE_IOH3420;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_X3130_UPSTREAM:
        return QEMU_CAPS_DEVICE_X3130_UPSTREAM;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_XIO3130_DOWNSTREAM:
        return QEMU_CAPS_DEVICE_XIO3130_DOWNSTREAM;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB:
        return QEMU_CAPS_DEVICE_PXB;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB_PCIE:
        return QEMU_CAPS_DEVICE_PXB_PCIE;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCIE_ROOT_PORT:
        return QEMU_CAPS_DEVICE_PCIE_ROOT_PORT;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE:
        return QEMU_CAPS_DEVICE_SPAPR_PCI_HOST_BRIDGE;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCIE_PCI_BRIDGE:
        return QEMU_CAPS_DEVICE_PCIE_PCI_BRIDGE;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE:
        return 0;
    case VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_LAST:
    default:
        return -1;
    }

    return -1;
}


static int
qemuValidateDomainDeviceDefControllerAttributes(const virDomainControllerDef *controller,
                                                virQEMUCapsPtr qemuCaps)
{
    if (!(controller->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI &&
          (controller->model == VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_SCSI ||
           controller->model == VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_TRANSITIONAL ||
           controller->model == VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_NON_TRANSITIONAL))) {
        if (controller->queues) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("'queues' is only supported by virtio-scsi controller"));
            return -1;
        }
        if (controller->cmd_per_lun) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("'cmd_per_lun' is only supported by virtio-scsi controller"));
            return -1;
        }
        if (controller->max_sectors) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("'max_sectors' is only supported by virtio-scsi controller"));
            return -1;
        }
        if (controller->ioeventfd) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("'ioeventfd' is only supported by virtio-scsi controller"));
            return -1;
        }
        if (controller->iothread) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("'iothread' is only supported for virtio-scsi controller"));
            return -1;
        }
        if (qemuValidateDomainVirtioOptions(controller->virtio, qemuCaps) < 0)
            return -1;
    }

    if (controller->type == VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL &&
        qemuValidateDomainVirtioOptions(controller->virtio, qemuCaps) < 0) {
        return -1;
    }

    return 0;
}


#define virReportControllerMissingOption(cont, model, modelName, option) \
    virReportError(VIR_ERR_INTERNAL_ERROR, \
                   _("Required option '%s' is not set for PCI controller " \
                     "with index '%d', model '%s' and modelName '%s'"), \
                   (option), (cont->idx), (model), (modelName));
#define virReportControllerInvalidOption(cont, model, modelName, option) \
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, \
                   _("Option '%s' is not valid for PCI controller " \
                     "with index '%d', model '%s' and modelName '%s'"), \
                   (option), (cont->idx), (model), (modelName));
#define virReportControllerInvalidValue(cont, model, modelName, option) \
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED, \
                   _("Option '%s' has invalid value for PCI controller " \
                     "with index '%d', model '%s' and modelName '%s'"), \
                   (option), (cont->idx), (model), (modelName));


static int
qemuValidateDomainDeviceDefControllerPCI(const virDomainControllerDef *cont,
                                         const virDomainDef *def,
                                         virQEMUCapsPtr qemuCaps)

{
    const virDomainPCIControllerOpts *pciopts = &cont->opts.pciopts;
    const char *model = virDomainControllerModelPCITypeToString(cont->model);
    const char *modelName = virDomainControllerPCIModelNameTypeToString(pciopts->modelName);
    int cap = virValidateControllerPCIModelNameToQEMUCaps(pciopts->modelName);

    if (!model) {
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }
    if (!modelName) {
        virReportEnumRangeError(virDomainControllerPCIModelName, pciopts->modelName);
        return -1;
    }

    /* modelName */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        /* modelName should have been set automatically */
        if (pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE) {
            virReportControllerMissingOption(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        /* modelName must be set for pSeries guests, but it's an error
         * for it to be set for any other guest */
        if (qemuDomainIsPSeries(def)) {
            if (pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE) {
                virReportControllerMissingOption(cont, model, modelName, "modelName");
                return -1;
            }
        } else {
            if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE) {
                virReportControllerInvalidOption(cont, model, modelName, "modelName");
                return -1;
            }
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE) {
            virReportControllerInvalidOption(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* modelName (cont'd) */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE &&
            pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCI_BRIDGE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_I82801B11_BRIDGE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_IOH3420 &&
            pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCIE_ROOT_PORT) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_X3130_UPSTREAM) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_XIO3130_DOWNSTREAM) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB_PCIE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCIE_PCI_BRIDGE) {
            virReportControllerInvalidValue(cont, model, modelName, "modelName");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* index */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (cont->idx == 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Index for '%s' controllers must be > 0"),
                           model);
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        /* pSeries guests can have multiple PHBs, so it's expected that
         * the index will not be zero for some of them */
        if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT &&
            pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE) {
            break;
        }

        /* For all other pci-root and pcie-root controllers, though,
         * the index must be zero */
        if (cont->idx != 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Index for '%s' controllers must be 0"),
                           model);
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* targetIndex */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        /* PHBs for pSeries guests must have been assigned a targetIndex */
        if (pciopts->targetIndex == -1 &&
            pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE) {
            virReportControllerMissingOption(cont, model, modelName, "targetIndex");
            return -1;
        }

        /* targetIndex only applies to PHBs, so for any other pci-root
         * controller it being present is an error */
        if (pciopts->targetIndex != -1 &&
            pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE) {
            virReportControllerInvalidOption(cont, model, modelName, "targetIndex");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->targetIndex != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "targetIndex");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* pcihole64 */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        if (pciopts->pcihole64 ||  pciopts->pcihole64size != 0) {
            if (!qemuDomainIsI440FX(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Setting the 64-bit PCI hole size is not "
                                 "supported for machine '%s'"), def->os.machine);
                return -1;
            }

            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_I440FX_PCI_HOLE64_SIZE)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("64-bit PCI hole size setting is not supported "
                                 "with this QEMU binary"));
                return -1;
            }
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        if (pciopts->pcihole64 || pciopts->pcihole64size != 0) {
            if (!qemuDomainIsQ35(def)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Setting the 64-bit PCI hole size is not "
                                 "supported for machine '%s'"), def->os.machine);
                return -1;
            }

            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_Q35_PCI_HOLE64_SIZE)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("64-bit PCI hole size setting is not supported "
                                 "with this QEMU binary"));
                return -1;
            }
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->pcihole64 ||
            pciopts->pcihole64size != 0) {
            virReportControllerInvalidOption(cont, model, modelName, "pcihole64");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* busNr */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        if (pciopts->busNr == -1) {
            virReportControllerMissingOption(cont, model, modelName, "busNr");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->busNr != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "busNr");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* numaNode */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        /* numaNode can be used for these controllers, but it's not set
         * automatically so it can be missing */
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        /* Only PHBs support numaNode */
        if (pciopts->numaNode != -1 &&
            pciopts->modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE) {
            virReportControllerInvalidOption(cont, model, modelName, "numaNode");
            return -1;
        }

        /* However, the default PHB doesn't support numaNode */
        if (pciopts->numaNode != -1 &&
            pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE &&
            pciopts->targetIndex == 0) {
            virReportControllerInvalidOption(cont, model, modelName, "numaNode");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->numaNode != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "numaNode");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* chassisNr */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
        if (pciopts->chassisNr == -1) {
            virReportControllerMissingOption(cont, model, modelName, "chassisNr");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->chassisNr != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "chassisNr");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        return -1;
    }

    /* chassis and port */
    switch ((virDomainControllerModelPCI) cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
        if (pciopts->chassis == -1) {
            virReportControllerMissingOption(cont, model, modelName, "chassis");
            return -1;
        }
        if (pciopts->port == -1) {
            virReportControllerMissingOption(cont, model, modelName, "port");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
        if (pciopts->chassis != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "chassis");
            return -1;
        }
        if (pciopts->port != -1) {
            virReportControllerInvalidOption(cont, model, modelName, "port");
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    default:
        virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
    }

    /* hotplug */
    if (pciopts->hotplug != VIR_TRISTATE_SWITCH_ABSENT) {
        switch ((virDomainControllerModelPCI) cont->model) {
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_PCIE_ROOT_PORT_HOTPLUG)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("setting the hotplug property on a '%s' device is not supported by this QEMU binary"),
                               modelName);
                return -1;
            }
            break;

        case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
        case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_TO_PCI_BRIDGE:
            virReportControllerInvalidOption(cont, model, modelName, "hotplug");
            return -1;

        case VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT:
        case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
        default:
            virReportEnumRangeError(virDomainControllerModelPCI, cont->model);
        }
    }

    /* QEMU device availability */
    if (cap < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown QEMU device for '%s' controller"),
                       modelName);
        return -1;
    }
    if (cap > 0 && !virQEMUCapsGet(qemuCaps, cap)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("The '%s' device is not supported by this QEMU binary"),
                       modelName);
        return -1;
    }

    /* PHBs didn't support numaNode from the very beginning, so an extra
     * capability check is required */
    if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT &&
        pciopts->modelName == VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_SPAPR_PCI_HOST_BRIDGE &&
        pciopts->numaNode != -1 &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPAPR_PCI_HOST_BRIDGE_NUMA_NODE)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Option '%s' is not supported by '%s' device with this QEMU binary"),
                       "numaNode", modelName);
        return -1;
    }

    return 0;
}


#undef virReportControllerInvalidValue
#undef virReportControllerInvalidOption
#undef virReportControllerMissingOption


static int
qemuValidateDomainDeviceDefController(const virDomainControllerDef *controller,
                                      const virDomainDef *def,
                                      virQEMUCapsPtr qemuCaps)
{
    int ret = 0;

    if (!qemuDomainCheckCCWS390AddressSupport(def, &controller->info, qemuCaps,
                                              "controller"))
        return -1;

    if (controller->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI &&
        !qemuValidateCheckSCSIControllerModel(qemuCaps, controller->model))
        return -1;

    if (qemuValidateDomainDeviceDefControllerAttributes(controller, qemuCaps) < 0)
        return -1;

    switch ((virDomainControllerType)controller->type) {
    case VIR_DOMAIN_CONTROLLER_TYPE_IDE:
        ret = qemuValidateDomainDeviceDefControllerIDE(controller, def);
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_SCSI:
        ret = qemuValidateDomainDeviceDefControllerSCSI(controller, def);
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_PCI:
        ret = qemuValidateDomainDeviceDefControllerPCI(controller, def,
                                                       qemuCaps);
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
        ret = qemuValidateDomainDeviceDefControllerSATA(controller, def,
                                                        qemuCaps);
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_FDC:
    case VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL:
    case VIR_DOMAIN_CONTROLLER_TYPE_CCID:
    case VIR_DOMAIN_CONTROLLER_TYPE_USB:
    case VIR_DOMAIN_CONTROLLER_TYPE_XENBUS:
    case VIR_DOMAIN_CONTROLLER_TYPE_LAST:
        break;
    }

    return ret;
}


static int
qemuValidateDomainDeviceDefSPICEGraphics(const virDomainGraphicsDef *graphics,
                                         virQEMUDriverPtr driver,
                                         virQEMUCapsPtr qemuCaps)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    virDomainGraphicsListenDefPtr glisten = NULL;
    int tlsPort = graphics->data.spice.tlsPort;

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPICE)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("spice graphics are not supported with this QEMU"));
        return -1;
    }

    glisten = virDomainGraphicsGetListen((virDomainGraphicsDefPtr)graphics, 0);
    if (!glisten) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing listen element"));
        return -1;
    }

    switch (glisten->type) {
    case VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_SOCKET:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPICE_UNIX)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("unix socket for spice graphics are not supported "
                             "with this QEMU"));
            return -1;
        }
        break;

    case VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_ADDRESS:
    case VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_NETWORK:
        if (tlsPort > 0 && !cfg->spiceTLS) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("spice TLS port set in XML configuration, "
                             "but TLS is disabled in qemu.conf"));
            return -1;
        }
        break;

    case VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_NONE:
        break;
    case VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_LAST:
        break;
    }

    if (graphics->data.spice.filetransfer == VIR_TRISTATE_BOOL_NO &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPICE_FILE_XFER_DISABLE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU can't disable file transfers through spice"));
            return -1;
    }

    if (graphics->data.spice.gl == VIR_TRISTATE_BOOL_YES) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPICE_GL)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support spice OpenGL"));
            return -1;
        }

        if (graphics->data.spice.rendernode &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_SPICE_RENDERNODE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support spice OpenGL rendernode"));
            return -1;
        }
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefGraphics(const virDomainGraphicsDef *graphics,
                                    const virDomainDef *def,
                                    virQEMUDriverPtr driver,
                                    virQEMUCapsPtr qemuCaps)
{
    bool have_egl_headless = false;
    size_t i;

    for (i = 0; i < def->ngraphics; i++) {
        if (def->graphics[i]->type == VIR_DOMAIN_GRAPHICS_TYPE_EGL_HEADLESS) {
            have_egl_headless = true;
            break;
        }
    }

    /* Only VNC and SPICE can be paired with egl-headless, the other types
     * either don't make sense to pair with egl-headless or aren't even
     * supported by QEMU.
     */
    if (have_egl_headless) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_EGL_HEADLESS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("egl-headless display is not supported with this "
                             "QEMU binary"));
            return -1;
        }

        if (graphics->type != VIR_DOMAIN_GRAPHICS_TYPE_EGL_HEADLESS &&
            graphics->type != VIR_DOMAIN_GRAPHICS_TYPE_VNC &&
            graphics->type != VIR_DOMAIN_GRAPHICS_TYPE_SPICE) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("graphics type 'egl-headless' is only supported "
                             "with one of: 'vnc', 'spice' graphics types"));
            return -1;
        }

        /* '-spice gl=on' and '-display egl-headless' are mutually
         * exclusive
         */
        if (graphics->type == VIR_DOMAIN_GRAPHICS_TYPE_SPICE &&
            graphics->data.spice.gl == VIR_TRISTATE_BOOL_YES) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("multiple OpenGL displays are not supported "
                             "by QEMU"));
            return -1;
        }
    }

    switch (graphics->type) {
    case VIR_DOMAIN_GRAPHICS_TYPE_SDL:
        if (graphics->data.sdl.gl != VIR_TRISTATE_BOOL_ABSENT) {
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SDL_GL)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("OpenGL for SDL is not supported with this QEMU "
                                 "binary"));
                return -1;
            }
        }
        break;

    case VIR_DOMAIN_GRAPHICS_TYPE_VNC:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_VNC)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("vnc graphics are not supported with this QEMU"));
            return -1;
        }
        break;

    case VIR_DOMAIN_GRAPHICS_TYPE_SPICE:
        if (qemuValidateDomainDeviceDefSPICEGraphics(graphics, driver,
                                                     qemuCaps) < 0)
            return -1;

        break;

    case VIR_DOMAIN_GRAPHICS_TYPE_EGL_HEADLESS:
        if (graphics->data.egl_headless.rendernode &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_EGL_HEADLESS_RENDERNODE)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("This QEMU doesn't support OpenGL rendernode "
                             "with egl-headless graphics type"));
            return -1;
        }

        break;
    case VIR_DOMAIN_GRAPHICS_TYPE_RDP:
    case VIR_DOMAIN_GRAPHICS_TYPE_DESKTOP:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("unsupported graphics type '%s'"),
                       virDomainGraphicsTypeToString(graphics->type));
        return -1;
    case VIR_DOMAIN_GRAPHICS_TYPE_LAST:
    default:
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDefVirtioFSSharedMemory(const virDomainDef *def)
{
    size_t numa_nodes = virDomainNumaGetNodeCount(def->numa);
    size_t i;

    if (numa_nodes == 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("virtiofs requires one or more NUMA nodes"));
        return -1;
    }

    for (i = 0; i < numa_nodes; i++) {
        virDomainMemoryAccess node_access =
            virDomainNumaGetNodeMemoryAccessMode(def->numa, i);

        switch (node_access) {
        case VIR_DOMAIN_MEMORY_ACCESS_DEFAULT:
            if (def->mem.access != VIR_DOMAIN_MEMORY_ACCESS_SHARED) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("virtiofs requires shared memory"));
                return -1;
            }
            break;
        case VIR_DOMAIN_MEMORY_ACCESS_SHARED:
            break;
        case VIR_DOMAIN_MEMORY_ACCESS_PRIVATE:
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs requires shared memory"));
            return -1;

        case VIR_DOMAIN_MEMORY_ACCESS_LAST:
        default:
            virReportEnumRangeError(virDomainMemoryAccess, node_access);
            return -1;

        }
    }
    return 0;
}


static int
qemuValidateDomainDeviceDefFS(virDomainFSDefPtr fs,
                              const virDomainDef *def,
                              virQEMUDriverPtr driver,
                              virQEMUCapsPtr qemuCaps)
{
    if (fs->type != VIR_DOMAIN_FS_TYPE_MOUNT) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("only supports mount filesystem type"));
        return -1;
    }
    if (fs->multidevs != VIR_DOMAIN_FS_MODEL_DEFAULT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_FSDEV_MULTIDEVS))
    {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("multidevs is not supported with this QEMU binary"));
        return -1;
    }

    switch ((virDomainFSDriverType) fs->fsdriver) {
    case VIR_DOMAIN_FS_DRIVER_TYPE_DEFAULT:
    case VIR_DOMAIN_FS_DRIVER_TYPE_PATH:
        break;

    case VIR_DOMAIN_FS_DRIVER_TYPE_HANDLE:
        if (fs->accessmode != VIR_DOMAIN_FS_ACCESSMODE_PASSTHROUGH) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("only supports passthrough accessmode"));
            return -1;
        }
        break;

    case VIR_DOMAIN_FS_DRIVER_TYPE_LOOP:
    case VIR_DOMAIN_FS_DRIVER_TYPE_NBD:
    case VIR_DOMAIN_FS_DRIVER_TYPE_PLOOP:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Filesystem driver type not supported"));
        return -1;

    case VIR_DOMAIN_FS_DRIVER_TYPE_VIRTIOFS:
        if (!driver->privileged) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs is not yet supported in session mode"));
            return -1;
        }
        if (fs->accessmode != VIR_DOMAIN_FS_ACCESSMODE_PASSTHROUGH) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs only supports passthrough accessmode"));
            return -1;
        }
        if (fs->wrpolicy != VIR_DOMAIN_FS_WRPOLICY_DEFAULT) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs does not support wrpolicy"));
            return -1;
        }
        if (fs->model != VIR_DOMAIN_FS_MODEL_DEFAULT) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs does not support model"));
            return -1;
        }
        if (fs->format != VIR_STORAGE_FILE_NONE) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs does not support format"));
            return -1;
        }
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VHOST_USER_FS)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs is not supported with this QEMU binary"));
            return -1;
        }
        if (fs->multidevs != VIR_DOMAIN_FS_MULTIDEVS_DEFAULT) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("virtiofs does not support multidevs"));
            return -1;
        }
        if (qemuValidateDomainDefVirtioFSSharedMemory(def) < 0)
            return -1;
        break;

    case VIR_DOMAIN_FS_DRIVER_TYPE_LAST:
    default:
        virReportEnumRangeError(virDomainFSDriverType, fs->fsdriver);
        return -1;
    }

    if (qemuValidateDomainVirtioOptions(fs->virtio, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuSoundCodecTypeToCaps(int type)
{
    switch (type) {
    case VIR_DOMAIN_SOUND_CODEC_TYPE_DUPLEX:
        return QEMU_CAPS_HDA_DUPLEX;
    case VIR_DOMAIN_SOUND_CODEC_TYPE_MICRO:
        return QEMU_CAPS_HDA_MICRO;
    case VIR_DOMAIN_SOUND_CODEC_TYPE_OUTPUT:
        return QEMU_CAPS_HDA_OUTPUT;
    default:
        return -1;
    }
}


static int
qemuValidateDomainDeviceDefSound(virDomainSoundDefPtr sound,
                                 virQEMUCapsPtr qemuCaps)
{
    size_t i;

    switch ((virDomainSoundModel) sound->model) {
    case VIR_DOMAIN_SOUND_MODEL_USB:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_USB_AUDIO)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("usb-audio controller is not supported "
                             "by this QEMU binary"));
            return -1;
        }
        break;
    case VIR_DOMAIN_SOUND_MODEL_ICH9:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_ICH9_INTEL_HDA)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("The ich9-intel-hda audio controller "
                             "is not supported in this QEMU binary"));
            return -1;
        }
        break;

    case VIR_DOMAIN_SOUND_MODEL_ES1370:
    case VIR_DOMAIN_SOUND_MODEL_AC97:
    case VIR_DOMAIN_SOUND_MODEL_ICH6:
    case VIR_DOMAIN_SOUND_MODEL_SB16:
    case VIR_DOMAIN_SOUND_MODEL_PCSPK:
        break;
    case VIR_DOMAIN_SOUND_MODEL_LAST:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("sound card model '%s' is not supported by qemu"),
                       virDomainSoundModelTypeToString(sound->model));
        return -1;
    }

    if (sound->model == VIR_DOMAIN_SOUND_MODEL_ICH6 ||
        sound->model == VIR_DOMAIN_SOUND_MODEL_ICH9) {
        for (i = 0; i < sound->ncodecs; i++) {
            const char *stype;
            int type, flags;

            type = sound->codecs[i]->type;
            stype = qemuSoundCodecTypeToString(type);
            flags = qemuSoundCodecTypeToCaps(type);

            if (flags == -1 || !virQEMUCapsGet(qemuCaps, flags)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("%s not supported in this QEMU binary"), stype);
                return -1;
            }
        }
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefVsock(const virDomainVsockDef *vsock,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VHOST_VSOCK)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("vsock device is not supported "
                         "with this QEMU binary"));
        return -1;
    }

    if (!qemuDomainCheckCCWS390AddressSupport(def, &vsock->info, qemuCaps,
                                              "vsock"))
        return -1;

    return 0;
}


static int
qemuValidateDomainDeviceDefTPM(virDomainTPMDef *tpm,
                               const virDomainDef *def,
                               virQEMUCapsPtr qemuCaps)
{
    virQEMUCapsFlags flag;

    /* TPM 1.2 and 2 are not compatible, so we choose a specific version here */
    if (tpm->version == VIR_DOMAIN_TPM_VERSION_DEFAULT)
        tpm->version = VIR_DOMAIN_TPM_VERSION_1_2;

    switch (tpm->version) {
    case VIR_DOMAIN_TPM_VERSION_1_2:
        /* TPM 1.2 + CRB do not work */
        if (tpm->type == VIR_DOMAIN_TPM_TYPE_EMULATOR &&
            tpm->model == VIR_DOMAIN_TPM_MODEL_CRB) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Unsupported interface %s for TPM 1.2"),
                           virDomainTPMModelTypeToString(tpm->model));
            return -1;
        }
        break;
    case VIR_DOMAIN_TPM_VERSION_2_0:
    case VIR_DOMAIN_TPM_VERSION_DEFAULT:
    case VIR_DOMAIN_TPM_VERSION_LAST:
        break;
    }

    switch (tpm->type) {
    case VIR_DOMAIN_TPM_TYPE_PASSTHROUGH:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_TPM_PASSTHROUGH))
            goto no_support;
        break;

    case VIR_DOMAIN_TPM_TYPE_EMULATOR:
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_TPM_EMULATOR))
            goto no_support;

        break;
    case VIR_DOMAIN_TPM_TYPE_LAST:
        break;
    }

    switch (tpm->model) {
    case VIR_DOMAIN_TPM_MODEL_TIS:
        flag = QEMU_CAPS_DEVICE_TPM_TIS;
        break;
    case VIR_DOMAIN_TPM_MODEL_CRB:
        flag = QEMU_CAPS_DEVICE_TPM_CRB;
        break;
    case VIR_DOMAIN_TPM_MODEL_SPAPR:
        flag = QEMU_CAPS_DEVICE_TPM_SPAPR;
        break;
    case VIR_DOMAIN_TPM_MODEL_LAST:
    default:
        virReportEnumRangeError(virDomainTPMModel, tpm->model);
        return -1;
    }

    if (!virQEMUCapsGet(qemuCaps, flag)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("The QEMU executable %s does not support TPM "
                         "model %s"),
                       def->emulator,
                       virDomainTPMModelTypeToString(tpm->model));
        return -1;
    }

    return 0;

 no_support:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("The QEMU executable %s does not support TPM "
                     "backend type %s"),
                   def->emulator,
                   virDomainTPMBackendTypeToString(tpm->type));
    return -1;
}


static int
qemuValidateDomainDeviceDefInput(const virDomainInputDef *input,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    const char *baseName;
    int cap;
    int ccwCap;

    if (input->bus == VIR_DOMAIN_INPUT_BUS_PS2 && !ARCH_IS_X86(def->os.arch) &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_I8042)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("%s is not supported by this QEMU binary"),
                       virDomainInputBusTypeToString(input->bus));
        return -1;
    }

    if (input->bus != VIR_DOMAIN_INPUT_BUS_VIRTIO)
        return 0;

    /* Only type=passthrough supports model=virtio-(non-)transitional */
    switch ((virDomainInputModel)input->model) {
    case VIR_DOMAIN_INPUT_MODEL_VIRTIO_TRANSITIONAL:
    case VIR_DOMAIN_INPUT_MODEL_VIRTIO_NON_TRANSITIONAL:
        switch ((virDomainInputType)input->type) {
        case VIR_DOMAIN_INPUT_TYPE_MOUSE:
        case VIR_DOMAIN_INPUT_TYPE_TABLET:
        case VIR_DOMAIN_INPUT_TYPE_KBD:
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("virtio (non-)transitional models are not "
                             "supported for input type=%s"),
                           virDomainInputTypeToString(input->type));
            return -1;
        case VIR_DOMAIN_INPUT_TYPE_PASSTHROUGH:
            break;
        case VIR_DOMAIN_INPUT_TYPE_LAST:
        default:
            virReportEnumRangeError(virDomainInputType,
                                    input->type);
            return -1;
        }
        break;
    case VIR_DOMAIN_INPUT_MODEL_VIRTIO:
    case VIR_DOMAIN_INPUT_MODEL_DEFAULT:
        break;
    case VIR_DOMAIN_INPUT_MODEL_LAST:
    default:
        virReportEnumRangeError(virDomainInputModel,
                                input->model);
        return -1;
    }

    switch ((virDomainInputType)input->type) {
    case VIR_DOMAIN_INPUT_TYPE_MOUSE:
        baseName = "virtio-mouse";
        cap = QEMU_CAPS_VIRTIO_MOUSE;
        ccwCap = QEMU_CAPS_DEVICE_VIRTIO_MOUSE_CCW;
        break;
    case VIR_DOMAIN_INPUT_TYPE_TABLET:
        baseName = "virtio-tablet";
        cap = QEMU_CAPS_VIRTIO_TABLET;
        ccwCap = QEMU_CAPS_DEVICE_VIRTIO_TABLET_CCW;
        break;
    case VIR_DOMAIN_INPUT_TYPE_KBD:
        baseName = "virtio-keyboard";
        cap = QEMU_CAPS_VIRTIO_KEYBOARD;
        ccwCap = QEMU_CAPS_DEVICE_VIRTIO_KEYBOARD_CCW;
        break;
    case VIR_DOMAIN_INPUT_TYPE_PASSTHROUGH:
        baseName = "virtio-input-host";
        cap = QEMU_CAPS_VIRTIO_INPUT_HOST;
        ccwCap = QEMU_CAPS_LAST;
        break;
    case VIR_DOMAIN_INPUT_TYPE_LAST:
    default:
        virReportEnumRangeError(virDomainInputType,
                                input->type);
        return -1;
    }

    if (!virQEMUCapsGet(qemuCaps, cap) ||
        (input->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW &&
         !virQEMUCapsGet(qemuCaps, ccwCap))) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("%s is not supported by this QEMU binary"),
                       baseName);
        return -1;
    }

    if (qemuValidateDomainVirtioOptions(input->virtio, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuValidateDomainDeviceDefMemballoon(const virDomainMemballoonDef *memballoon,
                                      virQEMUCapsPtr qemuCaps)
{
    if (!memballoon ||
        memballoon->model == VIR_DOMAIN_MEMBALLOON_MODEL_NONE) {
        return 0;
    }

    if (memballoon->model != VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO &&
        memballoon->model != VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO_TRANSITIONAL &&
        memballoon->model != VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO_NON_TRANSITIONAL) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Memory balloon device type '%s' is not supported by this version of qemu"),
                       virDomainMemballoonModelTypeToString(memballoon->model));
        return -1;
    }

    if (memballoon->autodeflate != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_BALLOON_AUTODEFLATE)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("deflate-on-oom is not supported by this QEMU binary"));
        return -1;
    }

    if (qemuValidateDomainVirtioOptions(memballoon->virtio, qemuCaps) < 0)
        return -1;

    return 0;
}


static int
qemuValidateDomainDeviceDefIOMMU(const virDomainIOMMUDef *iommu,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    switch (iommu->model) {
    case VIR_DOMAIN_IOMMU_MODEL_INTEL:
        if (!qemuDomainIsQ35(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("IOMMU device: '%s' is only supported with "
                             "Q35 machines"),
                           virDomainIOMMUModelTypeToString(iommu->model));
            return -1;
        }
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_INTEL_IOMMU) &&
            !virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_IOMMU)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("IOMMU device: '%s' is not supported with "
                             "this QEMU binary"),
                           virDomainIOMMUModelTypeToString(iommu->model));
            return -1;
        }
        break;

    case VIR_DOMAIN_IOMMU_MODEL_SMMUV3:
        if (!qemuDomainIsARMVirt(def)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("IOMMU device: '%s' is only supported with "
                             "ARM Virt machines"),
                           virDomainIOMMUModelTypeToString(iommu->model));
            return -1;
        }
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_MACHINE_VIRT_IOMMU)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("IOMMU device: '%s' is not supported with "
                             "this QEMU binary"),
                           virDomainIOMMUModelTypeToString(iommu->model));
            return -1;
        }
        break;

    case VIR_DOMAIN_IOMMU_MODEL_LAST:
    default:
        virReportEnumRangeError(virDomainIOMMUModel, iommu->model);
        return -1;
    }

    /* These capability checks ensure we're not trying to use features
     * of Intel IOMMU that the QEMU binary does not support, but they
     * also make sure we report an error when trying to use features
     * that are not implemented by SMMUv3 */

    if (iommu->intremap != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_INTEL_IOMMU_INTREMAP)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("iommu: interrupt remapping is not supported "
                         "with this QEMU binary"));
        return -1;
    }
    if (iommu->caching_mode != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_INTEL_IOMMU_CACHING_MODE))  {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("iommu: caching mode is not supported "
                         "with this QEMU binary"));
        return -1;
    }
    if (iommu->eim != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_INTEL_IOMMU_EIM))  {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("iommu: eim is not supported "
                         "with this QEMU binary"));
        return -1;
    }
    if (iommu->iotlb != VIR_TRISTATE_SWITCH_ABSENT &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_INTEL_IOMMU_DEVICE_IOTLB)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("iommu: device IOTLB is not supported "
                         "with this QEMU binary"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefNVRAM(virDomainNVRAMDefPtr nvram,
                                 const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps)
{
    if (!nvram)
        return 0;

    if (qemuDomainIsPSeries(def)) {
        if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_NVRAM)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("nvram device is not supported by "
                             "this QEMU binary"));
            return -1;
        }
    } else {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("nvram device is only supported for PPC64"));
        return -1;
    }

    if (!(nvram->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO &&
          nvram->info.addr.spaprvio.has_reg)) {

        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("nvram address type must be spaprvio"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefHub(virDomainHubDefPtr hub,
                               virQEMUCapsPtr qemuCaps)
{
    if (hub->type != VIR_DOMAIN_HUB_TYPE_USB) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("hub type %s not supported"),
                       virDomainHubTypeToString(hub->type));
        return -1;
    }

    if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_USB_HUB)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("usb-hub not supported by QEMU binary"));
        return -1;
    }

    return 0;
}


static int
qemuValidateDomainDeviceDefMemory(virDomainMemoryDefPtr mem,
                                  virQEMUCapsPtr qemuCaps)
{
    if (mem->model == VIR_DOMAIN_MEMORY_MODEL_NVDIMM &&
        !virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_NVDIMM)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("nvdimm isn't supported by this QEMU binary"));
        return -1;
    }

    return 0;
}


int
qemuValidateDomainDeviceDef(const virDomainDeviceDef *dev,
                            const virDomainDef *def,
                            void *opaque)
{
    int ret = 0;
    virQEMUDriverPtr driver = opaque;
    g_autoptr(virQEMUCaps) qemuCaps = NULL;
    g_autoptr(virDomainCaps) domCaps = NULL;

    if (!(qemuCaps = virQEMUCapsCacheLookup(driver->qemuCapsCache,
                                            def->emulator)))
        return -1;

    if (!(domCaps = virQEMUDriverGetDomainCapabilities(driver, qemuCaps,
                                                       def->os.machine,
                                                       def->os.arch,
                                                       def->virtType)))
        return -1;

    if ((ret = qemuValidateDomainDeviceDefAddress(dev, qemuCaps)) < 0)
        return ret;

    if ((ret = virDomainCapsDeviceDefValidate(domCaps, dev, def)) < 0)
        return ret;

    switch ((virDomainDeviceType)dev->type) {
    case VIR_DOMAIN_DEVICE_NET:
        ret = qemuValidateDomainDeviceDefNetwork(dev->data.net, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_CHR:
        ret = qemuValidateDomainChrDef(dev->data.chr, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_SMARTCARD:
        ret = qemuValidateDomainSmartcardDef(dev->data.smartcard, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_RNG:
        ret = qemuValidateDomainRNGDef(dev->data.rng, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_REDIRDEV:
        ret = qemuValidateDomainRedirdevDef(dev->data.redirdev, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_WATCHDOG:
        ret = qemuValidateDomainWatchdogDef(dev->data.watchdog, def);
        break;

    case VIR_DOMAIN_DEVICE_HOSTDEV:
        ret = qemuValidateDomainDeviceDefHostdev(dev->data.hostdev, def,
                                                 qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_VIDEO:
        ret = qemuValidateDomainDeviceDefVideo(dev->data.video, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_DISK:
        ret = qemuValidateDomainDeviceDefDisk(dev->data.disk, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_CONTROLLER:
        ret = qemuValidateDomainDeviceDefController(dev->data.controller, def,
                                                    qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_VSOCK:
        ret = qemuValidateDomainDeviceDefVsock(dev->data.vsock, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_TPM:
        ret = qemuValidateDomainDeviceDefTPM(dev->data.tpm, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_GRAPHICS:
        ret = qemuValidateDomainDeviceDefGraphics(dev->data.graphics, def,
                                                  driver, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_INPUT:
        ret = qemuValidateDomainDeviceDefInput(dev->data.input, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_MEMBALLOON:
        ret = qemuValidateDomainDeviceDefMemballoon(dev->data.memballoon, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_IOMMU:
        ret = qemuValidateDomainDeviceDefIOMMU(dev->data.iommu, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_FS:
        ret = qemuValidateDomainDeviceDefFS(dev->data.fs, def, driver, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_NVRAM:
        ret = qemuValidateDomainDeviceDefNVRAM(dev->data.nvram, def, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_HUB:
        ret = qemuValidateDomainDeviceDefHub(dev->data.hub, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_SOUND:
        ret = qemuValidateDomainDeviceDefSound(dev->data.sound, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_MEMORY:
        ret = qemuValidateDomainDeviceDefMemory(dev->data.memory, qemuCaps);
        break;

    case VIR_DOMAIN_DEVICE_LEASE:
    case VIR_DOMAIN_DEVICE_SHMEM:
    case VIR_DOMAIN_DEVICE_PANIC:
    case VIR_DOMAIN_DEVICE_NONE:
    case VIR_DOMAIN_DEVICE_LAST:
        break;
    }

    return ret;
}
