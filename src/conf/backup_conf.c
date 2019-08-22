/*
 * backup_conf.c: domain backup XML processing
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
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
 */

#include <config.h>

#include "internal.h"
#include "virbitmap.h"
#include "virbuffer.h"
#include "datatypes.h"
#include "domain_conf.h"
#include "virlog.h"
#include "viralloc.h"
#include "backup_conf.h"
#include "virstoragefile.h"
#include "viruuid.h"
#include "virfile.h"
#include "virerror.h"
#include "virxml.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_DOMAIN

VIR_LOG_INIT("conf.backup_conf");

VIR_ENUM_IMPL(virDomainBackup,
              VIR_DOMAIN_BACKUP_TYPE_LAST,
              "default", "push", "pull");

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
    virStorageNetHostDefFree(1, def->server);
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
        virDomainStorageSourceParse(cur, ctxt, def->store, 0, xmlopt) < 0)
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
        if (def->server->transport == VIR_STORAGE_NET_HOST_TRANS_UNIX &&
            def->server->socket[0] != '/') {
            virReportError(VIR_ERR_XML_ERROR,
                           _("backup socket path '%s' must be absolute"),
                           def->server->socket);
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
    /* TODO: per-disk backup=off? */

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

    if (virDomainDiskSourceFormat(buf, disk->store, push ? "target" : "scratch",
                                  0, false, 0, NULL) < 0)
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
        if (disk->store && !disk->store->path) {
            virStorageSourceClear(disk->store);
            disk->store = NULL;
        }
        if (virDomainBackupDefAssignStore(disk, dom->disks[idx]->src,
                                          suffix) < 0)
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
