/*
 * viriscsi.h: helper APIs for managing iSCSI
 *
 * Copyright (C) 2014 Red Hat, Inc.
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
 */

#ifndef __VIR_ISCSI_H__
# define __VIR_ISCSI_H__

# include "internal.h"

char *
virISCSIGetSession(const char *devpath,
                   bool probe);

int
virISCSIConnectionLogin(const char *portal,
                        const char *initiatoriqn,
                        const char *target)
    ATTRIBUTE_RETURN_CHECK;

int
virISCSIConnectionLogout(const char *portal,
                         const char *initiatoriqn,
                         const char *target)
    ATTRIBUTE_RETURN_CHECK;

int
virISCSIRescanLUNs(const char *session)
    ATTRIBUTE_RETURN_CHECK;

int
virISCSIScanTargets(const char *portal,
                    size_t *ntargetsret,
                    char ***targetsret)
    ATTRIBUTE_RETURN_CHECK;

int
virISCSINodeNew(const char *portal,
                const char *target)
    ATTRIBUTE_RETURN_CHECK;

int
virISCSINodeUpdate(const char *portal,
                   const char *target,
                   const char *name,
                   const char *value)
    ATTRIBUTE_RETURN_CHECK;
#endif
