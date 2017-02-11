/*
 * virinterfaceobj.c: interface object handling
 *                    (derived from interface_conf.c)
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

#include "datatypes.h"
#include "interface_conf.h"

#include "viralloc.h"
#include "virerror.h"
#include "virinterfaceobj.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_INTERFACE

VIR_LOG_INIT("conf.virinterfaceobj");


struct interfaceCountData {
    bool wantActive;
    int count;
};

static int
interfaceCount(virPoolObjPtr obj,
               void *opaque)
{
    struct interfaceCountData *data = opaque;

    if ((data->wantActive && virPoolObjIsActive(obj)) ||
        (!data->wantActive && !virPoolObjIsActive(obj)))
        data->count++;

    return 0;
}


int
virInterfaceObjNumOfInterfaces(virPoolObjTablePtr ifaces,
                               virConnectPtr conn,
                               bool wantActive,
                               virPoolObjACLFilter aclfilter)
{
    struct interfaceCountData data = { .wantActive = wantActive,
                                       .count = 0 };

    if (virPoolObjTableList(ifaces, conn, aclfilter, interfaceCount, &data) < 0)
        return 0;

    return data.count;
}


struct interfaceNameData {
    bool wantActive;
    int nnames;
    char **const names;
    int maxnames;
};

static int
interfaceGetNames(virPoolObjPtr obj,
                  void *opaque)
{
    struct interfaceNameData *data = opaque;

    if (data->nnames < data->maxnames) {
        if (data->wantActive && virPoolObjIsActive(obj)) {
            virInterfaceDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        } else if (!data->wantActive && !virPoolObjIsActive(obj)) {
            virInterfaceDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        }
    }

    return 0;
}


int
virInterfaceObjGetNames(virPoolObjTablePtr ifaces,
                        virConnectPtr conn,
                        bool wantActive,
                        virPoolObjACLFilter aclfilter,
                        char **const names,
                        int maxnames)
{
    struct interfaceNameData data = { .wantActive = wantActive,
                                      .nnames = 0,
                                      .names = names,
                                      .maxnames = maxnames };

    memset(names, 0, sizeof(*names) * maxnames);
    if (virPoolObjTableList(ifaces, conn, aclfilter,
                            interfaceGetNames, &data) < 0)
        goto error;

    return data.nnames;

 error:
    while (--data.nnames >= 0)
        VIR_FREE(names[data.nnames]);
    return -1;
}


struct interfaceListMACData {
    const char *mac;
    int nmatches;
    char **const matches;
    int maxmatches;
};

static int
interfaceListMACString(virPoolObjPtr obj,
                       void *opaque)
{
    virInterfaceDefPtr def = virPoolObjGetDef(obj);
    struct interfaceListMACData *data = opaque;

    if (STRCASEEQ(def->mac, data->mac)) {
        if (data->nmatches < data->maxmatches) {
            if (VIR_STRDUP(data->matches[data->nmatches++], def->name) < 0)
                return -1;
        }
    }
    return 0;
}


/* virInterfaceFindByMACString:
 * @interfaces: Pointer to object table
 * @mac: String to search on
 * @matches: Array of entries to store names matching MAC
 * @maxmatches: Size of the array
 *
 * Search the object table for matching MAC's
 *
 * Returns: count of matches found, -1 on error
 */
int
virInterfaceObjFindByMACString(virConnectPtr conn,
                               virPoolObjTablePtr interfaces,
                               const char *mac,
                               char **const matches,
                               int maxmatches)
{
    struct interfaceListMACData data = { .mac = mac,
                                         .nmatches = 0,
                                         .matches = matches,
                                         .maxmatches = maxmatches };

    if (virPoolObjTableList(interfaces, conn, NULL,
                            interfaceListMACString, &data) < 0)
        return -1;

    return data.nmatches;
}


static void *
cloneObjCallback(virPoolObjPtr src)
{
    virInterfaceDefPtr srcdef = virPoolObjGetDef(src);
    virInterfaceDefPtr dstdef;
    char *xml;

    if (!(xml = virInterfaceDefFormat(srcdef)))
        return NULL;

    dstdef = virInterfaceDefParseString(xml);

    VIR_FREE(xml);

    return dstdef;
}


virPoolObjTablePtr
virInterfaceObjClone(virPoolObjTablePtr src)
{
    return virPoolObjTableClone(src, cloneObjCallback);
}
