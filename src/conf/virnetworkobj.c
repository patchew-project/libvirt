/*
 * virnetworkobj.c: handle network objects
 *                  (derived from network_conf.c)
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
#include <dirent.h>

#include "datatypes.h"
#include "virnetworkobj.h"

#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NETWORK

VIR_LOG_INIT("conf.virnetworkobj");

/* currently, /sbin/tc implementation allows up to 16 bits for minor class size */
#define CLASS_ID_BITMAP_SIZE (1<<16)

struct _virNetworkObjPrivate {
    pid_t dnsmasqPid;
    pid_t radvdPid;

    virBitmapPtr class_id; /* bitmap of class IDs for QoS */
    unsigned long long floor_sum; /* sum of all 'floor'-s of attached NICs */

    unsigned int taint;

    /* Immutable pointer, self locking APIs */
    virMacMapPtr macmap;
};


static void
virNetworkObjPrivateFree(void *obj)
{
    virNetworkObjPrivatePtr objpriv = obj;
    if (!objpriv)
        return;

    virBitmapFree(objpriv->class_id);
    virObjectUnref(objpriv->macmap);

    VIR_FREE(objpriv);
}


static virNetworkObjPrivatePtr
virNetworkObjPrivateAlloc(void)
{
    virNetworkObjPrivatePtr objpriv = NULL;

    if (VIR_ALLOC(objpriv) < 0)
        return NULL;

    if (!(objpriv->class_id = virBitmapNew(CLASS_ID_BITMAP_SIZE)))
        goto error;

    /* The first three class IDs are already taken */
    ignore_value(virBitmapSetBit(objpriv->class_id, 0));
    ignore_value(virBitmapSetBit(objpriv->class_id, 1));
    ignore_value(virBitmapSetBit(objpriv->class_id, 2));

    return objpriv;

 error:
    virNetworkObjPrivateFree(objpriv);
    return NULL;
}


pid_t
virNetworkObjPrivateGetDnsmasqPid(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->dnsmasqPid;
}


void
virNetworkObjPrivateSetDnsmasqPid(virPoolObjPtr obj,
                                  pid_t dnsmasqPid)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    objpriv->dnsmasqPid = dnsmasqPid;
}


pid_t
virNetworkObjPrivateGetRadvdPid(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->radvdPid;
}


void
virNetworkObjPrivateSetRadvdPid(virPoolObjPtr obj,
                                pid_t radvdPid)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    objpriv->radvdPid = radvdPid;
}


virBitmapPtr
virNetworkObjPrivateGetClassId(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->class_id;
}


void
virNetworkObjPrivateSetClassId(virPoolObjPtr obj,
                               virBitmapPtr class_id)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    virBitmapFree(objpriv->class_id);
    objpriv->class_id = class_id;
}


unsigned long long
virNetworkObjPrivateGetFloorSum(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->floor_sum;
}


void
virNetworkObjPrivateSetFloorSum(virPoolObjPtr obj,
                                unsigned long long floor_sum)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    objpriv->floor_sum = floor_sum;
}


virMacMapPtr
virNetworkObjPrivateGetMacMap(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->macmap;
}


void
virNetworkObjPrivateSetMacMap(virPoolObjPtr obj,
                              virMacMapPtr macmap)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    virObjectUnref(objpriv->macmap);
    objpriv->macmap = macmap;
}


unsigned int
virNetworkObjPrivateGetTaint(virPoolObjPtr obj)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    return objpriv->taint;
}


bool
virNetworkObjPrivateIsTaint(virPoolObjPtr obj,
                            virNetworkTaintFlags taint)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    unsigned int flag = (1 << taint);

    if (objpriv->taint & flag)
        return false;

    objpriv->taint |= flag;
    return true;
}


void
virNetworkObjPrivateSetTaint(virPoolObjPtr obj,
                             unsigned int taint)
{
    virNetworkObjPrivatePtr objpriv = virPoolObjGetPrivateData(obj);

    objpriv->taint = taint;
}


/*
 * If flags & VIR_NETWORK_OBJ_LIST_ADD_CHECK_LIVE then this will
 * refuse updating an existing def if the current def is live
 *
 * If flags & VIR_NETWORK_OBJ_LIST_ADD_LIVE then the @def being
 * added is assumed to represent a live config, not a future
 * inactive config
 *
 * If flags is zero, network is considered as inactive and persistent.
 */
static int
networkAssignDef(virPoolObjPtr obj,
                 void *newDef,
                 void *oldDef ATTRIBUTE_UNUSED,
                 unsigned int assignFlags)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    virNetworkDefPtr newdef = newDef;

    if (assignFlags & VIR_NETWORK_OBJ_LIST_ADD_CHECK_LIVE) {
        /* UUID & name match, but if network is already active, refuse it */
        if (virPoolObjIsActive(obj)) {
            virReportError(VIR_ERR_OPERATION_INVALID,
                           _("network is already active as '%s'"),
                           def->name);
            return -1;
        }
    }

    if (assignFlags & VIR_NETWORK_OBJ_LIST_ADD_LIVE) {
        /* before setting new live def, save (into newDef) any
         * existing persistent (!live) def to be restored when the
         * network is destroyed, unless there is one already saved.
         */
        if (virPoolObjIsPersistent(obj) && !virPoolObjGetNewDef(obj)) {
            virPoolObjSetNewDef(obj, def);
            virPoolObjSetDef(obj, NULL); /* Don't want SetDef to free def! */
        }
        virPoolObjSetDef(obj, newdef);
    } else { /* !live */
        virNetworkDefFree(virPoolObjGetNewDef(obj));
        if (virPoolObjIsActive(obj)) {
            /* save new configuration to be restored on network
             * shutdown, leaving current live def alone
             */
            virPoolObjSetNewDef(obj, newdef);
        } else { /* !live and !active */
            if (def && !virPoolObjIsPersistent(obj)) {
                /* network isn't (yet) marked active or persistent,
                 * but already has a "live" def set. This means we are
                 * currently setting the persistent def as a part of
                 * the process of starting the network, so we need to
                 * preserve the "not yet live" def in network->def.
                 */
                virPoolObjSetNewDef(obj, newdef);
            } else {
                /* either there is no live def set, or this network
                 * was already set as persistent, so the proper thing
                 * is to overwrite network->def.
                 */
                virPoolObjSetNewDef(obj, NULL);
                virPoolObjSetDef(obj, newdef);
            }
        }
        virPoolObjSetPersistent(obj, !!newdef);
    }

    return 0;
}


/*
 * virNetworkObjAdd:
 * @netobjs: the network object to update
 * @def: the new NetworkDef (will be consumed by this function)
 * @assignFlags: assignment flags
 *
 * Replace the appropriate copy of the given network's def or newDef
 * with def. Use "live" and current state of the network to determine
 * which to replace and what to do with the old defs. When a non-live
 * def is set, indicate that the network is now persistent.
 *
 * NB: a persistent network can be made transient by calling with:
 * virNetworkObjAssignDef(network, NULL, false) (i.e. set the
 * persistent def to NULL)
 *
 */
virPoolObjPtr
virNetworkObjAdd(virPoolObjTablePtr netobjs,
                 virNetworkDefPtr def,
                 unsigned int assignFlags)
{
    virPoolObjPtr obj = NULL;
    virNetworkObjPrivatePtr objpriv = NULL;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(def->uuid, uuidstr);

    if (!(obj = virPoolObjTableAdd(netobjs, uuidstr, def->name,
                                   def, NULL, NULL, virNetworkDefFree,
                                   networkAssignDef, assignFlags)))
        return NULL;

    if (!(assignFlags & VIR_NETWORK_OBJ_LIST_ADD_LIVE))
        virPoolObjSetPersistent(obj, true);

    if (!(objpriv = virPoolObjGetPrivateData(obj))) {
        if (!(objpriv = virNetworkObjPrivateAlloc()))
            goto error;

        virPoolObjSetPrivateData(obj, objpriv, virNetworkObjPrivateFree);
    }

    return obj;

 error:
    virPoolObjTableRemove(netobjs, &obj);
    virPoolObjEndAPI(&obj);
    return NULL;
}


void
virNetworkObjAssignDef(virPoolObjPtr obj,
                       virNetworkDefPtr def)
{
    networkAssignDef(obj, def, NULL, false);
}


static char *
networkObjFormat(virPoolObjPtr obj,
                 unsigned int flags)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    char *class_id = virBitmapFormat(virNetworkObjPrivateGetClassId(obj));
    unsigned int taint = virNetworkObjPrivateGetTaint(obj);
    size_t i;

    if (!class_id)
        goto error;

    virBufferAddLit(&buf, "<networkstatus>\n");
    virBufferAdjustIndent(&buf, 2);
    virBufferAsprintf(&buf, "<class_id bitmap='%s'/>\n", class_id);
    virBufferAsprintf(&buf, "<floor sum='%llu'/>\n",
                      virNetworkObjPrivateGetFloorSum(obj));
    VIR_FREE(class_id);

    for (i = 0; i < VIR_NETWORK_TAINT_LAST; i++) {
        if (taint & (1 << i))
            virBufferAsprintf(&buf, "<taint flag='%s'/>\n",
                              virNetworkTaintTypeToString(i));
    }

    if (virNetworkDefFormatBuf(&buf, def, flags) < 0)
        goto error;

    virBufferAdjustIndent(&buf, -2);
    virBufferAddLit(&buf, "</networkstatus>");

    if (virBufferCheckError(&buf) < 0)
        goto error;

    return virBufferContentAndReset(&buf);

 error:
    virBufferFreeAndReset(&buf);
    return NULL;
}


int
virNetworkObjSaveStatus(const char *statusDir,
                        virPoolObjPtr obj)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    int ret = -1;
    int flags = 0;
    char *xml;

    if (!(xml = networkObjFormat(obj, flags)))
        goto cleanup;

    if (virNetworkSaveXML(statusDir, def, xml))
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(xml);
    return ret;
}


int
virNetworkObjDeleteConfig(const char *configDir,
                          const char *autostartDir,
                          virPoolObjPtr obj)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    char *configFile = NULL;
    char *autostartLink = NULL;
    int ret = -1;

    if (!(configFile = virNetworkConfigFile(configDir, def->name)))
        goto error;

    if (!(autostartLink = virNetworkConfigFile(autostartDir, def->name)))
        goto error;

    /* Not fatal if this doesn't work */
    unlink(autostartLink);
    virPoolObjSetAutostart(obj, false);

    if (unlink(configFile) < 0) {
        virReportSystemError(errno,
                             _("cannot remove config file '%s'"),
                             configFile);
        goto error;
    }

    ret = 0;

 error:
    VIR_FREE(configFile);
    VIR_FREE(autostartLink);
    return ret;
}


static virPoolObjPtr
networkLoadConfig(virPoolObjTablePtr netobjs,
                  const char *configDir,
                  const char *autostartDir,
                  const char *name)
{
    char *configFile = NULL;
    char *autostartLink = NULL;
    virNetworkDefPtr def = NULL;
    virPoolObjPtr obj;
    int autostart;

    if (!(configFile = virNetworkConfigFile(configDir, name)))
        goto error;

    if (!(autostartLink = virNetworkConfigFile(autostartDir, name)))
        goto error;

    if ((autostart = virFileLinkPointsTo(autostartLink, configFile)) < 0)
        goto error;

    if (!(def = virNetworkDefParseFile(configFile)))
        goto error;

    if (STRNEQ(name, def->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Network config filename '%s' does not match "
                         "network name '%s'"),
                       configFile, def->name);
        goto error;
    }

    if (def->forward.type == VIR_NETWORK_FORWARD_NONE ||
        def->forward.type == VIR_NETWORK_FORWARD_NAT ||
        def->forward.type == VIR_NETWORK_FORWARD_ROUTE ||
        def->forward.type == VIR_NETWORK_FORWARD_OPEN) {

        if (!def->mac_specified) {
            virNetworkSetBridgeMacAddr(def);
            virNetworkSaveConfig(configDir, def);
        }
    } else {
        /* Throw away MAC address for other forward types,
         * which could have been generated by older libvirt RPMs */
        def->mac_specified = false;
    }

    if (!(obj = virNetworkObjAdd(netobjs, def, 0)))
        goto error;
    def = NULL;

    virPoolObjSetAutostart(obj, autostart);

    VIR_FREE(configFile);
    VIR_FREE(autostartLink);

    return obj;

 error:
    VIR_FREE(configFile);
    VIR_FREE(autostartLink);
    virNetworkDefFree(def);
    return NULL;
}


int
virNetworkObjLoadAllConfigs(virPoolObjTablePtr netobjs,
                            const char *configDir,
                            const char *autostartDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, configDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, configDir)) > 0) {
        virPoolObjPtr obj;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        /* NB: ignoring errors, so one malformed config doesn't
           kill the whole process */
        obj = networkLoadConfig(netobjs, configDir, autostartDir,
                                entry->d_name);
        virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


static virPoolObjPtr
networkObjLoadState(virPoolObjTablePtr netobjs,
                    const char *stateDir,
                    const char *name)
{
    char *configFile = NULL;
    virNetworkDefPtr def = NULL;
    virPoolObjPtr obj = NULL;
    xmlDocPtr xml = NULL;
    xmlNodePtr node = NULL;
    xmlNodePtr *nodes = NULL;
    xmlXPathContextPtr ctxt = NULL;
    virBitmapPtr class_id_map = NULL;
    unsigned long long floor_sum_val = 0;
    unsigned int taint = 0;
    int n;
    size_t i;


    if ((configFile = virNetworkConfigFile(stateDir, name)) == NULL)
        goto error;

    if (!(xml = virXMLParseCtxt(configFile, NULL, _("(network status)"),
                                &ctxt)))
        goto error;

    if (!(node = virXPathNode("//network", ctxt))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find any 'network' element "
                         "in status file"));
        goto error;
    }

    /* parse the definition first */
    ctxt->node = node;
    if (!(def = virNetworkDefParseXML(ctxt)))
        goto error;

    if (STRNEQ(name, def->name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Network config filename '%s' does not match "
                         "network name '%s'"),
                       configFile, def->name);
        goto error;
    }

    /* now parse possible status data */
    node = xmlDocGetRootElement(xml);
    if (xmlStrEqual(node->name, BAD_CAST "networkstatus")) {
        /* Newer network status file. Contains useful
         * info which are not to be found in bare config XML */
        char *class_id = NULL;
        char *floor_sum = NULL;

        ctxt->node = node;
        if ((class_id =
             virXPathString("string(./class_id[1]/@bitmap)", ctxt))) {
            if (virBitmapParse(class_id, &class_id_map,
                               CLASS_ID_BITMAP_SIZE) < 0) {
                VIR_FREE(class_id);
                goto error;
            }
        }
        VIR_FREE(class_id);

        floor_sum = virXPathString("string(./floor[1]/@sum)", ctxt);
        if (floor_sum &&
            virStrToLong_ull(floor_sum, NULL, 10, &floor_sum_val) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Malformed 'floor_sum' attribute: %s"),
                           floor_sum);
            VIR_FREE(floor_sum);
            goto error;
        }
        VIR_FREE(floor_sum);

        if ((n = virXPathNodeSet("./taint", ctxt, &nodes)) < 0)
            goto error;

        for (i = 0; i < n; i++) {
            char *str = virXMLPropString(nodes[i], "flag");
            if (str) {
                int flag = virNetworkTaintTypeFromString(str);
                if (flag < 0) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Unknown taint flag %s"), str);
                    VIR_FREE(str);
                    goto error;
                }
                VIR_FREE(str);
                /* Compute taint mask here. The network object does not
                 * exist yet, so we can't use virNetworkObjtTaint. */
                taint |= (1 << flag);
            }
        }
        VIR_FREE(nodes);
    }

    /* create the object */
    if (!(obj = virNetworkObjAdd(netobjs, def, VIR_NETWORK_OBJ_LIST_ADD_LIVE)))
        goto error;
    def = NULL;

    /* assign status data stored in the network object */
    if (class_id_map)
        virNetworkObjPrivateSetClassId(obj, class_id_map);

    if (floor_sum_val > 0)
        virNetworkObjPrivateSetFloorSum(obj, floor_sum_val);

    virNetworkObjPrivateSetTaint(obj, taint);

    /* any network with a state file is by definition active */
    virPoolObjSetActive(obj, true);

 cleanup:
    VIR_FREE(configFile);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return obj;

 error:
    VIR_FREE(nodes);
    virBitmapFree(class_id_map);
    virNetworkDefFree(def);
    goto cleanup;
}


int
virNetworkObjLoadAllState(virPoolObjTablePtr netobjs,
                          const char *stateDir)
{
    DIR *dir;
    struct dirent *entry;
    int ret = -1;
    int rc;

    if ((rc = virDirOpenIfExists(&dir, stateDir)) <= 0)
        return rc;

    while ((ret = virDirRead(dir, &entry, stateDir)) > 0) {
        virPoolObjPtr obj;

        if (!virFileStripSuffix(entry->d_name, ".xml"))
            continue;

        obj = networkObjLoadState(netobjs, stateDir, entry->d_name);
        virPoolObjEndAPI(&obj);
    }

    VIR_DIR_CLOSE(dir);
    return ret;
}


/*
 * virNetworkObjSetDefTransient:
 * @obj: pool object pointer
 * @live: if true, run this operation even for an inactive network.
 *   this allows freely updated network->def with runtime defaults
 *   before starting the network, which will be discarded on network
 *   shutdown. Any cleanup paths need to be sure to handle newDef if
 *   the network is never started.
 *
 * Mark the active network config as transient. Ensures live-only update
 * operations do not persist past network destroy.
 *
 * Returns 0 on success, -1 on failure
 */
int
virNetworkObjSetDefTransient(virPoolObjPtr obj,
                             bool live)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    virNetworkDefPtr newDef;

    if (!virPoolObjIsActive(obj) && !live)
        return 0;

    if (!virPoolObjIsPersistent(obj) || virPoolObjGetNewDef(obj))
        return 0;

    newDef = virNetworkDefCopy(def, VIR_NETWORK_XML_INACTIVE);
    virPoolObjSetNewDef(obj, newDef);
    return newDef ? 0 : -1;
}


/* virNetworkObjUnsetDefTransient:
 *
 * This *undoes* what virNetworkObjSetDefTransient did.
 */
void
virNetworkObjUnsetDefTransient(virPoolObjPtr obj)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    virNetworkDefPtr newDef = virPoolObjGetNewDef(obj);
    if (newDef) {
        virNetworkDefFree(def);
        virPoolObjSetDef(obj, newDef);
        virPoolObjSetNewDef(obj, NULL);
    }
}


/*
 * virNetworkObjGetPersistentDef:
 * @obj: pool object pointer
 *
 * Return the persistent network configuration. If network is transient,
 * return the running config.
 *
 * Returns NULL on error, virNetworkDefPtr on success.
 */
virNetworkDefPtr
virNetworkObjGetPersistentDef(virPoolObjPtr obj)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    virNetworkDefPtr newDef = virPoolObjGetNewDef(obj);
    if (newDef)
        return newDef;
    else
        return def;
}


/*
 * virNetworkObjReplacePersistentDef:
 * @obj: pool object pointer
 * @def: new virNetworkDef to replace current persistent config
 *
 * Replace the "persistent" network configuration with the given new
 * virNetworkDef. This pays attention to whether or not the network
 * is active.
 *
 * Returns -1 on error, 0 on success
 */
int
virNetworkObjReplacePersistentDef(virPoolObjPtr obj,
                                  virNetworkDefPtr def)
{
    virNetworkDefPtr curDef = virPoolObjGetDef(obj);
    virNetworkDefPtr newDef = virPoolObjGetNewDef(obj);

    if (virPoolObjIsActive(obj)) {
        virNetworkDefFree(newDef);
        virPoolObjSetNewDef(obj, def);
    } else {
        virNetworkDefFree(curDef);
        virPoolObjSetDef(obj, def);
    }
    return 0;
}


/*
 * networkObjConfigChangeSetup:
 *
 * 1) checks whether network state is consistent with the requested
 *    type of modification.
 *
 * 3) make sure there are separate "def" and "newDef" copies of
 *    networkDef if appropriate.
 *
 * Returns 0 on success, -1 on error.
 */
static int
networkObjConfigChangeSetup(virPoolObjPtr obj,
                            unsigned int flags)
{
    bool isActive;
    int ret = -1;

    isActive = virPoolObjIsActive(obj);

    if (!isActive && (flags & VIR_NETWORK_UPDATE_AFFECT_LIVE)) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("network is not running"));
        goto cleanup;
    }

    if (flags & VIR_NETWORK_UPDATE_AFFECT_CONFIG) {
        if (!virPoolObjIsPersistent(obj)) {
            virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                           _("cannot change persistent config of a "
                             "transient network"));
            goto cleanup;
        }
        /* this should already have been done by the driver, but do it
         * anyway just in case.
         */
        if (isActive && (virNetworkObjSetDefTransient(obj, false) < 0))
            goto cleanup;
    }

    ret = 0;

 cleanup:
    return ret;
}


/*
 * virNetworkObjUpdate:
 *
 * Apply the supplied update to the given pool object. Except for
 * @obj pointing to an actual network object rather than the
 * opaque virNetworkPtr, parameters are identical to the public API
 * virNetworkUpdate.
 *
 * The original virNetworkDefs are copied, and all modifications made
 * to these copies. The originals are replaced with the copies only
 * after success has been guaranteed.
 *
 * Returns: -1 on error, 0 on success.
 */
int
virNetworkObjUpdate(virPoolObjPtr obj,
                    unsigned int command, /* virNetworkUpdateCommand */
                    unsigned int section, /* virNetworkUpdateSection */
                    int parentIndex,
                    const char *xml,
                    unsigned int flags)  /* virNetworkUpdateFlags */
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    int ret = -1;
    virNetworkDefPtr livedef = NULL, configdef = NULL;

    /* normalize config data, and check for common invalid requests. */
    if (networkObjConfigChangeSetup(obj, flags) < 0)
       goto cleanup;

    if (flags & VIR_NETWORK_UPDATE_AFFECT_LIVE) {
        virNetworkDefPtr checkdef;

        /* work on a copy of the def */
        if (!(livedef = virNetworkDefCopy(def, 0)))
            goto cleanup;
        if (virNetworkDefUpdateSection(livedef, command, section,
                                       parentIndex, xml, flags) < 0)
            goto cleanup;

        /* run a final format/parse cycle to make sure we didn't
         * add anything illegal to the def
         */
        if (!(checkdef = virNetworkDefCopy(livedef, 0)))
            goto cleanup;
        virNetworkDefFree(checkdef);
    }

    if (flags & VIR_NETWORK_UPDATE_AFFECT_CONFIG) {
        virNetworkDefPtr checkdef;

        /* work on a copy of the def */
        if (!(configdef = virNetworkDefCopy(virNetworkObjGetPersistentDef(obj),
                                            VIR_NETWORK_XML_INACTIVE))) {
            goto cleanup;
        }
        if (virNetworkDefUpdateSection(configdef, command, section,
                                       parentIndex, xml, flags) < 0) {
            goto cleanup;
        }
        if (!(checkdef = virNetworkDefCopy(configdef,
                                           VIR_NETWORK_XML_INACTIVE))) {
            goto cleanup;
        }
        virNetworkDefFree(checkdef);
    }

    if (configdef) {
        /* successfully modified copy, now replace original */
        if (virNetworkObjReplacePersistentDef(obj, configdef) < 0)
           goto cleanup;
        configdef = NULL;
    }
    if (livedef) {
        /* successfully modified copy, now replace original */
        virNetworkDefFree(def);
        virPoolObjSetDef(obj, livedef);
        livedef = NULL;
    }

    ret = 0;
 cleanup:
    virNetworkDefFree(livedef);
    virNetworkDefFree(configdef);
    return ret;
}


struct networkCountData {
    bool wantActive;
    int count;
};

static int
networkCount(virPoolObjPtr obj,
             void *opaque)
{
    struct networkCountData *data = opaque;

    if ((data->wantActive && virPoolObjIsActive(obj)) ||
        (!data->wantActive && !virPoolObjIsActive(obj)))
        data->count++;

    return 0;
}


int
virNetworkObjNumOfNetworks(virPoolObjTablePtr netobjs,
                           virConnectPtr conn,
                           bool wantActive,
                           virPoolObjACLFilter aclfilter)
{
    struct networkCountData data = { .count = 0,
                                     .wantActive = wantActive };

    if (virPoolObjTableList(netobjs, conn, aclfilter, networkCount, &data) < 0)
        return 0;

    return data.count;
}


struct networkNameData {
    bool wantActive;
    int nnames;
    char **const names;
    int maxnames;
};

static int
networkListNames(virPoolObjPtr obj,
                 void *opaque)
{
    struct networkNameData *data = opaque;

    if (data->nnames < data->maxnames) {
        if (data->wantActive && virPoolObjIsActive(obj)) {
            virNetworkDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        } else if (!data->wantActive && !virPoolObjIsActive(obj)) {
            virNetworkDefPtr def = virPoolObjGetDef(obj);

            if (VIR_STRDUP(data->names[data->nnames++], def->name) < 0)
                return -1;
        }
    }
    return 0;
}


int
virNetworkObjGetNames(virPoolObjTablePtr netobjs,
                      virConnectPtr conn,
                      bool wantActive,
                      virPoolObjACLFilter aclfilter,
                      char **const names,
                      int maxnames)
{
    struct networkNameData data = { .wantActive = wantActive,
                                 .nnames = 0,
                                 .names = names,
                                 .maxnames = maxnames };

    memset(names, 0, sizeof(*names) * maxnames);
    if (virPoolObjTableList(netobjs, conn, aclfilter,
                            networkListNames, &data) < 0)
        goto error;

    return data.nnames;

 error:
    while (--data.nnames >= 0)
        VIR_FREE(names[data.nnames]);
    return -1;
}


#define MATCH(FLAG) (flags & (FLAG))
static bool
networkMatch(virPoolObjPtr obj,
             unsigned int flags)
{
    /* filter by active state */
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_ACTIVE) &&
        !((MATCH(VIR_CONNECT_LIST_NETWORKS_ACTIVE) &&
           virPoolObjIsActive(obj)) ||
          (MATCH(VIR_CONNECT_LIST_NETWORKS_INACTIVE) &&
           !virPoolObjIsActive(obj))))
        return false;

    /* filter by persistence */
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_PERSISTENT) &&
        !((MATCH(VIR_CONNECT_LIST_NETWORKS_PERSISTENT) &&
           virPoolObjIsPersistent(obj)) ||
          (MATCH(VIR_CONNECT_LIST_NETWORKS_TRANSIENT) &&
           !virPoolObjIsPersistent(obj))))
        return false;

    /* filter by autostart option */
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_AUTOSTART) &&
        !((MATCH(VIR_CONNECT_LIST_NETWORKS_AUTOSTART) &&
           virPoolObjIsAutostart(obj)) ||
          (MATCH(VIR_CONNECT_LIST_NETWORKS_NO_AUTOSTART) &&
           !virPoolObjIsAutostart(obj))))
        return false;

    return true;
}
#undef MATCH

int
virNetworkObjExportList(virConnectPtr conn,
                        virPoolObjTablePtr netobjs,
                        virNetworkPtr **nets,
                        virPoolObjACLFilter aclfilter,
                        unsigned int flags)
{
    virPoolObjPtr *objs = NULL;
    size_t nobjs;
    virNetworkPtr *tmp_nets = NULL;
    size_t i;

    if (virPoolObjTableCollect(netobjs, conn, &objs, &nobjs, aclfilter,
                               networkMatch, flags) < 0)
        return -1;

    if (nets) {
        if (VIR_ALLOC_N(tmp_nets, nobjs + 1) < 0)
            goto cleanup;

        for (i = 0; i < nobjs; i++) {
            virNetworkDefPtr def;

            virObjectLock(objs[i]);
            def = virPoolObjGetDef(objs[i]);
            tmp_nets[i] = virGetNetwork(conn, def->name, def->uuid);
            virObjectUnlock(objs[i]);
            if (!tmp_nets[i])
                goto cleanup;
        }

        VIR_STEAL_PTR(*nets, tmp_nets);
    }

 cleanup:
    virObjectListFree(tmp_nets);
    virObjectListFreeCount(objs, nobjs);

    return nobjs;
}


/**
 * virNetworkObjListPrune:
 * @netobjs: network objects table
 * @flags: bitwise-OR of virConnectListAllNetworksFlags
 *
 * Iterate over list of network objects and remove the desired
 * ones from it.
 */
void
virNetworkObjPrune(virPoolObjTablePtr netobjs,
                   unsigned int flags)
{
    virPoolObjTablePrune(netobjs, networkMatch, flags);
}


struct bridgeInUseData {
    const char *bridge;
    const char *skipname;
};

static bool
bridgeInUse(virPoolObjPtr obj,
            void *opaque)
{
    virNetworkDefPtr def = virPoolObjGetDef(obj);
    virNetworkDefPtr newDef = virPoolObjGetNewDef(obj);
    const struct bridgeInUseData *data = opaque;

    if (data->skipname &&
        ((STREQ(def->name, data->skipname)) ||
         (newDef && STREQ(newDef->name, data->skipname))))
        return false;
    else if ((def->bridge && STREQ(def->bridge, data->bridge)) ||
             (newDef && newDef->bridge &&
              STREQ(newDef->bridge, data->bridge)))
        return true;

    return false;
}


bool
virNetworkObjBridgeInUse(virPoolObjTablePtr netobjs,
                         const char *bridge,
                         const char *skipname)
{
    virPoolObjPtr obj;
    struct bridgeInUseData data = { .bridge = bridge,
                                    .skipname = skipname };

    if ((obj = virPoolObjTableSearch(netobjs, bridgeInUse, &data))) {
        virObjectUnlock(obj);
        return true;
    }

    return false;
}
