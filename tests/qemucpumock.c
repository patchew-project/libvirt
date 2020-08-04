/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>


#include "conf/cpu_conf.h"
#include "cpu/cpu.h"
#include "qemu/qemu_capabilities.h"
#define LIBVIRT_QEMU_CAPSPRIV_H_ALLOW
#include "qemu/qemu_capspriv.h"
#include "testutilshostcpus.h"
#include "virarch.h"


virCPUDefPtr
virQEMUCapsProbeHostCPU(virArch hostArch G_GNUC_UNUSED,
                        virDomainCapsCPUModelsPtr models G_GNUC_UNUSED)
{
    const char *model = getenv("VIR_TEST_MOCK_FAKE_HOST_CPU");

    return testUtilsHostCpusGetDefForModel(model);
}
