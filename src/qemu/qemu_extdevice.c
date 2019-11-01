/*
 * qemu_extdevice.c: QEMU external devices support
 *
 * Copyright (C) 2014, 2018 IBM Corporation
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
#include "qemu_extdevice.h"
#include "qemu_vhost_user_gpu.h"
#include "qemu_domain.h"
#include "qemu_tpm.h"
#include "qemu_slirp.h"

#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"
#include "virtime.h"
#include "virtpm.h"
#include "virpidfile.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_extdevice");

int
qemuExtDeviceLogCommand(virQEMUDriverPtr driver,
                        virDomainObjPtr vm,
                        virCommandPtr cmd,
                        const char *info)
{
    g_autofree char *timestamp = virTimeStringNow();
    g_autofree char *cmds = virCommandToString(cmd, false);

    if (!timestamp || !cmds)
        return -1;

    return qemuDomainLogAppendMessage(driver, vm,
                                      _("%s: Starting external device: %s\n%s\n"),
                                      timestamp, info, cmds);
}



/*
 * qemuExtDevicesInitPaths:
 *
 * @driver: QEMU driver
 * @def: domain definition
 *
 * Initialize paths of external devices so that it is known where state is
 * stored and we can remove directories and files in case of domain XML
 * changes.
 */
static int
qemuExtDevicesInitPaths(virQEMUDriverPtr driver,
                        virDomainDefPtr def)
{
    int ret = 0;

    if (def->tpm)
        ret = qemuExtTPMInitPaths(driver, def);

    return ret;
}


/*
 * qemuExtDevicesPrepareDomain:
 *
 * @driver: QEMU driver
 * @vm: domain
 *
 * Code that modifies live XML of a domain which is about to start.
 */
int
qemuExtDevicesPrepareDomain(virQEMUDriverPtr driver,
                            virDomainObjPtr vm)
{
    int ret = 0;
    size_t i;

    for (i = 0; i < vm->def->nvideos; i++) {
        virDomainVideoDefPtr video = vm->def->videos[i];

        if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER) {
            if ((ret = qemuExtVhostUserGPUPrepareDomain(driver, video)) < 0)
                break;
        }
    }

    return ret;
}


/*
 * qemuExtDevicesPrepareHost:
 *
 * @driver: QEMU driver
 * @def: domain definition
 *
 * Prepare host storage paths for external devices.
 */
int
qemuExtDevicesPrepareHost(virQEMUDriverPtr driver,
                          virDomainObjPtr vm)
{
    virDomainDefPtr def = vm->def;
    size_t i;

    if (def->tpm &&
        qemuExtTPMPrepareHost(driver, def) < 0)
        return -1;

    for (i = 0; i < def->nnets; i++) {
        virDomainNetDefPtr net = def->nets[i];
        qemuSlirpPtr slirp = QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp;

        if (slirp && qemuSlirpOpen(slirp, driver, def) < 0)
            return -1;
    }

    return 0;
}


void
qemuExtDevicesCleanupHost(virQEMUDriverPtr driver,
                          virDomainDefPtr def)
{
    if (qemuExtDevicesInitPaths(driver, def) < 0)
        return;

    if (def->tpm)
        qemuExtTPMCleanupHost(def);
}

static char *
qemuExtVirtioFSCreatePidFilename(virQEMUDriverConfigPtr cfg,
                                 const virDomainDef *def,
                                 const char *alias)
{
    g_autofree char *shortName = NULL;
    g_autofree char *name = NULL;

    if (!(shortName = virDomainDefGetShortName(def)) ||
        virAsprintf(&name, "%s-%s-virtiofsd", shortName, alias) < 0)
        return NULL;

    return virPidFileBuildPath(cfg->stateDir, name);
}


static char *
qemuExtVirtioFSCreateSocketFilename(virQEMUDriverConfigPtr cfg,
                                    const virDomainDef *def,
                                    const char *alias)
{
    g_autofree char *shortName = NULL;
    g_autofree char *name = NULL;

    if (!(shortName = virDomainDefGetShortName(def)) ||
        virAsprintf(&name, "%s-%s-virtiofsd", shortName, alias) < 0)
        return NULL;

    return virFileBuildPath(cfg->stateDir, name, ".sock");
}


static int
qemuExtVirtioFSdStart(virQEMUDriverPtr driver,
                      virDomainObjPtr vm,
                      virDomainFSDefPtr fs)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autoptr(virCommand) cmd = NULL;
    g_autofree char *pidfile = NULL;
    char errbuf[1024] = { 0 };
    virDomainChrSourceDefPtr chrdev = virDomainChrSourceDefNew(NULL);
    pid_t pid = (pid_t) -1;
    VIR_AUTOCLOSE errfd = -1;
    VIR_AUTOCLOSE fd = -1;
    int exitstatus = 0;
    int cmdret = 0;
    int ret = -1;
    int rc;

    chrdev->type = VIR_DOMAIN_CHR_TYPE_UNIX;
    chrdev->data.nix.listen = true;

    if (!(pidfile = qemuExtVirtioFSCreatePidFilename(cfg, vm->def, fs->info.alias)))
        goto cleanup;

    if (!(chrdev->data.nix.path = qemuExtVirtioFSCreateSocketFilename(cfg, vm->def, fs->info.alias)))
        goto cleanup;

    if (qemuSecuritySetDaemonSocketLabel(driver->securityManager, vm->def) < 0)
        goto cleanup;
    fd = qemuOpenChrChardevUNIXSocket(chrdev);
    if (fd < 0)
        goto cleanup;
    if (qemuSecurityClearSocketLabel(driver->securityManager, vm->def) < 0)
        goto cleanup;

    /* TODO: do not call this here */
    virDomainChrDef chr = { .source = chrdev };
    if (qemuSecuritySetChardevLabel(driver, vm, &chr) < 0)
        goto cleanup;

    /* TODO: configure path */
    if (!(cmd = virCommandNew("/usr/libexec/virtiofsd")))
        goto cleanup;

    virCommandSetPidFile(cmd, pidfile);
    virCommandSetErrorFD(cmd, &errfd);
    virCommandDaemonize(cmd);

    virCommandAddArg(cmd, "--syslog");
    virCommandAddArgFormat(cmd, "--fd=%d", fd);
    virCommandPassFD(cmd, fd, VIR_COMMAND_PASS_FD_CLOSE_PARENT);
    fd = -1;

    virCommandAddArg(cmd, "-o");
    /* TODO: validate path? */
    virCommandAddArgFormat(cmd, "source=%s", fs->src->path);
    virCommandAddArg(cmd, "-d");

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

    fs->vhost_user_fs_path = g_steal_pointer(&chrdev->data.nix.path);
    ret = 0;

 cleanup:
    if (chrdev->data.nix.path)
        unlink(chrdev->data.nix.path);
    virObjectUnref(chrdev);
    return ret;

 error:
    if (pid != -1)
        virProcessKillPainfully(pid, true);
    if (pidfile)
        unlink(pidfile);
    goto cleanup;
}


static void
qemuExtVirtioFSdStop(virQEMUDriverPtr driver,
                     virDomainObjPtr vm,
                     virDomainFSDefPtr fs)
{
    g_autoptr(virQEMUDriverConfig) cfg = virQEMUDriverGetConfig(driver);
    g_autofree char *pidfile = NULL;
    virErrorPtr orig_err;
    pid_t pid = -1;
    int rc;

    virErrorPreserveLast(&orig_err);

    if (!(pidfile = qemuExtVirtioFSCreatePidFilename(cfg, vm->def, fs->info.alias)))
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

    if (fs->vhost_user_fs_path)
        unlink(fs->vhost_user_fs_path);

 cleanup:
    virErrorRestore(&orig_err);
}


int
qemuExtDevicesStart(virQEMUDriverPtr driver,
                    virDomainObjPtr vm,
                    bool incomingMigration)
{
    virDomainDefPtr def = vm->def;
    int ret = 0;
    size_t i;

    if (qemuExtDevicesInitPaths(driver, vm->def) < 0)
        return -1;

    for (i = 0; i < vm->def->nvideos; i++) {
        virDomainVideoDefPtr video = vm->def->videos[i];

        if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER) {
            ret = qemuExtVhostUserGPUStart(driver, vm, video);
            if (ret < 0)
                return ret;
        }
    }

    if (vm->def->tpm)
        ret = qemuExtTPMStart(driver, vm, incomingMigration);

    for (i = 0; i < def->nnets; i++) {
        virDomainNetDefPtr net = def->nets[i];
        qemuSlirpPtr slirp = QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp;

        if (slirp &&
            qemuSlirpStart(slirp, vm, driver, net, false, incomingMigration) < 0)
            return -1;
    }

    for (i = 0; i < def->nfss; i++) {
        /* FIXME: use private data */
        virDomainFSDefPtr fs = def->fss[i];

        if (fs->fsdriver == VIR_DOMAIN_FS_DRIVER_TYPE_VIRTIO_FS) {
            if (qemuExtVirtioFSdStart(driver, vm, fs) < 0)
                return -1;
        }
    }


    return ret;
}


void
qemuExtDevicesStop(virQEMUDriverPtr driver,
                   virDomainObjPtr vm)
{
    virDomainDefPtr def = vm->def;
    size_t i;

    if (qemuExtDevicesInitPaths(driver, def) < 0)
        return;

    for (i = 0; i < def->nvideos; i++) {
        virDomainVideoDefPtr video = def->videos[i];

        if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER)
            qemuExtVhostUserGPUStop(driver, vm, video);
    }

    if (def->tpm)
        qemuExtTPMStop(driver, vm);

    for (i = 0; i < def->nnets; i++) {
        virDomainNetDefPtr net = def->nets[i];
        qemuSlirpPtr slirp = QEMU_DOMAIN_NETWORK_PRIVATE(net)->slirp;

        if (slirp)
            qemuSlirpStop(slirp, vm, driver, net, false);
    }

    for (i = 0; i < def->nfss; i++) {
        virDomainFSDefPtr fs = def->fss[i];

        if (fs->fsdriver == VIR_DOMAIN_FS_DRIVER_TYPE_VIRTIO_FS)
            qemuExtVirtioFSdStop(driver, vm, fs);
    }
}


bool
qemuExtDevicesHasDevice(virDomainDefPtr def)
{
    size_t i;

    for (i = 0; i < def->nvideos; i++) {
        if (def->videos[i]->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER)
            return true;
    }

    if (def->tpm && def->tpm->type == VIR_DOMAIN_TPM_TYPE_EMULATOR)
        return true;

    return false;
}


int
qemuExtDevicesSetupCgroup(virQEMUDriverPtr driver,
                          virDomainDefPtr def,
                          virCgroupPtr cgroup)
{
    size_t i;

    for (i = 0; i < def->nvideos; i++) {
        virDomainVideoDefPtr video = def->videos[i];

        if (video->backend == VIR_DOMAIN_VIDEO_BACKEND_TYPE_VHOSTUSER &&
            qemuExtVhostUserGPUSetupCgroup(driver, def, video, cgroup) < 0)
            return -1;
    }

    if (def->tpm &&
        qemuExtTPMSetupCgroup(driver, def, cgroup) < 0)
        return -1;

    return 0;
}
