/*
 * virsh-backup.c: Commands to manage domain backup
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
 * Author: Nikolay Shirokovskiy <nshirokovskiy@virtuozzo.com>
 */

#include <config.h>
#include "virsh-backup.h"

#include "internal.h"
#include "virfile.h"
#include "virsh-domain.h"
#include "viralloc.h"

#define VIRSH_COMMON_OPT_DOMAIN_FULL                       \
    VIRSH_COMMON_OPT_DOMAIN(N_("domain name, id or uuid")) \

/*
 * "backup-start" command
 */
static const vshCmdInfo info_backup_start[] = {
    {.name = "help",
     .data = N_("Start pull backup")
    },
    {.name = "desc",
     .data = N_("Start pull backup")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_backup_start[] = {
    VIRSH_COMMON_OPT_DOMAIN_FULL,
    {.name = "xmlfile",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("domain backup XML")
    },
    {.name = "quiesce",
     .type = VSH_OT_BOOL,
     .help = N_("quiesce guest's file systems")
    },
    {.name = NULL}
};

static bool
cmdBackupStart(vshControl *ctl, const vshCmd *cmd)
{
    virDomainPtr dom = NULL;
    bool ret = false;
    const char *from = NULL;
    char *buffer = NULL;
    unsigned int flags = 0;

    if (vshCommandOptBool(cmd, "quiesce"))
        flags |= VIR_DOMAIN_BACKUP_START_QUIESCE;

    if (!(dom = virshCommandOptDomain(ctl, cmd, NULL)))
        goto cleanup;

    if (vshCommandOptStringReq(ctl, cmd, "xmlfile", &from) < 0)
        goto cleanup;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0) {
        vshSaveLibvirtError();
        goto cleanup;
    }

    if (virDomainBackupStart(dom, buffer, flags) < 0)
        goto cleanup;

    vshPrint(ctl, _("Domain backup started"));
    ret = true;

 cleanup:
    VIR_FREE(buffer);
    if (dom)
        virDomainFree(dom);

    return ret;
}

/*
 * "backup-stop" command
 */
static const vshCmdInfo info_backup_stop[] = {
    {.name = "help",
     .data = N_("Stop pull backup")
    },
    {.name = "desc",
     .data = N_("Stop pull backup")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_backup_stop[] = {
    VIRSH_COMMON_OPT_DOMAIN_FULL,
    {.name = NULL}
};

static bool
cmdBackupStop(vshControl *ctl, const vshCmd *cmd)
{
    virDomainPtr dom = NULL;
    bool ret = false;

    if (!(dom = virshCommandOptDomain(ctl, cmd, NULL)))
        goto cleanup;

    if (virDomainBackupStop(dom, 0) < 0)
        goto cleanup;

    vshPrint(ctl, _("Domain backup stopped"));
    ret = true;

 cleanup:
    if (dom)
        virDomainFree(dom);

    return ret;
}

const vshCmdDef backupCmds[] = {
    {.name = "backup-start",
     .handler = cmdBackupStart,
     .opts = opts_backup_start,
     .info = info_backup_start,
     .flags = 0
    },
    {.name = "backup-stop",
     .handler = cmdBackupStop,
     .opts = opts_backup_stop,
     .info = info_backup_stop,
     .flags = 0
    },
    {.name = NULL}
};
