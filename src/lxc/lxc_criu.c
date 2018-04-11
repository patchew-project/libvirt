/*
 * Copyright (C) 2016 Katerina Koukiou
 *
 * lxc_criu.c: Helper functions for checkpoint/restore of linux containers
 *
 * Authors:
 *  Katerina Koukiou <k.koukiou@gmail.com>
 *  Radostin Stoyanov <r.stoyanov.14@aberdeen.ac.uk>
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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include "virobject.h"
#include "virerror.h"
#include "virlog.h"
#include "virfile.h"
#include "vircommand.h"
#include "virstring.h"
#include "viralloc.h"

#include "lxc_domain.h"
#include "lxc_driver.h"
#include "lxc_criu.h"

#define VIR_FROM_THIS VIR_FROM_LXC

VIR_LOG_INIT("lxc.lxc_criu");

#ifdef CRIU

int lxcCriuDump(virLXCDriverPtr driver ATTRIBUTE_UNUSED,
                virDomainObjPtr vm,
                const char *checkpointdir)
{
    int fd;
    int ret = -1;
    pid_t initpid;
    virCommandPtr cmd;
    struct stat sb;
    char *path = NULL;
    char *tty_info_path = NULL;
    char *ttyinfo = NULL;
    int status;

    initpid = ((virLXCDomainObjPrivatePtr) vm->privateData)->initpid;

    if (virFileMakePath(checkpointdir) < 0) {
        virReportSystemError(errno, _("Failed to mkdir %s"), checkpointdir);
        return -1;
    }

    fd = open(checkpointdir, O_DIRECTORY);
    if (fd < 0) {
        virReportSystemError(errno,
                             _("Failed to open directory %s"), checkpointdir);
        return -1;
    }

    cmd = virCommandNew(CRIU);
    virCommandAddArg(cmd, "dump");
    virCommandAddArg(cmd, "--tree");
    virCommandAddArgFormat(cmd, "%d", initpid);
    virCommandAddArgList(cmd,
        "--images-dir", checkpointdir,
        "--tcp-established",
        "--log-file", "dump.log",
        "-v4",
        "--file-locks",
        "--link-remap",
        "--force-irmap",
        "--manage-cgroups=full",
        "--enable-fs", "hugetlbfs",
        "--enable-fs", "tracefs",
        "--external", "mnt[]{:ms}",
        "--external", "mnt[/proc/meminfo]:fuse",
        "--external", "mnt[/dev/console]:console",
        "--external", "mnt[/dev/tty1]:tty1",
        NULL
    );

    /* The master pair of the /dev/pts device lives outside from what is dumped
     * inside the libvirt-lxc process. Add the slave pair as an external tty
     * otherwise criu will fail.
     */
    if (virAsprintf(&path, "/proc/%d/root/dev/pts/0", initpid) < 0)
        goto cleanup;

    if (stat(path, &sb) < 0) {
        virReportSystemError(errno, _("Unable to stat %s"), path);
        goto cleanup;
    }

    if (virAsprintf(&tty_info_path, "%s/tty.info", checkpointdir) < 0)
        goto cleanup;

    if (virAsprintf(&ttyinfo, "tty[%llx:%llx]",
                    (long long unsigned) sb.st_rdev,
                    (long long unsigned) sb.st_dev) < 0)
        goto cleanup;

    if (virFileWriteStr(tty_info_path, ttyinfo, 0600) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to write tty info to %s"), tty_info_path);
        goto cleanup;
    }

    virCommandAddArg(cmd, "--external");
    virCommandAddArgFormat(cmd, "tty[%llx:%llx]",
                          (long long unsigned) sb.st_rdev,
                          (long long unsigned) sb.st_dev);

    virCommandAddEnvString(cmd, "PATH=/bin:/sbin");

    VIR_DEBUG("About to checkpoint domain %s (pid = %d)",
              vm->def->name, initpid);
    virCommandRawStatus(cmd);
    if (virCommandRun(cmd, &status) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FORCE_CLOSE(fd);
    VIR_FREE(path);
    VIR_FREE(tty_info_path);
    VIR_FREE(ttyinfo);
    virCommandFree(cmd);

    return (ret < 0) ? ret : status;
}


int lxcCriuRestore(virDomainDefPtr def, int restorefd,
                   int ttyfd)
{
    int ret = -1;
    virCommandPtr cmd;
    char *ttyinfo = NULL;
    char *inheritfd = NULL;
    char *tty_info_path = NULL;
    char *checkpointfd = NULL;
    char *checkpointdir = NULL;
    virDomainFSDefPtr root;
    gid_t *groups = NULL;
    int ngroups;

    cmd = virCommandNew(CRIU);
    virCommandAddArg(cmd, "restore");

    if (virAsprintf(&checkpointfd, "/proc/self/fd/%d", restorefd) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Failed to write checkpoint dir path"));
        goto cleanup;
    }

    if (virFileResolveLink(checkpointfd, &checkpointdir) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Failed to readlink checkpoint dir path"));
        goto cleanup;
    }

    virCommandAddArgList(cmd,
        "--pidfile", "pidfile",
        "--restore-detached",
        "--restore-sibling",
        "--tcp-established",
        "--file-locks",
        "--link-remap",
        "--manage-cgroups=full",
        "--enable-fs", "hugetlbfs",
        "--enable-fs", "tracefs",
        "--images-dir", checkpointdir,
        "--log-file", "restore.log",
        "-v4",
        "--external", "mnt[]{:ms}",
        "--external", "mnt[fuse]:/proc/meminfo",
        "--external", "mnt[console]:/dev/console",
        "--external", "mnt[tty1]:/dev/tty1",
        NULL
    );

    /* Restore external tty from tty.info file */
    if (virAsprintf(&tty_info_path, "%s/tty.info", checkpointdir) < 0)
        goto cleanup;

    if (virFileReadAll(tty_info_path, 1024, &ttyinfo) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to read tty info from %s"), tty_info_path);
        goto cleanup;
    }
    if (virAsprintf(&inheritfd, "fd[%d]:%s", ttyfd, ttyinfo) < 0)
        goto cleanup;

    virCommandAddArgList(cmd, "--inherit-fd", inheritfd, NULL);

    root = virDomainGetFilesystemForTarget(def, "/");
    virCommandAddArgList(cmd, "--root", root->src->path, NULL);

    virCommandAddEnvString(cmd, "PATH=/bin:/sbin");

    if ((ngroups = virGetGroupList(virCommandGetUID(cmd), virCommandGetGID(cmd), &groups)) < 0)
        goto cleanup;

    /* If virCommandExec returns here we have an error */
    ignore_value(virCommandExec(cmd, groups, ngroups));

    ret = -1;

 cleanup:
    VIR_FREE(tty_info_path);
    VIR_FREE(ttyinfo);
    VIR_FREE(inheritfd);
    VIR_FREE(checkpointdir);
    VIR_FREE(checkpointfd);
    virCommandFree(cmd);

    return ret;
}
#else
int lxcCriuDump(virLXCDriverPtr driver ATTRIBUTE_UNUSED,
                virDomainObjPtr vm ATTRIBUTE_UNUSED,
                const char *checkpointdir ATTRIBUTE_UNUSED)
{
    virReportUnsupportedError();
    return -1;
}

int lxcCriuRestore(virDomainDefPtr def ATTRIBUTE_UNUSED,
                   int fd ATTRIBUTE_UNUSED,
                   int ttyfd ATTRIBUTE_UNUSED)
{
    virReportUnsupportedError();
    return -1;
}
#endif
