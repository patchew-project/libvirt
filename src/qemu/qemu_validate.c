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
#include "qemu_domain.h"
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
qemuValidateDomainDef(const virDomainDef *def, void *opaque)
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


int
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


int
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


int
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


int
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


int
qemuValidateDomainRNGDef(const virDomainRNGDef *def,
                         virQEMUCapsPtr qemuCaps G_GNUC_UNUSED)
{
    if (def->backend == VIR_DOMAIN_RNG_BACKEND_EGD &&
        qemuValidateDomainChrSourceDef(def->source.chardev, qemuCaps) < 0)
        return -1;

    return 0;
}


int
qemuValidateDomainRedirdevDef(const virDomainRedirdevDef *def,
                              virQEMUCapsPtr qemuCaps)
{
    if (qemuValidateDomainChrSourceDef(def->source, qemuCaps) < 0)
        return -1;

    return 0;
}


int
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


int
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


int
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

    return 0;
}
