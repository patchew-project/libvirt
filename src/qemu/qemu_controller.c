/*
 * qemu_controller.c: QEMU process controller
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
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
 *
 */

#include <config.h>

#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "virgettext.h"
#include "virthread.h"
#include "virlog.h"
#include "virfile.h"
#include "viralloc.h"
#include "virstring.h"
#include "dirname.h"
#include "driver.h"

#include "qemu/qemu_conf.h"
#include "qemu/qemu_process.h"
#include "qemu/qemu_driver.h"
#include "libvirt_internal.h"

static const char *argv0;

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_controller");

typedef struct virQEMUController virQEMUController;
typedef virQEMUController *virQEMUControllerPtr;
struct virQEMUController {
    const char *uri;
    bool privileged;
    const char *xml;
    virQEMUDriverPtr driver;
    virConnectPtr conn;
    virDomainObjPtr vm;
};

static void
virQEMUControllerDriverFree(virQEMUDriverPtr driver)
{
    if (!driver)
        return;

    virObjectUnref(driver->config);
    virObjectUnref(driver->hostdevMgr);
    virHashFree(driver->sharedDevices);
    virObjectUnref(driver->caps);
    virObjectUnref(driver->qemuCapsCache);

    virObjectUnref(driver->domains);
    virObjectUnref(driver->remotePorts);
    virObjectUnref(driver->webSocketPorts);
    virObjectUnref(driver->migrationPorts);
    virObjectUnref(driver->migrationErrors);

    virObjectUnref(driver->xmlopt);

    virSysinfoDefFree(driver->hostsysinfo);

    virObjectUnref(driver->closeCallbacks);

    VIR_FREE(driver->qemuImgBinary);

    virObjectUnref(driver->securityManager);

    ebtablesContextFree(driver->ebtables);

    virLockManagerPluginUnref(driver->lockManager);

    virMutexDestroy(&driver->lock);
    VIR_FREE(driver);
}

static virQEMUDriverPtr virQEMUControllerNewDriver(bool privileged)
{
    virQEMUDriverPtr driver = NULL;
    char *driverConf = NULL;
    virConnectPtr conn = NULL;
    virQEMUDriverConfigPtr cfg;
    uid_t run_uid = -1;
    gid_t run_gid = -1;
    char *hugepagePath = NULL;
    char *memoryBackingPath = NULL;
    size_t i;

    if (VIR_ALLOC(driver) < 0)
        return NULL;

    if (virMutexInit(&driver->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("cannot initialize mutex"));
        VIR_FREE(driver);
        return NULL;
    }

    driver->privileged = privileged;

    /* read the host sysinfo */
    if (privileged)
        driver->hostsysinfo = virSysinfoRead();

    if (!(driver->config = cfg = virQEMUDriverConfigNew(privileged)))
        goto error;

    if (virAsprintf(&driverConf, "%s/qemu.conf", cfg->configBaseDir) < 0)
        goto error;

    if (virQEMUDriverConfigLoadFile(cfg, driverConf, privileged) < 0)
        goto error;
    VIR_FREE(driverConf);

    if (virQEMUDriverConfigValidate(cfg) < 0)
        goto error;

    if (virFileMakePath(cfg->stateDir) < 0) {
        virReportSystemError(errno, _("Failed to create state dir %s"),
                             cfg->stateDir);
        goto error;
    }
    if (virFileMakePath(cfg->libDir) < 0) {
        virReportSystemError(errno, _("Failed to create lib dir %s"),
                             cfg->libDir);
        goto error;
    }
    if (virFileMakePath(cfg->cacheDir) < 0) {
        virReportSystemError(errno, _("Failed to create cache dir %s"),
                             cfg->cacheDir);
        goto error;
    }
    if (virFileMakePath(cfg->saveDir) < 0) {
        virReportSystemError(errno, _("Failed to create save dir %s"),
                             cfg->saveDir);
        goto error;
    }
    if (virFileMakePath(cfg->snapshotDir) < 0) {
        virReportSystemError(errno, _("Failed to create save dir %s"),
                             cfg->snapshotDir);
        goto error;
    }
    if (virFileMakePath(cfg->autoDumpPath) < 0) {
        virReportSystemError(errno, _("Failed to create dump dir %s"),
                             cfg->autoDumpPath);
        goto error;
    }
    if (virFileMakePath(cfg->channelTargetDir) < 0) {
        virReportSystemError(errno, _("Failed to create channel target dir %s"),
                             cfg->channelTargetDir);
        goto error;
    }
    if (virFileMakePath(cfg->nvramDir) < 0) {
        virReportSystemError(errno, _("Failed to create nvram dir %s"),
                             cfg->nvramDir);
        goto error;
    }
    if (virFileMakePath(cfg->memoryBackingDir) < 0) {
        virReportSystemError(errno, _("Failed to create memory backing dir %s"),
                             cfg->memoryBackingDir);
        goto error;
    }

    driver->qemuImgBinary = virFindFileInPath("qemu-img");

    if (!(driver->lockManager =
          virLockManagerPluginNew(cfg->lockManagerName ?
                                  cfg->lockManagerName : "nop",
                                  "qemu",
                                  cfg->configBaseDir,
                                  0)))
        goto error;

   if (cfg->macFilter) {
        if (!(driver->ebtables = ebtablesContextNew("qemu"))) {
            virReportSystemError(errno,
                                 _("failed to enable mac filter in '%s'"),
                                 __FILE__);
            goto error;
        }

        if (ebtablesAddForwardPolicyReject(driver->ebtables) < 0)
            goto error;
   }

    /* Allocate bitmap for remote display port reservations. We cannot
     * do this before the config is loaded properly, since the port
     * numbers are configurable now */
    if ((driver->remotePorts =
         virPortAllocatorNew(_("display"),
                             cfg->remotePortMin,
                             cfg->remotePortMax,
                             0)) == NULL)
        goto error;

    if ((driver->webSocketPorts =
         virPortAllocatorNew(_("webSocket"),
                             cfg->webSocketPortMin,
                             cfg->webSocketPortMax,
                             0)) == NULL)
        goto error;

    if ((driver->migrationPorts =
         virPortAllocatorNew(_("migration"),
                             cfg->migrationPortMin,
                             cfg->migrationPortMax,
                             0)) == NULL)
        goto error;

    if (qemuSecurityInit(driver) < 0)
        goto error;

    if (!(driver->hostdevMgr = virHostdevManagerGetDefault()))
        goto error;

    if (!(driver->sharedDevices = virHashCreate(30, qemuSharedDeviceEntryFree)))
        goto error;

    if (privileged) {
        char *channeldir;

        if (chown(cfg->libDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to user %d:%d"),
                                 cfg->libDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->cacheDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->cacheDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->saveDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->saveDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->snapshotDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->snapshotDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->autoDumpPath, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->autoDumpPath, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (!(channeldir = mdir_name(cfg->channelTargetDir))) {
            virReportOOMError();
            goto error;
        }
        if (chown(channeldir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 channeldir, (int) cfg->user,
                                 (int) cfg->group);
            VIR_FREE(channeldir);
            goto error;
        }
        VIR_FREE(channeldir);
        if (chown(cfg->channelTargetDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->channelTargetDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->nvramDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->nvramDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }
        if (chown(cfg->memoryBackingDir, cfg->user, cfg->group) < 0) {
            virReportSystemError(errno,
                                 _("unable to set ownership of '%s' to %d:%d"),
                                 cfg->memoryBackingDir, (int) cfg->user,
                                 (int) cfg->group);
            goto error;
        }

        run_uid = cfg->user;
        run_gid = cfg->group;
    }

    driver->qemuCapsCache = virQEMUCapsCacheNew(cfg->libDir,
                                                     cfg->cacheDir,
                                                     run_uid,
                                                     run_gid);
    if (!driver->qemuCapsCache)
        goto error;

    if ((driver->caps = virQEMUDriverCreateCapabilities(driver)) == NULL)
        goto error;

    if (!(driver->xmlopt = virQEMUDriverCreateXMLConf(driver)))
        goto error;

    /* If hugetlbfs is present, then we need to create a sub-directory within
     * it, since we can't assume the root mount point has permissions that
     * will let our spawned QEMU instances use it. */
    for (i = 0; i < cfg->nhugetlbfs; i++) {
        hugepagePath = qemuGetBaseHugepagePath(&cfg->hugetlbfs[i]);

        if (!hugepagePath)
            goto error;

        if (virFileMakePath(hugepagePath) < 0) {
            virReportSystemError(errno,
                                 _("unable to create hugepage path %s"),
                                 hugepagePath);
            goto error;
        }
        if (privileged &&
            virFileUpdatePerm(cfg->hugetlbfs[i].mnt_dir,
                              0, S_IXGRP | S_IXOTH) < 0)
            goto error;
        VIR_FREE(hugepagePath);
    }

    if (qemuGetMemoryBackingBasePath(cfg, &memoryBackingPath) < 0)
        goto error;

    if (virFileMakePath(memoryBackingPath) < 0) {
        virReportSystemError(errno,
                             _("unable to create memory backing path %s"),
                             memoryBackingPath);
        goto error;
    }

    if (privileged &&
        virFileUpdatePerm(memoryBackingPath,
                          0, S_IXGRP | S_IXOTH) < 0)
        goto error;
    VIR_FREE(memoryBackingPath);

    if (!(driver->closeCallbacks = virCloseCallbacksNew()))
        goto error;

    return driver;

 error:
    virObjectUnref(conn);
    VIR_FREE(driverConf);
    VIR_FREE(hugepagePath);
    VIR_FREE(memoryBackingPath);
    virQEMUControllerDriverFree(driver);
    return NULL;
}

static void show_help(FILE *io)
{
    fprintf(io, "\n");
    fprintf(io, "syntax: %s [OPTIONS] PATH-TO-XML\n", argv0);
    fprintf(io, "\n");
    fprintf(io, "Options\n");
    fprintf(io, "\n");
    fprintf(io, "  -c URI, --connect URI\n");
    fprintf(io, "  -h, --help\n");
    fprintf(io, "\n");
}

static void
virQEMUControllerMain(void *opaque)
{
    int ret = -1;
    virQEMUControllerPtr ctrl = opaque;
    virQEMUDriverConfigPtr cfg;
    virDomainChrSourceDef monitor_chr = { 0 };
    qemuDomainObjPrivatePtr priv;
    virDomainPtr dom;

    if (!(ctrl->conn = virConnectOpen(ctrl->uri))) {
        fprintf(stderr, "Unable to connect to %s: %s",
                ctrl->uri, virGetLastErrorMessage());
        goto cleanup;
    }

    if (!(ctrl->driver = virQEMUControllerNewDriver(ctrl->privileged))) {
        fprintf(stderr, "Unable to initialize driver: %s",
                virGetLastErrorMessage());
        goto cleanup;
    }

    cfg = virObjectRef(ctrl->driver->config);

    if (qemuProcessPrepareMonitorChr(&monitor_chr, cfg->libDir) < 0) {
        fprintf(stderr, "Unable to prepare QEMU monitor: %s\n",
                virGetLastErrorMessage());
        goto cleanup;
    }

    if (!(ctrl->vm = virDomainObjNew(ctrl->driver->xmlopt))) {
        fprintf(stderr, "Unable to allocate domain object: %s\n",
                virGetLastErrorMessage());
        goto cleanup;
    }

    if (!(ctrl->vm->def = virDomainDefParseFile(ctrl->xml,
                                                ctrl->driver->caps, ctrl->driver->xmlopt,
                                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE))) {
        fprintf(stderr, "Unable to parse domain config %s\n",
                virGetLastErrorMessage());
        goto cleanup;
    }

    if (qemuProcessStart(NULL, ctrl->driver, ctrl->vm, NULL, 0, NULL, -1, NULL, NULL, 0, 0) < 0) {
        fprintf(stderr, "Unable to start QEMU: %s\n",
                virGetLastErrorMessage());
        goto cleanup;
    }

    priv = ctrl->vm->privateData;

    /* Release the monitor & agent sockets, so main libvirtd can take over */
    qemuMonitorClose(priv->mon);
    if (priv->agent)
        qemuAgentClose(priv->agent);

    dom = virDomainQemuReconnect(ctrl->conn, ctrl->vm->def->name, 0);
    if (!dom) {
        qemuProcessStop(ctrl->driver, ctrl->vm, 0, 0, 0);
        fprintf(stderr, "Unable to reconnect with libvirtd: %s\n",
                virGetLastErrorMessage());
        goto cleanup;
    }

    virObjectUnref(dom);

    fprintf(stderr, "QEMU running and connected\n");
    ret = 0;
 cleanup:
    virConnectClose(ctrl->conn);
    if (ret < 0)
        exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    int rc = 1;
    virThread thr;
    virQEMUControllerPtr ctrl;
    const struct option options[] = {
        { "connect",   1, NULL, 'c' },
        { "help", 0, NULL, 'h' },
        { 0, 0, 0, 0 },
    };

    argv0 = argv[0];

    if (virGettextInitialize() < 0 ||
        virThreadInitialize() < 0 ||
        virErrorInitialize() < 0) {
        fprintf(stderr, _("%s: initialization failed\n"), argv0);
        exit(EXIT_FAILURE);
    }

    /* Initialize logging */
    virLogSetFromEnv();

    virUpdateSelfLastChanged(argv[0]);

    virFileActivateDirOverride(argv[0]);

    if (VIR_ALLOC(ctrl) < 0)
        goto cleanup;

    ctrl->privileged = geteuid() == 0;
    ctrl->uri = ctrl->privileged ? "qemu:///system" : "qemu:///session";

    while (1) {
        int c;

        c = getopt_long(argc, argv, "c:h",
                        options, NULL);

        if (c == -1)
            break;

        switch (c) {
        case 'c':
            ctrl->uri = optarg;
            break;

        case 'h':
        case '?':
            show_help(stdout);
            rc = 0;
            goto cleanup;
        }
    }

    ctrl->xml = argv[optind];

    if (ctrl->xml == NULL) {
        fprintf(stderr, "Missing XML file path\n");
        show_help(stderr);
        goto cleanup;
    }

    if (virEventRegisterDefaultImpl() < 0) {
        fprintf(stderr, "Unable to initialize events: %s",
                virGetLastErrorMessage());
        goto cleanup;
    }

    if (virThreadCreate(&thr, false, virQEMUControllerMain, ctrl) < 0)
        goto cleanup;

    for (;;)
        virEventRunDefaultImpl();

    rc = 0;

 cleanup:
    virStateCleanup();
    if (ctrl->conn)
        virConnectClose(ctrl->conn);
    virObjectUnref(ctrl->vm);
    VIR_FREE(ctrl);
    return rc;
}
