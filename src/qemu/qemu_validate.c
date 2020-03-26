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
#include "qemu_domain.h"
#include "virutil.h"

#define VIR_FROM_THIS VIR_FROM_QEMU


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


int
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


int
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


int
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


int
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


int
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


int
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


int
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


int
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
