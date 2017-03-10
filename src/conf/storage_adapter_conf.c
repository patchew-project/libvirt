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
virStorageAdapterFCHostClear(virStoragePoolSourceAdapterPtr adapter)
{
    VIR_FREE(adapter->data.fchost.wwnn);
    VIR_FREE(adapter->data.fchost.wwpn);
    VIR_FREE(adapter->data.fchost.parent);
    VIR_FREE(adapter->data.fchost.parent_wwnn);
    VIR_FREE(adapter->data.fchost.parent_wwpn);
    VIR_FREE(adapter->data.fchost.parent_fabric_wwn);
}


void
virStorageAdapterClear(virStoragePoolSourceAdapterPtr adapter)
{
    if (adapter->type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_FC_HOST)
        virStorageAdapterFCHostClear(adapter);

    if (adapter->type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        VIR_FREE(adapter->data.scsi_host.name);
}


static int
virStorageAdapterFCHostParseXML(xmlNodePtr node,
                                virStoragePoolSourcePtr source)
{
    char *managed = NULL;

    source->adapter.data.fchost.parent = virXMLPropString(node, "parent");
    if ((managed = virXMLPropString(node, "managed"))) {
        source->adapter.data.fchost.managed =
            virTristateBoolTypeFromString(managed);
        if (source->adapter.data.fchost.managed < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown fc_host managed setting '%s'"),
                           managed);
            VIR_FREE(managed);
            return -1;
        }
    }

    source->adapter.data.fchost.parent_wwnn =
        virXMLPropString(node, "parent_wwnn");
    source->adapter.data.fchost.parent_wwpn =
        virXMLPropString(node, "parent_wwpn");
    source->adapter.data.fchost.parent_fabric_wwn =
        virXMLPropString(node, "parent_fabric_wwn");

    source->adapter.data.fchost.wwpn = virXMLPropString(node, "wwpn");
    source->adapter.data.fchost.wwnn = virXMLPropString(node, "wwnn");

    VIR_FREE(managed);
    return 0;
}


static int
virStorageAdapterSCSIHostParseXML(xmlNodePtr node,
                                  xmlXPathContextPtr ctxt,
                                  virStoragePoolSourcePtr source)
{
    source->adapter.data.scsi_host.name =
        virXMLPropString(node, "name");
    if (virXPathNode("./parentaddr", ctxt)) {
        xmlNodePtr addrnode = virXPathNode("./parentaddr/address", ctxt);
        virPCIDeviceAddressPtr addr =
            &source->adapter.data.scsi_host.parentaddr;

        if (!addrnode) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("Missing scsi_host PCI address element"));
            return -1;
        }
        source->adapter.data.scsi_host.has_parent = true;
        if (virPCIDeviceAddressParseXML(addrnode, addr) < 0)
            return -1;
        if ((virXPathInt("string(./parentaddr/@unique_id)",
                         ctxt,
                         &source->adapter.data.scsi_host.unique_id) < 0) ||
            (source->adapter.data.scsi_host.unique_id < 0)) {
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
                                virStoragePoolSourcePtr source)
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
    if ((source->adapter.data.scsi_host.name =
         virXMLPropString(node, "name")))
        source->adapter.type =
            VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST;

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
            if (virStorageAdapterFCHostParseXML(node, source) < 0)
                goto cleanup;
        } else if (source->adapter.type ==
                   VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST) {
            if (virStorageAdapterSCSIHostParseXML(node, ctxt, source) < 0)
                goto cleanup;

        }
    } else {
        if (virStorageAdapterLegacyParseXML(node, ctxt, source) < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    ctxt->node = relnode;
    VIR_FREE(adapter_type);
    return ret;
}


static int
virStorageAdapterFCHostParseValidate(virStoragePoolDefPtr ret)
{
    if (!ret->source.adapter.data.fchost.wwnn ||
        !ret->source.adapter.data.fchost.wwpn) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("'wwnn' and 'wwpn' must be specified for adapter "
                         "type 'fchost'"));
        return -1;
    }

    if (!virValidateWWN(ret->source.adapter.data.fchost.wwnn) ||
        !virValidateWWN(ret->source.adapter.data.fchost.wwpn))
        return -1;

    if ((ret->source.adapter.data.fchost.parent_wwnn &&
         !ret->source.adapter.data.fchost.parent_wwpn) ||
        (!ret->source.adapter.data.fchost.parent_wwnn &&
         ret->source.adapter.data.fchost.parent_wwpn)) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("must supply both parent_wwnn and "
                         "parent_wwpn not just one or the other"));
        return -1;
    }

    if (ret->source.adapter.data.fchost.parent_wwnn &&
        !virValidateWWN(ret->source.adapter.data.fchost.parent_wwnn))
        return -1;

    if (ret->source.adapter.data.fchost.parent_wwpn &&
        !virValidateWWN(ret->source.adapter.data.fchost.parent_wwpn))
        return -1;

    if (ret->source.adapter.data.fchost.parent_fabric_wwn &&
        !virValidateWWN(ret->source.adapter.data.fchost.parent_fabric_wwn))
        return -1;

    return 0;
}


static int
virStorageAdapterSCSIHostParseValidate(virStoragePoolDefPtr ret)
{
    if (!ret->source.adapter.data.scsi_host.name &&
        !ret->source.adapter.data.scsi_host.has_parent) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("Either 'name' or 'parent' must be specified "
                         "for the 'scsi_host' adapter"));
        return -1;
    }

    if (ret->source.adapter.data.scsi_host.name &&
        ret->source.adapter.data.scsi_host.has_parent) {
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
        return virStorageAdapterFCHostParseValidate(ret);

    if (ret->source.adapter.type ==
        VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        return virStorageAdapterSCSIHostParseValidate(ret);

    return 0;
}


static void
virStorageAdapterFCHostFormat(virBufferPtr buf,
                              virStoragePoolSourcePtr src)
{
    virBufferEscapeString(buf, " parent='%s'",
                          src->adapter.data.fchost.parent);
    if (src->adapter.data.fchost.managed)
        virBufferAsprintf(buf, " managed='%s'",
                          virTristateBoolTypeToString(src->adapter.data.fchost.managed));
    virBufferEscapeString(buf, " parent_wwnn='%s'",
                          src->adapter.data.fchost.parent_wwnn);
    virBufferEscapeString(buf, " parent_wwpn='%s'",
                          src->adapter.data.fchost.parent_wwpn);
    virBufferEscapeString(buf, " parent_fabric_wwn='%s'",
                          src->adapter.data.fchost.parent_fabric_wwn);

    virBufferAsprintf(buf, " wwnn='%s' wwpn='%s'/>\n",
                      src->adapter.data.fchost.wwnn,
                      src->adapter.data.fchost.wwpn);
}


static void
virStorageAdapterSCSIHostFormat(virBufferPtr buf,
                                virStoragePoolSourcePtr src)
{
    if (src->adapter.data.scsi_host.name) {
        virBufferAsprintf(buf, " name='%s'/>\n",
                          src->adapter.data.scsi_host.name);
    } else {
        virPCIDeviceAddress addr;
        virBufferAddLit(buf, ">\n");
        virBufferAdjustIndent(buf, 2);
        virBufferAsprintf(buf, "<parentaddr unique_id='%d'>\n",
                          src->adapter.data.scsi_host.unique_id);
        virBufferAdjustIndent(buf, 2);
        addr = src->adapter.data.scsi_host.parentaddr;
        ignore_value(virPCIDeviceAddressFormat(buf, addr, false));
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
        virStorageAdapterFCHostFormat(buf, src);

    if (src->adapter.type == VIR_STORAGE_POOL_SOURCE_ADAPTER_TYPE_SCSI_HOST)
        virStorageAdapterSCSIHostFormat(buf, src);
}
