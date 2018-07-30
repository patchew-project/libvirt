/*
 * secret_conf.h: internal <secret> XML handling API
 *
 * Copyright (C) 2009-2010, 2013-2014, 2016 Red Hat, Inc.
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
 * Red Hat Author: Miloslav Trmač <mitr@redhat.com>
 */

#ifndef __VIR_SECRET_CONF_H__
# define __VIR_SECRET_CONF_H__

# include "internal.h"
# include "virutil.h"

typedef struct _virSecretDef virSecretDef;
typedef virSecretDef *virSecretDefPtr;
struct _virSecretDef {
    bool isephemeral;
    bool isprivate;
    unsigned char uuid[VIR_UUID_BUFLEN];
    char *description;          /* May be NULL */
    int usage_type;  /* virSecretUsageType */
    char *usage_id; /* May be NULL */
};

void virSecretDefFree(virSecretDefPtr def);

typedef enum {
    /* Perform extra name validation on new secret usage ids which
     * will cause failure to parse the XML. Initially just that a
     * name cannot be all white space. */
    VIR_SECRET_DEF_PARSE_VALIDATE_USAGE_ID = 1 << 0,
} virSecretDefParseFlags;

virSecretDefPtr virSecretDefParseString(const char *xml,
                                        unsigned int flags);

virSecretDefPtr virSecretDefParseFile(const char *filename,
                                      unsigned int flags);

char *virSecretDefFormat(const virSecretDef *def);

# define VIR_CONNECT_LIST_SECRETS_FILTERS_EPHEMERAL \
                (VIR_CONNECT_LIST_SECRETS_EPHEMERAL     | \
                 VIR_CONNECT_LIST_SECRETS_NO_EPHEMERAL)

# define VIR_CONNECT_LIST_SECRETS_FILTERS_PRIVATE \
                (VIR_CONNECT_LIST_SECRETS_PRIVATE     | \
                 VIR_CONNECT_LIST_SECRETS_NO_PRIVATE)

# define VIR_CONNECT_LIST_SECRETS_FILTERS_ALL \
                (VIR_CONNECT_LIST_SECRETS_FILTERS_EPHEMERAL  | \
                 VIR_CONNECT_LIST_SECRETS_FILTERS_PRIVATE)


#endif
