/*
 * backup_conf.c: domain backup XML processing
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
#include "backup_conf.h"
#include "virstoragefile.h"
#include "viruuid.h"
#include "virfile.h"
#include "virerror.h"
#include "virxml.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_DOMAIN_BACKUP

VIR_LOG_INIT("conf.backup_conf");


/* Backup Def functions */
static void
virDomainBackupDiskDefClear(virDomainBackupDiskDefPtr disk)
{
    VIR_FREE(disk->name);
    virStorageSourceFree(disk->target);
    disk->target = NULL;
}


static int
virDomainBackupDiskDefParseXML(xmlNodePtr node,
                               xmlXPathContextPtr ctxt,
                               virDomainBackupDiskDefPtr def)
{
    int ret = -1;
    char *type = NULL;
    char *format = NULL;
    xmlNodePtr cur;
    xmlNodePtr saved = ctxt->node;
    virStorageSourcePtr target;

    ctxt->node = node;

    if (!(def->name = virXMLPropString(node, "name"))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing name from disk backup element"));
        goto cleanup;
    }

    if (!(cur = virXPathNode("./target", ctxt))) {
        virReportError(VIR_ERR_XML_ERROR,
                   _("missing target for disk '%s'"), def->name);
        goto cleanup;
    }

    if (VIR_ALLOC(def->target) < 0)
        goto cleanup;
    target = def->target;

    if ((type = virXMLPropString(node, "type"))) {
        if ((target->type = virStorageTypeFromString(type)) <= 0) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("unknown disk '%s' backup type '%s'"),
                           def->name, type);
            goto cleanup;
        }
    } else {
        target->type = VIR_STORAGE_TYPE_FILE;
    }

    if (virDomainDiskSourceParse(cur, ctxt, target) < 0)
        goto cleanup;

    if ((format = virXPathString("string(./target/@format)", ctxt)) &&
        (target->format = virStorageFileFormatTypeFromString(format)) <= 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("unknown disk '%s' backup format '%s'"),
                       def->name, format);
        goto cleanup;
    }

    if (virStorageSourceIsLocalStorage(def->target)) {
        if (!target->path) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("disk '%s' backup path is not specified"),
                           def->name);
            goto cleanup;
        }
        if (target->path[0] != '/') {
            virReportError(VIR_ERR_XML_ERROR,
                           _("disk '%s' backup path '%s' must be absolute"),
                           def->name, target->path);
            goto cleanup;
        }
    }

    ret = 0;
 cleanup:
    ctxt->node = saved;

    VIR_FREE(format);
    VIR_FREE(type);
    if (ret < 0)
        virDomainBackupDiskDefClear(def);

    return ret;
}


static virDomainBackupDefPtr
virDomainBackupDefParse(xmlXPathContextPtr ctxt,
                        virCapsPtr caps ATTRIBUTE_UNUSED,
                        virDomainXMLOptionPtr xmlopt ATTRIBUTE_UNUSED,
                        unsigned int fflags ATTRIBUTE_UNUSED)
{
    virDomainBackupDefPtr def;
    virDomainBackupDefPtr ret = NULL;
    xmlNodePtr *nodes = NULL;
    size_t i;
    int n;
    struct timeval tv;

    if (VIR_ALLOC(def) < 0)
        return NULL;

    gettimeofday(&tv, NULL);

    if (!(def->name = virXPathString("string(./name)", ctxt)) &&
        virAsprintf(&def->name, "%lld", (long long)tv.tv_sec) < 0)
        goto cleanup;

    def->description = virXPathString("string(./description)", ctxt);
    def->creationTime = tv.tv_sec;

    if ((n = virXPathNodeSet("./disk", ctxt, &nodes)) < 0)
        goto cleanup;

    if (n == 0) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("no disk is specified to be backed up"));
        goto cleanup;
    }

    if (VIR_ALLOC_N(def->disks, n) < 0)
        goto cleanup;
    def->ndisks = n;

    for (i = 0; i < def->ndisks; i++) {
        if (virDomainBackupDiskDefParseXML(nodes[i], ctxt, &def->disks[i]) < 0)
            goto cleanup;
    }

    ret = def;
    def = NULL;

 cleanup:
    VIR_FREE(nodes);
    virDomainBackupDefFree(def);

    return ret;
}


virDomainBackupDefPtr
virDomainBackupDefParseNode(xmlDocPtr xml,
                            xmlNodePtr root,
                            virCapsPtr caps,
                            virDomainXMLOptionPtr xmlopt,
                            unsigned int flags)
{
    xmlXPathContextPtr ctxt = NULL;
    virDomainBackupDefPtr def = NULL;

    if (!xmlStrEqual(root->name, BAD_CAST "domainbackup")) {
        virReportError(VIR_ERR_XML_ERROR, "%s", _("domainbackup"));
        goto cleanup;
    }

    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        virReportOOMError();
        goto cleanup;
    }

    ctxt->node = root;
    def = virDomainBackupDefParse(ctxt, caps, xmlopt, flags);

 cleanup:
    xmlXPathFreeContext(ctxt);

    return def;
}


virDomainBackupDefPtr
virDomainBackupDefParseString(const char *xmlStr,
                              virCapsPtr caps,
                              virDomainXMLOptionPtr xmlopt,
                              unsigned int flags)
{
    virDomainBackupDefPtr ret = NULL;
    xmlDocPtr xml;
    int keepBlanksDefault = xmlKeepBlanksDefault(0);

    if ((xml = virXMLParse(NULL, xmlStr, _("(domain_backup)")))) {
        xmlKeepBlanksDefault(keepBlanksDefault);
        ret = virDomainBackupDefParseNode(xml, xmlDocGetRootElement(xml),
                                          caps, xmlopt, flags);
        xmlFreeDoc(xml);
    }
    xmlKeepBlanksDefault(keepBlanksDefault);

    return ret;
}


void
virDomainBackupDefFree(virDomainBackupDefPtr def)
{
    size_t i;

    if (!def)
        return;

    VIR_FREE(def->name);
    VIR_FREE(def->description);

    for (i = 0; i < def->ndisks; i++)
        virDomainBackupDiskDefClear(&def->disks[i]);
    VIR_FREE(def->disks);

    VIR_FREE(def);
}
