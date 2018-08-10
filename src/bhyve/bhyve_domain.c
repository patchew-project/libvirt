/*
 * bhyve_domain.c: bhyve domain private state
 *
 * Copyright (C) 2014 Roman Bogorodskiy
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
 * Author: Roman Bogorodskiy
 */

#include <libxml/xpathInternals.h>
#include <config.h>

#include "bhyve_conf.h"
#include "bhyve_device.h"
#include "bhyve_domain.h"
#include "bhyve_capabilities.h"
#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_BHYVE

VIR_LOG_INIT("bhyve.bhyve_domain");

#define BHYVE_NAMESPACE_HREF "http://libvirt.org/schemas/domain/bhyve/1.0"

static void *
bhyveDomainObjPrivateAlloc(void *opaque ATTRIBUTE_UNUSED)
{
    bhyveDomainObjPrivatePtr priv;

    if (VIR_ALLOC(priv) < 0)
        return NULL;

    return priv;
}

static void
bhyveDomainObjPrivateFree(void *data)
{
    bhyveDomainObjPrivatePtr priv = data;

    virDomainPCIAddressSetFree(priv->pciaddrs);

    VIR_FREE(priv);
}

virDomainXMLPrivateDataCallbacks virBhyveDriverPrivateDataCallbacks = {
    .alloc = bhyveDomainObjPrivateAlloc,
    .free = bhyveDomainObjPrivateFree,
};

static void
bhyveDomainDefNamespaceFree(void *nsdata)
{
    bhyveDomainDefPtr def = nsdata;
    
    bhyveDomainDefFree(def);
}

static int
bhyveDomainDefNamespaceParse(xmlDocPtr xml ATTRIBUTE_UNUSED,
                            xmlNodePtr root ATTRIBUTE_UNUSED,
                            xmlXPathContextPtr ctxt,
                            void **data)
{
    bhyveDomainDefPtr domainDef = NULL;
    char *lpcslotnumberstring = NULL;

    if (xmlXPathRegisterNs(ctxt, BAD_CAST "bhyve", BAD_CAST BHYVE_NAMESPACE_HREF) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to register xml namespace '%s'"),
                       BHYVE_NAMESPACE_HREF);
        return -1;
    }

    xmlNodePtr lpcnode = virXPathNode("./bhyve:lpcslotnumber", ctxt);
    if  (lpcnode == NULL) 
        return 0;

    if (VIR_ALLOC(domainDef) < 0)
        return -1;

    lpcslotnumberstring = virXMLPropString(lpcnode, "value");
    if (lpcslotnumberstring == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
            "%s", _("Bhyve lpcslotnumber value property not found"));
        goto error;
    } else if (virStrToLong_ui(lpcslotnumberstring, NULL, 10, &domainDef->lpc_slot_number) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
            "%s got %s", _("Bhyve lpcslotnumber value property must be integer"), lpcslotnumberstring);
        VIR_FREE(lpcslotnumberstring);
        goto error;
    }
    VIR_FREE(lpcslotnumberstring);

    *data = domainDef;
    return 0;
 error:
    bhyveDomainDefFree(domainDef);
    return -1;
}

static int
bhyveDomainDefNamespaceFormatXML(virBufferPtr buf,
                                void *nsdata)
{
    bhyveDomainDefPtr domainDef = nsdata;

    if (domainDef) {
        virBufferAsprintf(buf, "<bhyve:lpcslotnumber value='%d'/>\n",
            domainDef->lpc_slot_number);
    }

    return 0;
}

static const char *
bhyveDomainDefNamespaceHref(void)
{
    return "xmlns:bhyve='" BHYVE_NAMESPACE_HREF "'";
}

virDomainXMLNamespace virBhyveDriverDomainXMLNamespace = {
    .parse = bhyveDomainDefNamespaceParse,
    .free = bhyveDomainDefNamespaceFree,
    .format = bhyveDomainDefNamespaceFormatXML,
    .href = bhyveDomainDefNamespaceHref,
};

static int
bhyveDomainDefPostParse(virDomainDefPtr def,
                        virCapsPtr caps ATTRIBUTE_UNUSED,
                        unsigned int parseFlags ATTRIBUTE_UNUSED,
                        void *opaque ATTRIBUTE_UNUSED,
                        void *parseOpaque ATTRIBUTE_UNUSED)
{
    /* Add an implicit PCI root controller */
    if (virDomainDefMaybeAddController(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0,
                                       VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT) < 0)
        return -1;

    return 0;
}

static int
bhyveDomainDiskDefAssignAddress(bhyveConnPtr driver,
                                virDomainDiskDefPtr def,
                                const virDomainDef *vmdef ATTRIBUTE_UNUSED)
{
    int idx = virDiskNameToIndex(def->dst);

    if (idx < 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Unknown disk name '%s' and no address specified"),
                       def->dst);
        return -1;
    }

    switch (def->bus) {
    case VIR_DOMAIN_DISK_BUS_SATA:
        def->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DRIVE;

        if ((driver->bhyvecaps & BHYVE_CAP_AHCI32SLOT) != 0) {
            def->info.addr.drive.controller = idx / 32;
            def->info.addr.drive.unit = idx % 32;
        } else {
            def->info.addr.drive.controller = idx;
            def->info.addr.drive.unit = 0;
        }

        def->info.addr.drive.bus = 0;
        break;
    }
    return 0;
}

static int
bhyveDomainDeviceDefPostParse(virDomainDeviceDefPtr dev,
                              const virDomainDef *def,
                              virCapsPtr caps ATTRIBUTE_UNUSED,
                              unsigned int parseFlags ATTRIBUTE_UNUSED,
                              void *opaque,
                              void *parseOpaque ATTRIBUTE_UNUSED)
{
    bhyveConnPtr driver = opaque;

    if (dev->type == VIR_DOMAIN_DEVICE_DISK) {
        virDomainDiskDefPtr disk = dev->data.disk;

        if (disk->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE &&
            bhyveDomainDiskDefAssignAddress(driver, disk, def) < 0)
            return -1;
    }

    if (dev->type == VIR_DOMAIN_DEVICE_CONTROLLER) {
        virDomainControllerDefPtr cont = dev->data.controller;

        if (cont->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI &&
            (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT ||
             cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT) &&
            cont->idx != 0) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("pci-root and pcie-root controllers "
                             "should have index 0"));
            return -1;
        }
    }

    return 0;
}

static int
bhyveDomainDefAssignAddresses(virDomainDef *def,
                              virCapsPtr caps ATTRIBUTE_UNUSED,
                              unsigned int parseFlags ATTRIBUTE_UNUSED,
                              void *opaque ATTRIBUTE_UNUSED,
                              void *parseOpaque ATTRIBUTE_UNUSED)
{
    if (bhyveDomainAssignAddresses(def, NULL) < 0)
        return -1;

    return 0;
}

virDomainXMLOptionPtr
virBhyveDriverCreateXMLConf(bhyveConnPtr driver)
{
    virBhyveDriverDomainDefParserConfig.priv = driver;
    return virDomainXMLOptionNew(&virBhyveDriverDomainDefParserConfig,
                                 &virBhyveDriverPrivateDataCallbacks,
                                 &virBhyveDriverDomainXMLNamespace, 
                                 NULL, NULL);
}

virDomainDefParserConfig virBhyveDriverDomainDefParserConfig = {
    .devicesPostParseCallback = bhyveDomainDeviceDefPostParse,
    .domainPostParseCallback = bhyveDomainDefPostParse,
    .assignAddressesCallback = bhyveDomainDefAssignAddresses,
};
