/*
 * qemu_virtiofs.c: virtiofs support
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

#include "qemu_command.h"
#include "qemu_conf.h"
#include "qemu_extdevice.h"
#include "qemu_security.h"
#include "qemu_virtiofs.h"
#include "virpidfile.h"

#define VIR_FROM_THIS VIR_FROM_QEMU


char *
qemuVirtioFSCreatePidFilename(virQEMUDriverConfigPtr cfg,
                              const virDomainDef *def,
                              const char *alias)
{
    g_autofree char *shortName = NULL;
    g_autofree char *name = NULL;

    if (!(shortName = virDomainDefGetShortName(def)))
        return NULL;

    name = g_strdup_printf("%s-%s-virtiofsd", shortName, alias);

    return virPidFileBuildPath(cfg->stateDir, name);
}


char *
qemuVirtioFSCreateSocketFilename(virDomainObjPtr vm,
                                 const char *alias)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    return virFileBuildPath(priv->libDir, alias, "-virtiofsd.sock");
}


static int
qemuVirtioFSOpenChardev(virQEMUDriverPtr driver,
                        virDomainObjPtr vm,
                        const char *socket_path)
{
    virDomainChrSourceDefPtr chrdev = virDomainChrSourceDefNew(NULL);
    virDomainChrDef chr = { .source = chrdev };
    VIR_AUTOCLOSE fd = -1;
    int ret = -1;

    chrdev->type = VIR_DOMAIN_CHR_TYPE_UNIX;
    chrdev->data.nix.listen = true;
    chrdev->data.nix.path = g_strdup(socket_path);

    if (qemuSecuritySetDaemonSocketLabel(driver->securityManager, vm->def) < 0)
        goto cleanup;
    fd = qemuOpenChrChardevUNIXSocket(chrdev);
    if (fd < 0) {
        ignore_value(qemuSecurityClearSocketLabel(driver->securityManager, vm->def));
        goto cleanup;
    }
    if (qemuSecurityClearSocketLabel(driver->securityManager, vm->def) < 0)
        goto cleanup;

    if (qemuSecuritySetChardevLabel(driver, vm, &chr) < 0)
        goto cleanup;

    ret = fd;
    fd = -1;

 cleanup:
    virObjectUnref(chrdev);
    return ret;
}

static virCommandPtr
qemuVirtioFSBuildCommandLine(virQEMUDriverConfigPtr cfg,
                             virDomainFSDefPtr fs,
                             int *fd)
{
    g_autoptr(virCommand) cmd = NULL;
    g_auto(virBuffer) opts = VIR_BUFFER_INITIALIZER;

    if (!(cmd = virCommandNew(fs->binary)))
        return NULL;

    virCommandAddArg(cmd, "--syslog");
    virCommandAddArgFormat(cmd, "--fd=%d", *fd);
    virCommandPassFD(cmd, *fd, VIR_COMMAND_PASS_FD_CLOSE_PARENT);
    *fd = -1;

    virCommandAddArg(cmd, "-o");
    virBufferAsprintf(&opts, "source=%s,", fs->src->path);
    if (fs->cache)
        virBufferAsprintf(&opts, "cache=%s,", virDomainFSCacheModeTypeToString(fs->cache));
    if (fs->xattr)
        virBufferAsprintf(&opts, "%sxattr,", fs->xattr == VIR_TRISTATE_SWITCH_OFF ? "no_" : "");
    if (fs->flock)
        virBufferAsprintf(&opts, "%sflock,", fs->flock == VIR_TRISTATE_SWITCH_OFF ? "no_" : "");
    if (fs->posix_lock)
        virBufferAsprintf(&opts, "%sposix_lock,", fs->posix_lock == VIR_TRISTATE_SWITCH_OFF ? "no_" : "");
    virBufferTrim(&opts, ",", -1);

    virCommandAddArgBuffer(cmd, &opts);
    if (cfg->virtiofsDebug)
        virCommandAddArg(cmd, "-d");

    return g_steal_pointer(&cmd);
}

int
qemuVirtioFSStart(virQEMUDriverPtr driver,
                  virDomainObjPtr vm,
                  virDomainFSDefPtr fs)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autoptr(virCommand) cmd = NULL;
    g_autofree char *socket_path = NULL;
    g_autofree char *pidfile = NULL;
    char errbuf[1024] = { 0 };
    pid_t pid = (pid_t) -1;
    VIR_AUTOCLOSE errfd = -1;
    VIR_AUTOCLOSE fd = -1;
    int exitstatus = 0;
    int cmdret = 0;
    int ret = -1;
    int rc;

    if (!(pidfile = qemuVirtioFSCreatePidFilename(cfg, vm->def, fs->info.alias)))
        goto cleanup;

    if (!(socket_path = qemuVirtioFSCreateSocketFilename(vm, fs->info.alias)))
        goto cleanup;

    if ((fd = qemuVirtioFSOpenChardev(driver, vm, socket_path)) < 0)
        goto cleanup;

    if (!(cmd = qemuVirtioFSBuildCommandLine(cfg, fs, &fd)))
        goto cleanup;

    virCommandSetPidFile(cmd, pidfile);
    virCommandSetErrorFD(cmd, &errfd);
    virCommandDaemonize(cmd);

    if (qemuExtDeviceLogCommand(driver, vm, cmd, "virtiofsd") < 0)
        goto cleanup;

    cmdret = virCommandRun(cmd, &exitstatus);

    if (cmdret < 0 || exitstatus != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not start 'virtiofsd'. exitstatus: %d"), exitstatus);
        goto error;
    }

    rc = virPidFileReadPath(pidfile, &pid);
    if (rc < 0) {
        virReportSystemError(-rc,
                             _("Unable to read virtiofsd pidfile '%s'"),
                             pidfile);
        goto error;
    }

    if (virProcessKill(pid, 0) != 0) {
        if (saferead(errfd, errbuf, sizeof(errbuf) - 1) < 0) {
            virReportSystemError(errno, "%s",
                                 _("virtiofsd died unexpectedly"));
        } else {
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("virtiofsd died and reported: %s"), errbuf);
        }
        goto error;
    }

    QEMU_DOMAIN_FS_PRIVATE(fs)->vhostuser_fs_sock = g_steal_pointer(&socket_path);
    ret = 0;

 cleanup:
    if (socket_path)
        unlink(socket_path);
    return ret;

 error:
    if (pid != -1)
        virProcessKillPainfully(pid, true);
    if (pidfile)
        unlink(pidfile);
    goto cleanup;
}


void
qemuVirtioFSStop(virQEMUDriverPtr driver,
                    virDomainObjPtr vm,
                    virDomainFSDefPtr fs)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autofree char *pidfile = NULL;
    virErrorPtr orig_err;
    pid_t pid = -1;
    int rc;

    virErrorPreserveLast(&orig_err);

    if (!(pidfile = qemuVirtioFSCreatePidFilename(cfg, vm->def, fs->info.alias)))
        goto cleanup;

    rc = virPidFileReadPathIfAlive(pidfile, &pid, NULL);
    if (rc >= 0 && pid != (pid_t) -1)
        virProcessKillPainfully(pid, true);

    if (unlink(pidfile) < 0 &&
        errno != ENOENT) {
        virReportSystemError(errno,
                             _("Unable to remove stale pidfile %s"),
                             pidfile);
    }

    if (QEMU_DOMAIN_FS_PRIVATE(fs)->vhostuser_fs_sock)
        unlink(QEMU_DOMAIN_FS_PRIVATE(fs)->vhostuser_fs_sock);

 cleanup:
    virErrorRestore(&orig_err);
}
