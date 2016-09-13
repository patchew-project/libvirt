/*
 * virhook.c: implementation of the synchronous hooks support
 *
 * Copyright (C) 2010-2014 Red Hat, Inc.
 * Copyright (C) 2010 Daniel Veillard
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
 * Author: Daniel Veillard <veillard@redhat.com>
 */

#include <config.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "virerror.h"
#include "virhook.h"
#include "virutil.h"
#include "virlog.h"
#include "viralloc.h"
#include "virfile.h"
#include "configmake.h"
#include "vircommand.h"

#define VIR_FROM_THIS VIR_FROM_HOOK

VIR_LOG_INIT("util.hook");

#define LIBVIRT_HOOK_DIR SYSCONFDIR "/libvirt/hooks"

#define virHookInstall(driver) virHooksFound |= (1 << driver);

#define virHookUninstall(driver) virHooksFound ^= (1 << driver);

VIR_ENUM_DECL(virHookDriver)
VIR_ENUM_DECL(virHookDaemonOp)
VIR_ENUM_DECL(virHookSubop)
VIR_ENUM_DECL(virHookQemuOp)
VIR_ENUM_DECL(virHookLxcOp)
VIR_ENUM_DECL(virHookNetworkOp)
VIR_ENUM_DECL(virHookLibxlOp)

VIR_ENUM_IMPL(virHookDriver,
              VIR_HOOK_DRIVER_LAST,
              "daemon",
              "qemu",
              "lxc",
              "network",
              "libxl")

VIR_ENUM_IMPL(virHookDaemonOp, VIR_HOOK_DAEMON_OP_LAST,
              "start",
              "shutdown",
              "reload")

VIR_ENUM_IMPL(virHookSubop, VIR_HOOK_SUBOP_LAST,
              "-",
              "begin",
              "end")

VIR_ENUM_IMPL(virHookQemuOp, VIR_HOOK_QEMU_OP_LAST,
              "start",
              "stopped",
              "prepare",
              "release",
              "migrate",
              "started",
              "reconnect",
              "attach",
              "restore")

VIR_ENUM_IMPL(virHookLxcOp, VIR_HOOK_LXC_OP_LAST,
              "start",
              "stopped",
              "prepare",
              "release",
              "started",
              "reconnect")

VIR_ENUM_IMPL(virHookNetworkOp, VIR_HOOK_NETWORK_OP_LAST,
              "start",
              "started",
              "stopped",
              "plugged",
              "unplugged",
              "updated")

VIR_ENUM_IMPL(virHookLibxlOp, VIR_HOOK_LIBXL_OP_LAST,
              "start",
              "stopped",
              "prepare",
              "release",
              "migrate",
              "started",
              "reconnect")

static int virHooksFound = -1;

static virHookInotifyPtr virHooksInotify = NULL;

/**
 * virHookCheck:
 * @driver: the driver name "daemon", "qemu", "lxc"...
 *
 * Check is there is an installed hook for the given driver, if this
 * is the case register it. Then subsequent calls to virHookCall
 * will call the hook if found.
 *
 * Returns 1 if found, 0 if not found, and -1 in case of error
 */
static int
virHookCheck(int no, const char *driver)
{
    char *path;
    int ret;

    if (driver == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Invalid hook name for #%d"), no);
        return -1;
    }

    if (virBuildPath(&path, LIBVIRT_HOOK_DIR, driver) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to build path for %s hook"),
                       driver);
        return -1;
    }

    if (!virFileExists(path)) {
        ret = 0;
        VIR_DEBUG("No hook script %s", path);
    } else if (!virFileIsExecutable(path)) {
        ret = 0;
        VIR_WARN("Non-executable hook script %s", path);
    } else {
        ret = 1;
        VIR_DEBUG("Found hook script %s", path);
    }

    VIR_FREE(path);
    return ret;
}

/**
 * virHookInotifyEvent:
 * @fd: inotify file descriptor.
 *
 * Identifies file events at libvirt's hook directory.
 * Install or uninstall hooks on demand. Acording file manipulation.
 */
static void
virHookInotifyEvent(int watch ATTRIBUTE_UNUSED,
                    int fd,
                    int events ATTRIBUTE_UNUSED,
                    void *data ATTRIBUTE_UNUSED)
{
    char buf[1024];
    struct inotify_event *e;
    int got;
    int driver;
    char *tmp, *name;

    VIR_DEBUG("inotify event in virHookInotify()");

reread:
    got = read(fd, buf, sizeof(buf));
    if (got == -1) {
        if (errno == EINTR)
            goto reread;
        return;
    }

    tmp = buf;
    while (got) {
        if (got < sizeof(struct inotify_event))
            return;

        VIR_WARNINGS_NO_CAST_ALIGN
        e = (struct inotify_event *)tmp;
        VIR_WARNINGS_RESET

        tmp += sizeof(struct inotify_event);
        got -= sizeof(struct inotify_event);

        if (got < e->len)
            return;

        tmp += e->len;
        got -= e->len;

        name = (char *)&(e->name);

        /* Removing hook file. */
        if (e->mask & (IN_DELETE | IN_MOVED_FROM)) {
            if ((driver = virHookDriverTypeFromString(name)) < 0) {
                VIR_DEBUG("Invalid hook name for %s", name);
                return;
            }

            virHookUninstall(driver);
        }

        /* Creating hook file. */
        if (e->mask & (IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO)) {
            if ((driver = virHookDriverTypeFromString(name)) < 0) {
                VIR_DEBUG("Invalid hook name for %s", name);
                return;
            }

            virHookInstall(driver);
        }
    }
}

/**
 * virHookInotifyInit:
 *
 * Initialize inotify hooks support.
 * Enable hooks installation on demand.
 *
 * Returns 0 if inotify was successfully installed, -1 in case of failure.
 */
static int
virHookInotifyInit(void) {

    if (VIR_ALLOC(virHooksInotify) < 0)
        goto error;

    if ((virHooksInotify->inotifyFD = inotify_init()) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("cannot initialize inotify"));
        goto error;
    }

    if ((virHooksInotify->inotifyWatch =
            inotify_add_watch(virHooksInotify->inotifyFD,
                              LIBVIRT_HOOK_DIR,
                              IN_CREATE | IN_MODIFY | IN_DELETE)) < 0) {
        virReportSystemError(errno, _("Failed to create inotify watch on %s"),
                             LIBVIRT_HOOK_DIR);
        goto error;
    }

    if ((virHooksInotify->inotifyHandler =
            virEventAddHandle(virHooksInotify->inotifyFD,
                              VIR_EVENT_HANDLE_READABLE,
                              virHookInotifyEvent, NULL, NULL)) < 0) {
        VIR_DEBUG("Failed to add inotify handle in virHook.");
        goto error;
    }

    return 0;

error:
    virHookCleanUp();
    return -1;
}


/*
 * virHookInitialize:
 *
 * Initialize synchronous hooks support.
 * Check is there is an installed hook for all the drivers
 *
 * Returns the number of hooks found or -1 in case of failure
 */
int
virHookInitialize(void)
{
    size_t i;
    int res, ret = 0;

    virHooksFound = 0;
    for (i = 0; i < VIR_HOOK_DRIVER_LAST; i++) {
        res = virHookCheck(i, virHookDriverTypeToString(i));
        if (res < 0)
            return -1;

        if (res == 1) {
            virHookInstall(i);
            ret++;
        }
    }

    if (virHookInotifyInit() < 0)
        VIR_INFO("Disabling hooks inotify support.");

    return ret;
}

/**
 * virHookPresent:
 * @driver: the driver number (from virHookDriver enum)
 *
 * Check if a hook exists for the given driver, this is needed
 * to avoid unnecessary work if the hook is not present
 *
 * Returns 1 if present, 0 otherwise
 */
int
virHookPresent(int driver)
{
    if ((driver < VIR_HOOK_DRIVER_DAEMON) ||
        (driver >= VIR_HOOK_DRIVER_LAST))
        return 0;
    if (virHooksFound == -1)
        return 0;

    if ((virHooksFound & (1 << driver)) == 0)
        return 0;
    return 1;
}

/**
 * virHookCall:
 * @driver: the driver number (from virHookDriver enum)
 * @id: an id for the object '-' if non available for example on daemon hooks
 * @op: the operation on the id e.g. VIR_HOOK_QEMU_OP_START
 * @sub_op: a sub_operation, currently unused
 * @extra: optional string information
 * @input: extra input given to the script on stdin
 * @output: optional address of variable to store malloced result buffer
 *
 * Implement a hook call, where the external script for the driver is
 * called with the given information. This is a synchronous call, we wait for
 * execution completion. If @output is non-NULL, *output is guaranteed to be
 * allocated after successful virHookCall, and is best-effort allocated after
 * failed virHookCall; the caller is responsible for freeing *output.
 *
 * Returns: 0 if the execution succeeded, 1 if the script was not found or
 *          invalid parameters, and -1 if script returned an error
 */
int
virHookCall(int driver,
            const char *id,
            int op,
            int sub_op,
            const char *extra,
            const char *input,
            char **output)
{
    int ret;
    char *path;
    virCommandPtr cmd;
    const char *drvstr;
    const char *opstr;
    const char *subopstr;

    if (output)
        *output = NULL;

    if ((driver < VIR_HOOK_DRIVER_DAEMON) ||
        (driver >= VIR_HOOK_DRIVER_LAST))
        return 1;

    /*
     * We cache the availability of the script to minimize impact at
     * runtime if no script is defined, this is being reset on SIGHUP
     */
    if ((virHooksFound == -1) ||
        ((driver == VIR_HOOK_DRIVER_DAEMON) &&
         (op == VIR_HOOK_DAEMON_OP_RELOAD ||
         op == VIR_HOOK_DAEMON_OP_SHUTDOWN)))
        virHookInitialize();

    if ((virHooksFound & (1 << driver)) == 0)
        return 1;

    drvstr = virHookDriverTypeToString(driver);

    opstr = NULL;
    switch (driver) {
        case VIR_HOOK_DRIVER_DAEMON:
            opstr = virHookDaemonOpTypeToString(op);
            break;
        case VIR_HOOK_DRIVER_QEMU:
            opstr = virHookQemuOpTypeToString(op);
            break;
        case VIR_HOOK_DRIVER_LXC:
            opstr = virHookLxcOpTypeToString(op);
            break;
        case VIR_HOOK_DRIVER_LIBXL:
            opstr = virHookLibxlOpTypeToString(op);
            break;
        case VIR_HOOK_DRIVER_NETWORK:
            opstr = virHookNetworkOpTypeToString(op);
    }
    if (opstr == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Hook for %s, failed to find operation #%d"),
                       drvstr, op);
        return 1;
    }
    subopstr = virHookSubopTypeToString(sub_op);
    if (subopstr == NULL)
        subopstr = "-";
    if (extra == NULL)
        extra = "-";

    if (virBuildPath(&path, LIBVIRT_HOOK_DIR, drvstr) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to build path for %s hook"),
                       drvstr);
        return -1;
    }

    VIR_DEBUG("Calling hook opstr=%s subopstr=%s extra=%s",
              opstr, subopstr, extra);

    cmd = virCommandNewArgList(path, id, opstr, subopstr, extra, NULL);

    virCommandAddEnvPassCommon(cmd);

    if (input)
        virCommandSetInputBuffer(cmd, input);
    if (output)
        virCommandSetOutputBuffer(cmd, output);

    ret = virHookCheck(driver, virHookDriverTypeToString(driver));

    if (ret > 0) {
        ret = virCommandRun(cmd, NULL);
    }

    if (ret < 0) {
        /* Convert INTERNAL_ERROR into known error.  */
        virReportError(VIR_ERR_HOOK_SCRIPT_FAILED, "%s",
                       virGetLastErrorMessage());
    }

    virCommandFree(cmd);

    VIR_FREE(path);

    return ret;
}

/**
 * virHookCall:
 *
 * Release all structures and data used in virhooks.
 *
 * Returns: 0 if the execution succeeded
 */
int
virHookCleanUp(void)
{
    if (!virHooksInotify)
        return -1;

    if ((virHooksInotify->inotifyFD >= 0) &&
        (virHooksInotify->inotifyWatch >= 0))
        if (inotify_rm_watch(virHooksInotify->inotifyFD,
                             virHooksInotify->inotifyWatch) < 0)
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Cannot remove inotify watcher."));

    if (virHooksInotify->inotifyHandler >= 0)
        virEventRemoveHandle(virHooksInotify->inotifyHandler);

    VIR_FORCE_CLOSE(virHooksInotify->inotifyFD);
    VIR_FREE(virHooksInotify);

    virHooksFound = -1;

    return 0;
}
