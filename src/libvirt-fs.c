/*
 * libvirt-fs.c: entry points for virFS{Pool, Item}Ptr APIs
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

#include "datatypes.h"
#include "virlog.h"

VIR_LOG_INIT("libvirt.fs");

#define VIR_FROM_THIS VIR_FROM_FSPOOL


/**
 * virFSPoolGetConnect:
 * @fspool: pointer to a fspool
 *
 * Provides the connection pointer associated with fspool.  The
 * reference counter on the connection is not increased by this
 * call.
 * Returns the virConnectPtr or NULL in case of failure.
 */
virConnectPtr
virFSPoolGetConnect(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);

    return fspool->conn;
}


/**
 * virConnectListAllFSPools:
 * @conn: Pointer to the hypervisor connection.
 * @fspools: Pointer to a variable to store the array containing fspool
 *         objects or NULL if the list is not required (just returns number
 *         of fspools).
 * @flags: bitwise-OR of virConnectListAllFSPoolsFlags.
 *
 * Collect the list of fspools, and allocate an array to store those
 * objects. This API solves the race inherent between
 * virConnectListFSPools and virConnectListDefinedFSPools.
 *
 * Normally, all fspools are returned; however, @flags can be used to
 * filter the results for a smaller list of targeted fspools.  The valid
 * flags are divided into groups, where each group contains bits that
 * describe mutually exclusive attributes of a fspool, and where all bits
 * within a group describe all possible fspools.
 *
 * The only group (at the moment) of @flags is provided to filter the fspools by the types,
 * the flags include:
 * VIR_CONNECT_LIST_FSPOOLS_DIR
 * VIR_CONNECT_LIST_FSPOOLS_VOLUME
 * VIR_CONNECT_LIST_FSPOOLS_NETFS
 *
 * Returns the number of fs fspools found or -1 and sets @fspools to
 * NULL in case of error.  On success, the array stored into @fspools is
 * guaranteed to have an extra allocated element set to NULL but not included
 * in the return count, to make iteration easier.  The caller is responsible
 * for calling virFSPoolFree() on each array element, then calling
 * free() on @fspools.
 */
int
virConnectListAllFSPools(virConnectPtr conn,
                         virFSPoolPtr **fspools,
                         unsigned int flags)
{
    VIR_DEBUG("conn=%p, fspools=%p, flags=%x", conn, fspools, flags);

    virResetLastError();

    if (fspools)
        *fspools = NULL;

    virCheckConnectReturn(conn, -1);

    if (conn->fsDriver &&
        conn->fsDriver->connectListAllFSPools) {
        int ret;
        ret = conn->fsDriver->connectListAllFSPools(conn, fspools, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return -1;
}

/**
 * virFSPoolLookupByName:
 * @conn: pointer to hypervisor connection
 * @name: name of fspool to fetch
 *
 * Fetch fspool based on its unique name
 *
 * virFSPoolFree should be used to free the resources after the
 * fs fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if no matching fspool is found
 */
virFSPoolPtr
virFSPoolLookupByName(virConnectPtr conn,
                      const char *name)
{
    VIR_DEBUG("conn=%p, name=%s", conn, NULLSTR(name));

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(name, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolLookupByName) {
        virFSPoolPtr ret;
        ret = conn->fsDriver->fsPoolLookupByName(conn, name);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSPoolLookupByUUID:
 * @conn: pointer to hypervisor connection
 * @uuid: globally unique id of fspool to fetch
 *
 * Fetch a fspool based on its globally unique id
 *
 * virFSPoolFree should be used to free the resources after the
 * fs fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if no matching fspool is found
 */
virFSPoolPtr
virFSPoolLookupByUUID(virConnectPtr conn,
                      const unsigned char *uuid)
{
    VIR_UUID_DEBUG(conn, uuid);

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(uuid, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolLookupByUUID) {
        virFSPoolPtr ret;
        ret = conn->fsDriver->fsPoolLookupByUUID(conn, uuid);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSPoolLookupByUUIDString:
 * @conn: pointer to hypervisor connection
 * @uuidstr: globally unique id of fspool to fetch
 *
 * Fetch a fs fspool based on its globally unique id
 *
 * virFSPoolFree should be used to free the resources after the
 * fs fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if no matching fspool is found
 */
virFSPoolPtr
virFSPoolLookupByUUIDString(virConnectPtr conn,
                            const char *uuidstr)
{
    unsigned char uuid[VIR_UUID_BUFLEN];
    VIR_DEBUG("conn=%p, uuidstr=%s", conn, NULLSTR(uuidstr));

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(uuidstr, error);

    if (virUUIDParse(uuidstr, uuid) < 0) {
        virReportInvalidArg(uuidstr,
                            _("uuidstr in %s must be a valid UUID"),
                            __FUNCTION__);
        goto error;
    }

    return virFSPoolLookupByUUID(conn, uuid);

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSPoolLookupByItem:
 * @item: pointer to fspool item
 *
 * Fetch a fspool which contains a particular item
 *
 * virFSPoolFree should be used to free the resources after the
 * fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if no matching fspool is found
 */
virFSPoolPtr
virFSPoolLookupByItem(virFSItemPtr item)
{
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);

    if (item->conn->fsDriver && item->conn->fsDriver->fsPoolLookupByItem) {
        virFSPoolPtr ret;
        ret = item->conn->fsDriver->fsPoolLookupByItem(item);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(item->conn);
    return NULL;
}


/**
 * virFSPoolCreateXML:
 * @conn: pointer to hypervisor connection
 * @xmlDesc: XML description for new fspool
 * @flags: bitwise-OR of virFSPoolCreateFlags
 *
 * Create a new fspool based on its XML description. The
 * fspool is not persistent, so its definition will disappear
 * when it is destroyed, or if the host is restarted
 *
 * virFSPoolFree should be used to free the resources after the
 *fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if creation failed
 */
virFSPoolPtr
virFSPoolCreateXML(virConnectPtr conn,
                   const char *xmlDesc,
                   unsigned int flags)
{
    VIR_DEBUG("conn=%p, xmlDesc=%s, flags=%x", conn, NULLSTR(xmlDesc), flags);

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolCreateXML) {
        virFSPoolPtr ret;
        ret = conn->fsDriver->fsPoolCreateXML(conn, xmlDesc, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSPoolDefineXML:
 * @conn: pointer to hypervisor connection
 * @xml: XML description for new fspool
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Define an inactive persistent fspool or modify an existing persistent
 * one from the XML description.
 *
 * virFSPoolFree should be used to free the resources after the
 * fspool object is no longer needed.
 *
 * Returns a virFSPoolPtr object, or NULL if creation failed
 */
virFSPoolPtr
virFSPoolDefineXML(virConnectPtr conn,
                   const char *xml,
                   unsigned int flags)
{
    VIR_DEBUG("conn=%p, xml=%s, flags=%x", conn, NULLSTR(xml), flags);

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckReadOnlyGoto(conn->flags, error);
    virCheckNonNullArgGoto(xml, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolDefineXML) {
        virFSPoolPtr ret;
        ret = conn->fsDriver->fsPoolDefineXML(conn, xml, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSPoolBuild:
 * @fspool: pointer to fspool
 * @flags: bitwise-OR of virFSPoolBuildFlags
 *
 * Build the underlying fspool
 *
 * Returns 0 on success, or -1 upon failure
 */
int
virFSPoolBuild(virFSPoolPtr fspool,
               unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, flags=%x", fspool, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolBuild) {
        int ret;
        ret = conn->fsDriver->fsPoolBuild(fspool, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolRefresh:
 * @fspool: pointer to fspool
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Request that the fspool refresh its list of itemms. This may
 * involve communicating with a remote server, and/or initializing
 * new devices at the OS layer
 *
 * Returns 0 if the items list was refreshed, -1 on failure
 */
int
virFSPoolRefresh(virFSPoolPtr fspool,
                 unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, flags=%x", fspool, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolRefresh) {
        int ret;
        ret = conn->fsDriver->fsPoolRefresh(fspool, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}
/**
 * virFSPoolUndefine:
 * @fspool: pointer to fspool
 *
 * Undefine an inactive fspool
 *
 * Returns 0 on success, -1 on failure
 */
int
virFSPoolUndefine(virFSPoolPtr fspool)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolUndefine) {
        int ret;
        ret = conn->fsDriver->fsPoolUndefine(fspool);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolCreate:
 * @fspool: pointer to fspool
 * @flags: bitwise-OR of virFSPoolCreateFlags
 *
 * Starts an inactive fspool
 *
 * Returns 0 on success, or -1 if it could not be started
 */
int
virFSPoolCreate(virFSPoolPtr fspool,
                     unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, flags=%x", fspool, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolCreate) {
        int ret;
        ret = conn->fsDriver->fsPoolCreate(fspool, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolDestroy:
 * @fspool: pointer to fspool
 *
 * Destroy an active fspool. This will deactivate the
 * fspool on the host, but keep any persistent config associated
 * with it. If it has a persistent config it can later be
 * restarted with virFSPoolCreate(). This does not free
 * the associated virFSPoolPtr object.
 *
 * Returns 0 on success, or -1 if it could not be destroyed
 */
int
virFSPoolDestroy(virFSPoolPtr fspool)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolDestroy) {
        int ret;
        ret = conn->fsDriver->fsPoolDestroy(fspool);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolDelete:
 * @fspool: pointer to fspool
 * @flags: bitwise-OR of virFSPoolDeleteFlags
 *
 * Delete the underlying fspool resources. This is
 * a non-recoverable operation. The virFSPoolPtr object
 * itself is not free'd.
 *
 * Returns 0 on success, or -1 if it could not be obliterate
 */
int
virFSPoolDelete(virFSPoolPtr fspool,
                unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, flags=%x", fspool, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolDelete) {
        int ret;
        ret = conn->fsDriver->fsPoolDelete(fspool, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolFree:
 * @fspool: pointer to fspool
 *
 * Free a fs fspool object, releasing all memory associated with
 * it. Does not change the state of the fspool on the host.
 *
 * Returns 0 on success, or -1 if it could not be free'd.
 */
int
virFSPoolFree(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    virObjectUnref(fspool);
    return 0;

}


/**
 * virFSPoolRef:
 * @fspool: the fspool to hold a reference on
 *
 * Increment the reference count on the fspool. For each
 * additional call to this method, there shall be a corresponding
 * call to virFSPoolFree to release the reference count, once
 * the caller no longer needs the reference to this object.
 *
 * This method is typically useful for applications where multiple
 * threads are using a connection, and it is required that the
 * connection remain open until all threads have finished using
 * it. ie, each new thread using a fspool would increment
 * the reference count.
 *
 * Returns 0 in case of success, -1 in case of failure.
 */
int
virFSPoolRef(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p refs=%d", fspool, fspool ? fspool->object.u.s.refs : 0);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    virObjectRef(fspool);
    return 0;
}

/**
 * virFSPoolGetName:
 * @fspool: pointer to fs fspool
 *
 * Fetch the locally unique name of the fs fspool
 *
 * Returns the name of the fspool, or NULL on error
 */
const char*
virFSPoolGetName(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);

    return fspool->name;
}


/**
 * virFSPoolGetUUID:
 * @fspool: pointer to fspool
 * @uuid: buffer of VIR_UUID_BUFLEN bytes in size
 *
 * Fetch the globally unique ID of the fspool
 *
 * Returns 0 on success, or -1 on error;
 */
int
virFSPoolGetUUID(virFSPoolPtr fspool,
                 unsigned char *uuid)
{
    VIR_DEBUG("fspool=%p, uuid=%p", fspool, uuid);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    virCheckNonNullArgGoto(uuid, error);

    memcpy(uuid, &fspool->uuid[0], VIR_UUID_BUFLEN);

    return 0;

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolGetUUIDString:
 * @fspool: pointer to fspool
 * @buf: buffer of VIR_UUID_STRING_BUFLEN bytes in size
 *
 * Fetch the globally unique ID of the fspool as a string
 *
 * Returns 0 on success, or -1 on error;
 */
int
virFSPoolGetUUIDString(virFSPoolPtr fspool,
                       char *buf)
{
    VIR_DEBUG("fspool=%p, buf=%p", fspool, buf);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    virCheckNonNullArgGoto(buf, error);

    virUUIDFormat(fspool->uuid, buf);
    return 0;

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolGetInfo:
 * @fspool: pointer to fs fspool
 * @info: pointer at which to store info
 *
 * Get information about the fspool
 * such as free space / usage summary
 *
 * Returns 0 on success, or -1 on failure.
 */
int
virFSPoolGetInfo(virFSPoolPtr fspool,
                 virFSPoolInfoPtr info)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, info=%p", fspool, info);

    virResetLastError();

    if (info)
        memset(info, 0, sizeof(*info));

    virCheckFSPoolReturn(fspool, -1);
    virCheckNonNullArgGoto(info, error);

    conn = fspool->conn;

    if (conn->fsDriver->fsPoolGetInfo) {
        int ret;
        ret = conn->fsDriver->fsPoolGetInfo(fspool, info);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolGetAutostart:
 * @fspool: pointer to fspool
 * @autostart: location in which to store autostart flag
 *
 * Fetches the value of the autostart flag, which determines
 * whether the fspool is automatically started at boot time
 *
 * Returns 0 on success, -1 on failure
 */
int
virFSPoolGetAutostart(virFSPoolPtr fspool,
                           int *autostart)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, autostart=%p", fspool, autostart);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    virCheckNonNullArgGoto(autostart, error);

    conn = fspool->conn;

    if (conn->fsDriver && conn->fsDriver->fsPoolGetAutostart) {
        int ret;
        ret = conn->fsDriver->fsPoolGetAutostart(fspool, autostart);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolSetAutostart:
 * @fspool: pointer to fspool
 * @autostart: new flag setting
 *
 * Sets the autostart flag
 *
 * Returns 0 on success, -1 on failure
 */
int
virFSPoolSetAutostart(virFSPoolPtr fspool,
                           int autostart)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, autostart=%d", fspool, autostart);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    conn = fspool->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsPoolSetAutostart) {
        int ret;
        ret = conn->fsDriver->fsPoolSetAutostart(fspool, autostart);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}

/**
 * virFSPoolGetXMLDesc:
 * @fspool: pointer to fspool
 * @flags: bitwise-OR of virFsXMLFlags
 *
 * Fetch an XML document describing all aspects of the
 * fs fspool. This is suitable for later feeding back
 * into the virFSPoolCreateXML method.
 *
 * Returns a XML document (caller frees), or NULL on error
 */
char *
virFSPoolGetXMLDesc(virFSPoolPtr fspool,
                    unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("fspool=%p, flags=%x", fspool, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);
    conn = fspool->conn;

    if (conn->fsDriver && conn->fsDriver->fsPoolGetXMLDesc) {
        char *ret;
        ret = conn->fsDriver->fsPoolGetXMLDesc(fspool, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return NULL;
}


/**
 * virFSPoolListAllItems:
 * @fspool: Pointer to fspool
 * @items: Pointer to a variable to store the array containing fs item
 *        objects or NULL if the list is not required (just returns number
 *        of items).
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Collect the list of fsitems, and allocate an array to store those
 * objects.
 *
 * Returns the number of fs items found or -1 and sets @items to
 * NULL in case of error.  On success, the array stored into @items is
 * guaranteed to have an extra allocated element set to NULL but not included
 * in the return count, to make iteration easier.  The caller is responsible
 * for calling virFSItemFree() on each array element, then calling
 * free() on @items.
 */
int
virFSPoolListAllItems(virFSPoolPtr fspool,
                      virFSItemPtr **items,
                      unsigned int flags)
{
    VIR_DEBUG("fspool=%p, items=%p, flags=%x", fspool, items, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    if (fspool->conn->fsDriver &&
        fspool->conn->fsDriver->fsPoolListAllItems) {
        int ret;
        ret = fspool->conn->fsDriver->fsPoolListAllItems(fspool, items, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolNumOfItems:
 * @fspool: pointer to fspool
 *
 * Fetch the number of items within a fspool
 *
 * Returns the number of fspools, or -1 on failure
 */
int
virFSPoolNumOfItems(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    if (fspool->conn->fsDriver && fspool->conn->fsDriver->fsPoolNumOfItems) {
        int ret;
        ret = fspool->conn->fsDriver->fsPoolNumOfItems(fspool);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolListItems:
 * @fspool: pointer to fspool
 * @names: array in which to fsitem names
 * @maxnames: size of names array
 *
 * Fetch list of fs item names, limiting to
 * at most maxnames.
 *
 * To list the item objects directly, see virFSPoolListAllItems().
 *
 * Returns the number of names fetched, or -1 on error
 */
int
virFSPoolListItems(virFSPoolPtr fspool,
                   char **const names,
                   int maxnames)
{
    VIR_DEBUG("fspool=%p, names=%p, maxnames=%d", fspool, names, maxnames);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);
    virCheckNonNullArgGoto(names, error);
    virCheckNonNegativeArgGoto(maxnames, error);

    if (fspool->conn->fsDriver && fspool->conn->fsDriver->fsPoolListItems) {
        int ret;
        ret = fspool->conn->fsDriver->fsPoolListItems(fspool, names, maxnames);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSItemGetConnect:
 * @item: pointer to a fspool
 *
 * Provides the connection pointer associated with a fs item.  The
 * reference counter on the connection is not increased by this
 * call.
 *
 * WARNING: When writing libvirt bindings in other languages, do
 * not use this function.  Instead, store the connection and
 * the item object together.
 *
 * Returns the virConnectPtr or NULL in case of failure.
 */
virConnectPtr
virFSItemGetConnect(virFSItemPtr item)
{
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);

    return item->conn;
}


/**
 * virFSItemLookupByName:
 * @fspool: pointer to fspool
 * @name: name of fsitem
 *
 * Fetch a pointer to a fs item based on its name
 * within a fspool
 *
 * virFSItemFree should be used to free the resources after the
 * fs item object is no longer needed.
 *
 * Returns a fsitem, or NULL if not found / error
 */
virFSItemPtr
virFSItemLookupByName(virFSPoolPtr fspool,
                      const char *name)
{
    VIR_DEBUG("fspool=%p, name=%s", fspool, NULLSTR(name));

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);
    virCheckNonNullArgGoto(name, error);

    if (fspool->conn->fsDriver && fspool->conn->fsDriver->fsItemLookupByName) {
        virFSItemPtr ret;
        ret = fspool->conn->fsDriver->fsItemLookupByName(fspool, name);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return NULL;
}


/**
 * virFSItemLookupByKey:
 * @conn: pointer to hypervisor connection
 * @key: globally unique key
 *
 * Fetch a pointer to a fspool item based on its
 * globally unique key
 *
 * virFSItemFree should be used to free the resources after the
 * fs item object is no longer needed.
 *
 * Returns a fs item, or NULL if not found / error
 */
virFSItemPtr
virFSItemLookupByKey(virConnectPtr conn,
                     const char *key)
{
    VIR_DEBUG("conn=%p, key=%s", conn, NULLSTR(key));

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(key, error);

    if (conn->fsDriver && conn->fsDriver->fsItemLookupByKey) {
        virFSItemPtr ret;
        ret = conn->fsDriver->fsItemLookupByKey(conn, key);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSItemLookupByPath:
 * @conn: pointer to hypervisor connection
 * @path: locally unique path
 *
 * Fetch a pointer to a fs item based on its
 * locally (host) unique path
 *
 * virFSItemFree should be used to free the resources after the
 * fs item object is no longer needed.
 *
 * Returns a fs item, or NULL if not found / error
 */
virFSItemPtr
virFSItemLookupByPath(virConnectPtr conn,
                      const char *path)
{
    VIR_DEBUG("conn=%p, path=%s", conn, NULLSTR(path));

    virResetLastError();

    virCheckConnectReturn(conn, NULL);
    virCheckNonNullArgGoto(path, error);

    if (conn->fsDriver && conn->fsDriver->fsItemLookupByPath) {
        virFSItemPtr ret;
        ret = conn->fsDriver->fsItemLookupByPath(conn, path);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(conn);
    return NULL;
}


/**
 * virFSItemGetName:
 * @item: pointer to fsitem
 *
 * Fetch the fsitem name. This is unique
 * within the scope of a fspool
 *
 * Returns the item name, or NULL on error
 */
const char*
virFSItemGetName(virFSItemPtr item)
{
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);

    return item->name;
}


/**
 * virFSItemGetKey:
 * @item: pointer to fspool item
 *
 * Fetch the fsitem key. This is globally
 * unique, so the same item will have the same
 * key no matter what host it is accessed from
 *
 * Returns the item key, or NULL on error
 */
const char*
virFSItemGetKey(virFSItemPtr item)
{
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);

    return item->key;
}


/**
 * virFSItemCreateXML:
 * @fspool: pointer to fspool
 * @xmlDesc: description of item to create
 * @flags: bitwise-OR of virFSItemCreateFlags
 *
 * Create a fs item within a fspool based
 * on an XML description.
 *
 * virFSItemFree should be used to free the resources after the
 * fs item object is no longer needed.
 *
 * Returns the fs item, or NULL on error
 */
virFSItemPtr
virFSItemCreateXML(virFSPoolPtr fspool,
                   const char *xmlDesc,
                   unsigned int flags)
{
    VIR_DEBUG("fspool=%p, xmlDesc=%s, flags=%x", fspool, NULLSTR(xmlDesc), flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);
    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(fspool->conn->flags, error);

    if (fspool->conn->fsDriver && fspool->conn->fsDriver->fsItemCreateXML) {
        virFSItemPtr ret;
        ret = fspool->conn->fsDriver->fsItemCreateXML(fspool, xmlDesc, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return NULL;
}


/**
 * virFSItemCreateXMLFrom:
 * @fspool: pointer to parent fspool for the new item
 * @xmlDesc: description of item to create
 * @cloneitem: fspool item to use as input
 * @flags: bitwise-OR of virFSItemCreateFlags
 *
 * Create a fs item in the parent fspool, using the
 * 'cloneitem' item as input. Information for the new
 * item (name, perms)  are passed via a typical item
 * XML description.
 *
 * virFSItemFree should be used to free the resources after the
 * fs item object is no longer needed.
 *
 * Returns the fs item, or NULL on error
 */
virFSItemPtr
virFSItemCreateXMLFrom(virFSPoolPtr fspool,
                       const char *xmlDesc,
                       virFSItemPtr cloneitem,
                       unsigned int flags)
{
    VIR_DEBUG("fspool=%p, xmlDesc=%s, cloneitem=%p, flags=%x",
              fspool, NULLSTR(xmlDesc), cloneitem, flags);

    virResetLastError();

    virCheckFSPoolReturn(fspool, NULL);
    virCheckFSItemGoto(cloneitem, error);
    virCheckNonNullArgGoto(xmlDesc, error);
    virCheckReadOnlyGoto(fspool->conn->flags | cloneitem->conn->flags, error);

    if (fspool->conn->fsDriver &&
        fspool->conn->fsDriver->fsItemCreateXMLFrom) {
        virFSItemPtr ret;
        ret = fspool->conn->fsDriver->fsItemCreateXMLFrom(fspool, xmlDesc,
                                                          cloneitem, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(fspool->conn);
    return NULL;
}

/**
 * virFSItemDelete:
 * @item: pointer to fspool item
 * @flags: bitwise-OR of virFSItemDeleteFlags
 *
 * Delete the fs item from the fspool
 *
 * Returns 0 on success, or -1 on error
 */
int
virFSItemDelete(virFSItemPtr item,
                unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("item=%p, flags=%x", item, flags);

    virResetLastError();

    virCheckFSItemReturn(item, -1);
    conn = item->conn;

    virCheckReadOnlyGoto(conn->flags, error);

    if (conn->fsDriver && conn->fsDriver->fsItemDelete) {
        int ret;
        ret = conn->fsDriver->fsItemDelete(item, flags);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(item->conn);
    return -1;
}

/**
 * virFSItemFree:
 * @item: pointer to fspool item
 *
 * Release the fs item handle. The underlying
 * fs item continues to exist.
 *
 * Returns 0 on success, or -1 on error
 */
int
virFSItemFree(virFSItemPtr item)
{
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, -1);

    virObjectUnref(item);
    return 0;
}


/**
 * virFSItemRef:
 * @item: the item to hold a reference on
 *
 * Increment the reference count on the item. For each
 * additional call to this method, there shall be a corresponding
 * call to virFSItemFree to release the reference count, once
 * the caller no longer needs the reference to this object.
 *
 * This method is typically useful for applications where multiple
 * threads are using a connection, and it is required that the
 * connection remain open until all threads have finished using
 * it. ie, each new thread using a item would increment
 * the reference count.
 *
 * Returns 0 in case of success, -1 in case of failure.
 */
int
virFSItemRef(virFSItemPtr item)
{
    VIR_DEBUG("item=%p refs=%d", item, item ? item->object.u.s.refs : 0);

    virResetLastError();

    virCheckFSItemReturn(item, -1);

    virObjectRef(item);
    return 0;
}


/**
 * virFSItemGetInfo:
 * @item: pointer to fspool item
 * @info: pointer at which to store info
 *
 * Fetches itematile information about the fspool
 * item such as its current allocation
 *
 * Returns 0 on success, or -1 on failure
 */
int
virFSItemGetInfo(virFSItemPtr item,
                 virFSItemInfoPtr info)
{
    virConnectPtr conn;
    VIR_DEBUG("item=%p, info=%p", item, info);

    virResetLastError();

    if (info)
        memset(info, 0, sizeof(*info));

    virCheckFSItemReturn(item, -1);
    virCheckNonNullArgGoto(info, error);

    conn = item->conn;

    if (conn->fsDriver->fsItemGetInfo) {
        int ret;
        ret = conn->fsDriver->fsItemGetInfo(item, info);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(item->conn);
    return -1;
}


/**
 * virFSItemGetXMLDesc:
 * @item: pointer to fsitem
 * @flags: extra flags; not used yet, so callers should always pass 0
 *
 * Fetch an XML document describing all aspects of
 * the fsitem
 *
 * Returns the XML document, or NULL on error
 */
char *
virFSItemGetXMLDesc(virFSItemPtr item,
                    unsigned int flags)
{
    virConnectPtr conn;
    VIR_DEBUG("item=%p, flags=%x", item, flags);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);
    conn = item->conn;

    if (conn->fsDriver && conn->fsDriver->fsItemGetXMLDesc) {
        char *ret;
        ret = conn->fsDriver->fsItemGetXMLDesc(item, flags);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(item->conn);
    return NULL;
}


/**
 * virFSItemGetPath:
 * @item: pointer to fspool item
 *
 * Fetch the fsitem path. Depending on the fspool
 * configuration this is either persistent across hosts,
 * or dynamically assigned at fspool startup. Consult
 * fspool documentation for information on getting the
 * persistent naming
 *
 * Returns the fs item path, or NULL on error. The
 * caller must free() the returned path after use.
 */
char *
virFSItemGetPath(virFSItemPtr item)
{
    virConnectPtr conn;
    VIR_DEBUG("item=%p", item);

    virResetLastError();

    virCheckFSItemReturn(item, NULL);
    conn = item->conn;

    if (conn->fsDriver && conn->fsDriver->fsItemGetPath) {
        char *ret;
        ret = conn->fsDriver->fsItemGetPath(item);
        if (!ret)
            goto error;
        return ret;
    }

    virReportUnsupportedError();

 error:
    virDispatchError(item->conn);
    return NULL;
}


/**
 * virFSPoolIsActive:
 */
int
virFSPoolIsActive(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    if (fspool->conn->fsDriver->fsPoolIsActive) {
        int ret;
        ret = fspool->conn->fsDriver->fsPoolIsActive(fspool);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(fspool->conn);
    return -1;
}


/**
 * virFSPoolIsPersistent:
 * @fspool: pointer to the fspool object
 *
 * Determine if the fspool has a persistent configuration
 * which means it will still exist after shutting down
 *
 * Returns 1 if persistent, 0 if transient, -1 on error
 */
int
virFSPoolIsPersistent(virFSPoolPtr fspool)
{
    VIR_DEBUG("fspool=%p", fspool);

    virResetLastError();

    virCheckFSPoolReturn(fspool, -1);

    if (fspool->conn->fsDriver->fsPoolIsPersistent) {
        int ret;
        ret = fspool->conn->fsDriver->fsPoolIsPersistent(fspool);
        if (ret < 0)
            goto error;
        return ret;
    }

    virReportUnsupportedError();
 error:
    virDispatchError(fspool->conn);
    return -1;
}
