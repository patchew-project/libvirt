/*
 * lxc_criu.c: wrapper functions for CRIU C API to be used for lxc migration
 *
 * Copyright (c) 2021 Red Hat, Inc.
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
#include "virutil.h"

#include "lxc_domain.h"
#include "lxc_driver.h"
#include "lxc_criu.h"

#define VIR_FROM_THIS VIR_FROM_LXC

VIR_LOG_INIT("lxc.lxc_criu");

#if WITH_CRIU
typedef enum {
    LXC_SAVE_FORMAT_RAW = 0,
    LXC_SAVE_FORMAT_GZIP = 1,
    LXC_SAVE_FORMAT_BZIP2 = 2,
    LXC_SAVE_FORMAT_XZ = 3,
    LXC_SAVE_FORMAT_LZOP = 4,

    LXC_SAVE_FORMAT_LAST
} virLXCSaveFormat;

VIR_ENUM_DECL(lxcSaveCompression);
VIR_ENUM_IMPL(lxcSaveCompression,
              LXC_SAVE_FORMAT_LAST,
              "raw",
              "gzip",
              "bzip2",
              "xz",
              "lzop",
);


/* lxcSaveImageGetCompressionProgram:
 * @imageFormat: String representation from lxc.conf for the compression
 *               image format being used (dump, save, or snapshot).
 * @compresspath: Pointer to a character string to store the fully qualified
 *                path from virFindFileInPath.
 * @styleFormat: String representing the style of format (dump, save, snapshot)
 *
 * Returns:
 *    virQEMUSaveFormat    - Integer representation of the compression
 *                           program to be used for particular style
 *                           (e.g. dump, save, or snapshot).
 *    LXC_SAVE_FORMAT_RAW  - If there is no lxc.conf imageFormat value or
 *                           no there was an error, then just return RAW
 *                           indicating none.
 */
static int
lxcSaveImageGetCompressionProgram(const char *imageFormat,
                                  virCommandPtr *compressor,
                                  const char *styleFormat)
{
    const char *prog;
    int ret;

    *compressor = NULL;

    /* Use tar to compress all .img files */
    if (!(prog = virFindFileInPath("tar")))
        return -1;

    *compressor = virCommandNew(prog);

    if (STREQ(styleFormat, "save")) {
        /* Remove files after added into tar */
        virCommandAddArgList(*compressor, "--create",
                             "--remove-files", NULL);
    } else if (STREQ(styleFormat, "dump")) {
        virCommandAddArg(*compressor, "--extract");
    } else {
        return -1;
    }

    if (!imageFormat)
        return 0;

    if ((ret = lxcSaveCompressionTypeFromString(imageFormat)) < 0)
        return -1;

    switch (ret) {
    case LXC_SAVE_FORMAT_GZIP:
        virCommandAddArg(*compressor, "--gzip");
        break;
    case LXC_SAVE_FORMAT_BZIP2:
        virCommandAddArg(*compressor, "--bzip2");
        break;
    case LXC_SAVE_FORMAT_XZ:
        virCommandAddArg(*compressor, "--xz");
        break;
    case LXC_SAVE_FORMAT_LZOP:
        virCommandAddArg(*compressor, "--lzop");
        break;
    case LXC_SAVE_FORMAT_RAW:
    default:
        break;
    }

    return ret;
}


int lxcCriuCompress(const char *checkpointdir,
                    char *compressionType)
{
    virCommandPtr cmd;
    g_autofree char *tarfile = NULL;
    int ret = -1;

    if ((ret = lxcSaveImageGetCompressionProgram(compressionType,
                                                 &cmd,
                                                 "save")) < 0)
        return -1;

    tarfile = g_strdup_printf("%s/criu.save", checkpointdir);

    virCommandAddArgFormat(cmd, "--file=%s", tarfile);
    virCommandAddArgFormat(cmd, "--directory=%s/save/", checkpointdir);
    virCommandAddArg(cmd, ".");

    if (virCommandRun(cmd, NULL) < 0)
        return -1;

    return ret;
}


int lxcCriuDecompress(const char *checkpointdir,
                      char *compressionType)
{
    virCommandPtr cmd;
    g_autofree char *tarfile = NULL;
    g_autofree char *savedir = NULL;
    int ret = -1;

    if ((ret = lxcSaveImageGetCompressionProgram(compressionType,
                                                 &cmd,
                                                 "dump")) < 0)
        return -1;

    savedir = g_strdup_printf("%s/save/", checkpointdir);
    if (virFileMakePath(savedir) < 0) {
        virReportSystemError(errno,
                             _("Failed to mkdir %s"), savedir);
        return -1;
    }

    tarfile = g_strdup_printf("%s/criu.save", checkpointdir);

    virCommandAddArgFormat(cmd, "--file=%s", tarfile);
    virCommandAddArgFormat(cmd, "--directory=%s", savedir);

    if (virCommandRun(cmd, NULL) < 0)
        return -1;

    return ret;
}


int lxcCriuDump(virDomainObjPtr vm,
                const char *checkpointdir)
{
    int ret = -1;
    virLXCDomainObjPrivatePtr priv = vm->privateData;
    virCommandPtr cmd;
    struct stat sb;
    g_autofree char *path = NULL;
    g_autofree char *tty_info_path = NULL;
    g_autofree char *ttyinfo = NULL;
    g_autofree char *pidfile = NULL;
    g_autofree char *pidbuf = NULL;
    g_autofree char *savedir = NULL;
    int pidlen;
    int pidfd;
    int status;

    savedir = g_strdup_printf("%s/save/", checkpointdir);
    if (virFileMakePath(savedir) < 0) {
        virReportSystemError(errno,
                             _("Failed to mkdir %s"), savedir);
        return -1;
    }

    pidfile = g_strdup_printf("%s/save/dump.pid", checkpointdir);
    pidbuf = g_strdup_printf("%d", priv->initpid);
    pidlen = strlen(pidbuf);

    pidfd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (safewrite(pidfd, pidbuf, pidlen) != pidlen) {
        virReportSystemError(errno, "%s", _("criu pid file write failed"));
        return -1;
    }

    cmd = virCommandNew(CRIU);
    virCommandAddArg(cmd, "dump");

    virCommandAddArgList(cmd, "--images-dir", savedir, NULL);

    virCommandAddArgList(cmd, "--log-file", "dump.log", NULL);

    virCommandAddArgList(cmd, "-vvvv", NULL);

    virCommandAddArg(cmd, "--tree");
    virCommandAddArgFormat(cmd, "%d", priv->initpid);

    virCommandAddArgList(cmd, "--tcp-established", "--file-locks",
                              "--link-remap", "--force-irmap", NULL);

    virCommandAddArgList(cmd, "--manage-cgroup", NULL);

    virCommandAddArgList(cmd, "--enable-external-sharing",
                              "--enable-external-masters", NULL);

    virCommandAddArgList(cmd, "--enable-fs", "hugetlbfs",
                              "--enable-fs", "tracefs", NULL);

    /* Add support for FUSE */
    virCommandAddArgList(cmd, "--ext-mount-map", "/proc/meminfo:fuse", NULL);
    virCommandAddArgList(cmd, "--ghost-limit", "10000000", NULL);

    virCommandAddArgList(cmd, "--ext-mount-map", "/dev/console:console", NULL);
    virCommandAddArgList(cmd, "--ext-mount-map", "/dev/tty1:tty1", NULL);
    virCommandAddArgList(cmd, "--ext-mount-map", "auto", NULL);

    /* The master pair of the /dev/pts device lives outside from what is dumped
     * inside the libvirt-lxc process. Add the slave pair as an external tty
     * otherwise criu will fail.
     */
    path = g_strdup_printf("/proc/%d/root/dev/pts/0", priv->initpid);

    if (stat(path, &sb) < 0) {
        virReportSystemError(errno,
                             _("Unable to stat %s"), path);
        goto cleanup;
    }

    tty_info_path = g_strdup_printf("%s/tty.info", savedir);
    ttyinfo = g_strdup_printf("tty[%x:%x]", (unsigned int)sb.st_rdev,
                              (unsigned int)sb.st_dev);

    if (virFileWriteStr(tty_info_path, ttyinfo, 0666) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to write tty info to %s"), tty_info_path);
        goto cleanup;
    }

    VIR_DEBUG("tty.info: tty[%x:%x]",
             (unsigned int)sb.st_dev, (unsigned int)sb.st_rdev);
    virCommandAddArg(cmd, "--external");
    virCommandAddArgFormat(cmd, "tty[%x:%x]",
                          (unsigned int)sb.st_rdev, (unsigned int)sb.st_dev);

    VIR_DEBUG("About to checkpoint domain %s (pid = %d)",
              vm->def->name, priv->initpid);
    virCommandRawStatus(cmd);
    if (virCommandRun(cmd, &status) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    if (ret < 0)
        return ret;
    return status;
}

int lxcCriuRestore(virDomainDefPtr def,
                   int restorefd, int ttyfd)
{
    virCommandPtr cmd;
    g_autofree char *ttyinfo = NULL;
    g_autofree char *inheritfd = NULL;
    g_autofree char *tty_info_path = NULL;
    g_autofree char *checkpointfd = NULL;
    g_autofree char *checkpointdir = NULL;
    g_autofree char *rootfs_mount = NULL;
    g_autofree gid_t *groups = NULL;
    int ret = -1;
    int ngroups;

    cmd = virCommandNew(CRIU);
    virCommandAddArg(cmd, "restore");

    checkpointfd = g_strdup_printf("/proc/self/fd/%d", restorefd);

    if (virFileResolveLink(checkpointfd, &checkpointdir) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Failed to readlink checkpoint dir path"));
        return -1;
    }

    /* CRIU needs the container's root bind mounted so that it is the root of
     * some mount.
     */
    rootfs_mount = g_strdup_printf("%s/save/%s", LXC_STATE_DIR, def->name);

    virCommandAddArgList(cmd, "--images-dir", checkpointdir, NULL);

    virCommandAddArgList(cmd, "--log-file", "restore.log", NULL);

    virCommandAddArgList(cmd, "--pidfile", "restore.pid", NULL);

    virCommandAddArgList(cmd, "-vvvv", NULL);
    virCommandAddArgList(cmd, "--tcp-established", "--file-locks",
                              "--link-remap", "--force-irmap", NULL);

    virCommandAddArgList(cmd, "--enable-external-sharing",
                              "--enable-external-masters", NULL);

    virCommandAddArgList(cmd, "--ext-mount-map", "auto", NULL);

    virCommandAddArgList(cmd, "--enable-fs", "hugetlbfs",
                              "--enable-fs", "tracefs", NULL);

    virCommandAddArgList(cmd, "--ext-mount-map", "fuse:/proc/meminfo", NULL);

    virCommandAddArgList(cmd, "--ext-mount-map", "console:/dev/console", NULL);
    virCommandAddArgList(cmd, "--ext-mount-map", "tty1:/dev/tty1", NULL);

    virCommandAddArgList(cmd, "--restore-detached", "--restore-sibling", NULL);

    /* Restore external tty that was saved in tty.info file
     */
    tty_info_path = g_strdup_printf("%s/tty.info", checkpointdir);

    if (virFileReadAll(tty_info_path, 1024, &ttyinfo) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to read tty info from %s"), tty_info_path);
        return -1;
    }

    inheritfd = g_strdup_printf("fd[%d]:%s", ttyfd, ttyinfo);

    virCommandAddArgList(cmd, "--inherit-fd", inheritfd, NULL);

    /* Change the root filesystem because we run  in mount namespace.
     */
    virCommandAddArgList(cmd, "--root", rootfs_mount, NULL);

    if ((ngroups = virGetGroupList(virCommandGetUID(cmd), virCommandGetGID(cmd),
                                   &groups)) < 0)
        return -1;


    VIR_DEBUG("Executing init binary");
    /* this function will only return if an error occurred */
    ret = virCommandExec(cmd, groups, ngroups);

    if (ret != 0) {
        VIR_DEBUG("Tearing down container");
        fprintf(stderr,
                _("Failure in libvirt_lxc startup: %s\n"),
                virGetLastErrorMessage());
    }

    return ret;
}
#else
int lxcCriuDump(virDomainObjPtr vm ATTRIBUTE_UNUSED,
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
