/*
 * vircgroupv2.c: methods for cgroups v2 backend
 *
 * Copyright (C) 2018 Red Hat, Inc.
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

#ifdef __linux__
# include <mntent.h>
# include <sys/mount.h>
#endif /* __linux__ */

#include "internal.h"

#define __VIR_CGROUP_ALLOW_INCLUDE_PRIV_H__
#include "vircgrouppriv.h"
#undef __VIR_CGROUP_ALLOW_INCLUDE_PRIV_H__

#include "vircgroup.h"
#include "vircgroupbackend.h"
#include "vircgroupv2.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"
#include "virsystemd.h"

VIR_LOG_INIT("util.cgroup");

#define VIR_FROM_THIS VIR_FROM_CGROUP

VIR_ENUM_DECL(virCgroupV2Controller);
VIR_ENUM_IMPL(virCgroupV2Controller, VIR_CGROUP_CONTROLLER_LAST,
              "cpu", "cpuacct", "cpuset", "memory", "devices",
              "freezer", "io", "net_cls", "perf_event", "name=systemd");

#ifdef __linux__

/* We're looking for one 'cgroup2' fs mount which has some
 * controllers enabled. */
static bool
virCgroupV2Available(void)
{
    bool ret = false;
    FILE *mounts = NULL;
    struct mntent entry;
    char buf[CGROUP_MAX_VAL];

    if (!(mounts = fopen("/proc/mounts", "r")))
        return false;

    while (getmntent_r(mounts, &entry, buf, sizeof(buf)) != NULL) {
        if (STREQ(entry.mnt_type, "cgroup2")) {
            ret = true;
            break;
        }
    }

    /* Systemd uses cgroup v2 for process tracking but no controller is
     * available. We should consider this configuration as cgroup v2 is
     * not available. */
    if (ret) {
        int rc;
        VIR_AUTOFREE(char *) contFile = NULL;
        VIR_AUTOFREE(char *) contStr = NULL;

        if (virAsprintf(&contFile, "%s/cgroup.controllers", entry.mnt_dir) < 0)
            return false;

        rc = virFileReadAll(contFile, 1024 * 1024, &contStr);
        if (rc < 0)
            return false;

        if (STREQ(contStr, ""))
            return false;
    }

    VIR_FORCE_FCLOSE(mounts);
    return ret;
}


static bool
virCgroupV2ValidateMachineGroup(virCgroupPtr group,
                                const char *name ATTRIBUTE_UNUSED,
                                const char *drivername,
                                const char *machinename)
{
    VIR_AUTOFREE(char *) partmachinename = NULL;
    VIR_AUTOFREE(char *) scopename = NULL;
    char *tmp;

    if (virAsprintf(&partmachinename, "%s.libvirt-%s", machinename,
                    drivername) < 0) {
        return false;
    }

    if (virCgroupPartitionEscape(&partmachinename) < 0)
        return false;

    if (!(scopename = virSystemdMakeScopeName(machinename, drivername,
                                              false))) {
        return false;
    }

    if (virCgroupPartitionEscape(&scopename) < 0)
        return false;

    if (!(tmp = strrchr(group->unified.placement, '/')))
        return false;
    tmp++;

    if (STRNEQ(tmp, partmachinename) &&
        STRNEQ(tmp, scopename)) {
        VIR_DEBUG("Name '%s' for unified does not match '%s' or '%s'",
                  tmp, partmachinename, scopename);
        return false;
    }

    return true;
}


static int
virCgroupV2CopyMounts(virCgroupPtr group,
                      virCgroupPtr parent)
{
    return VIR_STRDUP(group->unified.mountPoint, parent->unified.mountPoint);
}


static int
virCgroupV2CopyPlacement(virCgroupPtr group,
                         const char *path,
                         virCgroupPtr parent)
{
    if (path[0] == '/') {
        if (VIR_STRDUP(group->unified.placement, path) < 0)
            return -1;
    } else {
        /*
         * parent == "/" + path="" => "/"
         * parent == "/libvirt.service" + path == "" => "/libvirt.service"
         * parent == "/libvirt.service" + path == "foo" => "/libvirt.service/foo"
         */
        if (virAsprintf(&group->unified.placement, "%s%s%s",
                        parent->unified.placement,
                        (STREQ(parent->unified.placement, "/") ||
                         STREQ(path, "") ? "" : "/"),
                        path) < 0)
            return -1;
    }

    return 0;
}


static int
virCgroupV2DetectMounts(virCgroupPtr group,
                        const char *mntType,
                        const char *mntOpts ATTRIBUTE_UNUSED,
                        const char *mntDir)
{
    if (STRNEQ(mntType, "cgroup2"))
        return 0;

    VIR_FREE(group->unified.mountPoint);

    return VIR_STRDUP(group->unified.mountPoint, mntDir);
}


static int
virCgroupV2DetectPlacement(virCgroupPtr group,
                           const char *path,
                           const char *controllers ATTRIBUTE_UNUSED,
                           const char *selfpath)
{
    if (group->unified.placement)
        return 0;

    /*
     * selfpath == "/" + path="" -> "/"
     * selfpath == "/libvirt.service" + path == "" -> "/libvirt.service"
     * selfpath == "/libvirt.service" + path == "foo" -> "/libvirt.service/foo"
     */
    if (virAsprintf(&group->unified.placement,
                    "%s%s%s", selfpath,
                    (STREQ(selfpath, "/") ||
                     STREQ(path, "") ? "" : "/"),
                    path) < 0)
        return -1;

    return 0;
}


static int
virCgroupV2ValidatePlacement(virCgroupPtr group,
                             pid_t pid ATTRIBUTE_UNUSED)
{
    if (!group->unified.placement) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find placement for v2 controller"));
        return -1;
    }

    return 0;
}


static char *
virCgroupV2StealPlacement(virCgroupPtr group)
{
    char *ret;

    VIR_STEAL_PTR(ret, group->unified.placement);

    return ret;
}


static int
virCgroupV2ParseControllersFile(virCgroupPtr group)
{
    int rc;
    VIR_AUTOFREE(char *) contStr = NULL;
    VIR_AUTOFREE(char *) contFile = NULL;
    char **contList = NULL;
    char **tmp;

    if (virAsprintf(&contFile, "%s/cgroup.controllers",
                    group->unified.mountPoint) < 0)
        return -1;

    rc = virFileReadAll(contFile, 1024 * 1024, &contStr);
    if (rc < 0) {
        virReportSystemError(errno, _("Unable to read from '%s'"), contFile);
        return -1;
    }

    virTrimSpaces(contStr, NULL);

    contList = virStringSplit(contStr, " ", 20);
    if (!contList)
        return -1;

    tmp = contList;

    while (*tmp) {
        int type = virCgroupV2ControllerTypeFromString(*tmp);

        if (type >= 0)
            group->unified.controllers |= 1 << type;

        tmp++;
    }

    virStringListFree(contList);

    return 0;
}


static int
virCgroupV2DetectControllers(virCgroupPtr group,
                             int controllers)
{
    size_t i;

    if (virCgroupV2ParseControllersFile(group) < 0)
        return -1;

    group->unified.controllers |= 1 << VIR_CGROUP_CONTROLLER_CPUACCT;

    for (i = 0; i < VIR_CGROUP_CONTROLLER_LAST; i++)
        VIR_DEBUG("Controller '%s' present=%s",
                  virCgroupV2ControllerTypeToString(i),
                  (group->unified.controllers & 1 << i) ? "yes" : "no");

    if (controllers >= 0)
        return controllers & group->unified.controllers;
    else
        return group->unified.controllers;
}


static bool
virCgroupV2HasController(virCgroupPtr group,
                         int controller)
{
    return group->unified.controllers & (1 << controller);
}


static int
virCgroupV2GetAnyController(virCgroupPtr group)
{
    return ffs(group->unified.controllers) - 1;
}


static int
virCgroupV2PathOfController(virCgroupPtr group,
                            int controller,
                            const char *key,
                            char **path)
{
    if (!virCgroupV2HasController(group, controller)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("v2 controller '%s' is not available"),
                       virCgroupV2ControllerTypeToString(controller));
        return -1;
    }

    if (virAsprintf(path, "%s%s/%s",
                    group->unified.mountPoint,
                    group->unified.placement,
                    key ? key : "") < 0)
        return -1;

    return 0;
}


static int
virCgroupV2EnableController(virCgroupPtr parent,
                            int controller)
{
    VIR_AUTOFREE(char *) val = NULL;

    if (virAsprintf(&val, "+%s",
                    virCgroupV2ControllerTypeToString(controller)) < 0) {
        return -1;
    }

    if (virCgroupSetValueStr(parent, controller,
                             "cgroup.subtree_control", val) < 0) {
        return -1;
    }

    return 0;
}


static int
virCgroupV2MakeGroup(virCgroupPtr parent ATTRIBUTE_UNUSED,
                     virCgroupPtr group,
                     bool create,
                     unsigned int flags)
{
    VIR_AUTOFREE(char *) path = NULL;
    int controller;

    VIR_DEBUG("Make group %s", group->path);

    controller = virCgroupV2GetAnyController(group);
    if (virCgroupV2PathOfController(group, controller, "", &path) < 0)
        return -1;

    VIR_DEBUG("Make controller %s", path);

    if (!virFileExists(path)) {
        if (!create || mkdir(path, 0755 || errno == EEXIST) < 0) {
            virReportSystemError(errno,
                                 _("Failed to create v2 cgroup '%s'"),
                                 group->path);
            return -1;
        }
    }

    if (create) {
        if (flags & VIR_CGROUP_THREAD) {
            if (virCgroupSetValueStr(group, VIR_CGROUP_CONTROLLER_CPU,
                                     "cgroup.type", "threaded") < 0) {
                return -1;
            }

            if (virCgroupV2EnableController(parent,
                                            VIR_CGROUP_CONTROLLER_CPU) < 0) {
                return -1;
            }
        } else {
            size_t i;
            for (i = 0; i < VIR_CGROUP_CONTROLLER_LAST; i++) {
                if (!virCgroupV2HasController(parent, i))
                    continue;

                /* Controllers that are implicitly enabled if available. */
                if (i == VIR_CGROUP_CONTROLLER_CPUACCT)
                    continue;

                if (virCgroupV2EnableController(parent, i) < 0)
                    return -1;
            }
        }
    }

    return 0;
}


static int
virCgroupV2Remove(virCgroupPtr group)
{
    VIR_AUTOFREE(char *) grppath = NULL;
    int controller;

    /* Don't delete the root group, if we accidentally
       ended up in it for some reason */
    if (STREQ(group->unified.placement, "/"))
        return 0;

    controller = virCgroupV2GetAnyController(group);
    if (virCgroupV2PathOfController(group, controller, "", &grppath) < 0)
        return 0;

    return virCgroupRemoveRecursively(grppath);
}


static int
virCgroupV2AddTask(virCgroupPtr group,
                   pid_t pid,
                   unsigned int flags)
{
    int controller = virCgroupV2GetAnyController(group);

    if (flags & VIR_CGROUP_TASK_THREAD)
        return virCgroupSetValueI64(group, controller, "cgroup.threads", pid);
    else
        return virCgroupSetValueI64(group, controller, "cgroup.procs", pid);
}


static int
virCgroupV2HasEmptyTasks(virCgroupPtr cgroup,
                         int controller)
{
    int ret = -1;
    VIR_AUTOFREE(char *) content = NULL;

    ret = virCgroupGetValueStr(cgroup, controller, "cgroup.procs", &content);

    if (ret == 0 && content[0] == '\0')
        ret = 1;

    return ret;
}


static int
virCgroupV2BindMount(virCgroupPtr group,
                     const char *oldroot,
                     const char *mountopts)
{
    VIR_AUTOFREE(char *) opts = NULL;
    VIR_AUTOFREE(char *) src = NULL;

    VIR_DEBUG("Mounting cgroups at '%s'", group->unified.mountPoint);

    if (virFileMakePath(group->unified.mountPoint) < 0) {
        virReportSystemError(errno, _("Unable to create directory %s"),
                             group->unified.mountPoint);
        return -1;
    }

    if (virAsprintf(&opts, "mode=755,size=65536%s", mountopts) < 0)
        return -1;

    if (virAsprintf(&src, "%s%s", oldroot, group->unified.mountPoint) < 0)
        return -1;

    if (mount(src, group->unified.mountPoint, "none", MS_BIND, NULL) < 0) {
        virReportSystemError(errno, _("Failed to bind cgroup '%s' on '%s'"),
                             src, group->unified.mountPoint);
        return -1;
    }

    return 0;
}


static int
virCgroupV2SetOwner(virCgroupPtr cgroup,
                    uid_t uid,
                    gid_t gid,
                    int controllers ATTRIBUTE_UNUSED)
{
    int ret = -1;
    VIR_AUTOFREE(char *) base = NULL;
    DIR *dh = NULL;
    int direrr;

    struct dirent *de;

    if (virAsprintf(&base, "%s%s", cgroup->unified.mountPoint,
                    cgroup->unified.placement) < 0) {
        goto cleanup;
    }

    if (virDirOpen(&dh, base) < 0)
        goto cleanup;

    while ((direrr = virDirRead(dh, &de, base)) > 0) {
        VIR_AUTOFREE(char *) entry = NULL;

        if (virAsprintf(&entry, "%s/%s", base, de->d_name) < 0)
            goto cleanup;

        if (chown(entry, uid, gid) < 0) {
            virReportSystemError(errno, _("cannot chown '%s' to (%u, %u)"),
                                 entry, uid, gid);
            goto cleanup;
        }
    }

    if (direrr < 0)
        goto cleanup;

    if (chown(base, uid, gid) < 0) {
        virReportSystemError(errno, _("cannot chown '%s' to (%u, %u)"),
                             base, uid, gid);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_DIR_CLOSE(dh);
    return ret;
}


static int
virCgroupV2SetBlkioWeight(virCgroupPtr group,
                          unsigned int weight)
{
    VIR_AUTOFREE(char *) value = NULL;

    if (virAsprintf(&value, "default %u", weight) < 0)
        return -1;

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.weight",
                                value);
}


static int
virCgroupV2GetBlkioWeight(virCgroupPtr group,
                          unsigned int *weight)
{
    VIR_AUTOFREE(char *) value = NULL;
    char *tmp;

    if (virCgroupGetValueStr(group, VIR_CGROUP_CONTROLLER_BLKIO,
                             "io.weight", &value) < 0) {
        return -1;
    }

    if (!(tmp = strstr(value, "default "))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Cannot find default io weight."));
        return -1;
    }
    tmp += strlen("default ");

    if (virStrToLong_ui(tmp, NULL, 10, weight) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to parse '%s' as an integer"),
                       tmp);
        return -1;
    }

    return 0;
}


static int
virCgroupV2GetBlkioIoServiced(virCgroupPtr group,
                              long long *bytes_read,
                              long long *bytes_write,
                              long long *requests_read,
                              long long *requests_write)
{
    long long stats_val;
    VIR_AUTOFREE(char *) str1 = NULL;
    char *p1;
    size_t i;

    const char *value_names[] = {
        "rbytes=",
        "wbytes=",
        "rios=",
        "wios=",
    };
    long long *value_ptrs[] = {
        bytes_read,
        bytes_write,
        requests_read,
        requests_write
    };

    *bytes_read = 0;
    *bytes_write = 0;
    *requests_read = 0;
    *requests_write = 0;

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_BLKIO,
                             "io.stat", &str1) < 0) {
        return -1;
    }

    /* sum up all entries of the same kind, from all devices */
    for (i = 0; i < ARRAY_CARDINALITY(value_names); i++) {
        p1 = str1;

        while ((p1 = strstr(p1, value_names[i]))) {
            p1 += strlen(value_names[i]);
            if (virStrToLong_ll(p1, &p1, 10, &stats_val) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Cannot parse byte %sstat '%s'"),
                               value_names[i],
                               p1);
                return -1;
            }

            if (stats_val < 0 ||
                (stats_val > 0 && *value_ptrs[i] > (LLONG_MAX - stats_val))) {
                virReportError(VIR_ERR_OVERFLOW,
                               _("Sum of byte %sstat overflows"),
                               value_names[i]);
                return -1;
            }
            *value_ptrs[i] += stats_val;
        }
    }

    return 0;
}


static int
virCgroupV2GetBlkioIoDeviceServiced(virCgroupPtr group,
                                    const char *path,
                                    long long *bytes_read,
                                    long long *bytes_write,
                                    long long *requests_read,
                                    long long *requests_write)
{
    VIR_AUTOFREE(char *) str1 = NULL;
    VIR_AUTOFREE(char *) str2 = NULL;
    char *p1;
    size_t i;

    const char *value_names[] = {
        "rbytes=",
        "wbytes=",
        "rios=",
        "wios=",
    };
    long long *value_ptrs[] = {
        bytes_read,
        bytes_write,
        requests_read,
        requests_write
    };

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_BLKIO,
                             "io.stat", &str1) < 0) {
        return -1;
    }

    if (!(str2 = virCgroupGetBlockDevString(path)))
        return -1;

    if (!(p1 = strstr(str1, str2))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Cannot find byte stats for block device '%s'"),
                       str2);
        return -1;
    }

    for (i = 0; i < ARRAY_CARDINALITY(value_names); i++) {
        if (!(p1 = strstr(p1, value_names[i]))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Cannot find byte %sstats for block device '%s'"),
                           value_names[i], str2);
            return -1;
        }

        if (virStrToLong_ll(p1 + strlen(value_names[i]), &p1,
                            10, value_ptrs[i]) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Cannot parse %sstat '%s'"),
                           value_names[i], p1 + strlen(value_names[i]));
            return -1;
        }
    }

    return 0;
}


static int
virCgroupV2SetBlkioDeviceWeight(virCgroupPtr group,
                                const char *path,
                                unsigned int weight)
{
    VIR_AUTOFREE(char *) str = NULL;
    VIR_AUTOFREE(char *) blkstr = NULL;

    if (!(blkstr = virCgroupGetBlockDevString(path)))
        return -1;

    if (virAsprintf(&str, "%s%d", blkstr, weight) < 0)
        return -1;

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.weight",
                                str);
}


static int
virCgroupV2GetBlkioDeviceWeight(virCgroupPtr group,
                                const char *path,
                                unsigned int *weight)
{
    VIR_AUTOFREE(char *) str = NULL;

    if (virCgroupGetValueForBlkDev(group,
                                   VIR_CGROUP_CONTROLLER_BLKIO,
                                   "io.weight",
                                   path,
                                   &str) < 0) {
        return -1;
    }

    if (!str) {
        *weight = 0;
    } else if (virStrToLong_ui(str, NULL, 10, weight) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to parse '%s' as an integer"),
                       str);
        return -1;
    }

    return 0;
}


static int
virCgroupV2SetBlkioDeviceReadIops(virCgroupPtr group,
                                  const char *path,
                                  unsigned int riops)
{
    VIR_AUTOFREE(char *) str = NULL;
    VIR_AUTOFREE(char *) blkstr = NULL;

    if (!(blkstr = virCgroupGetBlockDevString(path)))
        return -1;

    if (riops == 0) {
        if (virAsprintf(&str, "%sriops=max", blkstr) < 0)
            return -1;
    } else {
        if (virAsprintf(&str, "%sriops=%u", blkstr, riops) < 0)
            return -1;
    }

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.max",
                                str);
}


static int
virCgroupV2GetBlkioDeviceReadIops(virCgroupPtr group,
                                  const char *path,
                                  unsigned int *riops)
{
    VIR_AUTOFREE(char *) str = NULL;
    char *tmp;

    if (virCgroupGetValueForBlkDev(group,
                                   VIR_CGROUP_CONTROLLER_BLKIO,
                                   "io.max",
                                   path,
                                   &str) < 0) {
        return -1;
    }

    if (!str) {
        *riops = 0;
    } else {
        tmp = strstr(str, "riops=");
        tmp += strlen("riops=");

        if (STREQLEN(tmp, "max", 3)) {
            *riops = 0;
        } else if (virStrToLong_ui(tmp, NULL, 10, riops) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse '%s' as an integer"),
                           str);
            return -1;
        }
    }

    return 0;
}


static int
virCgroupV2SetBlkioDeviceWriteIops(virCgroupPtr group,
                                   const char *path,
                                   unsigned int wiops)
{
    VIR_AUTOFREE(char *) str = NULL;
    VIR_AUTOFREE(char *) blkstr = NULL;

    if (!(blkstr = virCgroupGetBlockDevString(path)))
        return -1;

    if (wiops == 0) {
        if (virAsprintf(&str, "%swiops=max", blkstr) < 0)
            return -1;
    } else {
        if (virAsprintf(&str, "%swiops=%u", blkstr, wiops) < 0)
            return -1;
    }

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.max",
                                str);
}


static int
virCgroupV2GetBlkioDeviceWriteIops(virCgroupPtr group,
                                   const char *path,
                                   unsigned int *wiops)
{
    VIR_AUTOFREE(char *) str = NULL;
    char *tmp;

    if (virCgroupGetValueForBlkDev(group,
                                   VIR_CGROUP_CONTROLLER_BLKIO,
                                   "io.max",
                                   path,
                                   &str) < 0) {
        return -1;
    }

    if (!str) {
        *wiops = 0;
    } else {
        tmp = strstr(str, "wiops=");
        tmp += strlen("wiops=");

        if (STREQLEN(tmp, "max", 3)) {
            *wiops = 0;
        } else if (virStrToLong_ui(tmp, NULL, 10, wiops) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse '%s' as an integer"),
                           str);
            return -1;
        }
    }

    return 0;
}


static int
virCgroupV2SetBlkioDeviceReadBps(virCgroupPtr group,
                                 const char *path,
                                 unsigned long long rbps)
{
    VIR_AUTOFREE(char *) str = NULL;
    VIR_AUTOFREE(char *) blkstr = NULL;

    if (!(blkstr = virCgroupGetBlockDevString(path)))
        return -1;

    if (rbps == 0) {
        if (virAsprintf(&str, "%srbps=max", blkstr) < 0)
            return -1;
    } else {
        if (virAsprintf(&str, "%srbps=%llu", blkstr, rbps) < 0)
            return -1;
    }

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.max",
                                str);
}


static int
virCgroupV2GetBlkioDeviceReadBps(virCgroupPtr group,
                                 const char *path,
                                 unsigned long long *rbps)
{
    VIR_AUTOFREE(char *) str = NULL;
    char *tmp;

    if (virCgroupGetValueForBlkDev(group,
                                   VIR_CGROUP_CONTROLLER_BLKIO,
                                   "io.max",
                                   path,
                                   &str) < 0) {
        return -1;
    }

    if (!str) {
        *rbps = 0;
    } else {
        tmp = strstr(str, "rbps=");
        tmp += strlen("rbps=");

        if (STREQLEN(tmp, "max", 3)) {
            *rbps = 0;
        } else if (virStrToLong_ull(tmp, NULL, 10, rbps) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse '%s' as an integer"),
                           str);
            return -1;
        }
    }

    return 0;
}


static int
virCgroupV2SetBlkioDeviceWriteBps(virCgroupPtr group,
                                  const char *path,
                                  unsigned long long wbps)
{
    VIR_AUTOFREE(char *) str = NULL;
    VIR_AUTOFREE(char *) blkstr = NULL;

    if (!(blkstr = virCgroupGetBlockDevString(path)))
        return -1;

    if (wbps == 0) {
        if (virAsprintf(&str, "%swbps=max", blkstr) < 0)
            return -1;
    } else {
        if (virAsprintf(&str, "%swbps=%llu", blkstr, wbps) < 0)
            return -1;
    }

    return virCgroupSetValueStr(group,
                                VIR_CGROUP_CONTROLLER_BLKIO,
                                "io.max",
                                str);
}


static int
virCgroupV2GetBlkioDeviceWriteBps(virCgroupPtr group,
                                  const char *path,
                                  unsigned long long *wbps)
{
    VIR_AUTOFREE(char *) str = NULL;
    char *tmp;

    if (virCgroupGetValueForBlkDev(group,
                                   VIR_CGROUP_CONTROLLER_BLKIO,
                                   "io.max",
                                   path,
                                   &str) < 0) {
        return -1;
    }

    if (!str) {
        *wbps = 0;
    } else {
        tmp = strstr(str, "wbps=");
        tmp += strlen("wbps=");

        if (STREQLEN(tmp, "max", 3)) {
            *wbps = 0;
        } else if (virStrToLong_ull(tmp, NULL, 10, wbps) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse '%s' as an integer"),
                           str);
            return -1;
        }
    }

    return 0;
}


static int
virCgroupV2SetMemory(virCgroupPtr group,
                     unsigned long long kb)
{
    unsigned long long maxkb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    if (kb > maxkb) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("Memory '%llu' must be less than %llu"),
                       kb, maxkb);
        return -1;
    }

    if (kb == maxkb) {
        return virCgroupSetValueStr(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.max",
                                    "max");
    } else {
        return virCgroupSetValueU64(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.max",
                                    kb << 10);
    }
}


static int
virCgroupV2GetMemoryStat(virCgroupPtr group,
                         unsigned long long *cache,
                         unsigned long long *activeAnon,
                         unsigned long long *inactiveAnon,
                         unsigned long long *activeFile,
                         unsigned long long *inactiveFile,
                         unsigned long long *unevictable)
{
    VIR_AUTOFREE(char *) stat = NULL;
    char *line = NULL;
    unsigned long long cacheVal = 0;
    unsigned long long activeAnonVal = 0;
    unsigned long long inactiveAnonVal = 0;
    unsigned long long activeFileVal = 0;
    unsigned long long inactiveFileVal = 0;
    unsigned long long unevictableVal = 0;

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_MEMORY,
                             "memory.stat",
                             &stat) < 0) {
        return -1;
    }

    line = stat;

    while (line) {
        char *newLine = strchr(line, '\n');
        char *valueStr = strchr(line, ' ');
        unsigned long long value;

        if (newLine)
            *newLine = '\0';

        if (!valueStr) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot parse 'memory.stat' cgroup file."));
            return -1;
        }
        *valueStr = '\0';

        if (virStrToLong_ull(valueStr + 1, NULL, 10, &value) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unable to parse '%s' as an integer"),
                           valueStr + 1);
            return -1;
        }

        if (STREQ(line, "file"))
            cacheVal = value >> 10;
        else if (STREQ(line, "active_anon"))
            activeAnonVal = value >> 10;
        else if (STREQ(line, "inactive_anon"))
            inactiveAnonVal = value >> 10;
        else if (STREQ(line, "active_file"))
            activeFileVal = value >> 10;
        else if (STREQ(line, "inactive_file"))
            inactiveFileVal = value >> 10;
        else if (STREQ(line, "unevictable"))
            unevictableVal = value >> 10;
    }

    *cache = cacheVal;
    *activeAnon = activeAnonVal;
    *inactiveAnon = inactiveAnonVal;
    *activeFile = activeFileVal;
    *inactiveFile = inactiveFileVal;
    *unevictable = unevictableVal;

    return 0;
}


static int
virCgroupV2GetMemoryUsage(virCgroupPtr group,
                          unsigned long *kb)
{
    long long unsigned int usage_in_bytes;
    int ret = virCgroupGetValueU64(group,
                                   VIR_CGROUP_CONTROLLER_MEMORY,
                                   "memory.current", &usage_in_bytes);
    if (ret == 0)
        *kb = (unsigned long) usage_in_bytes >> 10;
    return ret;
}


static int
virCgroupV2SetMemoryHardLimit(virCgroupPtr group,
                              unsigned long long kb)
{
    return virCgroupV2SetMemory(group, kb);
}


static int
virCgroupV2GetMemoryHardLimit(virCgroupPtr group,
                              unsigned long long *kb)
{
    VIR_AUTOFREE(char *) value = NULL;
    unsigned long long max;

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_MEMORY,
                             "memory.max", &value) < 0) {
        return -1;
    }

    if (STREQ(value, "max")) {
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;
        return 0;
    }

    if (virStrToLong_ull(value, NULL, 10, &max) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to parse value '%s' as number."),
                       value);
        return -1;
    }

    *kb = max >> 10;
    if (*kb >= VIR_DOMAIN_MEMORY_PARAM_UNLIMITED)
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    return 0;
}


static int
virCgroupV2SetMemorySoftLimit(virCgroupPtr group,
                              unsigned long long kb)
{
    unsigned long long maxkb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    if (kb > maxkb) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("Memory '%llu' must be less than %llu"),
                       kb, maxkb);
        return -1;
    }

    if (kb == maxkb) {
        return virCgroupSetValueStr(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.high",
                                    "max");
    } else {
        return virCgroupSetValueU64(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.high",
                                    kb << 10);
    }
}


static int
virCgroupV2GetMemorySoftLimit(virCgroupPtr group,
                              unsigned long long *kb)
{
    VIR_AUTOFREE(char *) value = NULL;
    unsigned long long high;

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_MEMORY,
                             "memory.high", &value) < 0)
        return -1;

    if (STREQ(value, "max")) {
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;
        return 0;
    }

    if (virStrToLong_ull(value, NULL, 10, &high) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to parse value '%s' as number."),
                       value);
        return -1;
    }

    *kb = high >> 10;
    if (*kb >= VIR_DOMAIN_MEMORY_PARAM_UNLIMITED)
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    return 0;
}


static int
virCgroupV2SetMemSwapHardLimit(virCgroupPtr group,
                               unsigned long long kb)
{
    unsigned long long maxkb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    if (kb > maxkb) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("Memory '%llu' must be less than %llu"),
                       kb, maxkb);
        return -1;
    }

    if (kb == maxkb) {
        return virCgroupSetValueStr(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.swap.max",
                                    "max");
    } else {
        return virCgroupSetValueU64(group,
                                    VIR_CGROUP_CONTROLLER_MEMORY,
                                    "memory.swap.max",
                                    kb << 10);
    }
}


static int
virCgroupV2GetMemSwapHardLimit(virCgroupPtr group,
                               unsigned long long *kb)
{
    VIR_AUTOFREE(char *) value = NULL;
    unsigned long long max;

    if (virCgroupGetValueStr(group,
                             VIR_CGROUP_CONTROLLER_MEMORY,
                             "memory.swap.max", &value) < 0) {
        return -1;
    }

    if (STREQ(value, "max")) {
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;
        return 0;
    }

    if (virStrToLong_ull(value, NULL, 10, &max) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to parse value '%s' as number."),
                       value);
        return -1;
    }

    *kb = max >> 10;
    if (*kb >= VIR_DOMAIN_MEMORY_PARAM_UNLIMITED)
        *kb = VIR_DOMAIN_MEMORY_PARAM_UNLIMITED;

    return 0;
}


static int
virCgroupV2GetMemSwapUsage(virCgroupPtr group,
                           unsigned long long *kb)
{
    long long unsigned int usage_in_bytes;
    int ret;
    ret = virCgroupGetValueU64(group,
                               VIR_CGROUP_CONTROLLER_MEMORY,
                               "memory.swap.current", &usage_in_bytes);
    if (ret == 0)
        *kb = (unsigned long) usage_in_bytes >> 10;
    return ret;
}


static int
virCgroupV2SetCpuShares(virCgroupPtr group,
                        unsigned long long shares)
{
    return virCgroupSetValueU64(group,
                                VIR_CGROUP_CONTROLLER_CPU,
                                "cpu.weight", shares);
}


static int
virCgroupV2GetCpuShares(virCgroupPtr group,
                        unsigned long long *shares)
{
    return virCgroupGetValueU64(group,
                                VIR_CGROUP_CONTROLLER_CPU,
                                "cpu.weight", shares);
}


virCgroupBackend virCgroupV2Backend = {
    .type = VIR_CGROUP_BACKEND_TYPE_V2,

    .available = virCgroupV2Available,
    .validateMachineGroup = virCgroupV2ValidateMachineGroup,
    .copyMounts = virCgroupV2CopyMounts,
    .copyPlacement = virCgroupV2CopyPlacement,
    .detectMounts = virCgroupV2DetectMounts,
    .detectPlacement = virCgroupV2DetectPlacement,
    .validatePlacement = virCgroupV2ValidatePlacement,
    .stealPlacement = virCgroupV2StealPlacement,
    .detectControllers = virCgroupV2DetectControllers,
    .hasController = virCgroupV2HasController,
    .getAnyController = virCgroupV2GetAnyController,
    .pathOfController = virCgroupV2PathOfController,
    .makeGroup = virCgroupV2MakeGroup,
    .remove = virCgroupV2Remove,
    .addTask = virCgroupV2AddTask,
    .hasEmptyTasks = virCgroupV2HasEmptyTasks,
    .bindMount = virCgroupV2BindMount,
    .setOwner = virCgroupV2SetOwner,

    .setBlkioWeight = virCgroupV2SetBlkioWeight,
    .getBlkioWeight = virCgroupV2GetBlkioWeight,
    .getBlkioIoServiced = virCgroupV2GetBlkioIoServiced,
    .getBlkioIoDeviceServiced = virCgroupV2GetBlkioIoDeviceServiced,
    .setBlkioDeviceWeight = virCgroupV2SetBlkioDeviceWeight,
    .getBlkioDeviceWeight = virCgroupV2GetBlkioDeviceWeight,
    .setBlkioDeviceReadIops = virCgroupV2SetBlkioDeviceReadIops,
    .getBlkioDeviceReadIops = virCgroupV2GetBlkioDeviceReadIops,
    .setBlkioDeviceWriteIops = virCgroupV2SetBlkioDeviceWriteIops,
    .getBlkioDeviceWriteIops = virCgroupV2GetBlkioDeviceWriteIops,
    .setBlkioDeviceReadBps = virCgroupV2SetBlkioDeviceReadBps,
    .getBlkioDeviceReadBps = virCgroupV2GetBlkioDeviceReadBps,
    .setBlkioDeviceWriteBps = virCgroupV2SetBlkioDeviceWriteBps,
    .getBlkioDeviceWriteBps = virCgroupV2GetBlkioDeviceWriteBps,

    .setMemory = virCgroupV2SetMemory,
    .getMemoryStat = virCgroupV2GetMemoryStat,
    .getMemoryUsage = virCgroupV2GetMemoryUsage,
    .setMemoryHardLimit = virCgroupV2SetMemoryHardLimit,
    .getMemoryHardLimit = virCgroupV2GetMemoryHardLimit,
    .setMemorySoftLimit = virCgroupV2SetMemorySoftLimit,
    .getMemorySoftLimit = virCgroupV2GetMemorySoftLimit,
    .setMemSwapHardLimit = virCgroupV2SetMemSwapHardLimit,
    .getMemSwapHardLimit = virCgroupV2GetMemSwapHardLimit,
    .getMemSwapUsage = virCgroupV2GetMemSwapUsage,

    .setCpuShares = virCgroupV2SetCpuShares,
    .getCpuShares = virCgroupV2GetCpuShares,
};


void
virCgroupV2Register(void)
{
    virCgroupBackendRegister(&virCgroupV2Backend);
}

#else /* !__linux__ */

void
virCgroupV2Register(void)
{
    VIR_INFO("Control groups not supported on this platform");
}

#endif /* !__linux__ */
