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
 *
 * Author: Michal Privoznik <mprivozn@redhat.com>
 */

#include <config.h>

#include <stdio.h>
#include <mntent.h>
#include <sys/vfs.h>
#if HAVE_LINUX_MAGIC_H
# include <linux/magic.h>
#endif

#include "virmock.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static FILE *(*real_setmntent)(const char *filename, const char *type);
static int (*real_statfs)(const char *path, struct statfs *buf);


static void
init_syms(void)
{
    if (real_setmntent)
        return;

    VIR_MOCK_REAL_INIT(setmntent);
    VIR_MOCK_REAL_INIT(statfs);
}


FILE *
setmntent(const char *filename, const char *type)
{
    const char *mtab;

    init_syms();

    if ((mtab = getenv("LIBVIRT_MTAB")))
        filename = mtab;

    return real_setmntent(filename, type);
}


#ifndef NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef OCFS2_SUPER_MAGIC
# define OCFS2_SUPER_MAGIC 0x7461636f
#endif
#ifndef GFS2_MAGIC
# define GFS2_MAGIC 0x01161970
#endif
#ifndef AFS_FS_MAGIC
# define AFS_FS_MAGIC 0x6B414653
#endif
#ifndef SMB_SUPER_MAGIC
# define SMB_SUPER_MAGIC 0x517B
#endif
#ifndef CIFS_SUPER_MAGIC
# define CIFS_SUPER_MAGIC 0xFF534D42
#endif
#ifndef HUGETLBFS_MAGIC
# define HUGETLBFS_MAGIC 0x958458f6
#endif
#ifndef FUSE_SUPER_MAGIC
# define FUSE_SUPER_MAGIC 0x65735546
#endif


static int
statfs_mock(const char *mtab,
            const char *path,
            struct statfs *buf)
{
    FILE *f;
    struct mntent mb;
    char mntbuf[1024];
    int ret = -1;

    if (!(f = real_setmntent(mtab, "r"))) {
        fprintf(stderr, "Unable to open %s", mtab);
        abort();
    }

    while (getmntent_r(f, &mb, mntbuf, sizeof(mntbuf))) {
        int ftype;

        if (STRNEQ(mb.mnt_dir, path))
            continue;

        if (STREQ(mb.mnt_type, "nfs") ||
            STREQ(mb.mnt_type, "nfs4")) {
            ftype = NFS_SUPER_MAGIC;
        } else if (STREQ(mb.mnt_type, "gfs2")||
                   STREQ(mb.mnt_type, "gfs2meta")) {
            ftype = GFS2_MAGIC;
        } else if (STREQ(mb.mnt_type, "ocfs2")) {
            ftype = OCFS2_SUPER_MAGIC;
        } else if (STREQ(mb.mnt_type, "afs")) {
            ftype = AFS_FS_MAGIC;
        } else if (STREQ(mb.mnt_type, "smb3")) {
            ftype = SMB_SUPER_MAGIC;
        } else if (STREQ(mb.mnt_type, "cifs")) {
            ftype = CIFS_SUPER_MAGIC;
        } else if (STRPREFIX(mb.mnt_type, "fuse")) {
            ftype = FUSE_SUPER_MAGIC;
        } else {
            /* Everything else is EXT4. We don't care really for other paths. */
            ftype = EXT4_SUPER_MAGIC;
        }

        memset(buf, 0, sizeof(*buf));
        /* We only care about f_type so far. */
        buf->f_type = ftype;
        ret = 0;
        break;
    }

    endmntent(f);
    return ret;
}


int
statfs(const char *path, struct statfs *buf)
{
    const char *mtab;

    init_syms();

    if ((mtab = getenv("LIBVIRT_MTAB")))
        return statfs_mock(mtab, path, buf);

    return real_statfs(path, buf);
}
