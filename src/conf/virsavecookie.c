/**
 * virsavecookie.c: Save cookie handling
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virerror.h"
#include "virlog.h"
#include "virobject.h"
#include "virbuffer.h"
#include "virxml.h"
#include "virsavecookie.h"

#define VIR_FROM_THIS VIR_FROM_CONF

VIR_LOG_INIT("conf.savecookie");


static int
virSaveCookieParseNode(xmlXPathContextPtr ctxt,
                       virObjectPtr *obj,
                       virSaveCookieCallbacksPtr saveCookie)
{
    *obj = NULL;

    if (!virXMLNodeNameEqual(ctxt->node, "cookie")) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("XML does not contain expected 'cookie' element"));
        return -1;
    }

    if (!saveCookie || !saveCookie->parse)
        return 0;

    return saveCookie->parse(ctxt, obj);
}


int
virSaveCookieParse(xmlXPathContextPtr ctxt,
                   virObjectPtr *obj,
                   virSaveCookieCallbacksPtr saveCookie)
{
    VIR_XPATH_NODE_AUTORESTORE(ctxt);
    int ret = -1;

    *obj = NULL;

    if (!(ctxt->node = virXPathNode("./cookie", ctxt))) {
        ret = 0;
        goto cleanup;
    }

    ret = virSaveCookieParseNode(ctxt, obj, saveCookie);

 cleanup:
    return ret;
}


int
virSaveCookieParseString(const char *xml,
                         virObjectPtr *obj,
                         virSaveCookieCallbacksPtr saveCookie)
{
    xmlDocPtr doc = NULL;
    xmlXPathContextPtr ctxt = NULL;
    int ret = -1;

    *obj = NULL;

    if (!xml) {
        ret = 0;
        goto cleanup;
    }

    if (!(doc = virXMLParseStringCtxt(xml, _("(save cookie)"), &ctxt)))
        goto cleanup;

    ret = virSaveCookieParseNode(ctxt, obj, saveCookie);

 cleanup:
    xmlXPathFreeContext(ctxt);
    xmlFreeDoc(doc);
    return ret;
}


int
virSaveCookieFormatBuf(virBufferPtr buf,
                       virObjectPtr obj,
                       virSaveCookieCallbacksPtr saveCookie)
{
    if (!obj || !saveCookie || !saveCookie->format)
        return 0;

    virBufferAddLit(buf, "<cookie>\n");
    virBufferAdjustIndent(buf, 2);

    if (saveCookie->format(buf, obj) < 0)
        return -1;

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</cookie>\n");

    return 0;
}


char *
virSaveCookieFormat(virObjectPtr obj,
                    virSaveCookieCallbacksPtr saveCookie)
{
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    if (virSaveCookieFormatBuf(&buf, obj, saveCookie) < 0)
        return NULL;

    return virBufferContentAndReset(&buf);
}
