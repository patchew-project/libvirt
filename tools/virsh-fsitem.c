/*
 * virsh-fsitem.c: Commands to manage storage item
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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
#include "virsh-fsitem.h"

#include <fcntl.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xmlsave.h>

#include "internal.h"
#include "virbuffer.h"
#include "viralloc.h"
#include "virutil.h"
#include "virfile.h"
#include "virsh-fspool.h"
#include "virxml.h"
#include "virstring.h"

#define VIRSH_COMMON_OPT_FSPOOL_FULL                            \
    VIRSH_COMMON_OPT_FSPOOL(N_("fspool name or uuid"))            \

#define VIRSH_COMMON_OPT_FSPOOL_NAME                            \
    VIRSH_COMMON_OPT_FSPOOL(N_("fspool name"))                    \

#define VIRSH_COMMON_OPT_FSPOOL_OPTIONAL                        \
    {.name = "fspool",                                          \
     .type = VSH_OT_STRING,                                   \
     .help = N_("fspool name or uuid")                          \
    }                                                         \

#define VIRSH_COMMON_OPT_ITEM                           \
    {.name = "item",                                           \
     .type = VSH_OT_DATA,                                     \
     .flags = VSH_OFLAG_REQ,                                  \
     .help = N_("item name, key or path")                      \
    }                                                         \

virFSItemPtr
virshCommandOptItemBy(vshControl *ctl, const vshCmd *cmd,
                      const char *optname,
                      const char *fspooloptname,
                      const char **name, unsigned int flags)
{
    virFSItemPtr item = NULL;
    virFSPoolPtr fspool = NULL;
    const char *n = NULL, *p = NULL;
    virshControlPtr priv = ctl->privData;

    virCheckFlags(VIRSH_BYUUID | VIRSH_BYNAME, NULL);

    if (vshCommandOptStringReq(ctl, cmd, optname, &n) < 0)
        return NULL;

    if (fspooloptname != NULL &&
        vshCommandOptStringReq(ctl, cmd, fspooloptname, &p) < 0)
        return NULL;

    if (p) {
        if (!(fspool = virshCommandOptFSPoolBy(ctl, cmd, fspooloptname, name, flags)))
            return NULL;

        if (virFSPoolIsActive(fspool) != 1) {
            vshError(ctl, _("fspool '%s' is not active"), p);
            virFSPoolFree(fspool);
            return NULL;
        }
    }

    vshDebug(ctl, VSH_ERR_DEBUG, "%s: found option <%s>: %s\n",
             cmd->def->name, optname, n);

    if (name)
        *name = n;

    /* try it by name */
    if (fspool && (flags & VIRSH_BYNAME)) {
        vshDebug(ctl, VSH_ERR_DEBUG, "%s: <%s> trying as item name\n",
                 cmd->def->name, optname);
        item = virFSItemLookupByName(fspool, n);
    }
    /* try it by key */
    if (!item && (flags & VIRSH_BYUUID)) {
        vshDebug(ctl, VSH_ERR_DEBUG, "%s: <%s> trying as item key\n",
                 cmd->def->name, optname);
        item = virFSItemLookupByKey(priv->conn, n);
    }
    /* try it by path */
    if (!item && (flags & VIRSH_BYUUID)) {
        vshDebug(ctl, VSH_ERR_DEBUG, "%s: <%s> trying as item path\n",
                 cmd->def->name, optname);
        item = virFSItemLookupByPath(priv->conn, n);
    }

    if (!item) {
        if (fspool || !fspooloptname)
            vshError(ctl, _("failed to get item '%s'"), n);
        else
            vshError(ctl, _("failed to get item '%s', specifying --%s "
                            "might help"), n, fspooloptname);
    }

    /* If the fspool was specified, then make sure that the returned
     * item is from the given fspool */
    if (fspool && item) {
        virFSPoolPtr itemfspool = NULL;

        if ((itemfspool = virFSPoolLookupByItem(item))) {
            if (STRNEQ(virFSPoolGetName(itemfspool),
                       virFSPoolGetName(fspool))) {
                vshResetLibvirtError();
                vshError(ctl,
                         _("Requested item '%s' is not in fspool '%s'"),
                         n, virFSPoolGetName(fspool));
                virFSItemFree(item);
                item = NULL;
            }
            virFSPoolFree(itemfspool);
        }
    }

    if (fspool)
        virFSPoolFree(fspool);

    return item;
}

/*
 * "item-create-as" command
 */
static const vshCmdInfo info_item_create_as[] = {
    {.name = "help",
     .data = N_("create a item from a set of args")
    },
    {.name = "desc",
     .data = N_("Create a item.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_create_as[] = {
    VIRSH_COMMON_OPT_FSPOOL_NAME,
    {.name = "name",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("name of the item")
    },
    {.name = "capacity",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("size of the item, as scaled integer (default bytes)")
    },
    {.name = "allocation",
     .type = VSH_OT_STRING,
     .help = N_("initial allocation size, as scaled integer (default bytes)")
    },
    {.name = "format",
     .type = VSH_OT_STRING,
     .help = N_("file format type raw,bochs,qcow,qcow2,qed,vmdk")
    },
    {.name = "print-xml",
     .type = VSH_OT_BOOL,
     .help = N_("print XML document, but don't define/create")
    },
    {.name = NULL}
};

static int
virshItemSize(const char *data, unsigned long long *val)
{
    char *end;
    if (virStrToLong_ull(data, &end, 10, val) < 0)
        return -1;
    return virScaleInteger(val, end, 1, ULLONG_MAX);
}

static bool
cmdItemCreateAs(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    virFSItemPtr item = NULL;
    char *xml = NULL;
    bool printXML = vshCommandOptBool(cmd, "print-xml");
    const char *name, *capacityStr = NULL, *allocationStr = NULL, *format = NULL;
    unsigned long long capacity, allocation = 0;
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    unsigned long flags = 0;
    bool ret = false;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        return false;

    if (vshCommandOptStringReq(ctl, cmd, "name", &name) < 0)
        goto cleanup;

    if (vshCommandOptStringReq(ctl, cmd, "capacity", &capacityStr) < 0)
        goto cleanup;

    if (virshItemSize(capacityStr, &capacity) < 0) {
        vshError(ctl, _("Malformed size %s"), capacityStr);
        goto cleanup;
    }

    if (vshCommandOptStringQuiet(ctl, cmd, "allocation", &allocationStr) > 0 &&
        virshItemSize(allocationStr, &allocation) < 0) {
        vshError(ctl, _("Malformed size %s"), allocationStr);
        goto cleanup;
    }

    if (vshCommandOptStringReq(ctl, cmd, "format", &format))
        goto cleanup;

    virBufferAddLit(&buf, "<item>\n");
    virBufferAdjustIndent(&buf, 2);
    virBufferAsprintf(&buf, "<name>%s</name>\n", name);
    virBufferAsprintf(&buf, "<capacity>%llu</capacity>\n", capacity);
    if (allocationStr)
        virBufferAsprintf(&buf, "<allocation>%llu</allocation>\n", allocation);

    if (format) {
        virBufferAddLit(&buf, "<target>\n");
        virBufferAdjustIndent(&buf, 2);
        virBufferAsprintf(&buf, "<format type='%s'/>\n", format);
        virBufferAdjustIndent(&buf, -2);
        virBufferAddLit(&buf, "</target>\n");
    }

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</item>\n");

    if (virBufferError(&buf)) {
        vshPrint(ctl, "%s", _("Failed to allocate XML buffer"));
        goto cleanup;
    }
    xml = virBufferContentAndReset(&buf);

    if (printXML) {
        vshPrint(ctl, "%s", xml);
    } else {
        if (!(item = virFSItemCreateXML(fspool, xml, flags))) {
            vshError(ctl, _("Failed to create item %s"), name);
            goto cleanup;
        }
        vshPrint(ctl, _("Item %s created\n"), name);
    }

    ret = true;

 cleanup:
    virBufferFreeAndReset(&buf);
    if (item)
        virFSItemFree(item);
    virFSPoolFree(fspool);
    VIR_FREE(xml);
    return ret;
}

/*
 * "item-create" command
 */
static const vshCmdInfo info_item_create[] = {
    {.name = "help",
     .data = N_("create a item from an XML file")
    },
    {.name = "desc",
     .data = N_("Create a item.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_create[] = {
    VIRSH_COMMON_OPT_FSPOOL_NAME,
    VIRSH_COMMON_OPT_FILE(N_("file containing an XML item description")),
    {.name = NULL}
};

static bool
cmdItemCreate(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    virFSItemPtr item;
    const char *from = NULL;
    bool ret = false;
    unsigned int flags = 0;
    char *buffer = NULL;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        return false;

    if (vshCommandOptStringReq(ctl, cmd, "file", &from) < 0)
        goto cleanup;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0) {
        vshSaveLibvirtError();
        goto cleanup;
    }

    if ((item = virFSItemCreateXML(fspool, buffer, flags))) {
        vshPrint(ctl, _("Item %s created from %s\n"),
                 virFSItemGetName(item), from);
        virFSItemFree(item);
        ret = true;
    } else {
        vshError(ctl, _("Failed to create item from %s"), from);
    }

 cleanup:
    VIR_FREE(buffer);
    virFSPoolFree(fspool);
    return ret;
}

/*
 * "item-create-from" command
 */
static const vshCmdInfo info_item_create_from[] = {
    {.name = "help",
     .data = N_("create a item, using another item as input")
    },
    {.name = "desc",
     .data = N_("Create a item from an existing item.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_create_from[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,
    VIRSH_COMMON_OPT_FILE(N_("file containing an XML item description")),
    VIRSH_COMMON_OPT_ITEM,
    {.name = "inputfspool",
     .type = VSH_OT_STRING,
     .help = N_("fspool name or uuid of the input item's fspool")
    },
    {.name = NULL}
};

static bool
cmdItemCreateFrom(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool = NULL;
    virFSItemPtr newitem = NULL, inputitem = NULL;
    const char *from = NULL;
    bool ret = false;
    char *buffer = NULL;
    unsigned int flags = 0;

    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        goto cleanup;

    if (vshCommandOptStringReq(ctl, cmd, "file", &from) < 0)
        goto cleanup;

    if (!(inputitem = virshCommandOptItem(ctl, cmd, "item", "inputfspool", NULL)))
        goto cleanup;

    if (virFileReadAll(from, VSH_MAX_XML_FILE, &buffer) < 0) {
        vshReportError(ctl);
        goto cleanup;
    }

    newitem = virFSItemCreateXMLFrom(fspool, buffer, inputitem, flags);

    if (newitem != NULL) {
        vshPrint(ctl, _("Item %s created from input item %s\n"),
                 virFSItemGetName(newitem), virFSItemGetName(inputitem));
    } else {
        vshError(ctl, _("Failed to create item from %s"), from);
        goto cleanup;
    }

    ret = true;
 cleanup:
    VIR_FREE(buffer);
    if (fspool)
        virFSPoolFree(fspool);
    if (inputitem)
        virFSItemFree(inputitem);
    if (newitem)
        virFSItemFree(newitem);
    return ret;
}

static xmlChar *
virshMakeCloneXML(const char *origxml, const char *newname)
{

    xmlDocPtr doc = NULL;
    xmlXPathContextPtr ctxt = NULL;
    xmlXPathObjectPtr obj = NULL;
    xmlChar *newxml = NULL;
    int size;

    doc = virXMLParseStringCtxt(origxml, _("(item_definition)"), &ctxt);
    if (!doc)
        goto cleanup;

    obj = xmlXPathEval(BAD_CAST "/item/name", ctxt);
    if (obj == NULL || obj->nodesetval == NULL ||
        obj->nodesetval->nodeTab == NULL)
        goto cleanup;

    xmlNodeSetContent(obj->nodesetval->nodeTab[0], (const xmlChar *)newname);
    xmlDocDumpMemory(doc, &newxml, &size);

 cleanup:
    xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctxt);
    xmlFreeDoc(doc);
    return newxml;
}

/*
 * "item-clone" command
 */
static const vshCmdInfo info_item_clone[] = {
    {.name = "help",
     .data = N_("clone a item.")
    },
    {.name = "desc",
     .data = N_("Clone an existing item within the parent fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_clone[] = {
    VIRSH_COMMON_OPT_ITEM,
    {.name = "newname",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("clone name")
    },
    {.name = NULL}
};

static bool
cmdItemClone(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr origfspool = NULL;
    virFSItemPtr origitem = NULL, newitem = NULL;
    const char *name = NULL;
    char *origxml = NULL;
    xmlChar *newxml = NULL;
    bool ret = false;
    unsigned int flags = 0;

    if (!(origitem = virshCommandOptItem(ctl, cmd, "item", "fspool", NULL)))
        goto cleanup;

    origfspool = virFSPoolLookupByItem(origitem);
    if (!origfspool) {
        vshError(ctl, "%s", _("failed to get parent fspool"));
        goto cleanup;
    }

    if (vshCommandOptStringReq(ctl, cmd, "newname", &name) < 0)
        goto cleanup;

    origxml = virFSItemGetXMLDesc(origitem, 0);
    if (!origxml)
        goto cleanup;

    newxml = virshMakeCloneXML(origxml, name);
    if (!newxml) {
        vshPrint(ctl, "%s", _("Failed to allocate XML buffer"));
        goto cleanup;
    }

    newitem = virFSItemCreateXMLFrom(origfspool, (char *) newxml, origitem, flags);

    if (newitem != NULL) {
        vshPrint(ctl, _("Item %s cloned from %s\n"),
                 virFSItemGetName(newitem), virFSItemGetName(origitem));
    } else {
        vshError(ctl, _("Failed to clone item from %s"),
                 virFSItemGetName(origitem));
        goto cleanup;
    }

    ret = true;

 cleanup:
    VIR_FREE(origxml);
    xmlFree(newxml);
    if (origitem)
        virFSItemFree(origitem);
    if (newitem)
        virFSItemFree(newitem);
    if (origfspool)
        virFSPoolFree(origfspool);
    return ret;
}

/*
 * "item-delete" command
 */
static const vshCmdInfo info_item_delete[] = {
    {.name = "help",
     .data = N_("delete a item")
    },
    {.name = "desc",
     .data = N_("Delete a given item.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_delete[] = {
    VIRSH_COMMON_OPT_ITEM,
    VIRSH_COMMON_OPT_FSPOOL_OPTIONAL,
    {.name = NULL}
};

static bool
cmdItemDelete(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemPtr item;
    bool ret = true;
    const char *name;
    unsigned int flags = 0;

    if (!(item = virshCommandOptItem(ctl, cmd, "item", "fspool", &name)))
        return false;

    if (virFSItemDelete(item, flags) == 0) {
        vshPrint(ctl, _("Item %s deleted\n"), name);
    } else {
        vshError(ctl, _("Failed to delete item %s"), name);
        ret = false;
    }

    virFSItemFree(item);
    return ret;
}

VIR_ENUM_DECL(virshFSItem)
VIR_ENUM_IMPL(virshFSItem,
              VIR_FSITEM_LAST,
              N_("dir"))

static const char *
virshItemTypeToString(int type)
{
    const char *str = virshFSItemTypeToString(type);
    return str ? _(str) : _("unknown");
}


/*
 * "item-info" command
 */
static const vshCmdInfo info_item_info[] = {
    {.name = "help",
     .data = N_("storage item information")
    },
    {.name = "desc",
     .data = N_("Returns basic information about the storage item.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_info[] = {
    VIRSH_COMMON_OPT_ITEM,
    VIRSH_COMMON_OPT_FSPOOL_OPTIONAL,
    {.name = "bytes",
     .type = VSH_OT_BOOL,
     .help = N_("sizes are represented in bytes rather than pretty units")
    },
    {.name = NULL}
};

static bool
cmdItemInfo(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemInfo info;
    virFSItemPtr item;
    bool bytes = vshCommandOptBool(cmd, "bytes");
    bool ret = true;

    if (!(item = virshCommandOptItem(ctl, cmd, "item", "fspool", NULL)))
        return false;

    vshPrint(ctl, "%-15s %s\n", _("Name:"), virFSItemGetName(item));

    if (virFSItemGetInfo(item, &info) == 0) {
        double val;
        const char *unit;

        vshPrint(ctl, "%-15s %s\n", _("Type:"),
                 virshItemTypeToString(info.type));

        if (bytes) {
            vshPrint(ctl, "%-15s %llu %s\n", _("Capacity:"),
                     info.capacity, _("bytes"));
        } else {
            val = vshPrettyCapacity(info.capacity, &unit);
            vshPrint(ctl, "%-15s %2.2lf %s\n", _("Capacity:"), val, unit);
        }

        if (bytes) {
            vshPrint(ctl, "%-15s %llu %s\n", _("Allocation:"),
                     info.allocation, _("bytes"));
         } else {
            val = vshPrettyCapacity(info.allocation, &unit);
            vshPrint(ctl, "%-15s %2.2lf %s\n", _("Allocation:"), val, unit);
         }
    } else {
        ret = false;
    }

    virFSItemFree(item);
    return ret;
}

/*
 * "item-dumpxml" command
 */
static const vshCmdInfo info_item_dumpxml[] = {
    {.name = "help",
     .data = N_("item information in XML")
    },
    {.name = "desc",
     .data = N_("Output the item information as an XML dump to stdout.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_dumpxml[] = {
    VIRSH_COMMON_OPT_ITEM,
    VIRSH_COMMON_OPT_FSPOOL_OPTIONAL,
    {.name = NULL}
};

static bool
cmdItemDumpXML(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemPtr item;
    bool ret = true;
    char *dump;

    if (!(item = virshCommandOptItem(ctl, cmd, "item", "fspool", NULL)))
        return false;

    dump = virFSItemGetXMLDesc(item, 0);
    if (dump != NULL) {
        vshPrint(ctl, "%s", dump);
        VIR_FREE(dump);
    } else {
        ret = false;
    }

    virFSItemFree(item);
    return ret;
}

static int
virshFSItemSorter(const void *a, const void *b)
{
    virFSItemPtr *va = (virFSItemPtr *) a;
    virFSItemPtr *vb = (virFSItemPtr *) b;

    if (*va && !*vb)
        return -1;

    if (!*va)
        return *vb != NULL;

    return vshStrcasecmp(virFSItemGetName(*va),
                      virFSItemGetName(*vb));
}

struct virshFSItemList {
    virFSItemPtr *items;
    size_t nitems;
};
typedef struct virshFSItemList *virshFSItemListPtr;

static void
virshFSItemListFree(virshFSItemListPtr list)
{
    size_t i;

    if (list && list->items) {
        for (i = 0; i < list->nitems; i++) {
            if (list->items[i])
                virFSItemFree(list->items[i]);
        }
        VIR_FREE(list->items);
    }
    VIR_FREE(list);
}

static virshFSItemListPtr
virshFSItemListCollect(vshControl *ctl,
                           virFSPoolPtr fspool,
                           unsigned int flags)
{
    virshFSItemListPtr list = vshMalloc(ctl, sizeof(*list));
    size_t i;
    char **names = NULL;
    virFSItemPtr item = NULL;
    bool success = false;
    size_t deleted = 0;
    int nitems = 0;
    int ret = -1;

    /* try the list with flags support (0.10.2 and later) */
    if ((ret = virFSPoolListAllItems(fspool,
                                            &list->items,
                                            flags)) >= 0) {
        list->nitems = ret;
        goto finished;
    }

    /* check if the command is actually supported */
    if (last_error && last_error->code == VIR_ERR_NO_SUPPORT)
        goto fallback;

    /* there was an error during the call */
    vshError(ctl, "%s", _("Failed to list items"));
    goto cleanup;

 fallback:
    /* fall back to old method (0.10.1 and older) */
    vshResetLibvirtError();

    /* Determine the number of items in the fspool */
    if ((nitems = virFSPoolNumOfItems(fspool)) < 0) {
        vshError(ctl, "%s", _("Failed to list storage items"));
        goto cleanup;
    }

    if (nitems == 0) {
        success = true;
        return list;
    }

    /* Retrieve the list of item names in the fspool */
    names = vshCalloc(ctl, nitems, sizeof(*names));
    if ((nitems = virFSPoolListItems(fspool, names, nitems)) < 0) {
        vshError(ctl, "%s", _("Failed to list storage items"));
        goto cleanup;
    }

    list->items = vshMalloc(ctl, sizeof(virFSItemPtr) * (nitems));
    list->nitems = 0;

    /* get the items */
    for (i = 0; i < nitems; i++) {
        if (!(item = virFSItemLookupByName(fspool, names[i])))
            continue;
        list->items[list->nitems++] = item;
    }

    /* truncate the list for not found items */
    deleted = nitems - list->nitems;

 finished:
    /* sort the list */
    if (list->items && list->nitems)
        qsort(list->items, list->nitems, sizeof(*list->items), virshFSItemSorter);

    if (deleted)
        VIR_SHRINK_N(list->items, list->nitems, deleted);

    success = true;

 cleanup:
    if (nitems > 0)
        for (i = 0; i < nitems; i++)
            VIR_FREE(names[i]);
    VIR_FREE(names);

    if (!success) {
        virshFSItemListFree(list);
        list = NULL;
    }

    return list;
}

/*
 * "item-list" command
 */
static const vshCmdInfo info_item_list[] = {
    {.name = "help",
     .data = N_("list items")
    },
    {.name = "desc",
     .data = N_("Returns list of items by fspool.")
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_list[] = {
    VIRSH_COMMON_OPT_FSPOOL_FULL,
    {.name = "details",
     .type = VSH_OT_BOOL,
     .help = N_("display extended details for items")
    },
    {.name = NULL}
};

static bool
cmdItemList(vshControl *ctl, const vshCmd *cmd ATTRIBUTE_UNUSED)
{
    virFSItemInfo itemInfo;
    virFSPoolPtr fspool;
    char *outputStr = NULL;
    const char *unit;
    double val;
    bool details = vshCommandOptBool(cmd, "details");
    size_t i;
    bool ret = false;
    int stringLength = 0;
    size_t allocStrLength = 0, capStrLength = 0;
    size_t nameStrLength = 0, pathStrLength = 0;
    size_t typeStrLength = 0;
    struct itemInfoText {
        char *allocation;
        char *capacity;
        char *path;
        char *type;
    };
    struct itemInfoText *itemInfoTexts = NULL;
    virshFSItemListPtr list = NULL;

    /* Look up the fspool information given to us by the user */
    if (!(fspool = virshCommandOptFSPool(ctl, cmd, "fspool", NULL)))
        return false;

    if (!(list = virshFSItemListCollect(ctl, fspool, 0)))
        goto cleanup;

    if (list->nitems > 0)
        itemInfoTexts = vshCalloc(ctl, list->nitems, sizeof(*itemInfoTexts));

    /* Collect the rest of the item information for display */
    for (i = 0; i < list->nitems; i++) {
        /* Retrieve item info */
        virFSItemPtr item = list->items[i];

        /* Retrieve the item path */
        if ((itemInfoTexts[i].path = virFSItemGetPath(item)) == NULL) {
            /* Something went wrong retrieving a item path, cope with it */
            itemInfoTexts[i].path = vshStrdup(ctl, _("unknown"));
        }

        /* If requested, retrieve item type and sizing information */
        if (details) {
            if (virFSItemGetInfo(item, &itemInfo) != 0) {
                /* Something went wrong retrieving item info, cope with it */
                itemInfoTexts[i].allocation = vshStrdup(ctl, _("unknown"));
                itemInfoTexts[i].capacity = vshStrdup(ctl, _("unknown"));
                itemInfoTexts[i].type = vshStrdup(ctl, _("unknown"));
            } else {
                /* Convert the returned item info into output strings */

                /* Item type */
                itemInfoTexts[i].type = vshStrdup(ctl,
                                                 virshItemTypeToString(itemInfo.type));

                val = vshPrettyCapacity(itemInfo.capacity, &unit);
                if (virAsprintf(&itemInfoTexts[i].capacity,
                                "%.2lf %s", val, unit) < 0)
                    goto cleanup;

                val = vshPrettyCapacity(itemInfo.allocation, &unit);
                if (virAsprintf(&itemInfoTexts[i].allocation,
                                "%.2lf %s", val, unit) < 0)
                    goto cleanup;
            }

            /* Remember the largest length for each output string.
             * This lets us displaying header and item information rows
             * using a single, properly sized, printf style output string.
             */

            /* Keep the length of name string if longest so far */
            stringLength = strlen(virFSItemGetName(list->items[i]));
            if (stringLength > nameStrLength)
                nameStrLength = stringLength;

            /* Keep the length of path string if longest so far */
            stringLength = strlen(itemInfoTexts[i].path);
            if (stringLength > pathStrLength)
                pathStrLength = stringLength;

            /* Keep the length of type string if longest so far */
            stringLength = strlen(itemInfoTexts[i].type);
            if (stringLength > typeStrLength)
                typeStrLength = stringLength;

            /* Keep the length of capacity string if longest so far */
            stringLength = strlen(itemInfoTexts[i].capacity);
            if (stringLength > capStrLength)
                capStrLength = stringLength;

            /* Keep the length of allocation string if longest so far */
            stringLength = strlen(itemInfoTexts[i].allocation);
            if (stringLength > allocStrLength)
                allocStrLength = stringLength;
        }
    }

    /* If the --details option wasn't selected, we output the item
     * info using the fixed string format from previous versions to
     * maintain backward compatibility.
     */

    /* Output basic info then return if --details option not selected */
    if (!details) {
        /* The old output format */
        vshPrintExtra(ctl, " %-20s %-40s\n", _("Name"), _("Path"));
        vshPrintExtra(ctl, "---------------------------------------"
                           "---------------------------------------\n");
        for (i = 0; i < list->nitems; i++) {
            vshPrint(ctl, " %-20s %-40s\n", virFSItemGetName(list->items[i]),
                     itemInfoTexts[i].path);
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

    /* Use the length of path header string if it's longest */
    stringLength = strlen(_("Path"));
    if (stringLength > pathStrLength)
        pathStrLength = stringLength;

    /* Use the length of type header string if it's longest */
    stringLength = strlen(_("Type"));
    if (stringLength > typeStrLength)
        typeStrLength = stringLength;

    /* Use the length of capacity header string if it's longest */
    stringLength = strlen(_("Capacity"));
    if (stringLength > capStrLength)
        capStrLength = stringLength;

    /* Use the length of allocation header string if it's longest */
    stringLength = strlen(_("Allocation"));
    if (stringLength > allocStrLength)
        allocStrLength = stringLength;

    /* Display the string lengths for debugging */
    vshDebug(ctl, VSH_ERR_DEBUG,
             "Longest name string = %zu chars\n", nameStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG,
             "Longest path string = %zu chars\n", pathStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG,
             "Longest type string = %zu chars\n", typeStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG,
             "Longest capacity string = %zu chars\n", capStrLength);
    vshDebug(ctl, VSH_ERR_DEBUG,
             "Longest allocation string = %zu chars\n", allocStrLength);

    if (virAsprintf(&outputStr,
                    " %%-%lus  %%-%lus  %%-%lus  %%%lus  %%%lus\n",
                    (unsigned long) nameStrLength,
                    (unsigned long) pathStrLength,
                    (unsigned long) typeStrLength,
                    (unsigned long) capStrLength,
                    (unsigned long) allocStrLength) < 0)
        goto cleanup;

    /* Display the header */
    vshPrint(ctl, outputStr, _("Name"), _("Path"), _("Type"),
             ("Capacity"), _("Allocation"));
    for (i = nameStrLength + pathStrLength + typeStrLength
                           + capStrLength + allocStrLength
                           + 10; i > 0; i--)
        vshPrintExtra(ctl, "-");
    vshPrintExtra(ctl, "\n");

    /* Display the item info rows */
    for (i = 0; i < list->nitems; i++) {
        vshPrint(ctl, outputStr,
                 virFSItemGetName(list->items[i]),
                 itemInfoTexts[i].path,
                 itemInfoTexts[i].type,
                 itemInfoTexts[i].capacity,
                 itemInfoTexts[i].allocation);
    }

    /* Cleanup and return */
    ret = true;

 cleanup:

    /* Safely free the memory allocated in this function */
    if (list && list->nitems) {
        for (i = 0; i < list->nitems; i++) {
            /* Cleanup the memory for one item info structure per loop */
            VIR_FREE(itemInfoTexts[i].path);
            VIR_FREE(itemInfoTexts[i].type);
            VIR_FREE(itemInfoTexts[i].capacity);
            VIR_FREE(itemInfoTexts[i].allocation);
        }
    }

    /* Cleanup remaining memory */
    VIR_FREE(outputStr);
    VIR_FREE(itemInfoTexts);
    virFSPoolFree(fspool);
    virshFSItemListFree(list);

    /* Return the desired value */
    return ret;
}

/*
 * "item-name" command
 */
static const vshCmdInfo info_item_name[] = {
    {.name = "help",
     .data = N_("returns the item name for a given item key or path")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_name[] = {
    {.name = "item",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("item key or path")
    },
    {.name = NULL}
};

static bool
cmdItemName(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemPtr item;

    if (!(item = virshCommandOptItemBy(ctl, cmd, "item", NULL, NULL,
                                     VIRSH_BYUUID)))
        return false;

    vshPrint(ctl, "%s\n", virFSItemGetName(item));
    virFSItemFree(item);
    return true;
}

/*
 * "item-fspool" command
 */
static const vshCmdInfo info_item_fspool[] = {
    {.name = "help",
     .data = N_("returns the storage fspool for a given item key or path")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_fspool[] = {
    {.name = "item",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("item key or path")
    },
    {.name = "uuid",
     .type = VSH_OT_BOOL,
     .help = N_("return the fspool uuid rather than fspool name")
    },
    {.name = NULL}
};

static bool
cmdItemPool(vshControl *ctl, const vshCmd *cmd)
{
    virFSPoolPtr fspool;
    virFSItemPtr item;
    char uuid[VIR_UUID_STRING_BUFLEN];

    /* Use the supplied string to locate the item */
    if (!(item = virshCommandOptItemBy(ctl, cmd, "item", NULL, NULL,
                                     VIRSH_BYUUID))) {
        return false;
    }

    /* Look up the parent storage fspool for the item */
    fspool = virFSPoolLookupByItem(item);
    if (fspool == NULL) {
        vshError(ctl, "%s", _("failed to get parent fspool"));
        virFSItemFree(item);
        return false;
    }

    /* Return the requested details of the parent storage fspool */
    if (vshCommandOptBool(cmd, "uuid")) {
        /* Retrieve and return fspool UUID string */
        if (virFSPoolGetUUIDString(fspool, &uuid[0]) == 0)
            vshPrint(ctl, "%s\n", uuid);
    } else {
        /* Return the storage fspool name */
        vshPrint(ctl, "%s\n", virFSPoolGetName(fspool));
    }

    /* Cleanup */
    virFSItemFree(item);
    virFSPoolFree(fspool);
    return true;
}

/*
 * "item-key" command
 */
static const vshCmdInfo info_item_key[] = {
    {.name = "help",
     .data = N_("returns the item key for a given item name or path")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_key[] = {
    {.name = "item",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("item name or path")
    },
    VIRSH_COMMON_OPT_FSPOOL_OPTIONAL,
    {.name = NULL}
};

static bool
cmdItemKey(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemPtr item;

    if (!(item = virshCommandOptItem(ctl, cmd, "item", "fspool", NULL)))
        return false;

    vshPrint(ctl, "%s\n", virFSItemGetKey(item));
    virFSItemFree(item);
    return true;
}

/*
 * "item-path" command
 */
static const vshCmdInfo info_item_path[] = {
    {.name = "help",
     .data = N_("returns the item path for a given item name or key")
    },
    {.name = "desc",
     .data = ""
    },
    {.name = NULL}
};

static const vshCmdOptDef opts_item_path[] = {
    {.name = "item",
     .type = VSH_OT_DATA,
     .flags = VSH_OFLAG_REQ,
     .help = N_("item name or key")
    },
    VIRSH_COMMON_OPT_FSPOOL_OPTIONAL,
    {.name = NULL}
};

static bool
cmdItemPath(vshControl *ctl, const vshCmd *cmd)
{
    virFSItemPtr item;
    char * FSItemPath;

    if (!(item = virshCommandOptItem(ctl, cmd, "item", "fspool", NULL)))
        return false;

    if ((FSItemPath = virFSItemGetPath(item)) == NULL) {
        virFSItemFree(item);
        return false;
    }

    vshPrint(ctl, "%s\n", FSItemPath);
    VIR_FREE(FSItemPath);
    virFSItemFree(item);
    return true;
}

const vshCmdDef fsItemCmds[] = {
    {.name = "item-clone",
     .handler = cmdItemClone,
     .opts = opts_item_clone,
     .info = info_item_clone,
     .flags = 0
    },
    {.name = "item-create-as",
     .handler = cmdItemCreateAs,
     .opts = opts_item_create_as,
     .info = info_item_create_as,
     .flags = 0
    },
    {.name = "item-create",
     .handler = cmdItemCreate,
     .opts = opts_item_create,
     .info = info_item_create,
     .flags = 0
    },
    {.name = "item-create-from",
     .handler = cmdItemCreateFrom,
     .opts = opts_item_create_from,
     .info = info_item_create_from,
     .flags = 0
    },
    {.name = "item-delete",
     .handler = cmdItemDelete,
     .opts = opts_item_delete,
     .info = info_item_delete,
     .flags = 0
    },
    {.name = "item-dumpxml",
     .handler = cmdItemDumpXML,
     .opts = opts_item_dumpxml,
     .info = info_item_dumpxml,
     .flags = 0
    },
    {.name = "item-info",
     .handler = cmdItemInfo,
     .opts = opts_item_info,
     .info = info_item_info,
     .flags = 0
    },
    {.name = "item-key",
     .handler = cmdItemKey,
     .opts = opts_item_key,
     .info = info_item_key,
     .flags = 0
    },
    {.name = "item-list",
     .handler = cmdItemList,
     .opts = opts_item_list,
     .info = info_item_list,
     .flags = 0
    },
    {.name = "item-name",
     .handler = cmdItemName,
     .opts = opts_item_name,
     .info = info_item_name,
     .flags = 0
    },
    {.name = "item-path",
     .handler = cmdItemPath,
     .opts = opts_item_path,
     .info = info_item_path,
     .flags = 0
    },
    {.name = "item-fspool",
     .handler = cmdItemPool,
     .opts = opts_item_fspool,
     .info = info_item_fspool,
     .flags = 0
    },
    {.name = NULL}
};
