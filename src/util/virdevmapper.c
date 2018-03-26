/*
 * virdevmapper.c: Functions for handling devmapper
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
 *     Michal Privoznik <mprivozn@redhat.com>
 */

#include <config.h>

#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#elif MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#ifdef WITH_DEVMAPPER
# include <libdevmapper.h>
#endif

#include "virdevmapper.h"
#include "internal.h"
#include "virthread.h"
#include "viralloc.h"

#ifdef WITH_DEVMAPPER
static void
virDevMapperDummyLogger(int level ATTRIBUTE_UNUSED,
                        const char *file ATTRIBUTE_UNUSED,
                        int line ATTRIBUTE_UNUSED,
                        int dm_errno ATTRIBUTE_UNUSED,
                        const char *fmt ATTRIBUTE_UNUSED,
                        ...)
{
    return;
}

static int
virDevMapperOnceInit(void)
{
    /* Ideally, we would not need this. But libdevmapper prints
     * error messages to stderr by default. Sad but true. */
    dm_log_with_errno_init(virDevMapperDummyLogger);
    return 0;
}


VIR_ONCE_GLOBAL_INIT(virDevMapper)

/**
 * virDevMapperGetTargets:
 * @path: multipath device
 * @maj: returned array of MAJOR device numbers
 * @min: returner array of MINOR device numbers
 * @nmaj: number of items in @maj array
 *
 * For given @path figure out its targets, and store them in @maj
 * and @min arrays. Both arrays have the same number of items
 * upon return.
 *
 * If @path is not a multipath device, @ndevs is set to 0 and
 * success is returned.
 *
 * If we don't have permissions to talk to kernel, -1 is returned
 * and errno is set to EBADF.
 *
 * Returns 0 on success,
 *        -1 otherwise (with errno set, no libvirt error is
 *        reported)
 */
int
virDevMapperGetTargets(const char *path,
                       unsigned int **maj,
                       unsigned int **min,
                       size_t *nmaj)
{
    struct dm_task *dmt = NULL;
    struct dm_deps *deps;
    struct dm_info info;
    size_t i;
    int ret = -1;

    *nmaj = 0;

    if (virDevMapperInitialize() < 0)
        goto cleanup;

    if (!(dmt = dm_task_create(DM_DEVICE_DEPS)))
        goto cleanup;

    if (!dm_task_set_name(dmt, path)) {
        if (errno == ENOENT) {
            /* It's okay, @path is not managed by devmapper =>
             * not a multipath device. */
            ret = 0;
        }
        goto cleanup;
    }

    dm_task_no_open_count(dmt);

    if (!dm_task_run(dmt))
        goto cleanup;

    if (!dm_task_get_info(dmt, &info))
        goto cleanup;

    if (!info.exists) {
        ret = 0;
        goto cleanup;
    }

    if (!(deps = dm_task_get_deps(dmt)))
        goto cleanup;

    if (VIR_ALLOC_N_QUIET(*maj, deps->count) < 0 ||
        VIR_ALLOC_N_QUIET(*min, deps->count) < 0) {
        VIR_FREE(*maj);
        goto cleanup;
    }
    *nmaj = deps->count;

    for (i = 0; i < deps->count; i++) {
        (*maj)[i] = major(deps->device[i]);
        (*min)[i] = minor(deps->device[i]);
    }

    ret = 0;
 cleanup:
    dm_task_destroy(dmt);
    return ret;
}

#else /* ! WITH_DEVMAPPER */

int
virDevMapperGetTargets(const char *path ATTRIBUTE_UNUSED,
                       unsigned int **maj ATTRIBUTE_UNUSED,
                       unsigned int **min ATTRIBUTE_UNUSED,
                       size_t *nmaj ATTRIBUTE_UNUSED)
{
    errno = ENOSYS;
    return -1;
}
#endif /* ! WITH_DEVMAPPER */
