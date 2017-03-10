/*
 * storage_adapter_conf.c: helpers to handle storage pool adapter manipulation
 *                         (derived from storage_conf.c)
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

#include "storage_adapter_conf.h"

#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"
#include "virstring.h"
#include "virutil.h"
#include "virxml.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("conf.storage_adapter_conf");

VIR_ENUM_IMPL(virStoragePoolSourceAdapter,
              VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_LAST,
              "default", "scsi_host", "fc_host")


static void
virStorageAdapterFCHostClear(virStorageAdapterFCHostPtr fchost)
{
    VIR_FREE(fchost->wwnn);
    VIR_FREE(fchost->wwpn);
    VIR_FREE(fchost->parent);
    VIR_FREE(fchost->parent_wwnn);
    VIR_FREE(fchost->parent_wwpn);
    VIR_FREE(fchost->parent_fabric_wwn);
}


void
virStorageAdapterClear(virStoragePoolSourceAdapterPtr adapter)
{
    if (adapter->type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST)
        virStorageAdapterFCHostClear(&adapter->data.fchost);

    if (adapter->type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        VIR_FREE(adapter->data.scsi_host.name);
}


static int
virStorageAdapterFCHostParseXML(xmlNodePtr node,
                                virStorageAdapterFCHostPtr fchost)
{
    char *managed = NULL;

    fchost->parent = virXMLPropString(node, "parent");
    if ((managed = virXMLPropString(node, "managed"))) {
        if ((fchost->managed = virTristateBoolTypeFromString(managed)) < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown fc_host managed setting '%s'"),
                           managed);
            VIR_FREE(managed);
            return -1;
        }
    }

    fchost->parent_wwnn = virXMLPropString(node, "parent_wwnn");
    fchost->parent_wwpn = virXMLPropString(node, "parent_wwpn");
    fchost->parent_fabric_wwn = virXMLPropString(node, "parent_fabric_wwn");
    fchost->wwpn = virXMLPropString(node, "wwpn");
    fchost->wwnn = virXMLPropString(node, "wwnn");

    VIR_FREE(managed);
    return 0;
}


static int
virStorageAdapterSCSIHostParseXML(xmlNodePtr node,
                                  xmlXPathContextPtr ctxt,
                                  virStorageAdapterSCSIHostPtr scsi_host)
{
    scsi_host->name = virXMLPropString(node, "name");
    if (virXPathNode("./parentaddr", ctxt)) {
        xmlNodePtr addrnode = virXPathNode("./parentaddr/address", ctxt);

        if (!addrnode) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("Missing scsi_host PCI address element"));
            return -1;
        }
        scsi_host->has_parent = true;
        if (virPCIDeviceAddressParseXML(addrnode, &scsi_host->parentaddr) < 0)
            return -1;
        if ((virXPathInt("string(./parentaddr/@unique_id)",
                         ctxt,
                         &scsi_host->unique_id) < 0) ||
            (scsi_host->unique_id < 0)) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("Missing or invalid scsi adapter "
                             "'unique_id' value"));
            return -1;
        }
    }

    return 0;
}


static int
virStorageAdapterLegacyParseXML(xmlNodePtr node,
                                xmlXPathContextPtr ctxt,
                                virStoragePoolSourceAdapterPtr adapter)
{
    char *wwnn = virXMLPropString(node, "wwnn");
    char *wwpn = virXMLPropString(node, "wwpn");
    char *parent = virXMLPropString(node, "parent");

    /* "type" was not specified in the XML, so we must verify that
     * "wwnn", "wwpn", "parent", or "parentaddr" are also not in the
     * XML. If any are found, then we cannot just use "name" alone".
     */
    if (wwnn || wwpn || parent) {
        VIR_FREE(wwnn);
        VIR_FREE(wwpn);
        VIR_FREE(parent);
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("Use of 'wwnn', 'wwpn', and 'parent' attributes "
                         "requires use of the adapter 'type'"));
        return -1;
    }

    if (virXPathNode("./parentaddr", ctxt)) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("Use of 'parent' element requires use "
                         "of the adapter 'type'"));
        return -1;
    }

    /* To keep back-compat, 'type' is not required to specify
     * for scsi_host adapter.
     */
    if ((adapter->data.scsi_host.name = virXMLPropString(node, "name")))
        adapter->type = VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST;

    return 0;
}


int
virStorageAdapterParseXML(virStoragePoolSourcePtr source,
                          xmlNodePtr node,
                          xmlXPathContextPtr ctxt)
{
    int ret = -1;
    xmlNodePtr relnode = ctxt->node;
    char *adapter_type = NULL;

    ctxt->node = node;

    if ((adapter_type = virXMLPropString(node, "type"))) {
        if ((source->adapter.type =
             virStoragePoolSourceAdapterTypeFromString(adapter_type)) <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Unknown pool adapter type '%s'"),
                           adapter_type);
            goto cleanup;
        }

        if (source->adapter.type ==
            VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST) {
            if (virStorageAdapterFCHostParseXML(node, &source->adapter.data.fchost) < 0)
                goto cleanup;
        } else if (source->adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST) {
            if (virStorageAdapterSCSIHostParseXML(node, ctxt, &source->adapter.data.scsi_host) < 0)
                goto cleanup;

        }
    } else {
        if (virStorageAdapterLegacyParseXML(node, ctxt, &source->adapter) < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    ctxt->node = relnode;
    VIR_FREE(adapter_type);
    return ret;
}


static int
virStorageAdapterFCHostParseValidate(virStorageAdapterFCHostPtr fchost)
{
    if (!fchost->wwnn || !fchost->wwpn) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("'wwnn' and 'wwpn' must be specified for adapter "
                         "type 'fchost'"));
        return -1;
    }

    if (!virValidateWWN(fchost->wwnn) || !virValidateWWN(fchost->wwpn))
        return -1;

    if ((fchost->parent_wwnn && !fchost->parent_wwpn) ||
        (!fchost->parent_wwnn && fchost->parent_wwpn)) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("must supply both parent_wwnn and "
                         "parent_wwpn not just one or the other"));
        return -1;
    }

    if (fchost->parent_wwnn && !virValidateWWN(fchost->parent_wwnn))
        return -1;

    if (fchost->parent_wwpn && !virValidateWWN(fchost->parent_wwpn))
        return -1;

    if (fchost->parent_fabric_wwn && !virValidateWWN(fchost->parent_fabric_wwn))
        return -1;

    return 0;
}


static int
virStorageAdapterSCSIHostParseValidate(virStorageAdapterSCSIHostPtr scsi_host)
{
    if (!scsi_host->name && !scsi_host->has_parent) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("Either 'name' or 'parent' must be specified "
                         "for the 'scsi_host' adapter"));
        return -1;
    }

    if (scsi_host->name && scsi_host->has_parent) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("Both 'name' and 'parent' cannot be specified "
                         "for the 'scsi_host' adapter"));
        return -1;
    }

    return 0;
}


int
virStorageAdapterParseValidate(virStoragePoolDefPtr ret)
{
    if (!ret->source.adapter.type) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing storage pool source adapter"));
        return -1;
    }

    if (ret->source.adapter.type ==
        VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST)
        return virStorageAdapterFCHostParseValidate(&ret->source.adapter.data.fchost);

    if (ret->source.adapter.type ==
        VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        return virStorageAdapterSCSIHostParseValidate(&ret->source.adapter.data.scsi_host);

    return 0;
}


static void
virStorageAdapterFCHostFormat(virBufferPtr buf,
                              virStorageAdapterFCHostPtr fchost)
{
    virBufferEscapeString(buf, " parent='%s'", fchost->parent);
    if (fchost->managed)
        virBufferAsprintf(buf, " managed='%s'",
                          virTristateBoolTypeToString(fchost->managed));
    virBufferEscapeString(buf, " parent_wwnn='%s'", fchost->parent_wwnn);
    virBufferEscapeString(buf, " parent_wwpn='%s'", fchost->parent_wwpn);
    virBufferEscapeString(buf, " parent_fabric_wwn='%s'",
                          fchost->parent_fabric_wwn);

    virBufferAsprintf(buf, " wwnn='%s' wwpn='%s'/>\n",
                      fchost->wwnn, fchost->wwpn);
}


static void
virStorageAdapterSCSIHostFormat(virBufferPtr buf,
                                virStorageAdapterSCSIHostPtr scsi_host)
{
    if (scsi_host->name) {
        virBufferAsprintf(buf, " name='%s'/>\n", scsi_host->name);
    } else {
        virBufferAddLit(buf, ">\n");
        virBufferAdjustIndent(buf, 2);
        virBufferAsprintf(buf, "<parentaddr unique_id='%d'>\n",
                          scsi_host->unique_id);
        virBufferAdjustIndent(buf, 2);
        ignore_value(virPCIDeviceAddressFormat(buf, scsi_host->parentaddr,
                                               false));
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</parentaddr>\n");
        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</adapter>\n");
    }
}


void
virStorageAdapterFormat(virBufferPtr buf,
                        virStoragePoolSourcePtr src)
{
    virBufferAsprintf(buf, "<adapter type='%s'",
                      virStoragePoolSourceAdapterTypeToString(src->adapter.type));

    if (src->adapter.type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST)
        virStorageAdapterFCHostFormat(buf, &src->adapter.data.fchost);

    if (src->adapter.type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        virStorageAdapterSCSIHostFormat(buf, &src->adapter.data.scsi_host);
}
