/*
<<<<<<< HEAD
 * domain_format.h: XML formatter for the domain definition
=======
 * virconftypes.h: struct typedefs to avoid circular inclusion
>>>>>>> 094a03dc8a... format
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
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

#include "internal.h"
#include "virconftypes.h"
#include "virdomaintypes.h"

typedef enum {
    VIR_DOMAIN_DEF_FORMAT_SECURE          = 1 << 0,
    VIR_DOMAIN_DEF_FORMAT_INACTIVE        = 1 << 1,
    VIR_DOMAIN_DEF_FORMAT_MIGRATABLE      = 1 << 2,
    /* format internal domain status information */
    VIR_DOMAIN_DEF_FORMAT_STATUS          = 1 << 3,
    /* format <actual> element */
    VIR_DOMAIN_DEF_FORMAT_ACTUAL_NET      = 1 << 4,
    /* format original states of host PCI device */
    VIR_DOMAIN_DEF_FORMAT_PCI_ORIG_STATES = 1 << 5,
    VIR_DOMAIN_DEF_FORMAT_ALLOW_ROM       = 1 << 6,
    VIR_DOMAIN_DEF_FORMAT_ALLOW_BOOT      = 1 << 7,
    VIR_DOMAIN_DEF_FORMAT_CLOCK_ADJUST    = 1 << 8,
} virDomainDefFormatFlags;

/* When extending this list, remember that libvirt 1.2.12-5.0.0 had a
 * bug that silently ignored unknown flags.  A new flag to add
 * information is okay as long as clients still work when an older
 * server omits the requested output, but a new flag to suppress
 * information could result in a security hole when older libvirt
 * supplies the sensitive information in spite of the flag. */
#define VIR_DOMAIN_XML_COMMON_FLAGS \
    (VIR_DOMAIN_XML_SECURE | VIR_DOMAIN_XML_INACTIVE | \
     VIR_DOMAIN_XML_MIGRATABLE)
unsigned int virDomainDefFormatConvertXMLFlags(unsigned int flags);

char *virDomainDefFormat(virDomainDefPtr def,
                         virCapsPtr caps,
                         unsigned int flags);
char *virDomainObjFormat(virDomainXMLOptionPtr xmlopt,
                         virDomainObjPtr obj,
                         virCapsPtr caps,
                         unsigned int flags);
int virDomainDefFormatInternal(virDomainDefPtr def,
                               virCapsPtr caps,
                               unsigned int flags,
                               virBufferPtr buf,
                               virDomainXMLOptionPtr xmlopt);

int virDomainDiskSourceFormat(virBufferPtr buf,
                              virStorageSourcePtr src,
                              const char *element,
                              int policy,
                              bool attrIndex,
                              unsigned int flags,
                              virDomainXMLOptionPtr xmlopt);

int
virDomainDiskBackingStoreFormat(virBufferPtr buf,
                                virStorageSourcePtr src,
                                virDomainXMLOptionPtr xmlopt,
                                unsigned int flags);

int virDomainNetDefFormat(virBufferPtr buf,
                          virDomainNetDefPtr def,
                          char *prefix,
                          unsigned int flags);
