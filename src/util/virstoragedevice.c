/*
 * virstoragedevice.c: utility functions to share storage device mgmt
 *                     between storage pools and domains
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

#include "device_conf.h"

#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"
#include "virstoragedevice.h"
#include "virstring.h"
#include "virutil.h"
#include "virxml.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

VIR_LOG_INIT("util.virstoragedevice");

VIR_ENUM_IMPL(virStorageAdapter,
              VIR_STORAGE_ADAPTER_TYPE_LAST,
              "default", "scsi_host", "fc_host")


int
virStorageAdapterVHBAParseXML(xmlNodePtr node,
                              virStorageAdapterFCHostPtr fchost)
{
    char *managed = NULL;

    fchost->parent = virXMLPropString(node, "parent");

    if ((managed = virXMLPropString(node, "managed"))) {
        if ((fchost->managed =
             virTristateBoolTypeFromString(managed)) < 0) {
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


int
virStorageAdapterParseXML(virStorageAdapterPtr adapter,
                          xmlNodePtr node,
                          xmlXPathContextPtr ctxt)
{
    int ret = -1;
    xmlNodePtr relnode = ctxt->node;
    char *adapter_type = NULL;
    virStorageAdapterSCSIHostPtr scsi_host;

    ctxt->node = node;

    if ((adapter_type = virXMLPropString(node, "type"))) {
        if ((adapter->type =
             virStorageAdapterTypeFromString(adapter_type)) <= 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Unknown pool adapter type '%s'"),
                           adapter_type);
            goto cleanup;
        }

        if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_FC_HOST) {
            if (virStorageAdapterVHBAParseXML(node, &adapter->data.fchost) < 0)
                goto cleanup;
        }

        if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST) {
            scsi_host = &adapter->data.scsi_host;

            scsi_host->name = virXMLPropString(node, "name");
            if (virXPathNode("./parentaddr", ctxt)) {
                xmlNodePtr addrnode = virXPathNode("./parentaddr/address",
                                                   ctxt);
                virPCIDeviceAddressPtr addr = &scsi_host->parentaddr;
                if (!addrnode) {
                    virReportError(VIR_ERR_XML_ERROR, "%s",
                                   _("Missing scsi_host PCI address element"));
                    goto cleanup;
                }
                scsi_host->has_parent = true;
                if (virPCIDeviceAddressParseXML(addrnode, addr) < 0)
                    goto cleanup;
                if ((virXPathInt("string(./parentaddr/@unique_id)",
                                 ctxt,
                                 &scsi_host->unique_id) < 0) ||
                    (scsi_host->unique_id < 0)) {
                    virReportError(VIR_ERR_XML_ERROR, "%s",
                                   _("Missing or invalid scsi adapter "
                                     "'unique_id' value"));
                    goto cleanup;
                }
            }
        }
    } else {
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
            goto cleanup;
        }

        if (virXPathNode("./parentaddr", ctxt)) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("Use of 'parent' element requires use "
                             "of the adapter 'type'"));
            goto cleanup;
        }

        /* To keep back-compat, 'type' is not required to specify
         * for scsi_host adapter.
         */
        if ((adapter->data.scsi_host.name = virXMLPropString(node, "name")))
            adapter->type = VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST;
    }

    ret = 0;

 cleanup:
    ctxt->node = relnode;
    VIR_FREE(adapter_type);
    return ret;
}


void
virStorageAdapterVHBAFormat(virBufferPtr buf,
                            virStorageAdapterFCHostPtr fchost)
{
    virBufferEscapeString(buf, " parent='%s'", fchost->parent);
    virBufferEscapeString(buf, " parent_wwnn='%s'", fchost->parent_wwnn);
    virBufferEscapeString(buf, " parent_wwpn='%s'", fchost->parent_wwpn);
    virBufferEscapeString(buf, " parent_fabric_wwn='%s'",
                          fchost->parent_fabric_wwn);
    if (fchost->managed != VIR_TRISTATE_BOOL_ABSENT)
        virBufferAsprintf(buf, " managed='%s'",
                          virTristateBoolTypeToString(fchost->managed));

    virBufferAsprintf(buf, " wwnn='%s' wwpn='%s'/>\n",
                      fchost->wwnn, fchost->wwpn);
}


void
virStorageAdapterFormat(virBufferPtr buf,
                        virStorageAdapterPtr adapter)
{
    virBufferAsprintf(buf, "<adapter type='%s'",
                          virStorageAdapterTypeToString(adapter->type));

    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_FC_HOST)
        virStorageAdapterVHBAFormat(buf, &adapter->data.fchost);

    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST) {
        virStorageAdapterSCSIHostPtr scsi_host = &adapter->data.scsi_host;

        if (scsi_host->name) {
            virBufferAsprintf(buf, " name='%s'/>\n", scsi_host->name);
        } else {
            virPCIDeviceAddress addr;
            virBufferAddLit(buf, ">\n");
            virBufferAdjustIndent(buf, 2);
            virBufferAsprintf(buf, "<parentaddr unique_id='%d'>\n",
                              scsi_host->unique_id);
            virBufferAdjustIndent(buf, 2);
            addr = scsi_host->parentaddr;
            ignore_value(virPCIDeviceAddressFormat(buf, addr, false));
            virBufferAdjustIndent(buf, -2);
            virBufferAddLit(buf, "</parentaddr>\n");
            virBufferAdjustIndent(buf, -2);
            virBufferAddLit(buf, "</adapter>\n");
        }
    }
}


void
virStorageAdapterVHBAClear(virStorageAdapterFCHostPtr fchost)
{
    VIR_FREE(fchost->parent);
    VIR_FREE(fchost->parent_wwnn);
    VIR_FREE(fchost->parent_wwpn);
    VIR_FREE(fchost->parent_fabric_wwn);
    VIR_FREE(fchost->wwnn);
    VIR_FREE(fchost->wwpn);
}


void
virStorageAdapterClear(virStorageAdapterPtr adapter)
{
    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_FC_HOST)
        virStorageAdapterVHBAClear(&adapter->data.fchost);

    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST)
        VIR_FREE(adapter->data.scsi_host.name);
}


int
virStorageAdapterVHBAParseValidate(virStorageAdapterFCHostPtr fchost)
{
    if (!fchost->wwnn || !fchost->wwpn) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("'wwnn' and 'wwpn' must be specified for "
                         "a VHBA adapter"));
        return -1;
    }

    if (!virValidateWWN(fchost->wwnn) || !virValidateWWN(fchost->wwpn))
        return -1;

    if (fchost->parent_wwnn && !virValidateWWN(fchost->parent_wwnn))
        return -1;

    if (fchost->parent_wwpn && !virValidateWWN(fchost->parent_wwpn))
        return -1;

    if (fchost->parent_fabric_wwn &&
        !virValidateWWN(fchost->parent_fabric_wwn))
        return -1;

    return 0;
}


int
virStorageAdapterParseValidate(virStorageAdapterPtr adapter)
{
    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_FC_HOST)
        return virStorageAdapterVHBAParseValidate(&adapter->data.fchost);

    if (adapter->type == VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST) {
        virStorageAdapterSCSIHostPtr scsi_host = &adapter->data.scsi_host;

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
    }

    return 0;
}
