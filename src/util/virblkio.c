/*
 * virblkio.c: Block IO helpers
 *
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
 *
 * Authors:
 *  Fabiano Fidêncio <fidencio@redhat.com>
 */

#include <config.h>

#include "viralloc.h"
#include "virblkio.h"

void
virBlkioDeviceArrayClear(virBlkioDevicePtr devices,
                         int ndevices)
{
    size_t i;

    for (i = 0; i < ndevices; i++)
        VIR_FREE(devices[i].path);
}
