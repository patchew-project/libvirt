/*
 * vircgroupv2devices.c: methods for cgroups v2 BPF devices
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

#if HAVE_DECL_BPF_CGROUP_DEVICE
# include <fcntl.h>
# include <linux/bpf.h>
# include <sys/stat.h>
# include <sys/syscall.h>
# include <sys/types.h>
#endif /* !HAVE_DECL_BPF_CGROUP_DEVICE */

#include "internal.h"

#define LIBVIRT_VIRCGROUPPRIV_H_ALLOW
#include "vircgrouppriv.h"

#include "virbpf.h"
#include "vircgroup.h"
#include "vircgroupv2devices.h"
#include "virfile.h"
#include "virlog.h"

VIR_LOG_INIT("util.cgroup");

#define VIR_FROM_THIS VIR_FROM_CGROUP

#if HAVE_DECL_BPF_CGROUP_DEVICE
bool
virCgroupV2DevicesAvailable(virCgroupPtr group)
{
    bool ret = false;
    int cgroupfd = -1;
    unsigned int progCnt = 0;

    cgroupfd = open(group->unified.mountPoint, O_RDONLY);
    if (cgroupfd < 0) {
        VIR_DEBUG("failed to open cgroup '%s'", group->unified.mountPoint);
        goto cleanup;
    }

    if (virBPFQueryProg(cgroupfd, 0, BPF_CGROUP_DEVICE, &progCnt, NULL) < 0) {
        VIR_DEBUG("failed to query cgroup progs");
        goto cleanup;
    }

    ret = true;
 cleanup:
    VIR_FORCE_CLOSE(cgroupfd);
    return ret;
}


static int
virCgroupV2DevicesLoadProg(int mapfd)
{
# define VIR_CGROUP_BPF_LOOKUP \
    /* prepare key param on stack */ \
    VIR_BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -8), \
    VIR_BPF_MOV64_REG(BPF_REG_2, BPF_REG_10), \
    VIR_BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8), \
    /* lookup key (major << 32) | minor in map */ \
    VIR_BPF_LD_MAP_FD(BPF_REG_1, mapfd), \
    VIR_BPF_CALL_INSN(BPF_FUNC_map_lookup_elem)

# define VIR_CGROUP_BPF_CHECK_PERM \
    /* if no key skip perm check */ \
    VIR_BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6), \
    /* get perms from map */ \
    VIR_BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0), \
    /* get perms from ctx */ \
    VIR_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6, 0), \
    /* (map perms) & (ctx perms) */ \
    VIR_BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2), \
    /* if (map perms) & (ctx perms) == (ctx perms) exit, otherwise continue */ \
    VIR_BPF_JMP_REG(BPF_JNE, BPF_REG_1, BPF_REG_2, 2), \
    /* set ret 1 and exit */ \
    VIR_BPF_MOV64_IMM(BPF_REG_0, 1), \
    VIR_BPF_EXIT_INSN()

    struct bpf_insn prog[] = {
        /* save ctx, argument passed to BPF program */
        VIR_BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

        /* get major from ctx and shift << 32 */
        VIR_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6, 4),
        VIR_BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 32),
        /* get minor from ctx and | to shifted major */
        VIR_BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_6, 8),
        VIR_BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_3),
        /* lookup ((major << 32) | minor) in map and check perms */
        VIR_CGROUP_BPF_LOOKUP,
        VIR_CGROUP_BPF_CHECK_PERM,

        /* get major from ctx and shift << 32 */
        VIR_BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6, 4),
        VIR_BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 32),
        /* use -1 as minor and | to shifted major */
        VIR_BPF_MOV32_IMM(BPF_REG_3, -1),
        VIR_BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_3),
        /* lookup ((major << 32) | -1) in map and check perms */
        VIR_CGROUP_BPF_LOOKUP,
        VIR_CGROUP_BPF_CHECK_PERM,

        /* use -1 as major and shift << 32 */
        VIR_BPF_MOV32_IMM(BPF_REG_2, -1),
        VIR_BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 32),
        /* get minor from ctx and | to shifted major */
        VIR_BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_6, 8),
        VIR_BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_3),
        /* lookup ((-1 << 32) | minor) in map and check perms */
        VIR_CGROUP_BPF_LOOKUP,
        VIR_CGROUP_BPF_CHECK_PERM,

        /* use -1 as key which means major = -1 and minor = -1 */
        VIR_BPF_MOV64_IMM(BPF_REG_2, -1),
        /* lookup -1 in map and check perms*/
        VIR_CGROUP_BPF_LOOKUP,
        VIR_CGROUP_BPF_CHECK_PERM,

        /* no key was found, exit with 0 */
        VIR_BPF_MOV64_IMM(BPF_REG_0, 0),
        VIR_BPF_EXIT_INSN(),
    };

    return virBPFLoadProg(prog, BPF_PROG_TYPE_CGROUP_DEVICE, ARRAY_CARDINALITY(prog));
}


int
virCgroupV2DevicesAttachProg(virCgroupPtr group,
                             int mapfd,
                             size_t max)
{
    int ret = -1;
    int progfd = -1;
    int cgroupfd = -1;
    VIR_AUTOFREE(char *) path = NULL;

    if (virCgroupPathOfController(group, VIR_CGROUP_CONTROLLER_DEVICES,
                                  NULL, &path) < 0) {
        goto cleanup;
    }

    progfd = virCgroupV2DevicesLoadProg(mapfd);
    if (progfd < 0) {
        virReportSystemError(errno, "%s", _("failed to load cgroup BPF prog"));
        goto cleanup;
    }

    cgroupfd = open(path, O_RDONLY);
    if (cgroupfd < 0) {
        virReportSystemError(errno, _("unable to open '%s'"), path);
        goto cleanup;
    }

    if (virBPFAttachProg(progfd, cgroupfd, BPF_CGROUP_DEVICE) < 0) {
        virReportSystemError(errno, "%s", _("failed to attach cgroup BPF prog"));
        goto cleanup;
    }

    if (group->unified.devices.progfd > 0) {
        VIR_DEBUG("Closing existing program that was replaced by new one.");
        VIR_FORCE_CLOSE(group->unified.devices.progfd);
    }

    group->unified.devices.progfd = progfd;
    group->unified.devices.mapfd = mapfd;
    group->unified.devices.max = max;
    progfd = -1;
    mapfd = -1;

    ret = 0;
 cleanup:
    VIR_FORCE_CLOSE(cgroupfd);
    VIR_FORCE_CLOSE(progfd);
    VIR_FORCE_CLOSE(mapfd);
    return ret;
}
#else /* !HAVE_DECL_BPF_CGROUP_DEVICE */
bool
virCgroupV2DevicesAvailable(virCgroupPtr group ATTRIBUTE_UNUSED)
{
    return false;
}


int
virCgroupV2DevicesAttachProg(virCgroupPtr group ATTRIBUTE_UNUSED,
                             int mapfd ATTRIBUTE_UNUSED,
                             size_t max ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("cgroups v2 BPF devices not supported "
                           "with this kernel"));
    return -1;
}
#endif /* !HAVE_DECL_BPF_CGROUP_DEVICE */
