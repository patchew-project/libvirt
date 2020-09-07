/*
 * cpu_map.c: internal functions for handling CPU mapping configuration
 *
 * Copyright (C) 2009-2018 Red Hat, Inc.
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

#include "viralloc.h"
#include "virfile.h"
#include "cpu.h"
#include "cpu_map.h"
#include "configmake.h"
#include "virstring.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_CPU

VIR_LOG_INIT("cpu.cpu_map");

G_DEFINE_AUTOPTR_CLEANUP_FUNC(xmlNodePtr, g_free);

static int
loadData(const char *mapfile,
         xmlXPathContextPtr ctxt,
         const char *element,
         cpuMapLoadCallback callback,
         void *data)
{
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    g_autoptr(xmlNodePtr) nodes = NULL;
    int n;
    size_t i;
    int rv;

    if ((n = virXPathNodeSet(element, ctxt, &nodes)) < 0)
        return -1;

    if (n > 0 && !callback) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unexpected element '%s' in CPU map '%s'"), element, mapfile);
        return -1;
    }

    for (i = 0; i < n; i++) {
        char *name = virXMLPropString(nodes[i], "name");
        if (!name) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("cannot find %s name in CPU map '%s'"), element, mapfile);
            return -1;
        }
        VIR_DEBUG("Load %s name %s", element, name);
        ctxt->node = nodes[i];
        rv = callback(ctxt, name, data);
        VIR_FREE(name);
        if (rv < 0)
            return -1;
    }

    return 0;
}

static int
cpuMapLoadInclude(const char *filename,
                  cpuMapLoadCallback vendorCB,
                  cpuMapLoadCallback featureCB,
                  cpuMapLoadCallback modelCB,
                  void *data)
{
    xmlDocPtr xml = NULL;
    xmlXPathContextPtr ctxt = NULL;
    int ret = -1;
    char *mapfile;

    if (!(mapfile = virFileFindResource(filename,
                                        abs_top_srcdir "/src/cpu_map",
                                        PKGDATADIR "/cpu_map")))
        return -1;

    VIR_DEBUG("Loading CPU map include from %s", mapfile);

    if (!(xml = virXMLParseFileCtxt(mapfile, &ctxt)))
        goto cleanup;

    ctxt->node = xmlDocGetRootElement(xml);

    if (loadData(mapfile, ctxt, "vendor", vendorCB, data) < 0)
        goto cleanup;

    if (loadData(mapfile, ctxt, "feature", featureCB, data) < 0)
        goto cleanup;

    if (loadData(mapfile, ctxt, "model", modelCB, data) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    xmlXPathFreeContext(ctxt);
    xmlFreeDoc(xml);
    VIR_FREE(mapfile);

    return ret;
}


static int
loadIncludes(xmlXPathContextPtr ctxt,
             cpuMapLoadCallback vendorCB,
             cpuMapLoadCallback featureCB,
             cpuMapLoadCallback modelCB,
             void *data)
{
    int ret = -1;
    VIR_XPATH_NODE_AUTORESTORE(ctxt)
    xmlNodePtr *nodes = NULL;
    int n;
    size_t i;

    n = virXPathNodeSet("include", ctxt, &nodes);
    if (n < 0)
        goto cleanup;

    for (i = 0; i < n; i++) {
        char *filename = virXMLPropString(nodes[i], "filename");
        if (!filename) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Missing 'filename' in CPU map include"));
            goto cleanup;
        }
        VIR_DEBUG("Finding CPU map include '%s'", filename);
        if (cpuMapLoadInclude(filename, vendorCB, featureCB, modelCB, data) < 0) {
            VIR_FREE(filename);
            goto cleanup;
        }
        VIR_FREE(filename);
    }

    ret = 0;

 cleanup:
    VIR_FREE(nodes);

    return ret;
}


int cpuMapLoad(const char *arch,
               cpuMapLoadCallback vendorCB,
               cpuMapLoadCallback featureCB,
               cpuMapLoadCallback modelCB,
               void *data)
{
    g_autoptr(xmlDoc) xml = NULL;
    g_autoptr(xmlXPathContext) ctxt = NULL;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    g_autofree char *xpath = NULL;
    g_autofree char *mapfile = NULL;

    if (!(mapfile = virFileFindResource("index.xml",
                                        abs_top_srcdir "/src/cpu_map",
                                        PKGDATADIR "/cpu_map")))
        return -1;

    VIR_DEBUG("Loading '%s' CPU map from %s", NULLSTR(arch), mapfile);

    if (arch == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("undefined hardware architecture"));
        return -1;
    }

    if (!(xml = virXMLParseFileCtxt(mapfile, &ctxt)))
        return -1;

    virBufferAsprintf(&buf, "./arch[@name='%s']", arch);

    xpath = virBufferContentAndReset(&buf);

    ctxt->node = xmlDocGetRootElement(xml);

    if ((ctxt->node = virXPathNode(xpath, ctxt)) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot find CPU map for %s architecture"), arch);
        return -1;
    }

    if (loadData(mapfile, ctxt, "vendor", vendorCB, data) < 0)
        return -1;

    if (loadData(mapfile, ctxt, "feature", featureCB, data) < 0)
        return -1;

    if (loadData(mapfile, ctxt, "model", modelCB, data) < 0)
        return -1;

    if (loadIncludes(ctxt, vendorCB, featureCB, modelCB, data) < 0)
        return -1;

    return 0;
}
