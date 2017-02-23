/*
 * virsystemdpriv.h: Functions for testing virSystemd APIs
 *
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef __VIR_SYSTEMD_PRIV_H_ALLOW__
# error "virsystemdpriv.h may only be included by virsystemd.c or test suites"
#endif

#ifndef __VIR_SYSTEMD_PRIV_H__
# define __VIR_SYSTEMD_PRIV_H__

# include "virsystemd.h"

void virSystemdCreateMachineResetCachedValue(void);

#endif /* __VIR_SYSTEMD_PRIV_H__ */
