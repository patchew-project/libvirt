/*
 * virsh-fspool.c: Commands to manage fspool
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
 *
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
 *  Olga Krishtal <okrishtal@virtuozzo.com>
 *
 */

#include <config.h>
#include "virsh-fspool.h"

#include "internal.h"
#include "virbuffer.h"
#include "viralloc.h"
#include "virfile.h"
#include "conf/fs_conf.h"
#include "virstring.h"

#define VIRSH_COMMON_OPT_FSPOOL_FULL                            \
    VIRSH_COMMON_OPT_FSPOOL(N_("fspool name or uuid"))            \

#define VIRSH_COMMON_OPT_FSPOOL_BUILD                        \
    {.name = "build",                                         \
     .type = VSH_OT_BOOL,                                     \
     .flags = 0,                                              \
     .help = N_("build the fspool as normal")                 \
    }                                                         \

#define VIRSH_COMMON_OPT_FSPOOL_NO_OVERWRITE                        \
    {.name = "no-overwrite",                                         \
     .type = VSH_OT_BOOL,                                            \
     .flags = 0,                                                     \
     .help = N_("do not overwrite an existing fspool of this type")  \
    }                                                                \

#define VIRSH_COMMON_OPT_FSPOOL_OVERWRITE                           \
    {.name = "overwrite",                                         \
     .type = VSH_OT_BOOL,                                         \
     .flags = 0,                                                  \
     .help = N_("overwrite any existing data")                    \
    }                                                             \

#define VIRSH_COMMON_OPT_FSPOOL_X_AS                                  \
    {.name = "name",                                                   \
     .type = VSH_OT_DATA,                                              \
     .flags = VSH_OFLAG_REQ,                                           \
     .help = N_("name of the fspool")                                  \
    },                                                                 \
    {.name = "type",                                                   \
     .type = VSH_OT_DATA,                                              \
     .flags = VSH_OFLAG_REQ,                                           \
     .help = N_("type of the fspool")                                  \
    },                                                                 \
    {.name = "print-xml",                                              \
     .type = VSH_OT_BOOL,                                              \
     .help = N_("print XML document, but don't define/create")         \
    },                                                                 \
    {.name = "source-host",                                            \
     .type = VSH_OT_STRING,                                            \
     .help = N_("source-host for underlying storage")                  \
    },                                                                \
    {.name = "source-path",                                            \
     .type = VSH_OT_STRING,                                            \
     .help = N_("source path for underlying storage")                  \
    },                                                                 \
    {.name = "source-name",                                            \
     .type = VSH_OT_STRING,                                            \
     .help = N_("source name for underlying storage")                  \
    },                                                                 \
    {.name = "target",                                                 \
     .type = VSH_OT_STRING,                                            \
     .help = N_("target for underlying storage")                       \
    },                                                                 \
    {.name = "source-format",                                          \
     .type = VSH_OT_STRING,                                            \
     .help = N_("format for underlying storage")                       \
    }                                                                  \

virFSPoolPtr
virshCommandOptFSPoolBy(vshControl *ctl, const vshCmd *cmd, const char *optname,
                      const char **name, unsigned int flags)
{
    virFSPoolPtr fspool = NULL;
    const char *n = NULL;
    virshControlPtr priv = ctl->privData;

    virCheckFlags(VIRSH_BYUUID | VIRSH_BYNAME, NULL);

    if (vshCommandOptStringReq(ctl, cmd, optname, &n) < 0)
        return NULL;

    vshDebug(ctl, VSH_ERR_INFO, "%s: found option <%s>: %s\n",
             cmd->def->name, optname, n);

    if (name)
        *name = n;

    /* try it by UUID */
    if ((flags & VIRSH_BYUUID) && strlen(n) == VIR_UUID_STRING_BUFLEN-1) {
        vshDebug(ctl, VSH_ERR_DEBUG, "%s: <%s> trying as fspool UUID\n",
                 cmd->def->name, optname);
        fspool = virFSPoolLookupByUUIDString(priv->conn, n);
    }
    /* try it by NAME */
    if (!fspool && (flags & VIRSH_BYNAME)) {
        vshDebug(ctl, VSH_ERR_DEBUG, "%s: <%s> trying as fspool NAME\n",
                 cmd->def->name, optname);
        fspool = virFSPoolLookupByName(priv->conn, n);
    }

    if (!fspool)
        vshError(ctl, _("failed to get fspool '%s'"), n);

    return fspool;
}

/*
 * "fspool-create" command
 */
static const vshCmdInfo info_fspool_create[] = {
    {.name = "help",
     .data = N_("create a fspool from an XML file")
    },
    {.name = "desc",
     .data = N_("Create a fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_create[] = {
    VIRSH_COMMON_OPT_FILE(N_("file containing an XML fspool description")),
    VIRSH_COMMON_OPT_FSPOOL_BUILD,
    VIRSH_COMMON_OPT_FSPOOL_NO_OVERWRITE,
    VIRSH_COMMON_OPT_FSPOOL_OVERWRITE,

    {.name = NULL}
};

static bool
cmdFSPoolCreate(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    const char *from = NULL;
    bool ret = true;
    char *buffer;
    bool build;
    bool overwrite;
    bool no_overwrite;
    unsigned int flags = 0;
    virshControlPtr priv = ctl->privData;

    if (vshCommandOptStringReq(ctl, cmd, "file", &from) < 0)
        return false;

    build = vshCommandOptBool(cmd, "build");
    overwrite = vshCommandOptBool(cmd, "overwrite");
    no_overwrite = vshCommandOptBool(cmd, "no-overwrite");

    VSH_EXCLUSIVE_OPTIONS_EXPR("overwrite", overwrite,
                               "no-overwrite", no_overwrite);

    if (build)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD;
    if (overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE;
    if (no_overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0)
        return false;

    fspool = virFSPoolCreateXML(priv->conn, buffer, flags);
    VIR_FREE(buffer);

    if (fspool != NULL) {
        vshPrint(ctl, _("FSpool %s created from %s\n"),
                 virFSPoolGetName(fspool), from);
        virFSPoolFree(fspool);
    } else {
        vshError(ctl, _("Failed to create fspool from %s"), from);
        ret = false;
    }
    return ret;
}

static const vshCmdOptDef opts_fspool_define_as[] = {
    VIRSH_COMMON_OPT_FSPOOL_X_AS,

    {.name = NULL}
};

static int
virshBuildFSPoolXML(vshControl *ctl,
                    const vshCmd *cmd,
                    const char **retname,
                    char **xml)
{
    const char *name = NULL, *type = NULL, *srcHost = NULL, *srcPath = NULL,
               *srcName = NULL, *srcFormat = NULL, *target = NULL;
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    if (vshCommandOptStringReq(ctl, cmd, "name", &name) < 0)
        goto cleanup;
    if (vshCommandOptStringReq(ctl, cmd, "type", &type) < 0)
        goto cleanup;

    if (vshCommandOptStringReq(ctl, cmd, "source-host", &srcHost) < 0 ||
        vshCommandOptStringReq(ctl, cmd, "source-path", &srcPath) < 0 ||
        vshCommandOptStringReq(ctl, cmd, "source-name", &srcName) < 0 ||
        vshCommandOptStringReq(ctl, cmd, "source-format", &srcFormat) < 0 ||
        vshCommandOptStringReq(ctl, cmd, "target", &target) < 0)
        goto cleanup;

    virBufferAsprintf(&buf, "<fspool type='%s'>\n", type);
    virBufferAdjustIndent(&buf, 2);
    virBufferAsprintf(&buf, "<name>%s</name>\n", name);
    if (srcHost || srcPath || srcFormat || srcName) {
        virBufferAddLit(&buf, "<source>\n");
        virBufferAdjustIndent(&buf, 2);

        if (srcHost)
            virBufferAsprintf(&buf, "<host name='%s'/>\n", srcHost);
        if (srcPath)
            virBufferAsprintf(&buf, "<dir path='%s'/>\n", srcPath);
        if (srcFormat)
            virBufferAsprintf(&buf, "<format type='%s'/>\n", srcFormat);
        if (srcName)
            virBufferAsprintf(&buf, "<name>%s</name>\n", srcName);

        virBufferAdjustIndent(&buf, -2);
        virBufferAddLit(&buf, "</source>\n");
    }
    if (target) {
        virBufferAddLit(&buf, "<target>\n");
        virBufferAdjustIndent(&buf, 2);
        virBufferAsprintf(&buf, "<path>%s</path>\n", target);
        virBufferAdjustIndent(&buf, -2);
        virBufferAddLit(&buf, "</target>\n");
    }
    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</fspool>\n");

    if (virBufferError(&buf)) {
        vshPrint(ctl, "%s", _("Failed to allocate XML buffer"));
        return false;
    }

    *xml = virBufferContentAndReset(&buf);
    *retname = name;
    return true;

 cleanup:
    virBufferFreeAndReset(&buf);
    return false;
}

/*
 * "fspool-autostart" command
 */
static const vshCmdInfo info_fspool_autostart[] = {
    {.name = "help",
     .data = N_("autostart a fspool")
    },
    {.name = "desc",
     .data = N_("Configure a fspool to be automatically started at boot.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_autostart[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = "disable",
     .type = VSH_OT_BOOL,
     .help = N_("disable autostarting")
    },
    {.name = NULL}
};

static bool
cmdFSPoolAutostart(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    const char *name;
    int autostart;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    autostart = !vshCommandOptBool(cmd, "disable");

    if (virFSPoolSetAutostart(fspool, autostart) < 0) {
        if (autostart)
            vshError(ctl, _("failed to mark fspool %s as autostarted"), name);
        else
            vshError(ctl, _("failed to unmark fspool %s as autostarted"), name);
        virFSPoolFree(fspool);
        return false;
    }

    if (autostart)
        vshPrint(ctl, _("Fspool %s marked as autostarted\n"), name);
    else
        vshPrint(ctl, _("Fspool %s unmarked as autostarted\n"), name);

    virFSPoolFree(fspool);
    return true;
}

/*
 * "fspool-create-as" command
 */
static const vshCmdInfo info_fspool_create_as[] = {
    {.name = "help",
     .data = N_("create a fspool from a set of args")
    },
    {.name = "desc",
     .data = N_("Create a fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_create_as[] = {
    VIRSH_COMMON_OPT_FSPOOL_X_AS,
    VIRSH_COMMON_OPT_FSPOOL_BUILD,
    VIRSH_COMMON_OPT_FSPOOL_NO_OVERWRITE,
    VIRSH_COMMON_OPT_FSPOOL_OVERWRITE,

    {.name = NULL}
};

static bool
cmdFSPoolCreateAs(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    const char *name;
    char *xml;
    bool printXML = vshCommandOptBool(cmd, "print-xml");
    bool build;
    bool overwrite;
    bool no_overwrite;
    unsigned int flags = 0;
    virshControlPtr priv = ctl->privData;

    build = vshCommandOptBool(cmd, "build");
    overwrite = vshCommandOptBool(cmd, "overwrite");
    no_overwrite = vshCommandOptBool(cmd, "no-overwrite");

    VSH_EXCLUSIVE_OPTIONS_EXPR("overwrite", overwrite,
                               "no-overwrite", no_overwrite);

    if (build)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD;
    if (overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE;
    if (no_overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE;

    if (!virshBuildFSPoolXML(ctl, cmd, &name, &xml))
        return false;

    if (printXML) {
        vshPrint(ctl, "%s", xml);
        VIR_FREE(xml);
    } else {
        fspool = virFSPoolCreateXML(priv->conn, xml, flags);
        VIR_FREE(xml);

        if (fspool != NULL) {
            vshPrint(ctl, _("FSool %s created\n"), name);
            virFSPoolFree(fspool);
        } else {
            vshError(ctl, _("Failed to create fspool %s"), name);
            return false;
        }
    }
    return true;
}

/*
 * "fspool-define" command
 */
static const vshCmdInfo info_fspool_define[] = {
    {.name = "help",
     .data = N_("define an inactive persistent fspool or modify "
                "an existing persistent one from an XML file")
    },
    {.name = "desc",
     .data = N_("Define or modify a persistent fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_define[] = {
    VIRSH_COMMON_OPT_FILE(N_("file containing an XML fspool description")),

    {.name = NULL}
};

static bool
cmdFSPoolDefine(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    const char *from = NULL;
    bool ret = true;
    char *buffer;
    virshControlPtr priv = ctl->privData;

    if (vshCommandOptStringReq(ctl, cmd, "file", &from) < 0)
        return false;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0)
        return false;

    fspool = virFSPoolDefineXML(priv->conn, buffer, 0);
    VIR_FREE(buffer);

    if (fspool != NULL) {
        vshPrint(ctl, _("FSool %s defined from %s\n"),
                 virFSPoolGetName(fspool), from);
        virFSPoolFree(fspool);
    } else {
        vshError(ctl, _("Failed to define fspool from %s"), from);
        ret = false;
    }
    return ret;
}

/*
 * "fspool-define-as" command
 */
static const vshCmdInfo info_fspool_define_as[] = {
    {.name = "help",
     .data = N_("define a fspool from a set of args")
    },
    {.name = "desc",
     .data = N_("Define a fspool.")
    },
    {.name = NULL}
};

static bool
cmdFSPoolDefineAs(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    const char *name;
    char *xml;
    bool printXML = vshCommandOptBool(cmd, "print-xml");
    virshControlPtr priv = ctl->privData;

    if (!virshBuildFSPoolXML(ctl, cmd, &name, &xml))
        return false;

    if (printXML) {
        vshPrint(ctl, "%s", xml);
        VIR_FREE(xml);
    } else {
        fspool = virFSPoolDefineXML(priv->conn, xml, 0);
        VIR_FREE(xml);

        if (fspool != NULL) {
            vshPrint(ctl, _("FSpool %s defined\n"), name);
            virFSPoolFree(fspool);
        } else {
            vshError(ctl, _("Failed to define fspool %s"), name);
            return false;
        }
    }
    return true;
}

/*
 * "fspool-build" command
 */
static const vshCmdInfo info_fspool_build[] = {
    {.name = "help",
     .data = N_("build a fspool")
    },
    {.name = "desc",
     .data = N_("Build a given fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_build[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,
    VIRSH_COMMON_OPT_FSPOOL_NO_OVERWRITE,
    VIRSH_COMMON_OPT_FSPOOL_OVERWRITE,

    {.name = NULL}
};

static bool
cmdFSPoolBuild(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name;
    unsigned int flags = 0;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    if (vshCommandOptBool(cmd, "no-overwrite"))
        flags |= VIR_FSPOOL_BUILD_NO_OVERWRITE;

    if (vshCommandOptBool(cmd, "overwrite"))
        flags |= VIR_FSPOOL_BUILD_OVERWRITE;

    if (virFSPoolBuild(fspool, flags) == 0) {
        vshPrint(ctl, _("FSpool %s built\n"), name);
    } else {
        vshError(ctl, _("Failed to build fspool %s"), name);
        ret = false;
    }

    virFSPoolFree(fspool);

    return ret;
}

/*
 * "fspool-destroy" command
 */
static const vshCmdInfo info_fspool_destroy[] = {
    {.name = "help",
     .data = N_("stop a fspool")
    },
    {.name = "desc",
     .data = N_("Forcefully stop a given fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_destroy[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolDestroy(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    if (virFSPoolDestroy(fspool) == 0) {
        vshPrint(ctl, _("FSpool %s destroyed\n"), name);
    } else {
        vshError(ctl, _("Failed to destroy fspool %s"), name);
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

/*
 * "fspool-delete" command
 */
static const vshCmdInfo info_fspool_delete[] = {
    {.name = "help",
     .data = N_("delete a fspool")
    },
    {.name = "desc",
     .data = N_("Delete a given fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_delete[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolDelete(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    if (virFSPoolDelete(fspool, 0) == 0) {
        vshPrint(ctl, _("Pool %s deleted\n"), name);
    } else {
        vshError(ctl, _("Failed to delete fspool %s"), name);
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

/*
 * "fspool-refresh" command
 */
static const vshCmdInfo info_fspool_refresh[] = {
    {.name = "help",
     .data = N_("refresh a fspool")
    },
    {.name = "desc",
     .data = N_("Refresh a given fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_refresh[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolRefresh(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    if (virFSPoolRefresh(fspool, 0) == 0) {
        vshPrint(ctl, _("Pool %s refreshed\n"), name);
    } else {
        vshError(ctl, _("Failed to refresh fspool %s"), name);
        ret = false;
    }
    virFSPoolFree(fspool);

    return ret;
}

/*
 * "fspool-dumpxml" command
 */
static const vshCmdInfo info_fspool_dumpxml[] = {
    {.name = "help",
     .data = N_("fspool information in XML")
    },
    {.name = "desc",
     .data = N_("Output the fspool information as an XML dump to stdout.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_dumpxml[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = "inactive",
     .type = VSH_OT_BOOL,
     .help = N_("show inactive defined XML")
    },
    {.name = NULL}
};

static bool
cmdFSPoolDumpXML(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    bool inactive = vshCommandOptBool(cmd, "inactive");
    unsigned int flags = 0;
    char *dump;

    if (inactive)
        flags |= VIR_FS_XML_INACTIVE;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        return false;

    dump = virFSPoolGetXMLDesc(fspool, flags);
    if (dump != NULL) {
        vshPrint(ctl, "%s", dump);
        VIR_FREE(dump);
    } else {
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

static int
virshFSPoolSorter(const void *a, const void *b)
{
    virFSPoolPtr *pa = (virFSPoolPtr *) a;
    virFSPoolPtr *pb = (virFSPoolPtr *) b;

    if (*pa && !*pb)
        return -1;

    if (!*pa)
        return *pb != NULL;

    return vshStrcasecmp(virFSPoolGetName(*pa),
                         virFSPoolGetName(*pb));
}

struct virshFSPoolList {
    virFSPoolPtr *fspools;
    size_t nfspools;
};
typedef struct virshFSPoolList *virshFSPoolListPtr;

static void
virshFSPoolListFree(virshFSPoolListPtr list)
{
    size_t i;

    if (list && list->fspools) {
        for (i = 0; i < list->nfspools; i++) {
            if (list->fspools[i])
                virFSPoolFree(list->fspools[i]);
        }
        VIR_FREE(list->fspools);
    }
    VIR_FREE(list);
}

static virshFSPoolListPtr
virshFSPoolListCollect(vshControl *ctl,
                       unsigned int flags)
{
    virshFSPoolListPtr list = vshMalloc(ctl, sizeof(*list));
    int ret;
    virshControlPtr priv = ctl->privData;

    /* try the list with flags support (0.10.2 and later) */
    if ((ret = virConnectListAllFSPools(priv->conn,
                                        &list->fspools,
                                        flags)) < 0) {
        vshError(ctl, "%s", _("Failed to list fspools"));
        return NULL;
    }

    list->nfspools = ret;

    /* sort the list */
    if (list->fspools && list->nfspools)
        qsort(list->fspools, list->nfspools,
              sizeof(*list->fspools), virshFSPoolSorter);

    return list;
}


VIR_ENUM_DECL(virshFSPoolState)
VIR_ENUM_IMPL(virshFSPoolState,
              VIR_FSPOOL_STATE_LAST,
              N_("inactive"),
              N_("building"),
              N_("running"))

static const char *
virshFSPoolStateToString(int state)
{
    const char *str = virshFSPoolStateTypeToString(state);
    return str ? _(str) : _("unknown");
}


/*
 * "fspool-list" command
 */
static const vshCmdInfo info_fspool_list[] = {
    {.name = "help",
     .data = N_("list fspools")
    },
    {.name = "desc",
     .data = N_("Returns list of fspools.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_list[] = {
    {.name = "inactive",
     .type = VSH_OT_BOOL,
     .help = N_("list inactive fspools")
    },
    {.name = "all",
     .type = VSH_OT_BOOL,
     .help = N_("list inactive & active fspools")
    },
    {.name = "transient",
     .type = VSH_OT_BOOL,
     .help = N_("list transient fspools")
    },
    {.name = "persistent",
     .type = VSH_OT_BOOL,
     .help = N_("list persistent fspools")
    },
    {.name = "autostart",
     .type = VSH_OT_BOOL,
     .help = N_("list fspools with autostart enabled")
    },
    {.name = "no-autostart",
     .type = VSH_OT_BOOL,
     .help = N_("list fspools with autostart disabled")
    },
    {.name = "type",
     .type = VSH_OT_STRING,
     .help = N_("only list fspool of specified type(s) (if supported)")
    },
    {.name = "details",
     .type = VSH_OT_BOOL,
     .help = N_("display extended details for fspools")
    },
    {.name = NULL}
};

static bool
cmdFSPoolList(vshControl *ctl, const vshCmd *cmd ATTRIBUTE_UNUSED)
{
    virFSPoolInfo info;
    size_t i;
    bool ret = false;
    size_t stringLength = 0, nameStrLength = 0;
    size_t autostartStrLength = 0, persistStrLength = 0;
    size_t stateStrLength = 0, capStrLength = 0;
    size_t allocStrLength = 0, availStrLength = 0;
    struct fspoolInfoText {
        char *state;
        char *autostart;
        char *persistent;
        char *capacity;
        char *allocation;
        char *available;
    };
    struct fspoolInfoText *fspoolInfoTexts = NULL;
    unsigned int flags = VIR_CONNECT_LIST_FSPOOLS_ACTIVE;
    virshFSPoolListPtr list = NULL;
    const char *type = NULL;
    bool details = vshCommandOptBool(cmd, "details");
    bool inactive, all;
    char *outputStr = NULL;

    inactive = vshCommandOptBool(cmd, "inactive");
    all = vshCommandOptBool(cmd, "all");

    if (inactive)
        flags = VIR_CONNECT_LIST_FSPOOLS_INACTIVE;

    if (all)
        flags = VIR_CONNECT_LIST_FSPOOLS_ACTIVE |
                VIR_CONNECT_LIST_FSPOOLS_INACTIVE;

    if (vshCommandOptBool(cmd, "autostart"))
        flags |= VIR_CONNECT_LIST_FSPOOLS_AUTOSTART;

    if (vshCommandOptBool(cmd, "no-autostart"))
        flags |= VIR_CONNECT_LIST_FSPOOLS_NO_AUTOSTART;

    if (vshCommandOptBool(cmd, "persistent"))
        flags |= VIR_CONNECT_LIST_FSPOOLS_PERSISTENT;

    if (vshCommandOptBool(cmd, "transient"))
        flags |= VIR_CONNECT_LIST_FSPOOLS_TRANSIENT;

    if (vshCommandOptStringReq(ctl, cmd, "type", &type) < 0)
        return false;

    if (type) {
        int fspoolType = -1;
        char **fspoolTypes = NULL;
        int nfspoolTypes = 0;

        if ((nfspoolTypes = vshStringToArray(type, &fspoolTypes)) < 0)
            return false;

        for (i = 0; i < nfspoolTypes; i++) {
            if ((fspoolType = virFSPoolTypeFromString(fspoolTypes[i])) < 0) {
                vshError(ctl, _("Invalid fspool type '%s'"), fspoolTypes[i]);
                virStringFreeList(fspoolTypes);
                return false;
            }

            switch ((virFSPoolType) fspoolType) {
            case VIR_FSPOOL_DIR:
                flags |= VIR_CONNECT_LIST_FSPOOLS_DIR;
                break;
            case VIR_FSPOOL_LAST:
                break;
            }
        }
        virStringFreeList(fspoolTypes);
    }

    if (!(list = virshFSPoolListCollect(ctl, flags)))
        goto cleanup;

    fspoolInfoTexts = vshCalloc(ctl, list->nfspools, sizeof(*fspoolInfoTexts));

    /* Collect the storage fspool information for display */
    for (i = 0; i < list->nfspools; i++) {
        int autostart = 0, persistent = 0;

        /* Retrieve the autostart status of the fspool */
        if (virFSPoolGetAutostart(list->fspools[i], &autostart) < 0)
            fspoolInfoTexts[i].autostart = vshStrdup(ctl, _("no autostart"));
        else
            fspoolInfoTexts[i].autostart = vshStrdup(ctl, autostart ?
                                                     _("yes") : _("no"));

        /* Retrieve the persistence status of the fspool */
        if (details) {
            persistent = virFSPoolIsPersistent(list->fspools[i]);
            vshDebug(ctl, VSH_ERR_DEBUG, "Persistent flag value: %d\n",
                     persistent);
            if (persistent < 0)
                fspoolInfoTexts[i].persistent = vshStrdup(ctl, _("unknown"));
            else
                fspoolInfoTexts[i].persistent = vshStrdup(ctl, persistent ?
                                                         _("yes") : _("no"));

            /* Keep the length of persistent string if longest so far */
            stringLength = strlen(fspoolInfoTexts[i].persistent);
            if (stringLength > persistStrLength)
                persistStrLength = stringLength;
        }

        /* Collect further extended information about the fspool */
        if (virFSPoolGetInfo(list->fspools[i], &info) != 0) {
            /* Something went wrong retrieving fspool info, cope with it */
            vshError(ctl, "%s", _("Could not retrieve fspool information"));
            fspoolInfoTexts[i].state = vshStrdup(ctl, _("unknown"));
            if (details) {
                fspoolInfoTexts[i].capacity = vshStrdup(ctl, _("unknown"));
                fspoolInfoTexts[i].allocation = vshStrdup(ctl, _("unknown"));
                fspoolInfoTexts[i].available = vshStrdup(ctl, _("unknown"));
            }
        } else {
            /* Decide which state string to display */
            if (details) {
                const char *state = virshFSPoolStateToString(info.state);

                fspoolInfoTexts[i].state = vshStrdup(ctl, state);

                /* Create the fspool size related strings */
                if (info.state == VIR_FSPOOL_RUNNING) {
                    double val;
                    const char *unit;

                    val = vshPrettyCapacity(info.capacity, &unit);
                    if (virAsprintf(&fspoolInfoTexts[i].capacity,
                                    "%.2lf %s", val, unit) < 0)
                        goto cleanup;

                    val = vshPrettyCapacity(info.allocation, &unit);
                    if (virAsprintf(&fspoolInfoTexts[i].allocation,
                                    "%.2lf %s", val, unit) < 0)
                        goto cleanup;

                    val = vshPrettyCapacity(info.available, &unit);
                    if (virAsprintf(&fspoolInfoTexts[i].available,
                                    "%.2lf %s", val, unit) < 0)
                        goto cleanup;
                } else {
                    /* Capacity related information isn't available */
                    fspoolInfoTexts[i].capacity = vshStrdup(ctl, _("-"));
                    fspoolInfoTexts[i].allocation = vshStrdup(ctl, _("-"));
                    fspoolInfoTexts[i].available = vshStrdup(ctl, _("-"));
                }

                /* Keep the length of capacity string if longest so far */
                stringLength = strlen(fspoolInfoTexts[i].capacity);
                if (stringLength > capStrLength)
                    capStrLength = stringLength;

                /* Keep the length of allocation string if longest so far */
                stringLength = strlen(fspoolInfoTexts[i].allocation);
                if (stringLength > allocStrLength)
                    allocStrLength = stringLength;

                /* Keep the length of available string if longest so far */
                stringLength = strlen(fspoolInfoTexts[i].available);
                if (stringLength > availStrLength)
                    availStrLength = stringLength;
            } else {
                /* --details option was not specified, only active/inactive
                 * state strings are used */
                if (virFSPoolIsActive(list->fspools[i]))
                    fspoolInfoTexts[i].state = vshStrdup(ctl, _("active"));
                else
                    fspoolInfoTexts[i].state = vshStrdup(ctl, _("inactive"));
           }
        }

        /* Keep the length of name string if longest so far */
        stringLength = strlen(virFSPoolGetName(list->fspools[i]));
        if (stringLength > nameStrLength)
            nameStrLength = stringLength;

        /* Keep the length of state string if longest so far */
        stringLength = strlen(fspoolInfoTexts[i].state);
        if (stringLength > stateStrLength)
            stateStrLength = stringLength;

        /* Keep the length of autostart string if longest so far */
        stringLength = strlen(fspoolInfoTexts[i].autostart);
        if (stringLength > autostartStrLength)
            autostartStrLength = stringLength;
    }

    /* If the --details option wasn't selected, we output the fspool
     * info using the fixed string format from previous versions to
     * maintain backward compatibility.
     */

    /* Output basic info then return if --details option not selected */
    if (!details) {
        /* Output old style header */
        vshPrintExtra(ctl, " %-20s %-10s %-10s\n", _("Name"), _("State"),
                      _("Autostart"));
        vshPrintExtra(ctl, "-------------------------------------------\n");

        /* Output old style fspool info */
        for (i = 0; i < list->nfspools; i++) {
            const char *name = virFSPoolGetName(list->fspools[i]);
            vshPrint(ctl, " %-20s %-10s %-10s\n",
                 name,
                 fspoolInfoTexts[i].state,
                 fspoolInfoTexts[i].autostart);
        }

        /* Cleanup and return */
        ret = true;
        goto cleanup;
    }

    /* We only get here if the --details option was selected. */

    /* Use the length of name header string if it's longest */
    stringLength = strlen(_("Name"));
    if (stringLength > nameStrLength)
        nameStrLength = stringLength;

    /* Use the length of state header string if it's longest */
    stringLength = strlen(_("State"));
    if (stringLength > stateStrLength)
        stateStrLength = stringLength;

    /* Use the length of autostart header string if it's longest */
    stringLength = strlen(_("Autostart"));
    if (stringLength > autostartStrLength)
        autostartStrLength = stringLength;

    /* Use the length of persistent header string if it's longest */
    stringLength = strlen(_("Persistent"));
    if (stringLength > persistStrLength)
        persistStrLength = stringLength;

    /* Use the length of capacity header string if it's longest */
    stringLength = strlen(_("Capacity"));
    if (stringLength > capStrLength)
        capStrLength = stringLength;

    /* Use the length of allocation header string if it's longest */
    stringLength = strlen(_("Allocation"));
    if (stringLength > allocStrLength)
        allocStrLength = stringLength;

    /* Use the length of available header string if it's longest */
    stringLength = strlen(_("Available"));
    if (stringLength > availStrLength)
        availStrLength = stringLength;

    /* Display the string lengths for debugging. */
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest name string = %lu chars\n",
             (unsigned long) nameStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest state string = %lu chars\n",
             (unsigned long) stateStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest autostart string = %lu chars\n",
             (unsigned long) autostartStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest persistent string = %lu chars\n",
             (unsigned long) persistStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest capacity string = %lu chars\n",
             (unsigned long) capStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest allocation string = %lu chars\n",
             (unsigned long) allocStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG, "Longest available string = %lu chars\n",
             (unsigned long) availStrLength);

    /* Create the output template.  Each column is sized according to
     * the longest string.
     */
    if (virAsprintf(&outputStr,
                    " %%-%lus  %%-%lus  %%-%lus  %%-%lus  %%%lus  %%%lus  %%%lus\n",
                    (unsigned long) nameStrLength,
                    (unsigned long) stateStrLength,
                    (unsigned long) autostartStrLength,
                    (unsigned long) persistStrLength,
                    (unsigned long) capStrLength,
                    (unsigned long) allocStrLength,
                    (unsigned long) availStrLength) < 0)
        goto cleanup;

    /* Display the header */
    vshPrint(ctl, outputStr, _("Name"), _("State"), _("Autostart"),
             _("Persistent"), _("Capacity"), _("Allocation"), _("Available"));
    for (i = nameStrLength + stateStrLength + autostartStrLength
                           + persistStrLength + capStrLength
                           + allocStrLength + availStrLength
                           + 14; i > 0; i--)
        vshPrintExtra(ctl, "-");
    vshPrintExtra(ctl, "\n");

    /* Display the fspool info rows */
    for (i = 0; i < list->nfspools; i++) {
        vshPrint(ctl, outputStr,
                 virFSPoolGetName(list->fspools[i]),
                 fspoolInfoTexts[i].state,
                 fspoolInfoTexts[i].autostart,
                 fspoolInfoTexts[i].persistent,
                 fspoolInfoTexts[i].capacity,
                 fspoolInfoTexts[i].allocation,
                 fspoolInfoTexts[i].available);
    }

    /* Cleanup and return */
    ret = true;

 cleanup:
    VIR_FREE(outputStr);
    if (list && list->nfspools) {
        for (i = 0; i < list->nfspools; i++) {
            VIR_FREE(fspoolInfoTexts[i].state);
            VIR_FREE(fspoolInfoTexts[i].autostart);
            VIR_FREE(fspoolInfoTexts[i].persistent);
            VIR_FREE(fspoolInfoTexts[i].capacity);
            VIR_FREE(fspoolInfoTexts[i].allocation);
            VIR_FREE(fspoolInfoTexts[i].available);
        }
    }
    VIR_FREE(fspoolInfoTexts);

    virshFSPoolListFree(list);
    return ret;
}

/*
 * "fspool-info" command
 */
static const vshCmdInfo info_fspool_info[] = {
    {.name = "help",
     .data = N_("storage fspool information")
    },
    {.name = "desc",
     .data = N_("Returns basic information about the storage fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_info[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolInfo(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolInfo info;
    virFSPoolPtr fspool;
    int autostart = 0;
    int persistent = 0;
    bool ret = true;
    char uuid[VIR_UUID_STRING_BUFLEN];

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        return false;

    vshPrint(ctl, "%-15s %s\n", _("Name:"), virFSPoolGetName(fspool));

    if (virFSPoolGetUUIDString(fspool, &uuid[0]) == 0)
        vshPrint(ctl, "%-15s %s\n", _("UUID:"), uuid);

    if (virFSPoolGetInfo(fspool, &info) == 0) {
        double val;
        const char *unit;
        vshPrint(ctl, "%-15s %s\n", _("State:"),
                 virshFSPoolStateToString(info.state));

        /* Check and display whether the fspool is persistent or not */
        persistent = virFSPoolIsPersistent(fspool);
        vshDebug(ctl, VSH_ERR_DEBUG, "Pool persistent flag value: %d\n",
                 persistent);
        if (persistent < 0)
            vshPrint(ctl, "%-15s %s\n", _("Persistent:"),  _("unknown"));
        else
            vshPrint(ctl, "%-15s %s\n", _("Persistent:"), persistent ? _("yes") : _("no"));

        /* Check and display whether the fspool is autostarted or not */
        if (virFSPoolGetAutostart(fspool, &autostart) < 0)
            vshPrint(ctl, "%-15s %s\n", _("Autostart:"), _("no autostart"));
        else
            vshPrint(ctl, "%-15s %s\n", _("Autostart:"), autostart ? _("yes") : _("no"));

        if (info.state == VIR_FSPOOL_RUNNING) {
            val = vshPrettyCapacity(info.capacity, &unit);
            vshPrint(ctl, "%-15s %2.2lf %s\n", _("Capacity:"), val, unit);

            val = vshPrettyCapacity(info.allocation, &unit);
            vshPrint(ctl, "%-15s %2.2lf %s\n", _("Allocation:"), val, unit);

            val = vshPrettyCapacity(info.available, &unit);
            vshPrint(ctl, "%-15s %2.2lf %s\n", _("Available:"), val, unit);
        }
    } else {
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

/*
 * "fspool-name" command
 */
static const vshCmdInfo info_fspool_name[] = {
    {.name = "help",
     .data = N_("convert a fspool UUID to fspool name")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_name[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolName(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;

    if (!(fspool = virshCommandOptFSPoolBy(ctl, cmd, "fspool", NULL, VIRSH_BYUUID)))
        return false;

    vshPrint(ctl, "%s\n", virFSPoolGetName(fspool));
    virFSPoolFree(fspool);
    return true;
}

/*
 * "fspool-start" command
 */
static const vshCmdInfo info_fspool_start[] = {
    {.name = "help",
     .data = N_("start a (previously defined) inactive fspool")
    },
    {.name = "desc",
     .data = N_("Start a fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_start[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,
    VIRSH_COMMON_OPT_FSPOOL_BUILD,
    VIRSH_COMMON_OPT_FSPOOL_NO_OVERWRITE,
    VIRSH_COMMON_OPT_FSPOOL_OVERWRITE,

    {.name = NULL}
};

static bool
cmdFSPoolStart(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name = NULL;
    bool build;
    bool overwrite;
    bool no_overwrite;
    unsigned int flags = 0;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
         return false;

    build = vshCommandOptBool(cmd, "build");
    overwrite = vshCommandOptBool(cmd, "overwrite");
    no_overwrite = vshCommandOptBool(cmd, "no-overwrite");

    VSH_EXCLUSIVE_OPTIONS_EXPR("overwrite", overwrite,
                               "no-overwrite", no_overwrite);

    if (build)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD;
    if (overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_OVERWRITE;
    if (no_overwrite)
        flags |= VIR_FSPOOL_CREATE_WITH_BUILD_NO_OVERWRITE;

    if (virFSPoolCreate(fspool, flags) == 0) {
        vshPrint(ctl, _("FSpool %s started\n"), name);
    } else {
        vshError(ctl, _("Failed to start fspool %s"), name);
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

/*
 * "fspool-undefine" command
 */
static const vshCmdInfo info_fspool_undefine[] = {
    {.name = "help",
     .data = N_("undefine an inactive fspool")
    },
    {.name = "desc",
     .data = N_("Undefine the configuration for an inactive fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_undefine[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolUndefine(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    bool ret = true;
    const char *name;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", &name)))
        return false;

    if (virFSPoolUndefine(fspool) == 0) {
        vshPrint(ctl, _("Pool %s has been undefined\n"), name);
    } else {
        vshError(ctl, _("Failed to undefine fspool %s"), name);
        ret = false;
    }

    virFSPoolFree(fspool);
    return ret;
}

/*
 * "fspool-uuid" command
 */
static const vshCmdInfo info_fspool_uuid[] = {
    {.name = "help",
     .data = N_("convert a fspool name to fspool UUID")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_uuid[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolUuid(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    char uuid[VIR_UUID_STRING_BUFLEN];

    if (!(fspool = virshCommandOptFSPoolBy(ctl, cmd, "fspool", NULL, VIRSH_BYNAME)))
        return false;

    if (virFSPoolGetUUIDString(fspool, uuid) != -1)
        vshPrint(ctl, "%s\n", uuid);
    else
        vshError(ctl, "%s", _("failed to get fspool UUID"));

    virFSPoolFree(fspool);
    return true;
}

/*
 * "fspool-edit" command
 */
static const vshCmdInfo info_fspool_edit[] = {
    {.name = "help",
     .data = N_("edit XML configuration for a fspool")
    },
    {.name = "desc",
     .data = N_("Edit the XML configuration for a fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_fspool_edit[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,

    {.name = NULL}
};

static bool
cmdFSPoolEdit(vshControl *ctl, const vshCmd *cmd)
{
    bool ret = false;
    virFSPoolPtr fspool = NULL;
    virFSPoolPtr fspool_edited = NULL;
    unsigned int flags = VIR_FS_XML_INACTIVE;
    char *tmp_desc = NULL;
    virshControlPtr priv = ctl->privData;

    fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL);
    if (fspool == NULL)
        goto cleanup;

    /* Some old daemons don't support _INACTIVE flag */
    if (!(tmp_desc = virFSPoolGetXMLDesc(fspool, flags))) {
        if (last_error->code == VIR_ERR_INVALID_ARG) {
            flags &= ~VIR_FS_XML_INACTIVE;
            vshResetLibvirtError();
        } else {
            goto cleanup;
        }
    } else {
        VIR_FREE(tmp_desc);
    }

#define EDIT_GET_XML virFSPoolGetXMLDesc(fspool, flags)
#define EDIT_NOT_CHANGED                                                \
    do {                                                                \
        vshPrint(ctl, _("Pool %s XML configuration not changed.\n"),    \
                 virFSPoolGetName(fspool));                          \
        ret = true;                                                     \
        goto edit_cleanup;                                              \
    } while (0)
#define EDIT_DEFINE \
    (fspool_edited = virFSPoolDefineXML(priv->conn, doc_edited, 0))
#include "virsh-edit.c"

    vshPrint(ctl, _("Pool %s XML configuration edited.\n"),
             virFSPoolGetName(fspool_edited));

    ret = true;

 cleanup:
    if (fspool)
        virFSPoolFree(fspool);
    if (fspool_edited)
        virFSPoolFree(fspool_edited);

    return ret;
}

const vshCmdDef fsPoolCmds[] = {
    {.name = "fspool-autostart",
     .handler = cmdFSPoolAutostart,
     .opts = opts_fspool_autostart,
     .info = info_fspool_autostart,
     .flags = 0
    },
    {.name = "fspool-build",
     .handler = cmdFSPoolBuild,
     .opts = opts_fspool_build,
     .info = info_fspool_build,
     .flags = 0
    },
    {.name = "fspool-create-as",
     .handler = cmdFSPoolCreateAs,
     .opts = opts_fspool_create_as,
     .info = info_fspool_create_as,
     .flags = 0
    },
    {.name = "fspool-create",
     .handler = cmdFSPoolCreate,
     .opts = opts_fspool_create,
     .info = info_fspool_create,
     .flags = 0
    },
    {.name = "fspool-define-as",
     .handler = cmdFSPoolDefineAs,
     .opts = opts_fspool_define_as,
     .info = info_fspool_define_as,
     .flags = 0
    },
    {.name = "fspool-define",
     .handler = cmdFSPoolDefine,
     .opts = opts_fspool_define,
     .info = info_fspool_define,
     .flags = 0
    },
    {.name = "fspool-delete",
     .handler = cmdFSPoolDelete,
     .opts = opts_fspool_delete,
     .info = info_fspool_delete,
     .flags = 0
    },
    {.name = "fspool-destroy",
     .handler = cmdFSPoolDestroy,
     .opts = opts_fspool_destroy,
     .info = info_fspool_destroy,
     .flags = 0
    },
    {.name = "fspool-dumpxml",
     .handler = cmdFSPoolDumpXML,
     .opts = opts_fspool_dumpxml,
     .info = info_fspool_dumpxml,
     .flags = 0
    },
    {.name = "fspool-edit",
     .handler = cmdFSPoolEdit,
     .opts = opts_fspool_edit,
     .info = info_fspool_edit,
     .flags = 0
    },
    {.name = "fspool-info",
     .handler = cmdFSPoolInfo,
     .opts = opts_fspool_info,
     .info = info_fspool_info,
     .flags = 0
    },
    {.name = "fspool-list",
     .handler = cmdFSPoolList,
     .opts = opts_fspool_list,
     .info = info_fspool_list,
     .flags = 0
    },
    {.name = "fspool-name",
     .handler = cmdFSPoolName,
     .opts = opts_fspool_name,
     .info = info_fspool_name,
     .flags = 0
    },
    {.name = "fspool-refresh",
     .handler = cmdFSPoolRefresh,
     .opts = opts_fspool_refresh,
     .info = info_fspool_refresh,
     .flags = 0
    },
    {.name = "fspool-undefine",
     .handler = cmdFSPoolUndefine,
     .opts = opts_fspool_undefine,
     .info = info_fspool_undefine,
     .flags = 0
    },
    {.name = "fspool-uuid",
     .handler = cmdFSPoolUuid,
     .opts = opts_fspool_uuid,
     .info = info_fspool_uuid,
     .flags = 0
    },
    {.name = "fspool-start",
     .handler = cmdFSPoolStart,
     .opts = opts_fspool_start,
     .info = info_fspool_start,
     .flags = 0
    },
    {.name = NULL}
};
