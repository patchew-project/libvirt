/*
 * backup_conf.c: domain backup XML processing
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

VIR_ENUM_IMPL(virDomainBackupAddress, VIR_DOMAIN_BACKUP_ADDRESS_LAST,
              "ip",
              "unix")

static int
virDomainBackupDiskDefParseXML(xmlNodePtr node,
                               xmlXPathContextPtr ctxt,
                               virDomainBackupDiskDefPtr def)
{
    int ret = -1;
    char *present = NULL;
    char *type = NULL;
    xmlNodePtr cur;
    xmlNodePtr saved = ctxt->node;

    ctxt->node = node;

    if ((type = virXMLPropString(node, "type")) &&
        virStorageTypeFromString(type) != VIR_STORAGE_TYPE_FILE) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("'file' is the only supported backup source type"));
        goto cleanup;
    }

    if ((cur = virXPathNode("./source", ctxt))) {
        if (VIR_ALLOC(def->src) < 0)
            goto cleanup;

        def->src->type = VIR_STORAGE_TYPE_FILE;
        def->src->format = VIR_STORAGE_FILE_QCOW2;

        if (virDomainDiskSourceParse(cur, ctxt, def->src) < 0)
            goto cleanup;
    }

    def->name = virXMLPropString(node, "name");
    if (!def->name) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("missing name from disk backup element"));
        goto cleanup;
    }

    present = virXMLPropString(node, "present");
    if (present && (def->present = virTristateBoolTypeFromString(present)) <= 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Invalid disk '%s' present attribute value"),
                       def->name);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(present);
    VIR_FREE(type);

    ctxt->node = saved;

    return ret;
}

static int
virDomainBackupAddressDefParseXML(xmlNodePtr node,
                                  virDomainBackupAddressDefPtr def)
{
    char *type = virXMLPropString(node, "type");
    char *port = NULL;
    int ret = -1;

    if (!type) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("backup address type must be specified"));
        goto cleanup;
    }

    if ((def->type = virDomainBackupAddressTypeFromString(type)) < 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("unknown backup address type '%s'"), type);
        goto cleanup;
    }

    switch (def->type) {
    case VIR_DOMAIN_BACKUP_ADDRESS_IP:
        def->data.ip.host = virXMLPropString(node, "host");
        port = virXMLPropString(node, "port");
        if (!def->data.ip.host || !port) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("both host and port must be specified "
                             "for ip address type"));
            goto cleanup;
        }
        if (virStrToLong_i(port, NULL, 10, &def->data.ip.port) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("cannot parse port %s"), port);
            goto cleanup;
        }
        break;
    case VIR_DOMAIN_BACKUP_ADDRESS_UNIX:
        def->data.socket.path = virXMLPropString(node, "path");
        if (!def->data.socket.path) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("path must be specified for unix address type"));
            goto cleanup;
        }
        break;
    }

    ret = 0;

 cleanup:
    VIR_FREE(type);
    VIR_FREE(port);

    return ret;
}

static virDomainBackupDefPtr
virDomainBackupDefParse(xmlXPathContextPtr ctxt)
{
    virDomainBackupDefPtr def = NULL;
    virDomainBackupDefPtr ret = NULL;
    xmlNodePtr *nodes = NULL, node;
    size_t i;
    int n;

    if (VIR_ALLOC(def) < 0)
        goto cleanup;

    if (!(node = virXPathNode("./address[1]", ctxt))) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("export address must be specifed"));
        goto cleanup;
    }

    if (virDomainBackupAddressDefParseXML(node, &def->address) < 0)
        goto cleanup;

    if ((n = virXPathNodeSet("./disks/*", ctxt, &nodes)) < 0)
        goto cleanup;

    if (n && VIR_ALLOC_N(def->disks, n) < 0)
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
                            virCapsPtr caps ATTRIBUTE_UNUSED,
                            virDomainXMLOptionPtr xmlopt ATTRIBUTE_UNUSED,
                            unsigned int flags)
{
    xmlXPathContextPtr ctxt = NULL;
    virDomainBackupDefPtr def = NULL;

    virCheckFlags(0, NULL);

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
    def = virDomainBackupDefParse(ctxt);

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

static
void virDomainBackupAddressDefFree(virDomainBackupAddressDefPtr def)
{
    switch (def->type) {
    case VIR_DOMAIN_BACKUP_ADDRESS_IP:
        VIR_FREE(def->data.ip.host);
        break;
    case VIR_DOMAIN_BACKUP_ADDRESS_UNIX:
        VIR_FREE(def->data.socket.path);
        break;
    }
}

void virDomainBackupDefFree(virDomainBackupDefPtr def)
{
    size_t i;

    if (!def)
        return;

    for (i = 0; i < def->ndisks; i++) {
        virDomainBackupDiskDefPtr disk = &def->disks[i];

        virStorageSourceFree(disk->src);
        VIR_FREE(disk->name);
    }
    VIR_FREE(def->disks);

    virDomainBackupAddressDefFree(&def->address);

    VIR_FREE(def);
}
