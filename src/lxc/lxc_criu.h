/*
 * lxc_criu.h: CRIU C API methods wrapper
 *
 * Copyright (c) 2021 Red Hat, Inc.
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

#ifndef LXC_CRIU_H
# define LXC_CRIU_H

# include "virobject.h"

#define LXC_SAVE_MAGIC   "LXCCriuSaveMagic"
#define LXC_SAVE_VERSION 2

typedef struct _virLXCSaveHeader virLXCSaveHeader;
typedef virLXCSaveHeader *virLXCSaveHeaderPtr;
struct _virLXCSaveHeader {
    char magic[sizeof(LXC_SAVE_MAGIC)-1];
    uint32_t version;
    uint32_t xmlLen;
    uint32_t compressed;
    uint32_t unused[9];
};

int lxcCriuCompress(const char *checkpointdir,
                    char *compressionType);

int lxcCriuDecompress(const char *checkpointdir,
                      char *compressionType);

int lxcCriuDump(virDomainObjPtr vm,
                const char *checkpointdir);

int lxcCriuRestore(virDomainDefPtr def,
                   int fd, int ttyfd);
#endif /* LXC_CRIU_H */
