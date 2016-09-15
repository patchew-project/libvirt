/*
 * fs_conf.c: config handling for fs driver
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include "virerror.h"
#include "datatypes.h"
#include "fs_conf.h"

#include "virxml.h"
#include "viruuid.h"
#include "virbuffer.h"
#include "viralloc.h"
#include "virfile.h"
#include "virstring.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_FSPOOL

VIR_LOG_INIT("conf.fs_conf");

VIR_ENUM_IMPL(virFSPool,
              VIR_FSPOOL_LAST,
              "dir")

VIR_ENUM_IMPL(virFSItem,
              VIR_FSITEM_LAST,
              "dir")

/* Flags to indicate mandatory components in the fspool source */
enum {
    VIR_FSPOOL_SOURCE_DIR             = (1 << 2),
    VIR_FSPOOL_SOURCE_NAME            = (1 << 4),
    VIR_FSPOOL_SOURCE_NETWORK         = (1 << 6),
};

typedef const char *(*virFSItemFormatToString)(int format);
typedef int (*virFSItemFormatFromString)(const char *format);

typedef const char *(*virFSPoolFormatToString)(int format);
typedef int (*virFSPoolFormatFromString)(const char *format);

typedef struct _virFSItemOptions virFSItemOptions;
typedef virFSItemOptions *virFSItemOptionsPtr;
struct _virFSItemOptions {
    int defaultFormat;
    virFSItemFormatToString formatToString;
    virFSItemFormatFromString formatFromString;
};

typedef struct _virFSPoolOptions virFSPoolOptions;
typedef virFSPoolOptions *virFSPoolOptionsPtr;
struct _virFSPoolOptions {
    unsigned int flags;
    int defaultFormat;
    virFSPoolFormatToString formatToString;
    virFSPoolFormatFromString formatFromString;
};

typedef struct _virFSPoolTypeInfo virFSPoolTypeInfo;
typedef virFSPoolTypeInfo *virFSPoolTypeInfoPtr;
struct _virFSPoolTypeInfo {
    int fspoolType;
    virFSPoolOptions fspoolOptions;
    virFSItemOptions itemOptions;
};

static virFSPoolTypeInfo fspoolTypeInfo[] = {
    {.fspoolType = VIR_FSPOOL_DIR}
};


static virFSPoolTypeInfoPtr
virFSPoolTypeInfoLookup(int type)
{
    size_t i;
    for (i = 0; i < ARRAY_CARDINALITY(fspoolTypeInfo); i++)
        if (fspoolTypeInfo[i].fspoolType == type)
            return &fspoolTypeInfo[i];

    virReportError(VIR_ERR_INTERNAL_ERROR,
                   _("missing backend for fspool type %d"), type);
    return NULL;
}

static virFSPoolOptionsPtr
virFSPoolOptionsForPoolType(int type)
{
    virFSPoolTypeInfoPtr backend = virFSPoolTypeInfoLookup(type);
    if (backend == NULL)
        return NULL;
    return &backend->fspoolOptions;
}

static virFSItemOptionsPtr
virFSItemOptionsForPoolType(int type)
{
    virFSPoolTypeInfoPtr backend = virFSPoolTypeInfoLookup(type);
    if (backend == NULL)
        return NULL;
    return &backend->itemOptions;
}

static void
virFSSourcePoolDefFree(virFSSourcePoolDefPtr def)
{
    if (!def)
        return;

    VIR_FREE(def->pool);
    VIR_FREE(def->item);

    VIR_FREE(def);
}

static void
virFSPermsFree(virFSPermsPtr def)
{
    if (!def)
        return;

    VIR_FREE(def->label);
    VIR_FREE(def);
}

static void
virFSSourceClear(virFSSourcePtr def)
{
    if (!def)
        return;

    VIR_FREE(def->path);
    virFSSourcePoolDefFree(def->srcpool);
    VIR_FREE(def->driverName);
    virFSPermsFree(def->perms);
}

void
virFSItemDefFree(virFSItemDefPtr def)
{
    if (!def)
        return;

    VIR_FREE(def->name);
    VIR_FREE(def->key);

    virFSSourceClear(&def->target);
    VIR_FREE(def);
}

void
virFSPoolSourceClear(virFSPoolSourcePtr source)
{
    if (!source)
        return;

    VIR_FREE(source->dir);
    VIR_FREE(source->name);
    VIR_FREE(source->product);
}

void
virFSPoolSourceFree(virFSPoolSourcePtr source)
{
    virFSPoolSourceClear(source);
    VIR_FREE(source);
}

void
virFSPoolDefFree(virFSPoolDefPtr def)
{
    if (!def)
        return;

    VIR_FREE(def->name);

    virFSPoolSourceClear(&def->source);

    VIR_FREE(def->target.path);
    VIR_FREE(def->target.perms.label);
    VIR_FREE(def);
}

void
virFSPoolObjFree(virFSPoolObjPtr obj)
{
    if (!obj)
        return;

    virFSPoolObjClearItems(obj);

    virFSPoolDefFree(obj->def);
    virFSPoolDefFree(obj->newDef);

    VIR_FREE(obj->configFile);
    VIR_FREE(obj->autostartLink);

    virMutexDestroy(&obj->lock);

    VIR_FREE(obj);
}

void
virFSPoolObjListFree(virFSPoolObjListPtr fspools)
{
    size_t i;
    for (i = 0; i < fspools->count; i++)
        virFSPoolObjFree(fspools->objs[i]);
    VIR_FREE(fspools->objs);
    fspools->count = 0;
}

void
virFSPoolObjRemove(virFSPoolObjListPtr fspools,
                   virFSPoolObjPtr fspool)
{
    size_t i;

    virFSPoolObjUnlock(fspool);

    for (i = 0; i < fspools->count; i++) {
        virFSPoolObjLock(fspools->objs[i]);
        if (fspools->objs[i] == fspool) {
            virFSPoolObjUnlock(fspools->objs[i]);
            virFSPoolObjFree(fspools->objs[i]);

            VIR_DELETE_ELEMENT(fspools->objs, i, fspools->count);
            break;
        }
        virFSPoolObjUnlock(fspools->objs[i]);
    }
}

static int
virFSPoolDefParseSource(xmlXPathContextPtr ctxt,
                        virFSPoolSourcePtr source,
                        int fspool_type,
                        xmlNodePtr node)
{
    int ret = -1;
    xmlNodePtr relnode /*, authnode, *nodeset = NULL*/;
    virFSPoolOptionsPtr options;

    relnode = ctxt->node;
    ctxt->node = node;

    if ((options = virFSPoolOptionsForPoolType(fspool_type)) == NULL)
        goto cleanup;

    source->name = virXPathString("string(./name)", ctxt);

    if (options->formatFromString) {
        char *format = virXPathString("string(./format/@type)", ctxt);
        if (format == NULL)
            source->format = options->defaultFormat;
        else
            source->format = options->formatFromString(format);

        if (source->format < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown fspool format type %s"), format);
            VIR_FREE(format);
            goto cleanup;
        }
        VIR_FREE(format);
    }

    source->dir = virXPathString("string(./dir/@path)", ctxt);

    source->product = virXPathString("string(./product/@name)", ctxt);

    ret = 0;
 cleanup:
    ctxt->node = relnode;

    return ret;
}

virFSPoolSourcePtr
virFSPoolDefParseSourceString(const char *srcSpec,
                              int fspool_type)
{
    xmlDocPtr doc = NULL;
    xmlNodePtr node = NULL;
    xmlXPathContextPtr xpath_ctxt = NULL;
    virFSPoolSourcePtr def = NULL, ret = NULL;

    if (!(doc = virXMLParseStringCtxt(srcSpec,
                                      _("(storage_source_specification)"),
                                      &xpath_ctxt)))
        goto cleanup;

    if (VIR_ALLOC(def) < 0)
        goto cleanup;

    if (!(node = virXPathNode("/source", xpath_ctxt))) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("root element was not source"));
        goto cleanup;
    }

    if (virFSPoolDefParseSource(xpath_ctxt, def, fspool_type,
                                node) < 0)
        goto cleanup;

    ret = def;
    def = NULL;
 cleanup:
    virFSPoolSourceFree(def);
    xmlFreeDoc(doc);
    xmlXPathFreeContext(xpath_ctxt);

    return ret;
}

static int
virFSDefParsePerms(xmlXPathContextPtr ctxt,
                   virFSPermsPtr perms,
                   const char *permxpath)
{
    char *mode;
    long long val;
    int ret = -1;
    xmlNodePtr relnode;
    xmlNodePtr node;

    node = virXPathNode(permxpath, ctxt);
    if (node == NULL) {
        /* Set default values if there is not <permissions> element */
        perms->mode = (mode_t) -1;
        perms->uid = (uid_t) -1;
        perms->gid = (gid_t) -1;
        perms->label = NULL;
        return 0;
    }

    relnode = ctxt->node;
    ctxt->node = node;

    if ((mode = virXPathString("string(./mode)", ctxt))) {
        int tmp;

        if (virStrToLong_i(mode, NULL, 8, &tmp) < 0 || (tmp & ~0777)) {
            VIR_FREE(mode);
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("malformed octal mode"));
            goto error;
        }
        perms->mode = tmp;
        VIR_FREE(mode);
    } else {
        perms->mode = (mode_t) -1;
    }

    if (virXPathNode("./owner", ctxt) == NULL) {
        perms->uid = (uid_t) -1;
    } else {
        /* We previously could output -1, so continue to parse it */
        if (virXPathLongLong("number(./owner)", ctxt, &val) < 0 ||
            ((uid_t)val != val &&
             val != -1)) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("malformed owner element"));
            goto error;
        }

        perms->uid = val;
    }

    if (virXPathNode("./group", ctxt) == NULL) {
        perms->gid = (gid_t) -1;
    } else {
        /* We previously could output -1, so continue to parse it */
        if (virXPathLongLong("number(./group)", ctxt, &val) < 0 ||
            ((gid_t) val != val &&
             val != -1)) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("malformed group element"));
            goto error;
        }
        perms->gid = val;
    }

    /* NB, we're ignoring missing labels here - they'll simply inherit */
    perms->label = virXPathString("string(./label)", ctxt);

    ret = 0;
 error:
    ctxt->node = relnode;
    return ret;
}

static virFSPoolDefPtr
virFSPoolDefParseXML(xmlXPathContextPtr ctxt)
{
    virFSPoolOptionsPtr options;
    virFSPoolDefPtr ret;
    xmlNodePtr source_node;
    char *type = NULL;
    char *uuid = NULL;
    char *target_path = NULL;

    if (VIR_ALLOC(ret) < 0)
        return NULL;

    type = virXPathString("string(./@type)", ctxt);
    if (type == NULL) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("fspool missing type attribute"));
        goto error;
    }

    if ((ret->type = virFSPoolTypeFromString(type)) < 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("unknown fspool type %s"), type);
        goto error;
    }

    if ((options = virFSPoolOptionsForPoolType(ret->type)) == NULL)
        goto error;

    source_node = virXPathNode("./source", ctxt);
    if (source_node) {
        if (virFSPoolDefParseSource(ctxt, &ret->source, ret->type,
                                         source_node) < 0)
            goto error;
    }

    ret->name = virXPathString("string(./name)", ctxt);
    if (ret->name == NULL &&
        options->flags & VIR_FSPOOL_SOURCE_NAME)
        ret->name = ret->source.name;
    if (ret->name == NULL) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing pool source name element"));
        goto error;
    }

    if (strchr(ret->name, '/')) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("name %s cannot contain '/'"), ret->name);
        goto error;
    }

    uuid = virXPathString("string(./uuid)", ctxt);
    if (uuid == NULL) {
        if (virUUIDGenerate(ret->uuid) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("unable to generate uuid"));
            goto error;
        }
    } else {
        if (virUUIDParse(uuid, ret->uuid) < 0) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("malformed uuid element"));
            goto error;
        }
    }


    if (options->flags & VIR_FSPOOL_SOURCE_DIR) {
        if (!ret->source.dir) {
            virReportError(VIR_ERR_XML_ERROR, "%s",
                           _("missing storage pool source path"));
            goto error;
        }
    }
    if (options->flags & VIR_FSPOOL_SOURCE_NAME) {
        if (ret->source.name == NULL) {
            /* source name defaults to pool name */
            if (VIR_STRDUP(ret->source.name, ret->name) < 0)
                goto error;
        }
    }
    target_path = virXPathString("string(./target/path)", ctxt);
            if (!target_path) {
                virReportError(VIR_ERR_XML_ERROR, "%s",
                               _("missing storage pool target path"));
                goto error;
    }
    ret->target.path = virFileSanitizePath(target_path);
        if (!ret->target.path)
            goto error;

    if (virFSDefParsePerms(ctxt, &ret->target.perms,
                           "./target/permissions") < 0)
            goto error;

 cleanup:
    VIR_FREE(uuid);
    VIR_FREE(type);
    VIR_FREE(target_path);
    return ret;

 error:
    virFSPoolDefFree(ret);
    ret = NULL;
    goto cleanup;
}

virFSPoolDefPtr
virFSPoolDefParseNode(xmlDocPtr xml,
                      xmlNodePtr root)
{
    xmlXPathContextPtr ctxt = NULL;
    virFSPoolDefPtr def = NULL;

    if (!xmlStrEqual(root->name, BAD_CAST "fspool")) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("unexpected root element <%s>, "
                         "expecting <fspool>"),
                       root->name);
        goto cleanup;
    }

    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        virReportOOMError();
        goto cleanup;
    }

    ctxt->node = root;
    def = virFSPoolDefParseXML(ctxt);
 cleanup:
    xmlXPathFreeContext(ctxt);
    return def;
}

static virFSPoolDefPtr
virFSPoolDefParse(const char *xmlStr,
                  const char *filename)
{
    virFSPoolDefPtr ret = NULL;
    xmlDocPtr xml;

    if ((xml = virXMLParse(filename, xmlStr, _("(fs_pool_definition)")))) {
        ret = virFSPoolDefParseNode(xml, xmlDocGetRootElement(xml));
        xmlFreeDoc(xml);
    }

    return ret;
}

virFSPoolDefPtr
virFSPoolDefParseString(const char *xmlStr)
{
    return virFSPoolDefParse(xmlStr, NULL);
}

virFSPoolDefPtr
virFSPoolDefParseFile(const char *filename)
{
    return virFSPoolDefParse(NULL, filename);
}

static int
virFSPoolSourceFormat(virBufferPtr buf,
                      virFSPoolOptionsPtr options,
                      virFSPoolSourcePtr src)
{

    virBufferAddLit(buf, "<source>\n");
    virBufferAdjustIndent(buf, 2);

    if (options->flags & VIR_FSPOOL_SOURCE_DIR)
        virBufferEscapeString(buf, "<dir path='%s'/>\n", src->dir);

    if (options->flags & VIR_FSPOOL_SOURCE_NAME)
        virBufferEscapeString(buf, "<name>%s</name>\n", src->name);

    if (options->formatToString) {
        const char *format = (options->formatToString)(src->format);
        if (!format) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("unknown pool format number %d"),
                           src->format);
            return -1;
        }
        virBufferAsprintf(buf, "<format type='%s'/>\n", format);
    }


    virBufferEscapeString(buf, "<product name='%s'/>\n", src->product);

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</source>\n");
    return 0;
}


static int
virFSPoolDefFormatBuf(virBufferPtr buf,
                           virFSPoolDefPtr def)
{
    virFSPoolOptionsPtr options;
    char uuid[VIR_UUID_STRING_BUFLEN];
    const char *type;

    options = virFSPoolOptionsForPoolType(def->type);
    if (options == NULL)
        return -1;

    type = virFSPoolTypeToString(def->type);
    if (!type) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unexpected fspool type"));
        return -1;
    }
    virBufferAsprintf(buf, "<fspool type='%s'>\n", type);
    virBufferAdjustIndent(buf, 2);
    virBufferEscapeString(buf, "<name>%s</name>\n", def->name);

    virUUIDFormat(def->uuid, uuid);
    virBufferAsprintf(buf, "<uuid>%s</uuid>\n", uuid);

    virBufferAsprintf(buf, "<capacity unit='bytes'>%llu</capacity>\n",
                      def->capacity);
    virBufferAsprintf(buf, "<allocation unit='bytes'>%llu</allocation>\n",
                      def->allocation);
    virBufferAsprintf(buf, "<available unit='bytes'>%llu</available>\n",
                      def->available);

    if (virFSPoolSourceFormat(buf, options, &def->source) < 0)
        return -1;

    virBufferAddLit(buf, "<target>\n");
    virBufferAdjustIndent(buf, 2);

    virBufferEscapeString(buf, "<path>%s</path>\n", def->target.path);

    if (def->target.perms.mode != (mode_t) -1 ||
        def->target.perms.uid != (uid_t) -1 ||
        def->target.perms.gid != (gid_t) -1 ||
        def->target.perms.label) {
        virBufferAddLit(buf, "<permissions>\n");
        virBufferAdjustIndent(buf, 2);
        if (def->target.perms.mode != (mode_t) -1)
        virBufferAsprintf(buf, "<mode>0%o</mode>\n",
                          def->target.perms.mode);
        if (def->target.perms.uid != (uid_t) -1)
            virBufferAsprintf(buf, "<owner>%d</owner>\n",
                              (int) def->target.perms.uid);
        if (def->target.perms.gid != (gid_t) -1)
            virBufferAsprintf(buf, "<group>%d</group>\n",
                              (int) def->target.perms.gid);
            virBufferEscapeString(buf, "<label>%s</label>\n",
                                  def->target.perms.label);

            virBufferAdjustIndent(buf, -2);
            virBufferAddLit(buf, "</permissions>\n");
        }

        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</target>\n");

    virBufferAdjustIndent(buf, -2);
    virBufferAddLit(buf, "</fspool>\n");

    return 0;
}

char *
virFSPoolDefFormat(virFSPoolDefPtr def)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    if (virFSPoolDefFormatBuf(&buf, def) < 0)
        goto error;

    if (virBufferCheckError(&buf) < 0)
        goto error;

    return virBufferContentAndReset(&buf);

 error:
    virBufferFreeAndReset(&buf);
    return NULL;
}


static int
virFSSize(const char *unit,
          const char *val,
          unsigned long long *ret)
{
    if (virStrToLong_ull(val, NULL, 10, ret) < 0) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("malformed capacity element"));
        return -1;
    }
    /* off_t is signed, so you cannot create a file larger than 2**63
     * bytes in the first place.  */
    if (virScaleInteger(ret, unit, 1, LLONG_MAX) < 0)
        return -1;

    return 0;
}

static virFSItemDefPtr
virFSItemDefParseXML(virFSPoolDefPtr fspool,
                     xmlXPathContextPtr ctxt,
                     unsigned int flags)
{
    virFSItemDefPtr ret;
    virFSItemOptionsPtr options;
    char *type = NULL;
    char *allocation = NULL;
    char *capacity = NULL;
    char *unit = NULL;
    xmlNodePtr *nodes = NULL;

    virCheckFlags(VIR_ITEM_XML_PARSE_NO_CAPACITY |
                  VIR_ITEM_XML_PARSE_OPT_CAPACITY, NULL);

    options = virFSItemOptionsForPoolType(fspool->type);
    if (options == NULL)
        return NULL;

    if (VIR_ALLOC(ret) < 0)
        return NULL;

    ret->name = virXPathString("string(./name)", ctxt);
    if (ret->name == NULL) {
        virReportError(VIR_ERR_XML_ERROR, "%s",
                       _("missing item name element"));
        goto error;
    }

    /* Normally generated by pool refresh, but useful for unit tests */
    ret->key = virXPathString("string(./key)", ctxt);

    /* Technically overridden by pool refresh, but useful for unit tests */
    type = virXPathString("string(./@type)", ctxt);
    if (type) {
        if ((ret->type = virFSItemTypeFromString(type)) < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown item type '%s'"), type);
            goto error;
        }
    }

    capacity = virXPathString("string(./capacity)", ctxt);
    unit = virXPathString("string(./capacity/@unit)", ctxt);
    if (capacity) {
        if (virFSSize(unit, capacity, &ret->target.capacity) < 0)
            goto error;
    } else if (!(flags & VIR_ITEM_XML_PARSE_NO_CAPACITY) &&
               !((flags & VIR_ITEM_XML_PARSE_OPT_CAPACITY))) {
        virReportError(VIR_ERR_XML_ERROR, "%s", _("missing capacity element"));
        goto error;
    }
    VIR_FREE(unit);

    allocation = virXPathString("string(./allocation)", ctxt);
    if (allocation) {
        unit = virXPathString("string(./allocation/@unit)", ctxt);
        if (virFSSize(unit, allocation, &ret->target.allocation) < 0)
            goto error;
    } else {
        ret->target.allocation = ret->target.capacity;
    }

    ret->target.path = virXPathString("string(./target/path)", ctxt);

    if (VIR_ALLOC(ret->target.perms) < 0)
        goto error;
    if (virFSDefParsePerms(ctxt, ret->target.perms,
                                "./target/permissions") < 0)
        goto error;

 cleanup:
    VIR_FREE(nodes);
    VIR_FREE(allocation);
    VIR_FREE(capacity);
    VIR_FREE(unit);
    VIR_FREE(type);
    return ret;

 error:
    virFSItemDefFree(ret);
    ret = NULL;
    goto cleanup;
}

virFSItemDefPtr
virFSItemDefParseNode(virFSPoolDefPtr fspool,
                      xmlDocPtr xml,
                      xmlNodePtr root,
                      unsigned int flags)
{
    xmlXPathContextPtr ctxt = NULL;
    virFSItemDefPtr def = NULL;

    if (!xmlStrEqual(root->name, BAD_CAST "item")) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("unexpected root element <%s>, "
                         "expecting <item>"),
                       root->name);
        goto cleanup;
    }

    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        virReportOOMError();
        goto cleanup;
    }

    ctxt->node = root;
    def = virFSItemDefParseXML(fspool, ctxt, flags);
 cleanup:
    xmlXPathFreeContext(ctxt);
    return def;
}

static virFSItemDefPtr
virFSItemDefParse(virFSPoolDefPtr fspool,
                  const char *xmlStr,
                  const char *filename,
                  unsigned int flags)
{
    virFSItemDefPtr ret = NULL;
    xmlDocPtr xml;

    if ((xml = virXMLParse(filename, xmlStr, _("(fspool_item_definition)")))) {
        ret = virFSItemDefParseNode(fspool, xml, xmlDocGetRootElement(xml), flags);
        xmlFreeDoc(xml);
    }

    return ret;
}

virFSItemDefPtr
virFSItemDefParseString(virFSPoolDefPtr fspool,
                        const char *xmlStr,
                        unsigned int flags)
{
    return virFSItemDefParse(fspool, xmlStr, NULL, flags);
}

virFSItemDefPtr
virFSItemDefParseFile(virFSPoolDefPtr fspool,
                      const char *filename,
                      unsigned int flags)
{
    return virFSItemDefParse(fspool, NULL, filename, flags);
}

static int
virFSItemTargetDefFormat(virFSItemOptionsPtr options ATTRIBUTE_UNUSED,
                         virBufferPtr buf,
                         virFSSourcePtr def,
                         const char *type)
{
    virBufferAsprintf(buf, "<%s>\n", type);
    virBufferAdjustIndent(buf, 2);

    if (def->perms &&
        (def->perms->mode != (mode_t) -1 ||
         def->perms->uid != (uid_t) -1 ||
         def->perms->gid != (gid_t) -1 ||
         def->perms->label)) {
        virBufferAddLit(buf, "<permissions>\n");
        virBufferAdjustIndent(buf, 2);

        if (def->perms->mode != (mode_t) -1)
            virBufferAsprintf(buf, "<mode>0%o</mode>\n",
                              def->perms->mode);
        if (def->perms->uid != (uid_t) -1)
            virBufferAsprintf(buf, "<owner>%d</owner>\n",
                              (int) def->perms->uid);
        if (def->perms->gid != (gid_t) -1)
            virBufferAsprintf(buf, "<group>%d</group>\n",
                              (int) def->perms->gid);

        virBufferEscapeString(buf, "<label>%s</label>\n",
                              def->perms->label);

        virBufferAdjustIndent(buf, -2);
        virBufferAddLit(buf, "</permissions>\n");
    }

    virBufferAdjustIndent(buf, -2);
    virBufferAsprintf(buf, "</%s>\n", type);
    return 0;
}

char *
virFSItemDefFormat(virFSPoolDefPtr fspool,
                   virFSItemDefPtr def)
{
    virFSItemOptionsPtr options;
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    options = virFSItemOptionsForPoolType(fspool->type);
    if (options == NULL)
        return NULL;

    virBufferAddLit(&buf, "<item>\n");
    virBufferAdjustIndent(&buf, 2);

    virBufferEscapeString(&buf, "<name>%s</name>\n", def->name);
    virBufferEscapeString(&buf, "<key>%s</key>\n", def->key);

    virBufferAsprintf(&buf, "<capacity unit='bytes'>%llu</capacity>\n",
                      def->target.capacity);
    virBufferAsprintf(&buf, "<allocation unit='bytes'>%llu</allocation>\n",
                      def->target.allocation);

    if (virFSItemTargetDefFormat(options, &buf,
                                     &def->target, "target") < 0)
        goto cleanup;

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</item>\n");

    if (virBufferCheckError(&buf) < 0)
        goto cleanup;

    return virBufferContentAndReset(&buf);

 cleanup:
    virBufferFreeAndReset(&buf);
    return NULL;
}


virFSPoolObjPtr
virFSPoolObjFindByUUID(virFSPoolObjListPtr fspools,
                       const unsigned char *uuid)
{
    size_t i;

    for (i = 0; i < fspools->count; i++) {
        virFSPoolObjLock(fspools->objs[i]);
        if (!memcmp(fspools->objs[i]->def->uuid, uuid, VIR_UUID_BUFLEN))
            return fspools->objs[i];
        virFSPoolObjUnlock(fspools->objs[i]);
    }

    return NULL;
}

virFSPoolObjPtr
virFSPoolObjFindByName(virFSPoolObjListPtr fspools,
                       const char *name)
{
    size_t i;

    for (i = 0; i < fspools->count; i++) {
        virFSPoolObjLock(fspools->objs[i]);
        if (STREQ(fspools->objs[i]->def->name, name))
            return fspools->objs[i];
        virFSPoolObjUnlock(fspools->objs[i]);
    }

    return NULL;
}


void
virFSPoolObjClearItems(virFSPoolObjPtr fspool)
{
    size_t i;
    for (i = 0; i < fspool->items.count; i++)
        virFSItemDefFree(fspool->items.objs[i]);

    VIR_FREE(fspool->items.objs);
    fspool->items.count = 0;
}

virFSItemDefPtr
virFSItemDefFindByKey(virFSPoolObjPtr fspool,
                      const char *key)
{
    size_t i;

    for (i = 0; i < fspool->items.count; i++)
        if (STREQ(fspool->items.objs[i]->key, key))
            return fspool->items.objs[i];

    return NULL;
}

virFSItemDefPtr
virFSItemDefFindByPath(virFSPoolObjPtr fspool,
                       const char *path)
{
    size_t i;

    for (i = 0; i < fspool->items.count; i++)
        if (STREQ(fspool->items.objs[i]->target.path, path))
            return fspool->items.objs[i];

    return NULL;
}

virFSItemDefPtr
virFSItemDefFindByName(virFSPoolObjPtr fspool,
                           const char *name)
{
    size_t i;

    for (i = 0; i < fspool->items.count; i++)
        if (STREQ(fspool->items.objs[i]->name, name))
            return fspool->items.objs[i];

    return NULL;
}

virFSPoolObjPtr
virFSPoolObjAssignDef(virFSPoolObjListPtr fspools,
                           virFSPoolDefPtr def)
{
    virFSPoolObjPtr fspool;

    if ((fspool = virFSPoolObjFindByName(fspools, def->name))) {
        if (!virFSPoolObjIsActive(fspool)) {
            virFSPoolDefFree(fspool->def);
            fspool->def = def;
        } else {
            virFSPoolDefFree(fspool->newDef);
            fspool->newDef = def;
        }
        return fspool;
    }

    if (VIR_ALLOC(fspool) < 0)
        return NULL;

    if (virMutexInit(&fspool->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("cannot initialize mutex"));
        VIR_FREE(fspool);
        return NULL;
    }
    virFSPoolObjLock(fspool);
    fspool->active = 0;

    if (VIR_APPEND_ELEMENT_COPY(fspools->objs, fspools->count, fspool) < 0) {
        virFSPoolObjUnlock(fspool);
        virFSPoolObjFree(fspool);
        return NULL;
    }
    fspool->def = def;

    return fspool;
}

static virFSPoolObjPtr
virFSPoolObjLoad(virFSPoolObjListPtr fspools,
                 const char *file,
                 const char *path,
                 const char *autostartLink)
{
    virFSPoolDefPtr def;
    virFSPoolObjPtr fspool;

    if (!(def = virFSPoolDefParseFile(path)))
        return NULL;

    if (!virFileMatchesNameSuffix(file, def->name, ".xml")) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Storage fspool config filename '%s' does "
                         "not match fspool name '%s'"),
                       path, def->name);
        virFSPoolDefFree(def);
        return NULL;
    }

    if (!(fspool = virFSPoolObjAssignDef(fspools, def))) {
        virFSPoolDefFree(def);
        return NULL;
    }

    VIR_FREE(fspool->configFile);  /* for driver reload */
    if (VIR_STRDUP(fspool->configFile, path) < 0) {
        virFSPoolObjRemove(fspools, fspool);
        return NULL;
    }
    VIR_FREE(fspool->autostartLink); /* for driver reload */
    if (VIR_STRDUP(fspool->autostartLink, autostartLink) < 0) {
        virFSPoolObjRemove(fspools, fspool);
        return NULL;
    }

    fspool->autostart = virFileLinkPointsTo(fspool->autostartLink,
                                          fspool->configFile);

    return fspool;
}


virFSPoolObjPtr
virFSPoolLoadState(virFSPoolObjListPtr fspools,
                        const char *stateDir,
                        const char *name)
{
    char *stateFile = NULL;
    virFSPoolDefPtr def = NULL;
    virFSPoolObjPtr fspool = NULL;
    xmlDocPtr xml = NULL;
    xmlXPathContextPtr ctxt = NULL;
    xmlNodePtr node = NULL;

    if (!(stateFile = virFileBuildPath(stateDir, name, ".xml")))
        goto error;

    if (!(xml = virXMLParseCtxt(stateFile, NULL, _("(fspool state)"), &ctxt)))
        goto error;

    if (!(node = virXPathNode("//fspool", ctxt))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find any 'fspool' element in state file"));
        goto error;
    }

    ctxt->node = node;
    if (!(def = virFSPoolDefParseXML(ctxt)))
        goto error;

    if (STRNEQ(name, def->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Storage fspool state file '%s' does not match "
                         "fspool name '%s'"),
                       stateFile, def->name);
        goto error;
    }

    /* create the object */
    if (!(fspool = virFSPoolObjAssignDef(fspools, def)))
        goto error;

    /* XXX: future handling of some additional useful status data,
     * for now, if a status file for a fspool exists, the fspool will be marked
     * as active
     */

    fspool->active = 1;

 cleanup:
    VIR_FREE(stateFile);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return fspool;

 error:
    virFSPoolDefFree(def);
    goto cleanup;
}


int
virFSPoolLoadAllState(virFSPoolObjListPtr fspools,
                      const char *stateDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, stateDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, stateDir)) > 0) {
        virFSPoolObjPtr fspool;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        if (!(fspool = virFSPoolLoadState(fspools, stateDir, entry->d_name)))
            continue;
        virFSPoolObjUnlock(fspool);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


int
virFSPoolLoadAllConfigs(virFSPoolObjListPtr fspools,
                             const char *configDir,
                             const char *autostartDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, configDir)) > 0) {
        char *path;
        char *autostartLink;
        virFSPoolObjPtr fspool;

        if (!virFileHasSuffix(entry->d_name, ".xml"))
            continue;

        if (!(path = virFileBuildPath(configDir, entry->d_name, NULL)))
            continue;

        if (!(autostartLink = virFileBuildPath(autostartDir, entry->d_name,
                                               NULL))) {
            VIR_FREE(path);
            continue;
        }

        fspool = virFSPoolObjLoad(fspools, entry->d_name, path,
                                     autostartLink);
        if (fspool)
            virFSPoolObjUnlock(fspool);

        VIR_FREE(path);
        VIR_FREE(autostartLink);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


static int virFSPoolSaveXML(const char *path,
                                 virFSPoolDefPtr def,
                                 const char *xml)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    int ret = -1;

    virUUIDFormat(def->uuid, uuidstr);
    ret = virXMLSaveFile(path,
                         virXMLPickShellSafeComment(def->name, uuidstr),
                         "fspool-edit", xml);

    return ret;
}


int
virFSPoolSaveState(const char *stateFile,
                        virFSPoolDefPtr def)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    int ret = -1;
    char *xml;

    virBufferAddLit(&buf, "<fspoolstate>\n");
    virBufferAdjustIndent(&buf, 2);

    if (virFSPoolDefFormatBuf(&buf, def) < 0)
        goto error;

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</fspoolstate>\n");

    if (virBufferCheckError(&buf) < 0)
        goto error;

    if (!(xml = virBufferContentAndReset(&buf)))
        goto error;

    if (virFSPoolSaveXML(stateFile, def, xml))
        goto error;

    ret = 0;

 error:
    VIR_FREE(xml);
    return ret;
}


int
virFSPoolSaveConfig(const char *configFile,
                         virFSPoolDefPtr def)
{
    char *xml;
    int ret = -1;

    if (!(xml = virFSPoolDefFormat(def))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to generate XML"));
        return -1;
    }

    if (virFSPoolSaveXML(configFile, def, xml))
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(xml);
    return ret;
}

int
virFSPoolObjSaveDef(virFSDriverStatePtr driver,
                         virFSPoolObjPtr fspool,
                         virFSPoolDefPtr def)
{
    if (!fspool->configFile) {
        if (virFileMakePath(driver->configDir) < 0) {
            virReportSystemError(errno,
                                 _("cannot create config directory %s"),
                                 driver->configDir);
            return -1;
        }

        if (!(fspool->configFile = virFileBuildPath(driver->configDir,
                                                  def->name, ".xml"))) {
            return -1;
        }

        if (!(fspool->autostartLink = virFileBuildPath(driver->autostartDir,
                                                     def->name, ".xml"))) {
            VIR_FREE(fspool->configFile);
            return -1;
        }
    }

    return virFSPoolSaveConfig(fspool->configFile, def);
}

int
virFSPoolObjDeleteDef(virFSPoolObjPtr fspool)
{
    if (!fspool->configFile) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("no config file for %s"), fspool->def->name);
        return -1;
    }

    if (unlink(fspool->configFile) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("cannot remove config for %s"),
                       fspool->def->name);
        return -1;
    }

    return 0;
}

virFSPoolSourcePtr
virFSPoolSourceListNewSource(virFSPoolSourceListPtr list)
{
    virFSPoolSourcePtr source;

    if (VIR_REALLOC_N(list->sources, list->nsources + 1) < 0)
        return NULL;

    source = &list->sources[list->nsources++];
    memset(source, 0, sizeof(*source));

    return source;
}

char *
virFSPoolSourceListFormat(virFSPoolSourceListPtr def)
{
    virFSPoolOptionsPtr options;
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    const char *type;
    size_t i;

    options = virFSPoolOptionsForPoolType(def->type);
    if (options == NULL)
        return NULL;

    type = virFSPoolTypeToString(def->type);
    if (!type) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("unexpected fspool type"));
        goto cleanup;
    }

    virBufferAddLit(&buf, "<sources>\n");
    virBufferAdjustIndent(&buf, 2);

    for (i = 0; i < def->nsources; i++)
        virFSPoolSourceFormat(&buf, options, &def->sources[i]);

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</sources>\n");

    if (virBufferCheckError(&buf) < 0)
        goto cleanup;

    return virBufferContentAndReset(&buf);

 cleanup:
    virBufferFreeAndReset(&buf);
    return NULL;
}


/*
 * virFSPoolObjIsDuplicate:
 * @doms : virFSPoolObjListPtr to search
 * @def  : virFSPoolDefPtr definition of fspool to lookup
 * @check_active: If true, ensure that fspool is not active
 *
 * Returns: -1 on error
 *          0 if fspool is new
 *          1 if fspool is a duplicate
 */
int
virFSPoolObjIsDuplicate(virFSPoolObjListPtr fspools,
                             virFSPoolDefPtr def,
                             unsigned int check_active)
{
    int ret = -1;
    virFSPoolObjPtr fspool = NULL;

    /* See if a Pool with matching UUID already exists */
    fspool = virFSPoolObjFindByUUID(fspools, def->uuid);
    if (fspool) {
        /* UUID matches, but if names don't match, refuse it */
        if (STRNEQ(fspool->def->name, def->name)) {
            char uuidstr[VIR_UUID_STRING_BUFLEN];
            virUUIDFormat(fspool->def->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("fspool '%s' is already defined with uuid %s"),
                           fspool->def->name, uuidstr);
            goto cleanup;
        }

        if (check_active) {
            /* UUID & name match, but if Pool is already active, refuse it */
            if (virFSPoolObjIsActive(fspool)) {
                virReportError(VIR_ERR_OPERATION_INVALID,
                               _("fspool is already active as '%s'"),
                               fspool->def->name);
                goto cleanup;
            }
        }

        ret = 1;
    } else {
        /* UUID does not match, but if a name matches, refuse it */
        fspool = virFSPoolObjFindByName(fspools, def->name);
        if (fspool) {
            char uuidstr[VIR_UUID_STRING_BUFLEN];
            virUUIDFormat(fspool->def->uuid, uuidstr);
            virReportError(VIR_ERR_OPERATION_FAILED,
                           _("fspool '%s' already exists with uuid %s"),
                           def->name, uuidstr);
            goto cleanup;
        }
        ret = 0;
    }

 cleanup:
    if (fspool)
        virFSPoolObjUnlock(fspool);
    return ret;
}

int
virFSPoolSourceFindDuplicate(virConnectPtr conn ATTRIBUTE_UNUSED,
                             virFSPoolObjListPtr fspools,
                             virFSPoolDefPtr def)
{
    size_t i;
    int ret = 1;
    virFSPoolObjPtr fspool = NULL;
    virFSPoolObjPtr matchfspool = NULL;

    /* Check the fspool list for duplicate underlying storage */
    for (i = 0; i < fspools->count; i++) {
        fspool = fspools->objs[i];
        if (def->type != fspool->def->type)
            continue;

        /* Don't mach against ourself if re-defining existing fspool ! */
        if (STREQ(fspool->def->name, def->name))
            continue;

        virFSPoolObjLock(fspool);

        switch ((virFSPoolType)fspool->def->type) {
        case VIR_FSPOOL_DIR:
            if (STREQ(fspool->def->target.path, def->target.path))
                matchfspool = fspool;
            break;

        case VIR_FSPOOL_LAST:
            break;
        }
        virFSPoolObjUnlock(fspool);

        if (matchfspool)
            break;
    }

    if (matchfspool) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("FS source conflict with fspool: '%s'"),
                       matchfspool->def->name);
        ret = -1;
    }
    return ret;
}

void
virFSPoolObjLock(virFSPoolObjPtr obj)
{
    virMutexLock(&obj->lock);
}

void
virFSPoolObjUnlock(virFSPoolObjPtr obj)
{
    virMutexUnlock(&obj->lock);
}

#define MATCH(FLAG) (flags & (FLAG))
static bool
virFSPoolMatch(virFSPoolObjPtr fspoolobj,
                    unsigned int flags)
{
    /* filter by active state */
    if (MATCH(VIR_CONNECT_LIST_FSPOOLS_FILTERS_ACTIVE) &&
        !((MATCH(VIR_CONNECT_LIST_FSPOOLS_ACTIVE) &&
           virFSPoolObjIsActive(fspoolobj)) ||
          (MATCH(VIR_CONNECT_LIST_FSPOOLS_INACTIVE) &&
           !virFSPoolObjIsActive(fspoolobj))))
        return false;

    /* filter by persistence */
    if (MATCH(VIR_CONNECT_LIST_FSPOOLS_FILTERS_PERSISTENT) &&
        !((MATCH(VIR_CONNECT_LIST_FSPOOLS_PERSISTENT) &&
           fspoolobj->configFile) ||
          (MATCH(VIR_CONNECT_LIST_FSPOOLS_TRANSIENT) &&
           !fspoolobj->configFile)))
        return false;

    /* filter by autostart option */
    if (MATCH(VIR_CONNECT_LIST_FSPOOLS_FILTERS_AUTOSTART) &&
        !((MATCH(VIR_CONNECT_LIST_FSPOOLS_AUTOSTART) &&
           fspoolobj->autostart) ||
          (MATCH(VIR_CONNECT_LIST_FSPOOLS_NO_AUTOSTART) &&
           !fspoolobj->autostart)))
        return false;

    /* filter by fspool type */
    if (MATCH(VIR_CONNECT_LIST_FSPOOLS_FILTERS_POOL_TYPE)) {
        if (!(MATCH(VIR_CONNECT_LIST_FSPOOLS_DIR) &&
               (fspoolobj->def->type == VIR_FSPOOL_DIR)))
            return false;
    }

    return true;
}
#undef MATCH

int
virFSPoolObjListExport(virConnectPtr conn,
                       virFSPoolObjList fspoolobjs,
                       virFSPoolPtr **fspools,
                       virFSPoolObjListFilter filter,
                       unsigned int flags)
{
    virFSPoolPtr *tmp_fspools = NULL;
    virFSPoolPtr fspool = NULL;
    int nfspools = 0;
    int ret = -1;
    size_t i;

    if (fspools && VIR_ALLOC_N(tmp_fspools, fspoolobjs.count + 1) < 0)
        goto cleanup;

    for (i = 0; i < fspoolobjs.count; i++) {
        virFSPoolObjPtr fspoolobj = fspoolobjs.objs[i];
        virFSPoolObjLock(fspoolobj);
        if ((!filter || filter(conn, fspoolobj->def)) &&
            virFSPoolMatch(fspoolobj, flags)) {
            if (fspools) {
                if (!(fspool = virGetFSPool(conn,
                                            fspoolobj->def->name,
                                            fspoolobj->def->uuid,
                                            NULL, NULL))) {
                    virFSPoolObjUnlock(fspoolobj);
                    goto cleanup;
                }
                tmp_fspools[nfspools] = fspool;
            }
            nfspools++;
        }
        virFSPoolObjUnlock(fspoolobj);
    }

    if (tmp_fspools) {
        /* trim the array to the final size */
        ignore_value(VIR_REALLOC_N(tmp_fspools, nfspools + 1));
        *fspools = tmp_fspools;
        tmp_fspools = NULL;
    }

    ret = nfspools;

 cleanup:
    if (tmp_fspools) {
        for (i = 0; i < nfspools; i++)
            virObjectUnref(tmp_fspools[i]);
    }

    VIR_FREE(tmp_fspools);
    return ret;
}
