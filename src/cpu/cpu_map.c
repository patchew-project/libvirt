/*
 * cpu_map.c: internal functions for handling CPU mapping configuration
 *
 * Copyright (C) 2009-2010 Red Hat, Inc.
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
 * Authors:
 *      Jiri Denemark <jdenemar@redhat.com>
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

static int
loadData(const char *mapfile,
         xmlXPathContextPtr ctxt,
         const char *xpath,
         cpuMapLoadCallback callback,
         void *data)
{
    int ret = -1;
    xmlNodePtr ctxt_node;
    xmlNodePtr *nodes = NULL;
    int n;
    size_t i;
    int rv;

    ctxt_node = ctxt->node;

    n = virXPathNodeSet(xpath, ctxt, &nodes);
    if (n < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot find '%s' in CPU map '%s'"), xpath, mapfile);
        goto cleanup;
    }

    if (n > 0 && !callback) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unexpected %s in CPU map '%s'"), xpath, mapfile);
        goto cleanup;
    }

    for (i = 0; i < n; i++) {
        xmlNodePtr old = ctxt->node;
        char *name = virXMLPropString(nodes[i], "name");
        if (!name) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("cannot find %s name in CPU map '%s'"), xpath, mapfile);
            goto cleanup;
        }
        VIR_DEBUG("Load %s name %s", xpath, name);
        ctxt->node = nodes[i];
        rv = callback(ctxt, name, data);
        ctxt->node = old;
        VIR_FREE(name);
        if (rv < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    ctxt->node = ctxt_node;
    VIR_FREE(nodes);

    return ret;
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
                                        abs_topsrcdir "/src/cpu",
                                        PKGDATADIR)))
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


static int loadIncludes(xmlXPathContextPtr ctxt,
                        cpuMapLoadCallback vendorCB,
                        cpuMapLoadCallback featureCB,
                        cpuMapLoadCallback modelCB,
                        void *data)
{
    int ret = -1;
    xmlNodePtr ctxt_node;
    xmlNodePtr *nodes = NULL;
    int n;
    size_t i;

    ctxt_node = ctxt->node;

    n = virXPathNodeSet("include", ctxt, &nodes);
    if (n < 0)
        goto cleanup;

    for (i = 0; i < n; i++) {
        char *filename = virXMLPropString(nodes[i], "filename");
        VIR_DEBUG("Finding CPU map include '%s'", filename);
        if (cpuMapLoadInclude(filename, vendorCB, featureCB, modelCB, data) < 0) {
            VIR_FREE(filename);
            goto cleanup;
        }
        VIR_FREE(filename);
    }

    ret = 0;

 cleanup:
    ctxt->node = ctxt_node;
    VIR_FREE(nodes);

    return ret;
}


int cpuMapLoad(const char *arch,
               cpuMapLoadCallback vendorCB,
               cpuMapLoadCallback featureCB,
               cpuMapLoadCallback modelCB,
               void *data)
{
    xmlDocPtr xml = NULL;
    xmlXPathContextPtr ctxt = NULL;
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    char *xpath = NULL;
    int ret = -1;
    char *mapfile;

    if (!(mapfile = virFileFindResource("cpu_map.xml",
                                        abs_topsrcdir "/src/cpu",
                                        PKGDATADIR)))
        return -1;

    VIR_DEBUG("Loading '%s' CPU map from %s", arch, mapfile);

    if (arch == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("undefined hardware architecture"));
        goto cleanup;
    }

    if (vendorCB == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("no vendor callback provided"));
        goto cleanup;
    }

    if (modelCB == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("no model callback provided"));
        goto cleanup;
    }

    if (!(xml = virXMLParseFileCtxt(mapfile, &ctxt)))
        goto cleanup;

    virBufferAsprintf(&buf, "./arch[@name='%s']", arch);
    if (virBufferCheckError(&buf) < 0)
        goto cleanup;

    xpath = virBufferContentAndReset(&buf);

    ctxt->node = xmlDocGetRootElement(xml);

    if ((ctxt->node = virXPathNode(xpath, ctxt)) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot find CPU map for %s architecture"), arch);
        goto cleanup;
    }

    if (loadData(mapfile, ctxt, "vendor", vendorCB, data) < 0)
        goto cleanup;

    if (loadData(mapfile, ctxt, "feature", featureCB, data) < 0)
        goto cleanup;

    if (loadData(mapfile, ctxt, "model", modelCB, data) < 0)
        goto cleanup;

    if (loadIncludes(ctxt, vendorCB, featureCB, modelCB, data) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    xmlXPathFreeContext(ctxt);
    xmlFreeDoc(xml);
    VIR_FREE(xpath);
    VIR_FREE(mapfile);

    return ret;
}
