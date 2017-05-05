/*
 * virsh-backup.c: Commands to manage domain backup
 *
 * Copyright (C) 2017 Parallels International GmbH
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
#include "virsh-backup.h"

#include "internal.h"
#include "virfile.h"
#include "viralloc.h"
#include "virsh-domain.h"
#include "virsh-util.h"

#define VIRSH_COMMON_OPT_DOMAIN_FULL                       \
    VIRSH_COMMON_OPT_DOMAIN(N_("domain name, id or uuid")) \

/*
 * "backup-create" command
 */
static const vshCmdInfo info_backup_create[] = {
    {.name = "help",
     .data = N_("Create a backup from XML")
    },
    {.name = "desc",
     .data = N_("Create a disks backup from XML description")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_backup_create[] = {
    VIRSH_COMMON_OPT_DOMAIN_FULL,
    {.name = "xmlfile",
     .type = VSH_OT_STRING,
     .help = N_("domain backup XML")
    },
    {.name = NULL}
};

static bool
cmdBackupCreate(vshControl *ctl, const vshCmd *cmd)
{
    virDomainPtr dom = NULL;
    bool ret = false;
    const char *from = NULL;
    char *buffer = NULL;
    unsigned int flags = 0;
    virDomainBackupPtr backup = NULL;

    if (!(dom = virshCommandOptDomain(ctl, cmd, NULL)))
        goto cleanup;

    if (vshCommandOptStringReq(ctl, cmd, "xmlfile", &from) < 0)
        goto cleanup;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0) {
        vshSaveLibvirtError();
        goto cleanup;
    }

    if (!(backup = virDomainBackupCreateXML(dom, buffer, flags)))
        goto cleanup;

    vshPrint(ctl, _("Domain backup started from '%s'"), from);

    ret = true;

 cleanup:
    VIR_FREE(buffer);
    virshDomainFree(dom);
    virshDomainBackupFree(backup);

    return ret;
}

const vshCmdDef backupCmds[] = {
    {.name = "backup-create",
     .handler = cmdBackupCreate,
     .opts = opts_backup_create,
     .info = info_backup_create,
     .flags = 0
    },
    {.name = NULL}
};
