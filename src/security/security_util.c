/*
 * Copyright (C) 2018 Red Hat, Inc.
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

#include "viralloc.h"
#include "virfile.h"
#include "virstring.h"

#include "security_util.h"

#define VIR_FROM_THIS VIR_FROM_SECURITY

/* There are four namespaces available (xattr(7)):
 *
 *  user - can be modified by anybody,
 *  system - used by ACLs
 *  security - used by SELinux
 *  trusted - accessibly by CAP_SYS_ADMIN processes only
 *
 * Looks like the last one is way to go.
 */
#define XATTR_NAMESPACE "trusted"

static char *
virSecurityGetAttrName(const char *name)
{
    char *ret;
    ignore_value(virAsprintf(&ret, XATTR_NAMESPACE".libvirt.security.%s", name));
    return ret;
}


static char *
virSecurityGetRefCountAttrName(const char *name)
{
    char *ret;
    ignore_value(virAsprintf(&ret, XATTR_NAMESPACE".libvirt.security.ref_%s", name));
    return ret;
}


/**
 * virSecurityGetRememberedLabel:
 * @name: security driver name
 * @path: file name
 * @label: label
 *
 * For given @path and security driver (@name) fetch remembered
 * @label. The caller must not restore label if an error is
 * indicated or if @label is NULL upon return.
 *
 * Returns: 0 on success,
 *         -1 otherwise (with error reported)
 */
int
virSecurityGetRememberedLabel(const char *name,
                              const char *path,
                              char **label)
{
    char *ref_name = NULL;
    char *attr_name = NULL;
    char *value = NULL;
    unsigned int refcount = 0;
    int ret = -1;

    *label = NULL;

    if (!(ref_name = virSecurityGetRefCountAttrName(name)))
        goto cleanup;

    if (virFileGetXAtrr(path, ref_name, &value) < 0) {
        if (errno == ENOSYS || errno == ENODATA || errno == ENOTSUP) {
            ret = 0;
        } else {
            virReportSystemError(errno,
                                 _("Unable to get XATTR %s on %s"),
                                 ref_name,
                                 path);
        }
        goto cleanup;
    }

    if (virStrToLong_ui(value, NULL, 10, &refcount) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("malformed refcount %s on %s"),
                       value, path);
        goto cleanup;
    }

    VIR_FREE(value);

    refcount--;

    if (refcount > 0) {
        if (virAsprintf(&value, "%u", refcount) < 0)
            goto cleanup;

        if (virFileSetXAtrr(path, ref_name, value) < 0)
            goto cleanup;
    } else {
        if (virFileRemoveXAttr(path, ref_name) < 0)
            goto cleanup;

        if (!(attr_name = virSecurityGetAttrName(name)))
            goto cleanup;

        if (virFileGetXAtrr(path, attr_name, label) < 0)
            goto cleanup;

        if (virFileRemoveXAttr(path, attr_name) < 0)
            goto cleanup;
    }

    ret = 0;
 cleanup:
    VIR_FREE(value);
    VIR_FREE(attr_name);
    VIR_FREE(ref_name);
    return ret;
}


int
virSecuritySetRememberedLabel(const char *name,
                              const char *path,
                              const char *label)
{
    char *ref_name = NULL;
    char *attr_name = NULL;
    char *value = NULL;
    unsigned int refcount = 0;
    int ret = -1;

    if (!(ref_name = virSecurityGetRefCountAttrName(name)))
        goto cleanup;

    if (virFileGetXAtrr(path, ref_name, &value) < 0) {
        if (errno == ENOSYS || errno == ENOTSUP) {
            ret = 0;
            goto cleanup;
        } else if (errno != ENODATA) {
            virReportSystemError(errno,
                                 _("Unable to get XATTR %s on %s"),
                                 ref_name,
                                 path);
            goto cleanup;
        }
    }

    if (value &&
        virStrToLong_ui(value, NULL, 10, &refcount) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("malformed refcount %s on %s"),
                       value, path);
        goto cleanup;
    }

    VIR_FREE(value);

    refcount++;

    if (refcount == 1) {
        if (!(attr_name = virSecurityGetAttrName(name)))
            goto cleanup;

        if (virFileSetXAtrr(path, attr_name, label) < 0)
            goto cleanup;
    }

    if (virAsprintf(&value, "%u", refcount) < 0)
        goto cleanup;

    if (virFileSetXAtrr(path, ref_name, value) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(value);
    VIR_FREE(attr_name);
    VIR_FREE(ref_name);
    return ret;
}
