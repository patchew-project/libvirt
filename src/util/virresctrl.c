/*
 * virresctrl.c: methods for managing resource control
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
 *  Eli Qiao <liyong.qiao@intel.com>
 */

#include <config.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "virresctrl.h"
#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virhostcpu.h"
#include "virlog.h"
#include "virstring.h"
#include "virarch.h"

VIR_LOG_INIT("util.resctrl");

#define VIR_FROM_THIS VIR_FROM_RESCTRL

VIR_ENUM_IMPL(virResctrl, VIR_RESCTRL_TYPE_LAST,
              "L3",
              "L3CODE",
              "L3DATA",
              "L2")

/**
 * a virResctrlDomain represents a resource control group, it's a directory
 * under /sys/fs/resctrl.
 * eg: /sys/fs/resctrl/CG1
 * |-- cpus
 * |-- schemata
 * `-- tasks
 * # cat schemata
 * L3DATA:0=fffff;1=fffff
 * L3CODE:0=fffff;1=fffff
 *
 * Besides, it can also represent the default resource control group of the
 * host.
 */

typedef struct _virResctrlGroup virResctrlGroup;
typedef virResctrlGroup *virResctrlGroupPtr;
struct _virResctrlGroup {
    char *name; /* resource group name, eg: CG1. If it represent host's
                   default resource group name, should be a NULL pointer */
    size_t n_tasks; /* number of task assigned to the resource group */
    char **tasks; /* task list which contains task id eg: 77454 */

    size_t n_schematas; /* number of schemata the resource group contains,
                         eg: 2 */
    virResctrlSchemataPtr *schematas; /* scheamta list */
};

/* All resource control groups on this host, including default resource group */
typedef struct _virResCtrlDomain virResCtrlDomain;
typedef virResCtrlDomain *virResCtrlDomainPtr;
struct _virResCtrlDomain {
    size_t n_groups; /* number of resource control group */
    virResctrlGroupPtr groups; /* list of resource control group */
};
