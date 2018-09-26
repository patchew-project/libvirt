/*
 * vircgrouppriv.h: methods for managing control cgroups
 *
 * Copyright (C) 2011-2013 Red Hat, Inc.
 * Copyright IBM Corp. 2008
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
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 */

#ifndef __VIR_CGROUP_ALLOW_INCLUDE_PRIV_H__
# error "vircgrouppriv.h may only be included by vircgroup.c or its test suite"
#endif

#ifndef __VIR_CGROUP_PRIV_H__
# define __VIR_CGROUP_PRIV_H__

# include <dirent.h>
# include <unistd.h>

# include "vircgroup.h"
# include "vircgroupbackend.h"

# if defined(__linux__) && defined(HAVE_GETMNTENT_R) && \
    defined(_DIRENT_HAVE_D_TYPE) && defined(_SC_CLK_TCK)
#  define VIR_CGROUP_SUPPORTED
# endif

struct _virCgroupV1Controller {
    int type;
    char *mountPoint;
    /* If mountPoint holds several controllers co-mounted,
     * then linkPoint is path of the symlink to the mountPoint
     * for just the one controller
     */
    char *linkPoint;
    char *placement;
};
typedef struct _virCgroupV1Controller virCgroupV1Controller;
typedef virCgroupV1Controller *virCgroupV1ControllerPtr;

struct _virCgroup {
    char *path;

    virCgroupBackendPtr backend;

    virCgroupV1Controller legacy[VIR_CGROUP_CONTROLLER_LAST];
};

int virCgroupSetValueStr(virCgroupPtr group,
                         int controller,
                         const char *key,
                         const char *value);

int virCgroupGetValueStr(virCgroupPtr group,
                         int controller,
                         const char *key,
                         char **value);

int virCgroupSetValueU64(virCgroupPtr group,
                         int controller,
                         const char *key,
                         unsigned long long int value);

int virCgroupGetValueU64(virCgroupPtr group,
                         int controller,
                         const char *key,
                         unsigned long long int *value);

int virCgroupSetValueI64(virCgroupPtr group,
                         int controller,
                         const char *key,
                         long long int value);

int virCgroupGetValueI64(virCgroupPtr group,
                         int controller,
                         const char *key,
                         long long int *value);

int virCgroupPartitionEscape(char **path);

char *virCgroupGetBlockDevString(const char *path);

int virCgroupGetValueForBlkDev(virCgroupPtr group,
                               int controller,
                               const char *key,
                               const char *path,
                               char **value);

int virCgroupNew(pid_t pid,
                 const char *path,
                 virCgroupPtr parent,
                 int controllers,
                 virCgroupPtr *group);

int virCgroupNewPartition(const char *path,
                          bool create,
                          int controllers,
                          virCgroupPtr *group)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4);

int virCgroupNewDomainPartition(virCgroupPtr partition,
                                const char *driver,
                                const char *name,
                                bool create,
                                virCgroupPtr *group)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(5);

int virCgroupRemoveRecursively(char *grppath);

#endif /* __VIR_CGROUP_PRIV_H__ */
