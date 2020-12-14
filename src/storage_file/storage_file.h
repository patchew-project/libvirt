/*
 * storage_file.h: file utility functions for FS storage backend
 *
 * Copyright (C) 2007-2009, 2012-2016 Red Hat, Inc.
 * Copyright (C) 2007-2008 Daniel P. Berrange
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

#pragma once

#include "storage_source.h"


/* Minimum header size required to probe all known formats with
 * virStorageFileProbeFormat, or obtain metadata from a known format.
 * Rounded to multiple of 512 (ISO has a 5-byte magic at offset
 * 32769).  Some formats can be probed with fewer bytes.  Although
 * some formats theoretically permit metadata that can rely on offsets
 * beyond this size, in practice that doesn't matter.  */
#define VIR_STORAGE_MAX_HEADER 0x8200

#ifndef DEV_BSIZE
# define DEV_BSIZE 512
#endif

int
virStorageFileProbeFormat(const char *path,
                          uid_t uid,
                          gid_t gid);

virStorageSourcePtr
virStorageFileGetMetadataFromFD(const char *path,
                                int fd,
                                int format);

virStorageSourcePtr
virStorageFileGetMetadataFromBuf(const char *path,
                                 char *buf,
                                 size_t len,
                                 int format)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virStorageFileParseChainIndex(const char *diskTarget,
                              const char *name,
                              unsigned int *chainIndex)
    ATTRIBUTE_NONNULL(3);

int
virStorageFileParseBackingStoreStr(const char *str,
                                   char **target,
                                   unsigned int *chainIndex)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3);

virStorageSourcePtr
virStorageFileChainLookup(virStorageSourcePtr chain,
                          virStorageSourcePtr startFrom,
                          const char *name,
                          unsigned int idx,
                          virStorageSourcePtr *parent)
    ATTRIBUTE_NONNULL(1);

int
virStorageFileUpdatePhysicalSize(virStorageSourcePtr src,
                                 int fd,
                                 struct stat const *sb);
int
virStorageFileUpdateBackingSizes(virStorageSourcePtr src,
                                 int fd,
                                 struct stat const *sb);

int
virStorageFileUpdateCapacity(virStorageSourcePtr src,
                             char *buf,
                             ssize_t len);

int
virStorageFileNewFromBacking(virStorageSourcePtr parent,
                             virStorageSourcePtr *backing);

int
virStorageFileParseRBDColonString(const char *rbdstr,
                                  virStorageSourcePtr src)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

typedef int
(*virStorageFileSimplifyPathReadlinkCallback)(const char *path,
                                              char **link,
                                              void *data);

char *
virStorageFileCanonicalizePath(const char *path,
                               virStorageFileSimplifyPathReadlinkCallback cb,
                               void *cbdata);

int
virStorageFileGetRelativeBackingPath(virStorageSourcePtr from,
                                     virStorageSourcePtr to,
                                     char **relpath)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

int
virStorageFileNewFromBackingAbsolute(const char *path,
                                     virStorageSourcePtr *src);

virStorageSourcePtr
virStorageFileFindByNodeName(virStorageSourcePtr top,
                             const char *nodeName)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virStorageFilePrivateDataParseRelPath(xmlXPathContextPtr ctxt,
                                      virStorageSourcePtr src);

int
virStorageFilePrivateDataFormatRelPath(virStorageSourcePtr src,
                                       virBufferPtr buf);

int
virStorageFileInit(virStorageSourcePtr src);

int
virStorageFileInitAs(virStorageSourcePtr src,
                     uid_t uid,
                     gid_t gid);

void
virStorageFileDeinit(virStorageSourcePtr src);

int
virStorageFileCreate(virStorageSourcePtr src);

int
virStorageFileUnlink(virStorageSourcePtr src);

int
virStorageFileStat(virStorageSourcePtr src,
                   struct stat *stat);

ssize_t
virStorageFileRead(virStorageSourcePtr src,
                   size_t offset,
                   size_t len,
                   char **buf);

const char *
virStorageFileGetUniqueIdentifier(virStorageSourcePtr src);

int
virStorageFileAccess(virStorageSourcePtr src,
                     int mode);

int
virStorageFileChown(const virStorageSource *src,
                    uid_t uid,
                    gid_t gid);

int
virStorageFileSupportsSecurityDriver(const virStorageSource *src);

int
virStorageFileSupportsAccess(const virStorageSource *src);

int
virStorageFileSupportsCreate(const virStorageSource *src);

int
virStorageFileSupportsBackingChainTraversal(const virStorageSource *src);

int
virStorageFileGetMetadata(virStorageSourcePtr src,
                          uid_t uid, gid_t gid,
                          bool report_broken)
    ATTRIBUTE_NONNULL(1);

int
virStorageFileGetBackingStoreStr(virStorageSourcePtr src,
                                 char **backing)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

void
virStorageFileReportBrokenChain(int errcode,
                                virStorageSourcePtr src,
                                virStorageSourcePtr parent);
