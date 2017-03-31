/*
 * virsysfs.c: Helper functions for manipulating sysfs files
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
 * Author: Martin Kletzander <mkletzan@redhat.com>
 */

#include <config.h>
#include <fcntl.h>

#include "internal.h"

#include "virsysfspriv.h"

#include "viralloc.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.sysfs");


#define VIR_SYSFS_VALUE_MAXLEN 8192
#define SYSFS_SYSTEM_PATH "/sys/devices/system"
#define SYSFS_RESCTRL_PATH "/sys/fs/resctrl"

static const char *sysfs_system_path = SYSFS_SYSTEM_PATH;
static const char *sysfs_resctrl_path = SYSFS_RESCTRL_PATH;


void virSysfsSetSystemPath(const char *path)
{
    if (path)
        sysfs_system_path = path;
    else
        sysfs_system_path = SYSFS_SYSTEM_PATH;
}


const char *
virSysfsGetSystemPath(void)
{
    return sysfs_system_path;
}

void virSysfsSetResctrlPath(const char *path)
{
    if (path)
        sysfs_resctrl_path  = path;
    else
        sysfs_resctrl_path = SYSFS_RESCTRL_PATH;
}

const char *
virSysfsGetResctrlPath(void)
{
    return sysfs_resctrl_path;
}

int
virSysfsGetValueInt(const char *file,
                    int *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/%s", sysfs_system_path, file) < 0)
        return -1;

    ret = virFileReadValueInt(path, value);

    VIR_FREE(path);
    return ret;
}

int
virSysfsGetValueString(const char *file,
                       char **value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/%s", sysfs_system_path, file) < 0)
        return -1;

    if (!virFileExists(path)) {
        ret = -2;
        goto cleanup;
    }

    if (virFileReadAll(path, VIR_SYSFS_VALUE_MAXLEN, value) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetValueBitmap(const char *file,
                       virBitmapPtr *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/%s", sysfs_system_path, file) < 0)
        return -1;

    ret = virFileReadValueBitmap(path, VIR_SYSFS_VALUE_MAXLEN, value);
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetCpuValueInt(unsigned int cpu,
                       const char *file,
                       int *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/cpu/cpu%u/%s", sysfs_system_path, cpu, file) < 0)
        return -1;

    ret = virFileReadValueInt(path, value);

    VIR_FREE(path);
    return ret;
}


int
virSysfsGetCpuValueUint(unsigned int cpu,
                        const char *file,
                        unsigned int *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/cpu/cpu%u/%s", sysfs_system_path, cpu, file) < 0)
        return -1;

    ret = virFileReadValueUint(path, value);

    VIR_FREE(path);
    return ret;
}


int
virSysfsGetCpuValueString(unsigned int cpu,
                          const char *file,
                          char **value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/cpu/cpu%u/%s", sysfs_system_path, cpu, file) < 0)
        return -1;

    if (!virFileExists(path)) {
        ret = -2;
        goto cleanup;
    }

    if (virFileReadAll(path, VIR_SYSFS_VALUE_MAXLEN, value) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetCpuValueBitmap(unsigned int cpu,
                          const char *file,
                          virBitmapPtr *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/cpu/cpu%u/%s", sysfs_system_path, cpu, file) < 0)
        return -1;

    ret = virFileReadValueBitmap(path, VIR_SYSFS_VALUE_MAXLEN, value);
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetNodeValueString(unsigned int node,
                           const char *file,
                           char **value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/node/node%u/%s", sysfs_system_path, node, file) < 0)
        return -1;

    if (!virFileExists(path)) {
        ret = -2;
        goto cleanup;
    }

    if (virFileReadAll(path, VIR_SYSFS_VALUE_MAXLEN, value) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetNodeValueBitmap(unsigned int node,
                           const char *file,
                           virBitmapPtr *value)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/node/node%u/%s", sysfs_system_path, node, file) < 0)
        return -1;

    ret = virFileReadValueBitmap(path, VIR_SYSFS_VALUE_MAXLEN, value);
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetResctrlString(const char* file,
                         char **value)
{
    char *path = NULL;
    int ret = -1;
    if (virAsprintf(&path, "%s/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    if (virFileReadAll(path, VIR_SYSFS_VALUE_MAXLEN, value) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetResctrlUint(const char *file,
                       unsigned int *value)
{
    char *path = NULL;
    int ret = -1;
    if (virAsprintf(&path, "%s/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    ret = virFileReadValueUint(path, value);

    VIR_FREE(path);
    return ret;
}

int
virSysfsGetResctrlInfoString(const char* file,
                             char **value)
{
    char *path = NULL;
    int ret = -1;
    if (virAsprintf(&path, "%s/info/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    if (virFileReadAll(path, VIR_SYSFS_VALUE_MAXLEN, value) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(path);
    return ret;
}

int
virSysfsGetResctrlInfoUint(const char *file,
                           unsigned int *value)
{
    char *path = NULL;
    int ret;

    if (virAsprintf(&path, "%s/info/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    ret = virFileReadValueUint(path, value);

    VIR_FREE(path);
    return ret;
}

int
virSysfsWriteResctrlString(const char *file,
                           const char *content)
{
    char *path = NULL;
    int ret = -1;
    int writefd;

    if (virAsprintf(&path, "%s/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    /* We can not use virFileWriteStr because resctrl requires oflag should be as
     O_WRONLY | O_APPEND */
    if ((writefd = open(path, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR)) < 0)
        goto cleanup;

    if (safewrite(writefd, content, strlen(content)) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FORCE_CLOSE(writefd);
    VIR_FREE(path);
    return ret;
}

int
virSysfsCreateResCtrlDir(const char *file)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    ret = virDirCreate(path, 0755, 0, 0, 0);

    VIR_FREE(path);
    return ret;
}

int
virSysfsRemoveResCtrlDir(const char *file)
{
    char *path = NULL;
    int ret = -1;

    if (virAsprintf(&path, "%s/%s", sysfs_resctrl_path, file) < 0)
        return -1;

    ret = virFileDeleteTree(path);

    VIR_FREE(path);
    return ret;
}
