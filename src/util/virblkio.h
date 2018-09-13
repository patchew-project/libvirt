/*
 * virblkio.h: Block IO definitions and helpers
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
 * Author: Fabiano Fidêncio <fidencio@redhat.com>
 */

#ifndef __VIR_BLKIO_H__
# define __VIR_BLKIO_H__

# include "virutil.h"

typedef struct _virBlkioDevice virBlkioDevice;
typedef virBlkioDevice *virBlkioDevicePtr;
struct _virBlkioDevice {
    char *path;
    unsigned int weight;
    unsigned int riops;
    unsigned int wiops;
    unsigned long long rbps;
    unsigned long long wbps;
};


typedef struct _virBlkioTune virBlkioTune;
typedef virBlkioTune *virBlkioTunePtr;
struct _virBlkioTune {
    unsigned int weight;

    size_t ndevices;
    virBlkioDevicePtr devices;
};

void virBlkioDeviceArrayClear(virBlkioDevicePtr deviceWeights,
                              int ndevices);

#endif /* __VIR_BLKIO_H__ */
