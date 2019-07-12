/*
 * virtpm.c: TPM support
 *
 * Copyright (C) 2013 IBM Corporation
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

#include <sys/stat.h>

#include "virstring.h"
#include "virerror.h"
#include "viralloc.h"
#include "virfile.h"
#include "virtpm.h"

#define VIR_FROM_THIS VIR_FROM_NONE

/**
 * virTPMCreateCancelPath:
 * @devpath: Path to the TPM device
 *
 * Create the cancel path given the path to the TPM device
 */
char *
virTPMCreateCancelPath(const char *devpath)
{
    char *path = NULL;
    const char *dev;
    const char *prefix[] = {"misc/", "tpm/"};
    size_t i;

    if (devpath) {
        dev = strrchr(devpath, '/');
        if (dev) {
            dev++;
            for (i = 0; i < ARRAY_CARDINALITY(prefix); i++) {
                if (virAsprintf(&path, "/sys/class/%s%s/device/cancel",
                                prefix[i], dev) < 0)
                     goto cleanup;

                if (virFileExists(path))
                    break;

                VIR_FREE(path);
            }
            if (!path)
                ignore_value(VIR_STRDUP(path, "/dev/null"));
        } else {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("TPM device path %s is invalid"), devpath);
        }
    } else {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Missing TPM device path"));
    }

 cleanup:
    return path;
}

/*
 * executables for the swtpm; to be found on the host
 */
static char *swtpm_path;
static struct stat swtpm_stat;

static char *swtpm_setup;
static struct stat swtpm_setup_stat;

static char *swtpm_ioctl;
static struct stat swtpm_ioctl_stat;

const char *
virTPMGetSwtpm(void)
{
    if (!swtpm_path)
        virTPMEmulatorInit();
    return swtpm_path;
}

const char *
virTPMGetSwtpmSetup(void)
{
    if (!swtpm_setup)
        virTPMEmulatorInit();
    return swtpm_setup;
}

const char *
virTPMGetSwtpmIoctl(void)
{
    if (!swtpm_ioctl)
        virTPMEmulatorInit();
    return swtpm_ioctl;
}

/*
 * virTPMEmulatorInit
 *
 * Initialize the Emulator functions by searching for necessary
 * executables that we will use to start and setup the swtpm
 */
int
virTPMEmulatorInit(void)
{
    static const struct {
        const char *name;
        char **path;
        struct stat *stat;
    } prgs[] = {
        {
            .name = "swtpm",
            .path = &swtpm_path,
            .stat = &swtpm_stat,
        },
        {
            .name = "swtpm_setup",
            .path = &swtpm_setup,
            .stat = &swtpm_setup_stat,
        },
        {
            .name = "swtpm_ioctl",
            .path = &swtpm_ioctl,
            .stat = &swtpm_ioctl_stat,
        }
    };
    size_t i;

    for (i = 0; i < ARRAY_CARDINALITY(prgs); i++) {
        char *path;
        bool findit = *prgs[i].path == NULL;
        struct stat statbuf;
        char *tmp;

        if (!findit) {
            /* has executables changed? */
            if (stat(*prgs[i].path, &statbuf) < 0) {
                virReportSystemError(errno,
                                     _("Could not stat %s"), path);
                findit = true;
            }
            if (!findit &&
                memcmp(&statbuf.st_mtim,
                       &prgs[i].stat->st_mtime,
                       sizeof(statbuf.st_mtim))) {
                findit = true;
            }
        }

        if (findit) {
            path = virFindFileInPath(prgs[i].name);
            if (!path) {
                virReportSystemError(ENOENT,
                                _("Unable to find '%s' binary in $PATH"),
                                prgs[i].name);
                return -1;
            }
            if (!virFileIsExecutable(path)) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("%s is not an executable"),
                               path);
                VIR_FREE(path);
                return -1;
            }
            if (stat(path, prgs[i].stat) < 0) {
                virReportSystemError(errno,
                                     _("Could not stat %s"), path);
                VIR_FREE(path);
                return -1;
            }
            tmp = *prgs[i].path;
            *prgs[i].path = path;
            VIR_FREE(tmp);
        }
    }

    return 0;
}
