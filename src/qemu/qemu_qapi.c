/*
 * qemu_qapi.c: helper functions for QEMU QAPI schema handling
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

#include "qemu_qapi.h"

#include "viralloc.h"
#include "virstring.h"
#include "virerror.h"
#include "virlog.h"

#include "c-ctype.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_qapi");


/**
 * virQEMUQAPISchemaObjectGet:
 * @field: name of the object containing the requested type
 * @name: name of the requested type
 * @namefield: name of the object property holding @name
 * @elem: QAPI schema entry JSON object
 *
 * Helper that selects the type of a QMP schema object member or it's variant
 * member. Returns the QMP entry on success or NULL on error.
 */
static virJSONValuePtr
virQEMUQAPISchemaObjectGet(const char *field,
                           const char *name,
                           const char *namefield,
                           virJSONValuePtr elem)
{
    virJSONValuePtr arr;
    virJSONValuePtr cur;
    const char *curname;
    size_t i;

    if (!(arr = virJSONValueObjectGetArray(elem, field)))
        return NULL;

    for (i = 0; i < virJSONValueArraySize(arr); i++) {
        if (!(cur = virJSONValueArrayGet(arr, i)) ||
            !(curname = virJSONValueObjectGetString(cur, namefield)))
            continue;

        if (STREQ(name, curname))
            return cur;
    }

    return NULL;
}


struct virQEMUQAPISchemaTraverseContext {
    virHashTablePtr schema;
    char **query;
    virJSONValuePtr returnType;
};


static int
virQEMUQAPISchemaTraverse(const char *baseName,
                          struct virQEMUQAPISchemaTraverseContext *ctxt);


static int
virQEMUQAPISchemaTraverseObject(virJSONValuePtr cur,
                                struct virQEMUQAPISchemaTraverseContext *ctxt)
{
    virJSONValuePtr obj;
    const char *querystr = *ctxt->query;
    char modifier = *querystr;

    if (!c_isalpha(modifier))
        querystr++;

    if (modifier == '+') {
        obj = virQEMUQAPISchemaObjectGet("variants", querystr, "case", cur);
    } else {
        obj = virQEMUQAPISchemaObjectGet("members", querystr, "name", cur);

        if (modifier == '*' &&
            !virJSONValueObjectHasKey(obj, "default"))
            return 0;
    }

    if (!obj)
        return 0;

    ctxt->query++;

    return virQEMUQAPISchemaTraverse(virJSONValueObjectGetString(obj, "type"), ctxt);
}


static int
virQEMUQAPISchemaTraverseArray(virJSONValuePtr cur,
                               struct virQEMUQAPISchemaTraverseContext *ctxt)
{
    const char *querytype;

    /* arrays are just flattened by default */
    if (!(querytype = virJSONValueObjectGetString(cur, "element-type")))
        return 0;

    return virQEMUQAPISchemaTraverse(querytype, ctxt);
}


static int
virQEMUQAPISchemaTraverseCommand(virJSONValuePtr cur,
                                 struct virQEMUQAPISchemaTraverseContext *ctxt)
{
    const char *querytype;

    if (!(querytype = virJSONValueObjectGetString(cur, *ctxt->query)))
        return 0;

    ctxt->query++;

    return virQEMUQAPISchemaTraverse(querytype, ctxt);
}


static int
virQEMUQAPISchemaTraverse(const char *baseName,
                          struct virQEMUQAPISchemaTraverseContext *ctxt)
{
    virJSONValuePtr cur;
    const char *metatype;

    if (!(cur = virHashLookup(ctxt->schema, baseName)))
        return 0;

    if (!ctxt->query[0]) {
        ctxt->returnType = cur;
        return 1;
    }

    if (!(metatype = virJSONValueObjectGetString(cur, "meta-type")))
        return 0;

    if (STREQ(metatype, "array")) {
        return virQEMUQAPISchemaTraverseArray(cur, ctxt);
    } else if (STREQ(metatype, "object")) {
        return virQEMUQAPISchemaTraverseObject(cur, ctxt);
    } else if (STREQ(metatype, "command") ||
               STREQ(metatype, "event")) {
        return virQEMUQAPISchemaTraverseCommand(cur, ctxt);
    } else {
        /* alternates, basic types and enums can't be entered */
        return 0;
    }

    return 0;
}


/**
 * virQEMUQAPISchemaPathGet:
 * @query: string specifying the required data type (see below)
 * @schema: hash table containing the schema data
 * @entry: filled with the located schema object requested by @query (optional)
 *
 * Retrieves the requested schema entry specified by @query to @entry. The
 * @query parameter has the following syntax which is very closely tied to the
 * qemu schema syntax entries separated by slashes with a few special characters:
 *
 * "command_or_event/attribute/subattribute/+variant_discriminator/subattribute"
 *
 * command_or_event: name of the event or attribute to introspect
 * attribute: selects whether arguments or return type should be introspected
 *            ("arg-type" or "ret-type" for commands, "arg-type" for events)
 * subattribute: specifies member name of object types
 * *subattribute: same as above but must be optional (has a property named
 *                'default' field in the schema)
 * +variant_discriminator: In the case of unionized objects, select a
 *                         specific case to introspect.
 *
 * If the name of any (sub)attribute starts with non-alphabetical symbols it
 * needs to be prefixed by a single space.
 *
 * Array types are automatically flattened to the singular type. Alternate
 * types are currently not supported.
 *
 * The above types can be chained arbitrarily using slashes to construct any
 * path into the schema tree.
 *
 * Returns 1 if @query was found in @schema filling @entry if non-NULL, 0 if
 * @query was not found in @schema and -1 on other errors along with an appropriate
 * error message.
 */
int
virQEMUQAPISchemaPathGet(const char *query,
                         virHashTablePtr schema,
                         virJSONValuePtr *entry)
{
    VIR_AUTOSTRINGLIST elems = NULL;
    struct virQEMUQAPISchemaTraverseContext ctxt = { .schema = schema };
    int rc;

    if (entry)
        *entry = NULL;

    if (!(elems = virStringSplit(query, "/", 0)))
        return -1;

    if (!*elems) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("malformed query string"));
        return -1;
    }

    ctxt.query = elems + 1;

    rc = virQEMUQAPISchemaTraverse(elems[0], &ctxt);

    if (entry)
        *entry = ctxt.returnType;

    return rc;
}


bool
virQEMUQAPISchemaPathExists(const char *query,
                            virHashTablePtr schema)
{
    return virQEMUQAPISchemaPathGet(query, schema, NULL) == 1;
}

static int
virQEMUQAPISchemaEntryProcess(size_t pos ATTRIBUTE_UNUSED,
                              virJSONValuePtr item,
                              void *opaque)
{
    const char *name;
    virHashTablePtr schema = opaque;

    if (!(name = virJSONValueObjectGetString(item, "name"))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("malformed QMP schema"));
        return -1;
    }

    if (virHashAddEntry(schema, name, item) < 0)
        return -1;

    return 0;
}


/**
 * virQEMUQAPISchemaConvert:
 * @schemareply: Schema data as returned by the qemu monitor
 *
 * Converts the schema into the hash-table used by the functions working with
 * the schema. @schemareply is consumed and freed.
 */
virHashTablePtr
virQEMUQAPISchemaConvert(virJSONValuePtr schemareply)
{
    VIR_AUTOPTR(virHashTable) schema = NULL;
    VIR_AUTOPTR(virJSONValue) schemajson = schemareply;

    if (!(schema = virHashCreate(512, virJSONValueHashFree)))
        return NULL;

    if (virJSONValueArrayForeachSteal(schemajson,
                                      virQEMUQAPISchemaEntryProcess,
                                      schema) < 0)
        return NULL;

    VIR_RETURN_PTR(schema);
}
