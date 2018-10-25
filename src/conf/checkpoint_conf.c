/*
 * checkpoint_conf.c: domain checkpoint XML processing
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
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
 * Author: Eric Blake <eblake@redhat.com>
 */

#include <config.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "internal.h"
#include "virbitmap.h"
#include "virbuffer.h"
#include "count-one-bits.h"
#include "datatypes.h"
#include "domain_conf.h"
#include "virlog.h"
#include "viralloc.h"
#include "netdev_bandwidth_conf.h"
#include "netdev_vport_profile_conf.h"
#include "nwfilter_conf.h"
#include "secret_conf.h"
#include "checkpoint_conf.h"
#include "virstoragefile.h"
#include "viruuid.h"
#include "virfile.h"
#include "virerror.h"
#include "virxml.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_DOMAIN_CHECKPOINT

VIR_LOG_INIT("conf.checkpoint_conf");

VIR_ENUM_IMPL(virDomainCheckpoint, VIR_DOMAIN_CHECKPOINT_TYPE_LAST,
              "default",
              "no",
              "bitmap")

struct _virDomainCheckpointObjList {
    /* name string -> virDomainCheckpointObj mapping
     * for O(1), lockless lookup-by-name */
    virHashTable *objs;

    virDomainCheckpointObj metaroot; /* Special parent of all root checkpoints */
};

/* Checkpoint Def functions */
static void
virDomainCheckpointDiskDefClear(virDomainCheckpointDiskDefPtr disk)
{
    VIR_FREE(disk->name);
    VIR_FREE(disk->node);
    VIR_FREE(disk->bitmap);
}

void virDomainCheckpointDefFree(virDomainCheckpointDefPtr def)
{
    size_t i;

    if (!def)
        return;

    VIR_FREE(def->name);
    VIR_FREE(def->description);
    VIR_FREE(def->parent);
    for (i = 0; i < def->ndisks; i++)
        virDomainCheckpointDiskDefClear(&def->disks[i]);
    VIR_FREE(def->disks);
    virDomainDefFree(def->dom);
    VIR_FREE(def);
}

static int
virDomainCheckpointDiskDefParseXML(xmlNodePtr node,
                                   xmlXPathContextPtr ctxt,
                                   unsigned int flags,
                                   virDomainCheckpointDiskDefPtr def)
{
    int ret = -1;
    char *checkpoint = NULL;
    char *bitmap = NULL;
    xmlNodePtr saved = ctxt->node;

    ctxt->node = node;

    def->name = virXMLPropString(node, "name");
    if (!def->name) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing name from disk checkpoint element"));
        goto cleanup;
    }
    if (flags & VIR_DOMAIN_CHECKPOINT_PARSE_INTERNAL)
        def->node = virXMLPropString(node, "node");

    checkpoint = virXMLPropString(node, "checkpoint");
    if (checkpoint) {
        def->type = virDomainCheckpointTypeFromString(checkpoint);
        if (def->type <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown disk checkpoint setting '%s'"),
                           checkpoint);
            goto cleanup;
        }
    } else {
        def->type = VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP;
    }

    bitmap = virXMLPropString(node, "bitmap");
    if (bitmap) {
        if (def->type != VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("disk checkpoint bitmap '%s' requires "
                             "type='bitmap'"),
                           bitmap);
            goto cleanup;
        }
        VIR_STEAL_PTR(def->bitmap, bitmap);
    }

    ret = 0;
 cleanup:
    ctxt->node = saved;

    VIR_FREE(checkpoint);
    VIR_FREE(bitmap);
    if (ret < 0)
        virDomainCheckpointDiskDefClear(def);
    return ret;
}

/* flags is bitwise-or of virDomainCheckpointParseFlags.
 * If flags does not include VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE, then
 * caps are ignored.
 */
static virDomainCheckpointDefPtr
virDomainCheckpointDefParse(xmlXPathContextPtr ctxt,
                            virCapsPtr caps,
                            virDomainXMLOptionPtr xmlopt,
                            unsigned int flags)
{
    virDomainCheckpointDefPtr def = NULL;
    virDomainCheckpointDefPtr ret = NULL;
    xmlNodePtr *nodes = NULL;
    size_t i;
    int n;
    char *creation = NULL;
    struct timeval tv;
    int active;
    char *tmp;

    if (VIR_ALLOC(def) < 0)
        goto cleanup;

    gettimeofday(&tv, NULL);

    def->name = virXPathString("string(./name)", ctxt);
    if (def->name == NULL) {
        if (flags & VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("a redefined checkpoint must have a name"));
            goto cleanup;
        }
        if (virAsprintf(&def->name, "%lld", (long long)tv.tv_sec) < 0)
            goto cleanup;
    }

    def->description = virXPathString("string(./description)", ctxt);

    if (flags & VIR_DOMAIN_CHECKPOINT_PARSE_REDEFINE) {
        if (virXPathLongLong("string(./creationTime)", ctxt,
                             &def->creationTime) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("missing creationTime from existing checkpoint"));
            goto cleanup;
        }

        def->parent = virXPathString("string(./parent/name)", ctxt);

        if ((tmp = virXPathString("string(./domain/@type)", ctxt))) {
            int domainflags = VIR_DOMAIN_DEF_PARSE_INACTIVE |
                              VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE;
            xmlNodePtr domainNode = virXPathNode("./domain", ctxt);

            VIR_FREE(tmp);
            if (!domainNode) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("missing domain in checkpoint"));
                goto cleanup;
            }
            def->dom = virDomainDefParseNode(ctxt->node->doc, domainNode,
                                             caps, xmlopt, NULL, domainflags);
            if (!def->dom)
                goto cleanup;
        } else {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("missing domain in checkpoint redefine"));
            goto cleanup;
        }
    } else {
        def->creationTime = tv.tv_sec;
    }

    if ((n = virXPathNodeSet("./disks/*", ctxt, &nodes)) < 0)
        goto cleanup;
    if (flags & VIR_DOMAIN_CHECKPOINT_PARSE_DISKS) {
        if (n && VIR_ALLOC_N(def->disks, n) < 0)
            goto cleanup;
        def->ndisks = n;
        for (i = 0; i < def->ndisks; i++) {
            if (virDomainCheckpointDiskDefParseXML(nodes[i], ctxt, flags,
                                                   &def->disks[i]) < 0)
                goto cleanup;
        }
        VIR_FREE(nodes);
    } else if (n) {
        virReportError(VIR_ERR_ARGUMENT_UNSUPPORTED, "%s",
                       _("unable to handle disk requests in checkpoint"));
        goto cleanup;
    }

    if (flags & VIR_DOMAIN_CHECKPOINT_PARSE_INTERNAL) {
        if (virXPathInt("string(./active)", ctxt, &active) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Could not find 'active' element"));
            goto cleanup;
        }
        def->current = active != 0;
    }

    VIR_STEAL_PTR(ret, def);

 cleanup:
    VIR_FREE(creation);
    VIR_FREE(nodes);
    virDomainCheckpointDefFree(def);

    return ret;
}

virDomainCheckpointDefPtr
virDomainCheckpointDefParseNode(xmlDocPtr xml,
                                xmlNodePtr root,
                                virCapsPtr caps,
                                virDomainXMLOptionPtr xmlopt,
                                unsigned int flags)
{
    xmlXPathContextPtr ctxt = NULL;
    virDomainCheckpointDefPtr def = NULL;

    if (!virXMLNodeNameEqual(root, "domaincheckpoint")) {
        virReportError(VIR_ERR_XML_ERROR, "%s", _("domaincheckpoint"));
        goto cleanup;
    }

    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        virReportOOMError();
        goto cleanup;
    }

    ctxt->node = root;
    def = virDomainCheckpointDefParse(ctxt, caps, xmlopt, flags);
 cleanup:
    xmlXPathFreeContext(ctxt);
    return def;
}

virDomainCheckpointDefPtr
virDomainCheckpointDefParseString(const char *xmlStr,
                                  virCapsPtr caps,
                                  virDomainXMLOptionPtr xmlopt,
                                  unsigned int flags)
{
    virDomainCheckpointDefPtr ret = NULL;
    xmlDocPtr xml;
    int keepBlanksDefault = xmlKeepBlanksDefault(0);

    if ((xml = virXMLParse(NULL, xmlStr, _("(domain_checkpoint)")))) {
        xmlKeepBlanksDefault(keepBlanksDefault);
        ret = virDomainCheckpointDefParseNode(xml, xmlDocGetRootElement(xml),
                                              caps, xmlopt, flags);
        xmlFreeDoc(xml);
    }
    xmlKeepBlanksDefault(keepBlanksDefault);

    return ret;
}

/**
 * virDomainCheckpointDefAssignBitmapNames:
 * @def: checkpoint def object
 *
 * Generate default bitmap names for checkpoint targets. Returns 0 on
 * success, -1 on error.
 */
static int
virDomainCheckpointDefAssignBitmapNames(virDomainCheckpointDefPtr def)
{
    size_t i;

    for (i = 0; i < def->ndisks; i++) {
        virDomainCheckpointDiskDefPtr disk = &def->disks[i];

        if (disk->type != VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP ||
            disk->bitmap)
            continue;

        if (VIR_STRDUP(disk->bitmap, def->name) < 0)
            return -1;
    }

    return 0;
}


static int
virDomainCheckpointCompareDiskIndex(const void *a, const void *b)
{
    const virDomainCheckpointDiskDef *diska = a;
    const virDomainCheckpointDiskDef *diskb = b;

    /* Integer overflow shouldn't be a problem here.  */
    return diska->idx - diskb->idx;
}

/* Align def->disks to def->domain.  Sort the list of def->disks,
 * filling in any missing disks with appropriate default.  Convert
 * paths to disk targets for uniformity.  Issue an error and return -1
 * if any def->disks[n]->name appears more than once or does not map
 * to dom->disks. */
int
virDomainCheckpointAlignDisks(virDomainCheckpointDefPtr def)
{
    int ret = -1;
    virBitmapPtr map = NULL;
    size_t i;
    int ndisks;
    int checkpoint_default = VIR_DOMAIN_CHECKPOINT_TYPE_NONE;

    if (!def->dom) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing domain in checkpoint"));
        goto cleanup;
    }

    if (def->ndisks > def->dom->ndisks) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("too many disk checkpoint requests for domain"));
        goto cleanup;
    }

    /* Unlikely to have a guest without disks but technically possible.  */
    if (!def->dom->ndisks) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("domain must have at least one disk to perform "
                         "checkpoints"));
        goto cleanup;
    }

    /* If <disks> omitted, do bitmap on all disks; otherwise, do nothing
     * for omitted disks */
    if (!def->ndisks)
        checkpoint_default = VIR_DOMAIN_CHECKPOINT_TYPE_BITMAP;

    if (!(map = virBitmapNew(def->dom->ndisks)))
        goto cleanup;

    /* Double check requested disks.  */
    for (i = 0; i < def->ndisks; i++) {
        virDomainCheckpointDiskDefPtr disk = &def->disks[i];
        int idx = virDomainDiskIndexByName(def->dom, disk->name, false);

        if (idx < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("no disk named '%s'"), disk->name);
            goto cleanup;
        }

        if (virBitmapIsBitSet(map, idx)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("disk '%s' specified twice"),
                           disk->name);
            goto cleanup;
        }
        ignore_value(virBitmapSetBit(map, idx));
        disk->idx = idx;

        if (STRNEQ(disk->name, def->dom->disks[idx]->dst)) {
            VIR_FREE(disk->name);
            if (VIR_STRDUP(disk->name, def->dom->disks[idx]->dst) < 0)
                goto cleanup;
        }
    }

    /* Provide defaults for all remaining disks.  */
    ndisks = def->ndisks;
    if (VIR_EXPAND_N(def->disks, def->ndisks,
                     def->dom->ndisks - def->ndisks) < 0)
        goto cleanup;

    for (i = 0; i < def->dom->ndisks; i++) {
        virDomainCheckpointDiskDefPtr disk;

        if (virBitmapIsBitSet(map, i))
            continue;
        disk = &def->disks[ndisks++];
        if (VIR_STRDUP(disk->name, def->dom->disks[i]->dst) < 0)
            goto cleanup;
        disk->idx = i;

        /* Don't checkpoint empty drives */
        if (virStorageSourceIsEmpty(def->dom->disks[i]->src))
            disk->type = VIR_DOMAIN_CHECKPOINT_TYPE_NONE;
        else
            disk->type = checkpoint_default;
    }

    qsort(&def->disks[0], def->ndisks, sizeof(def->disks[0]),
          virDomainCheckpointCompareDiskIndex);

    /* Generate default bitmap names for checkpoint */
    if (virDomainCheckpointDefAssignBitmapNames(def) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virBitmapFree(map);
    return ret;
}

static int
virDomainCheckpointDiskDefFormat(virBufferPtr buf,
                                 virDomainCheckpointDiskDefPtr disk,
                                 unsigned int flags, bool internal)
{
    if (!disk->name)
        return 0;

    virBufferEscapeString(buf, "<disk name='%s'", disk->name);
    if (internal)
        virBufferEscapeString(buf, " node='%s'", disk->node);
    if (disk->type)
        virBufferAsprintf(buf, " checkpoint='%s'",
                          virDomainCheckpointTypeToString(disk->type));
    if (disk->bitmap) {
        virBufferEscapeString(buf, " bitmap='%s'", disk->bitmap);
        if (flags & VIR_DOMAIN_CHECKPOINT_XML_SIZE)
            virBufferAsprintf(buf, " size='%llu'", disk->size);
    }
    virBufferAddLit(buf, "/>\n");
    return 0;
}


char *
virDomainCheckpointDefFormat(virDomainCheckpointDefPtr def,
                             virCapsPtr caps,
                             virDomainXMLOptionPtr xmlopt,
                             unsigned int flags,
                             bool internal)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    size_t i;
    unsigned int domflags = VIR_DOMAIN_DEF_FORMAT_INACTIVE;

    virCheckFlags(VIR_DOMAIN_CHECKPOINT_XML_SECURE |
                  VIR_DOMAIN_CHECKPOINT_XML_NO_DOMAIN |
                  VIR_DOMAIN_CHECKPOINT_XML_SIZE, NULL);
    if (flags & VIR_DOMAIN_CHECKPOINT_XML_SECURE)
        domflags |= VIR_DOMAIN_DEF_FORMAT_SECURE;

    virBufferAddLit(&buf, "<domaincheckpoint>\n");
    virBufferAdjustIndent(&buf, 2);

    virBufferEscapeString(&buf, "<name>%s</name>\n", def->name);
    if (def->description)
        virBufferEscapeString(&buf, "<description>%s</description>\n",
                              def->description);

    if (def->parent) {
        virBufferAddLit(&buf, "<parent>\n");
        virBufferAdjustIndent(&buf, 2);
        virBufferEscapeString(&buf, "<name>%s</name>\n", def->parent);
        virBufferAdjustIndent(&buf, -2);
        virBufferAddLit(&buf, "</parent>\n");
    }

    virBufferAsprintf(&buf, "<creationTime>%lld</creationTime>\n",
                      def->creationTime);

    if (def->ndisks) {
        virBufferAddLit(&buf, "<disks>\n");
        virBufferAdjustIndent(&buf, 2);
        for (i = 0; i < def->ndisks; i++) {
            if (virDomainCheckpointDiskDefFormat(&buf, &def->disks[i],
                                                 flags, internal) < 0)
                goto error;
        }
        virBufferAdjustIndent(&buf, -2);
        virBufferAddLit(&buf, "</disks>\n");
    }

    if (!(flags & VIR_DOMAIN_CHECKPOINT_XML_NO_DOMAIN) &&
        virDomainDefFormatInternal(def->dom, caps, domflags, &buf, xmlopt) < 0)
        goto error;

    if (internal)
        virBufferAsprintf(&buf, "<active>%d</active>\n", def->current);

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</domaincheckpoint>\n");

    if (virBufferCheckError(&buf) < 0)
        return NULL;

    return virBufferContentAndReset(&buf);

 error:
    virBufferFreeAndReset(&buf);
    return NULL;
}

/* Checkpoint Obj functions */
static virDomainCheckpointObjPtr virDomainCheckpointObjNew(void)
{
    virDomainCheckpointObjPtr checkpoint;

    if (VIR_ALLOC(checkpoint) < 0)
        return NULL;

    VIR_DEBUG("obj=%p", checkpoint);

    return checkpoint;
}

static void virDomainCheckpointObjFree(virDomainCheckpointObjPtr checkpoint)
{
    if (!checkpoint)
        return;

    VIR_DEBUG("obj=%p", checkpoint);

    virDomainCheckpointDefFree(checkpoint->def);
    VIR_FREE(checkpoint);
}

virDomainCheckpointObjPtr virDomainCheckpointAssignDef(virDomainCheckpointObjListPtr checkpoints,
                                                       virDomainCheckpointDefPtr def)
{
    virDomainCheckpointObjPtr chk;

    if (virHashLookup(checkpoints->objs, def->name) != NULL) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("domain checkpoint %s already exists"),
                       def->name);
        return NULL;
    }

    if (!(chk = virDomainCheckpointObjNew()))
        return NULL;
    chk->def = def;

    if (virHashAddEntry(checkpoints->objs, chk->def->name, chk) < 0) {
        VIR_FREE(chk);
        return NULL;
    }

    return chk;
}

/* Checkpoint Obj List functions */
static void
virDomainCheckpointObjListDataFree(void *payload,
                                   const void *name ATTRIBUTE_UNUSED)
{
    virDomainCheckpointObjPtr obj = payload;

    virDomainCheckpointObjFree(obj);
}

virDomainCheckpointObjListPtr
virDomainCheckpointObjListNew(void)
{
    virDomainCheckpointObjListPtr checkpoints;
    if (VIR_ALLOC(checkpoints) < 0)
        return NULL;
    checkpoints->objs = virHashCreate(50, virDomainCheckpointObjListDataFree);
    if (!checkpoints->objs) {
        VIR_FREE(checkpoints);
        return NULL;
    }
    return checkpoints;
}

void
virDomainCheckpointObjListFree(virDomainCheckpointObjListPtr checkpoints)
{
    if (!checkpoints)
        return;
    virHashFree(checkpoints->objs);
    VIR_FREE(checkpoints);
}

struct virDomainCheckpointNameData {
    char **const names;
    int maxnames;
    unsigned int flags;
    int count;
    bool error;
};

static int
virDomainCheckpointObjListCopyNames(void *payload,
                                    const void *name ATTRIBUTE_UNUSED,
                                    void *opaque)
{
    virDomainCheckpointObjPtr obj = payload;
    struct virDomainCheckpointNameData *data = opaque;

    if (data->error)
        return 0;
    /* Caller already sanitized flags.  Filtering on DESCENDANTS was
     * done by choice of iteration in the caller.  */
    if ((data->flags & VIR_DOMAIN_CHECKPOINT_LIST_LEAVES) && obj->nchildren)
        return 0;
    if ((data->flags & VIR_DOMAIN_CHECKPOINT_LIST_NO_LEAVES) && !obj->nchildren)
        return 0;

    if (data->names && data->count < data->maxnames &&
        VIR_STRDUP(data->names[data->count], obj->def->name) < 0) {
        data->error = true;
        return 0;
    }
    data->count++;
    return 0;
}

static int
virDomainCheckpointObjListGetNames(virDomainCheckpointObjListPtr checkpoints,
                                   virDomainCheckpointObjPtr from,
                                   char **const names, int maxnames,
                                   unsigned int flags)
{
    struct virDomainCheckpointNameData data = { names, maxnames, flags, 0,
                                              false };
    size_t i;

    if (!from) {
        /* LIST_ROOTS and LIST_DESCENDANTS have the same bit value,
         * but opposite semantics.  Toggle here to get the correct
         * traversal on the metaroot.  */
        flags ^= VIR_DOMAIN_CHECKPOINT_LIST_ROOTS;
        from = &checkpoints->metaroot;
    }

    /* We handle LIST_ROOT/LIST_DESCENDANTS directly, mask that bit
     * out to determine when we must use the filter callback.  */
    data.flags &= ~VIR_DOMAIN_CHECKPOINT_LIST_DESCENDANTS;

    /* If this common code is being used, we assume that all checkpoints
     * have metadata, and thus can handle METADATA up front as an
     * all-or-none filter.  XXX This might not always be true, if we
     * add the ability to track qcow2 bitmaps without the
     * use of metadata.  */
    if ((data.flags & VIR_DOMAIN_CHECKPOINT_FILTERS_METADATA) ==
        VIR_DOMAIN_CHECKPOINT_LIST_NO_METADATA)
        return 0;
    data.flags &= ~VIR_DOMAIN_CHECKPOINT_FILTERS_METADATA;

    /* For ease of coding the visitor, it is easier to zero each group
     * where all of the bits are set.  */
    if ((data.flags & VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES) ==
        VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES)
        data.flags &= ~VIR_DOMAIN_CHECKPOINT_FILTERS_LEAVES;

    if (flags & VIR_DOMAIN_CHECKPOINT_LIST_DESCENDANTS) {
        if (from->def)
            virDomainCheckpointForEachDescendant(from,
                                                 virDomainCheckpointObjListCopyNames,
                                                 &data);
        else if (names || data.flags)
            virHashForEach(checkpoints->objs,
                           virDomainCheckpointObjListCopyNames,
                           &data);
        else
            data.count = virHashSize(checkpoints->objs);
    } else if (names || data.flags) {
        virDomainCheckpointForEachChild(from,
                                        virDomainCheckpointObjListCopyNames,
                                        &data);
    } else {
        data.count = from->nchildren;
    }

    if (data.error) {
        for (i = 0; i < data.count; i++)
            VIR_FREE(names[i]);
        return -1;
    }

    return data.count;
}

static int
virDomainCheckpointObjListNum(virDomainCheckpointObjListPtr checkpoints,
                              virDomainCheckpointObjPtr from,
                              unsigned int flags)
{
    return virDomainCheckpointObjListGetNames(checkpoints, from, NULL, 0,
                                              flags);
}

virDomainCheckpointObjPtr
virDomainCheckpointFindByName(virDomainCheckpointObjListPtr checkpoints,
                              const char *name)
{
    return name ? virHashLookup(checkpoints->objs, name) :
        &checkpoints->metaroot;
}

void virDomainCheckpointObjListRemove(virDomainCheckpointObjListPtr checkpoints,
                                      virDomainCheckpointObjPtr checkpoint)
{
    virHashRemoveEntry(checkpoints->objs, checkpoint->def->name);
}

int
virDomainCheckpointForEach(virDomainCheckpointObjListPtr checkpoints,
                           virHashIterator iter,
                           void *data)
{
    return virHashForEach(checkpoints->objs, iter, data);
}

/* Run iter(data) on all direct children of checkpoint, while ignoring all
 * other entries in checkpoints.  Return the number of children
 * visited.  No particular ordering is guaranteed.  */
int
virDomainCheckpointForEachChild(virDomainCheckpointObjPtr checkpoint,
                                virHashIterator iter,
                                void *data)
{
    virDomainCheckpointObjPtr child = checkpoint->first_child;

    while (child) {
        virDomainCheckpointObjPtr next = child->sibling;
        (iter)(child, child->def->name, data);
        child = next;
    }

    return checkpoint->nchildren;
}

struct checkpoint_act_on_descendant {
    int number;
    virHashIterator iter;
    void *data;
};

static int
virDomainCheckpointActOnDescendant(void *payload,
                                   const void *name,
                                   void *data)
{
    virDomainCheckpointObjPtr obj = payload;
    struct checkpoint_act_on_descendant *curr = data;

    curr->number += 1 + virDomainCheckpointForEachDescendant(obj,
                                                             curr->iter,
                                                             curr->data);
    (curr->iter)(payload, name, curr->data);
    return 0;
}

/* Run iter(data) on all descendants of checkpoint, while ignoring all
 * other entries in checkpoints.  Return the number of descendants
 * visited.  No particular ordering is guaranteed.  */
int
virDomainCheckpointForEachDescendant(virDomainCheckpointObjPtr checkpoint,
                                     virHashIterator iter,
                                     void *data)
{
    struct checkpoint_act_on_descendant act;

    act.number = 0;
    act.iter = iter;
    act.data = data;
    virDomainCheckpointForEachChild(checkpoint,
                                    virDomainCheckpointActOnDescendant, &act);

    return act.number;
}

/* Struct and callback function used as a hash table callback; each call
 * inspects the pre-existing checkpoint->def->parent field, and adjusts
 * the checkpoint->parent field as well as the parent's child fields to
 * wire up the hierarchical relations for the given checkpoint.  The error
 * indicator gets set if a parent is missing or a requested parent would
 * cause a circular parent chain.  */
struct checkpoint_set_relation {
    virDomainCheckpointObjListPtr checkpoints;
    int err;
};
static int
virDomainCheckpointSetRelations(void *payload,
                                const void *name ATTRIBUTE_UNUSED,
                                void *data)
{
    virDomainCheckpointObjPtr obj = payload;
    struct checkpoint_set_relation *curr = data;
    virDomainCheckpointObjPtr tmp;

    obj->parent = virDomainCheckpointFindByName(curr->checkpoints,
                                                obj->def->parent);
    if (!obj->parent) {
        curr->err = -1;
        obj->parent = &curr->checkpoints->metaroot;
        VIR_WARN("checkpoint %s lacks parent", obj->def->name);
    } else {
        tmp = obj->parent;
        while (tmp && tmp->def) {
            if (tmp == obj) {
                curr->err = -1;
                obj->parent = &curr->checkpoints->metaroot;
                VIR_WARN("checkpoint %s in circular chain", obj->def->name);
                break;
            }
            tmp = tmp->parent;
        }
    }
    obj->parent->nchildren++;
    obj->sibling = obj->parent->first_child;
    obj->parent->first_child = obj;
    return 0;
}

/* Populate parent link and child count of all checkpoints, with all
 * relations starting as 0/NULL.  Return 0 on success, -1 if a parent
 * is missing or if a circular relationship was requested.  */
int
virDomainCheckpointUpdateRelations(virDomainCheckpointObjListPtr checkpoints)
{
    struct checkpoint_set_relation act = { checkpoints, 0 };

    virHashForEach(checkpoints->objs, virDomainCheckpointSetRelations, &act);
    return act.err;
}

/* Prepare to reparent or delete checkpoint, by removing it from its
 * current listed parent.  Note that when bulk removing all children
 * of a parent, it is faster to just 0 the count rather than calling
 * this function on each child.  */
void
virDomainCheckpointDropParent(virDomainCheckpointObjPtr checkpoint)
{
    virDomainCheckpointObjPtr prev = NULL;
    virDomainCheckpointObjPtr curr = NULL;

    checkpoint->parent->nchildren--;
    curr = checkpoint->parent->first_child;
    while (curr != checkpoint) {
        if (!curr) {
            VIR_WARN("inconsistent checkpoint relations");
            return;
        }
        prev = curr;
        curr = curr->sibling;
    }
    if (prev)
        prev->sibling = checkpoint->sibling;
    else
        checkpoint->parent->first_child = checkpoint->sibling;
    checkpoint->parent = NULL;
    checkpoint->sibling = NULL;
}

int
virDomainListAllCheckpoints(virDomainCheckpointObjListPtr checkpoints,
                            virDomainCheckpointObjPtr from,
                            virDomainPtr dom,
                            virDomainCheckpointPtr **chks,
                            unsigned int flags)
{
    int count = virDomainCheckpointObjListNum(checkpoints, from, flags);
    virDomainCheckpointPtr *list = NULL;
    char **names;
    int ret = -1;
    size_t i;

    if (!chks || count < 0)
        return count;
    if (VIR_ALLOC_N(names, count) < 0 ||
        VIR_ALLOC_N(list, count + 1) < 0)
        goto cleanup;

    if (virDomainCheckpointObjListGetNames(checkpoints, from, names, count,
                                         flags) < 0)
        goto cleanup;
    for (i = 0; i < count; i++)
        if ((list[i] = virGetDomainCheckpoint(dom, names[i])) == NULL)
            goto cleanup;

    ret = count;
    *chks = list;

 cleanup:
    for (i = 0; i < count; i++)
        VIR_FREE(names[i]);
    VIR_FREE(names);
    if (ret < 0 && list) {
        for (i = 0; i < count; i++)
            virObjectUnref(list[i]);
        VIR_FREE(list);
    }
    return ret;
}


int
virDomainCheckpointRedefinePrep(virDomainPtr domain,
                                virDomainObjPtr vm,
                                virDomainCheckpointDefPtr *defptr,
                                virDomainCheckpointObjPtr *chk,
                                virDomainXMLOptionPtr xmlopt,
                                bool *update_current)
{
    virDomainCheckpointDefPtr def = *defptr;
    int ret = -1;
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    virDomainCheckpointObjPtr other;

    virUUIDFormat(domain->uuid, uuidstr);

    /* Prevent circular chains */
    if (def->parent) {
        if (STREQ(def->name, def->parent)) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("cannot set checkpoint %s as its own parent"),
                           def->name);
            goto cleanup;
        }
        other = virDomainCheckpointFindByName(vm->checkpoints, def->parent);
        if (!other) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("parent %s for checkpoint %s not found"),
                           def->parent, def->name);
            goto cleanup;
        }
        while (other->def->parent) {
            if (STREQ(other->def->parent, def->name)) {
                virReportError(VIR_ERR_INVALID_ARG,
                               _("parent %s would create cycle to %s"),
                               other->def->name, def->name);
                goto cleanup;
            }
            other = virDomainCheckpointFindByName(vm->checkpoints,
                                                other->def->parent);
            if (!other) {
                VIR_WARN("checkpoints are inconsistent for %s",
                         vm->def->name);
                break;
            }
        }
    }

    if (def->dom &&
        memcmp(def->dom->uuid, domain->uuid, VIR_UUID_BUFLEN)) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("definition for checkpoint %s must use uuid %s"),
                       def->name, uuidstr);
        goto cleanup;
    }

    other = virDomainCheckpointFindByName(vm->checkpoints, def->name);
    if (other) {
        if (other->def->dom) {
            if (def->dom) {
                if (!virDomainDefCheckABIStability(other->def->dom,
                                                   def->dom, xmlopt))
                    goto cleanup;
            } else {
                /* Transfer the domain def */
                def->dom = other->def->dom;
                other->def->dom = NULL;
            }
        }

        if (def->dom) {
            if (virDomainCheckpointAlignDisks(def) < 0) {
                /* revert stealing of the checkpoint domain definition */
                if (def->dom && !other->def->dom) {
                    other->def->dom = def->dom;
                    def->dom = NULL;
                }
                goto cleanup;
            }
        }

        if (other == vm->current_checkpoint) {
            *update_current = true;
            vm->current_checkpoint = NULL;
        }

        /* Drop and rebuild the parent relationship, but keep all
         * child relations by reusing chk.  */
        virDomainCheckpointDropParent(other);
        virDomainCheckpointDefFree(other->def);
        other->def = def;
        *defptr = NULL;
        *chk = other;
    } else if (def->dom && virDomainCheckpointAlignDisks(def) < 0) {
        goto cleanup;
    }

    ret = 0;
 cleanup:
    return ret;
}

/* Backup Def functions */

VIR_ENUM_IMPL(virDomainBackup, VIR_DOMAIN_BACKUP_TYPE_LAST,
              "default",
              "push",
              "pull")

static void
virDomainBackupDiskDefClear(virDomainBackupDiskDefPtr disk)
{
    VIR_FREE(disk->name);
    virStorageSourceClear(disk->store);
    disk->store = NULL;
}

void
virDomainBackupDefFree(virDomainBackupDefPtr def)
{
    size_t i;

    if (!def)
        return;

    VIR_FREE(def->incremental);
    VIR_FREE(def->server); // FIXME which struct
    for (i = 0; i < def->ndisks; i++)
        virDomainBackupDiskDefClear(&def->disks[i]);
    VIR_FREE(def->disks);
    VIR_FREE(def);
}

static int
virDomainBackupDiskDefParseXML(xmlNodePtr node,
                               xmlXPathContextPtr ctxt,
                               virDomainBackupDiskDefPtr def,
                               bool push, bool internal,
                               virDomainXMLOptionPtr xmlopt)
{
    int ret = -1;
    //    char *backup = NULL; /* backup="yes|no"? */
    char *type = NULL;
    char *driver = NULL;
    xmlNodePtr cur;
    xmlNodePtr saved = ctxt->node;

    ctxt->node = node;

    if (VIR_ALLOC(def->store) < 0)
        goto cleanup;

    def->name = virXMLPropString(node, "name");
    if (!def->name) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing name from disk backup element"));
        goto cleanup;
    }

    /* Needed? A way for users to list a disk and explicitly mark it
     * as not participating, and then output shows all disks rather
     * than just active disks */
#if 0
    backup = virXMLPropString(node, "backup");
    if (backup) {
        def->type = virDomainCheckpointTypeFromString(checkpoint);
        if (def->type <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown disk checkpoint setting '%s'"),
                           checkpoint);
            goto cleanup;
        }
    }
#endif

    if ((type = virXMLPropString(node, "type"))) {
        if ((def->store->type = virStorageTypeFromString(type)) <= 0 ||
            def->store->type == VIR_STORAGE_TYPE_VOLUME ||
            def->store->type == VIR_STORAGE_TYPE_DIR) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("unknown disk backup type '%s'"), type);
            goto cleanup;
        }
    } else {
        def->store->type = VIR_STORAGE_TYPE_FILE;
    }

    if ((cur = virXPathNode(push ? "./target" : "./scratch", ctxt)) &&
        virDomainDiskSourceParse(cur, ctxt, def->store, 0, xmlopt) < 0)
        goto cleanup;

    if (internal) {
        int detected;
        if (virXPathInt("string(./node/@detected)", ctxt, &detected) < 0)
            goto cleanup;
        def->store->detected = detected;
        def->store->nodeformat = virXPathString("string(./node)", ctxt);
    }

    if ((driver = virXPathString("string(./driver/@type)", ctxt))) {
        def->store->format = virStorageFileFormatTypeFromString(driver);
        if (def->store->format <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown disk backup driver '%s'"), driver);
            goto cleanup;
        } else if (!push && def->store->format != VIR_STORAGE_FILE_QCOW2) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("pull mode requires qcow2 driver, not '%s'"),
                           driver);
            goto cleanup;
        }
    }

    /* validate that the passed path is absolute */
    if (virStorageSourceIsRelative(def->store)) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("disk backup image path '%s' must be absolute"),
                       def->store->path);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    ctxt->node = saved;

    VIR_FREE(driver);
//    VIR_FREE(backup);
    VIR_FREE(type);
    if (ret < 0)
        virDomainBackupDiskDefClear(def);
    return ret;
}

static virDomainBackupDefPtr
virDomainBackupDefParse(xmlXPathContextPtr ctxt,
                        virDomainXMLOptionPtr xmlopt,
                        unsigned int flags)
{
    virDomainBackupDefPtr def = NULL;
    virDomainBackupDefPtr ret = NULL;
    xmlNodePtr *nodes = NULL;
    xmlNodePtr node = NULL;
    char *mode = NULL;
    bool push;
    size_t i;
    int n;

    if (VIR_ALLOC(def) < 0)
        goto cleanup;

    mode = virXMLPropString(ctxt->node, "mode");
    if (mode) {
        def->type = virDomainBackupTypeFromString(mode);
        if (def->type <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown backup mode '%s'"), mode);
            goto cleanup;
        }
    } else {
        def->type = VIR_DOMAIN_BACKUP_TYPE_PUSH;
    }
    push = def->type == VIR_DOMAIN_BACKUP_TYPE_PUSH;

    if (flags & VIR_DOMAIN_BACKUP_PARSE_INTERNAL) {
        char *tmp = virXMLPropString(ctxt->node, "id");
        if (tmp && virStrToLong_i(tmp, NULL, 10, &def->id) < 0) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("invalid 'id' value '%s'"), tmp);
            VIR_FREE(tmp);
            goto cleanup;
        }
        VIR_FREE(tmp);
    }

    def->incremental = virXPathString("string(./incremental)", ctxt);

    node = virXPathNode("./server", ctxt);
    if (node) {
        if (def->type != VIR_DOMAIN_BACKUP_TYPE_PULL) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("use of <server> requires pull mode backup"));
            goto cleanup;
        }
        if (VIR_ALLOC(def->server) < 0)
            goto cleanup;
        if (virDomainStorageNetworkParseHost(node, def->server) < 0)
            goto cleanup;
        if (def->server->transport == VIR_STORAGE_NET_HOST_TRANS_RDMA) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("transport rdma is not supported for <server>"));
            goto cleanup;
        }
    }

    if ((n = virXPathNodeSet("./disks/*", ctxt, &nodes)) < 0)
        goto cleanup;
    if (n && VIR_ALLOC_N(def->disks, n) < 0)
        goto cleanup;
    def->ndisks = n;
    for (i = 0; i < def->ndisks; i++) {
        if (virDomainBackupDiskDefParseXML(nodes[i], ctxt,
                                           &def->disks[i], push,
                                           flags & VIR_DOMAIN_BACKUP_PARSE_INTERNAL,
                                           xmlopt) < 0)
            goto cleanup;
    }
    VIR_FREE(nodes);

    VIR_STEAL_PTR(ret, def);

 cleanup:
    VIR_FREE(mode);
    VIR_FREE(nodes);
    virDomainBackupDefFree(def);

    return ret;
}

virDomainBackupDefPtr
virDomainBackupDefParseString(const char *xmlStr,
                              virDomainXMLOptionPtr xmlopt,
                              unsigned int flags)
{
    virDomainBackupDefPtr ret = NULL;
    xmlDocPtr xml;
    int keepBlanksDefault = xmlKeepBlanksDefault(0);

    if ((xml = virXMLParse(NULL, xmlStr, _("(domain_backup)")))) {
        xmlKeepBlanksDefault(keepBlanksDefault);
        ret = virDomainBackupDefParseNode(xml, xmlDocGetRootElement(xml),
                                          xmlopt, flags);
        xmlFreeDoc(xml);
    }
    xmlKeepBlanksDefault(keepBlanksDefault);

    return ret;
}

virDomainBackupDefPtr
virDomainBackupDefParseNode(xmlDocPtr xml,
                            xmlNodePtr root,
                            virDomainXMLOptionPtr xmlopt,
                            unsigned int flags)
{
    xmlXPathContextPtr ctxt = NULL;
    virDomainBackupDefPtr def = NULL;

    if (!virXMLNodeNameEqual(root, "domainbackup")) {
        virReportError(VIR_ERR_XML_ERROR, "%s", _("domainbackup"));
        goto cleanup;
    }

    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        virReportOOMError();
        goto cleanup;
    }

    ctxt->node = root;
    def = virDomainBackupDefParse(ctxt, xmlopt, flags);
 cleanup:
    xmlXPathFreeContext(ctxt);
    return def;
}

static int
virDomainBackupDiskDefFormat(virBufferPtr buf,
                             virDomainBackupDiskDefPtr disk,
                             bool push, bool internal)
{
    int type = disk->store->type;
    virBuffer attrBuf = VIR_BUFFER_INITIALIZER;
    virBuffer childBuf = VIR_BUFFER_INITIALIZER;
    int ret = -1;

    if (!disk->name)
        return 0;

    virBufferEscapeString(buf, "<disk name='%s'", disk->name);
    // TODO: per-disk backup=off?

    virBufferAsprintf(buf, " type='%s'>\n", virStorageTypeToString(type));
    virBufferAdjustIndent(buf, 2);

    if (disk->store->format > 0)
        virBufferEscapeString(buf, "<driver type='%s'/>\n",
                              virStorageFileFormatTypeToString(disk->store->format));
    /* TODO: should node names be part of storage file xml, rather
     * than a one-off hack for qemu? */
    if (internal) {
        virBufferEscapeString(buf, "<node detected='%s'",
                              disk->store->detected ? "1" : "0");
        virBufferEscapeString(buf, ">%s</node>\n", disk->store->nodeformat);
    }

    virBufferSetChildIndent(&childBuf, buf);
    if (virDomainStorageSourceFormat(&attrBuf, &childBuf, disk->store, 0,
                                     false) < 0)
        goto cleanup;
    if (virXMLFormatElement(buf, push ? "target" : "scratch",
                            &attrBuf, &childBuf) < 0)
        goto cleanup;

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</disk>\n");

    ret = 0;

 cleanup:
    virBufferFreeAndReset(&attrBuf);
    virBufferFreeAndReset(&childBuf);
    return ret;
}

int
virDomainBackupDefFormat(virBufferPtr buf, virDomainBackupDefPtr def,
                         bool internal)
{
    size_t i;

    virBufferAsprintf(buf, "<domainbackup mode='%s'",
                      virDomainBackupTypeToString(def->type));
    if (def->id)
        virBufferAsprintf(buf, " id='%d'", def->id);
    virBufferAddLit(buf, ">\n");
    virBufferAdjustIndent(buf, 2);

    virBufferEscapeString(buf, "<incremental>%s</incremental>\n",
                          def->incremental);
    if (def->server) {
        virBufferAsprintf(buf, "<server transport='%s'",
                          virStorageNetHostTransportTypeToString(def->server->transport));
        virBufferEscapeString(buf, " name='%s'", def->server->name);
        if (def->server->port)
            virBufferAsprintf(buf, " port='%u'", def->server->port);
        virBufferEscapeString(buf, " socket='%s'", def->server->socket);
        virBufferAddLit(buf, "/>\n");
    }

    if (def->ndisks) {
        virBufferAddLit(buf, "<disks>\n");
        virBufferAdjustIndent(buf, 2);
        for (i = 0; i < def->ndisks; i++) {
            if (!def->disks[i].store)
                continue;
            if (virDomainBackupDiskDefFormat(buf, &def->disks[i],
                                             def->type == VIR_DOMAIN_BACKUP_TYPE_PUSH,
                                             internal) < 0)
                return -1;
        }
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</disks>\n");
    }

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</domainbackup>\n");

    return virBufferCheckError(buf);
}


static int
virDomainBackupCompareDiskIndex(const void *a, const void *b)
{
    const virDomainBackupDiskDef *diska = a;
    const virDomainBackupDiskDef *diskb = b;

    /* Integer overflow shouldn't be a problem here.  */
    return diska->idx - diskb->idx;
}

static int
virDomainBackupDefAssignStore(virDomainBackupDiskDefPtr disk,
                              virStorageSourcePtr src,
                              const char *suffix)
{
    int ret = -1;

    if (virStorageSourceIsEmpty(src)) {
        if (disk->store) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("disk '%s' has no media"), disk->name);
            goto cleanup;
        }
    } else if (src->readonly && disk->store) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("backup of readonly disk '%s' makes no sense"),
                       disk->name);
        goto cleanup;
    } else if (!disk->store) {
        if (virStorageSourceGetActualType(src) == VIR_STORAGE_TYPE_FILE) {
            if (VIR_ALLOC(disk->store) < 0)
                goto cleanup;
            disk->store->type = VIR_STORAGE_TYPE_FILE;
            if (virAsprintf(&disk->store->path, "%s.%s", src->path,
                            suffix) < 0)
                goto cleanup;
            disk->store->detected = true;
        } else {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("refusing to generate file name for disk '%s'"),
                           disk->name);
            goto cleanup;
        }
    }
    ret = 0;
 cleanup:
    return ret;
}

/* Align def->disks to domain.  Sort the list of def->disks,
 * generating storage names using suffix as needed.  Convert paths to
 * disk targets for uniformity.  Issue an error and return -1 if any
 * def->disks[n]->name appears more than once or does not map to
 * dom->disks. */
int
virDomainBackupAlignDisks(virDomainBackupDefPtr def, virDomainDefPtr dom,
                          const char *suffix)
{
    int ret = -1;
    virBitmapPtr map = NULL;
    size_t i;
    int ndisks;
    bool alloc_all = false;

    if (def->ndisks > dom->ndisks) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("too many disk backup requests for domain"));
        goto cleanup;
    }

    /* Unlikely to have a guest without disks but technically possible.  */
    if (!dom->ndisks) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("domain must have at least one disk to perform "
                         "backups"));
        goto cleanup;
    }

    if (!(map = virBitmapNew(dom->ndisks)))
        goto cleanup;

    /* Double check requested disks.  */
    for (i = 0; i < def->ndisks; i++) {
        virDomainBackupDiskDefPtr disk = &def->disks[i];
        int idx = virDomainDiskIndexByName(dom, disk->name, false);

        if (idx < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("no disk named '%s'"), disk->name);
            goto cleanup;
        }

        if (virBitmapIsBitSet(map, idx)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("disk '%s' specified twice"),
                           disk->name);
            goto cleanup;
        }
        ignore_value(virBitmapSetBit(map, idx));
        disk->idx = idx;

        if (STRNEQ(disk->name, dom->disks[idx]->dst)) {
            VIR_FREE(disk->name);
            if (VIR_STRDUP(disk->name, dom->disks[idx]->dst) < 0)
                goto cleanup;
        }
        if (virDomainBackupDefAssignStore(disk, dom->disks[i]->src, suffix) < 0)
            goto cleanup;
    }

    /* Provide fillers for all remaining disks, for easier iteration.  */
    if (!def->ndisks)
        alloc_all = true;
    ndisks = def->ndisks;
    if (VIR_EXPAND_N(def->disks, def->ndisks,
                     dom->ndisks - def->ndisks) < 0)
        goto cleanup;

    for (i = 0; i < dom->ndisks; i++) {
        virDomainBackupDiskDefPtr disk;

        if (virBitmapIsBitSet(map, i))
            continue;
        disk = &def->disks[ndisks++];
        if (VIR_STRDUP(disk->name, dom->disks[i]->dst) < 0)
            goto cleanup;
        disk->idx = i;
        if (alloc_all &&
            virDomainBackupDefAssignStore(disk, dom->disks[i]->src, suffix) < 0)
            goto cleanup;
    }

    qsort(&def->disks[0], def->ndisks, sizeof(def->disks[0]),
          virDomainBackupCompareDiskIndex);

    ret = 0;

 cleanup:
    virBitmapFree(map);
    return ret;
}
