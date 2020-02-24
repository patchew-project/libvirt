/*
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2012 Fujitsu Limited.
 *
 * lxc_fuse.c: fuse filesystem support for libvirt lxc
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
#include <fcntl.h>
#include <sys/mount.h>
#include <mntent.h>

#include "lxc_fuse.h"
#include "lxc_cgroup.h"
#include "virerror.h"
#include "virfile.h"
#include "virbuffer.h"
#include "virstring.h"
#include "viralloc.h"

#define VIR_FROM_THIS VIR_FROM_LXC

#if WITH_FUSE

static const char *fuse_meminfo_path = "/meminfo";
static const char *fuse_cpuinfo_path = "/cpuinfo";

static int lxcProcGetattr(const char *path, struct stat *stbuf)
{
    g_autofree char *procpath = NULL;
    struct stat sb;
    struct fuse_context *context = fuse_get_context();
    virDomainDefPtr def = (virDomainDefPtr)context->private_data;

    memset(stbuf, 0, sizeof(struct stat));
    procpath = g_strdup_printf("/proc/%s", path);

    if (STREQ(path, "/")) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (STREQ(path, fuse_meminfo_path)) {
        if (stat(mempath, &sb) < 0)
            return -errno;

        stbuf->st_uid = def->idmap.uidmap ? def->idmap.uidmap[0].target : 0;
        stbuf->st_gid = def->idmap.gidmap ? def->idmap.gidmap[0].target : 0;
        stbuf->st_mode = sb.st_mode;
        stbuf->st_nlink = 1;
        stbuf->st_blksize = sb.st_blksize;
        stbuf->st_blocks = sb.st_blocks;
        stbuf->st_size = sb.st_size;
        stbuf->st_atime = sb.st_atime;
        stbuf->st_ctime = sb.st_ctime;
        stbuf->st_mtime = sb.st_mtime;
    } else {
        return -ENOENT;
    }

    return 0;
}

static int lxcProcReaddir(const char *path, void *buf,
                          fuse_fill_dir_t filler,
                          off_t offset G_GNUC_UNUSED,
                          struct fuse_file_info *fi G_GNUC_UNUSED)
{
    if (STRNEQ(path, "/"))
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, fuse_meminfo_path + 1, NULL, 0);
    filler(buf, fuse_cpuinfo_path + 1, NULL, 0);

    return 0;
}

static int lxcProcOpen(const char *path G_GNUC_UNUSED,
                       struct fuse_file_info *fi G_GNUC_UNUSED)
{
    if (STRNEQ(path, fuse_meminfo_path) &&
        STRNEQ(path, fuse_cpuinfo_path))
        return -ENOENT;

    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    return 0;
}

static int lxcProcHostRead(char *path, char *buf, size_t size, off_t offset)
{
    int fd;
    int res;

    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    if ((res = pread(fd, buf, size, offset)) < 0)
        res = -errno;

    VIR_FORCE_CLOSE(fd);
    return res;
}

static int lxcProcReadMeminfo(char *hostpath, virDomainDefPtr def,
                              char *buf, size_t size, off_t offset)
{
    int res;
    FILE *fd = NULL;
    g_autofree char *line = NULL;
    size_t n;
    struct virLXCMeminfo meminfo;
    virBuffer buffer = VIR_BUFFER_INITIALIZER;
    virBufferPtr new_meminfo = &buffer;

    if (virLXCCgroupGetMeminfo(&meminfo) < 0) {
        virErrorSetErrnoFromLastError();
        return -errno;
    }

    fd = fopen(hostpath, "r");
    if (fd == NULL) {
        virReportSystemError(errno, _("Cannot open %s"), hostpath);
        res = -errno;
        goto cleanup;
    }

    if (fseek(fd, offset, SEEK_SET) < 0) {
        virReportSystemError(errno, "%s", _("fseek failed"));
        res = -errno;
        goto cleanup;
    }

    res = -1;
    while (getline(&line, &n, fd) > 0) {
        char *ptr = strchr(line, ':');
        if (!ptr)
            continue;
        *ptr = '\0';

        if (STREQ(line, "MemTotal") &&
            (virMemoryLimitIsSet(def->mem.hard_limit) ||
             virDomainDefGetMemoryTotal(def))) {
            virBufferAsprintf(new_meminfo, "MemTotal:       %8llu kB\n",
                              meminfo.memtotal);
        } else if (STREQ(line, "MemFree") &&
                   (virMemoryLimitIsSet(def->mem.hard_limit) ||
                    virDomainDefGetMemoryTotal(def))) {
            virBufferAsprintf(new_meminfo, "MemFree:        %8llu kB\n",
                              (meminfo.memtotal - meminfo.memusage));
        } else if (STREQ(line, "MemAvailable") &&
                   (virMemoryLimitIsSet(def->mem.hard_limit) ||
                    virDomainDefGetMemoryTotal(def))) {
            /* MemAvailable is actually MemFree + SRReclaimable +
               some other bits, but MemFree is the closest approximation
               we have */
            virBufferAsprintf(new_meminfo, "MemAvailable:   %8llu kB\n",
                              (meminfo.memtotal - meminfo.memusage));
        } else if (STREQ(line, "Buffers")) {
            virBufferAsprintf(new_meminfo, "Buffers:        %8d kB\n", 0);
        } else if (STREQ(line, "Cached")) {
            virBufferAsprintf(new_meminfo, "Cached:         %8llu kB\n",
                              meminfo.cached);
        } else if (STREQ(line, "Active")) {
            virBufferAsprintf(new_meminfo, "Active:         %8llu kB\n",
                              (meminfo.active_anon + meminfo.active_file));
        } else if (STREQ(line, "Inactive")) {
            virBufferAsprintf(new_meminfo, "Inactive:       %8llu kB\n",
                              (meminfo.inactive_anon + meminfo.inactive_file));
        } else if (STREQ(line, "Active(anon)")) {
            virBufferAsprintf(new_meminfo, "Active(anon):   %8llu kB\n",
                              meminfo.active_anon);
        } else if (STREQ(line, "Inactive(anon)")) {
            virBufferAsprintf(new_meminfo, "Inactive(anon): %8llu kB\n",
                              meminfo.inactive_anon);
        } else if (STREQ(line, "Active(file)")) {
            virBufferAsprintf(new_meminfo, "Active(file):   %8llu kB\n",
                              meminfo.active_file);
        } else if (STREQ(line, "Inactive(file)")) {
            virBufferAsprintf(new_meminfo, "Inactive(file): %8llu kB\n",
                              meminfo.inactive_file);
        } else if (STREQ(line, "Unevictable")) {
            virBufferAsprintf(new_meminfo, "Unevictable:    %8llu kB\n",
                              meminfo.unevictable);
        } else if (STREQ(line, "SwapTotal") &&
                   virMemoryLimitIsSet(def->mem.swap_hard_limit)) {
            virBufferAsprintf(new_meminfo, "SwapTotal:      %8llu kB\n",
                              (meminfo.swaptotal - meminfo.memtotal));
        } else if (STREQ(line, "SwapFree") &&
                   virMemoryLimitIsSet(def->mem.swap_hard_limit)) {
            virBufferAsprintf(new_meminfo, "SwapFree:       %8llu kB\n",
                              (meminfo.swaptotal - meminfo.memtotal -
                               meminfo.swapusage + meminfo.memusage));
        } else if (STREQ(line, "Slab")) {
            virBufferAsprintf(new_meminfo, "Slab:           %8d kB\n", 0);
        } else if (STREQ(line, "SReclaimable")) {
            virBufferAsprintf(new_meminfo, "SReclaimable:   %8d kB\n", 0);
        } else if (STREQ(line, "SUnreclaim")) {
            virBufferAsprintf(new_meminfo, "SUnreclaim:     %8d kB\n", 0);
        } else {
            *ptr = ':';
            virBufferAdd(new_meminfo, line, -1);
        }

    }
    res = strlen(virBufferCurrentContent(new_meminfo));
    if (res > size)
        res = size;
    memcpy(buf, virBufferCurrentContent(new_meminfo), res);

 cleanup:
    virBufferFreeAndReset(new_meminfo);
    VIR_FORCE_FCLOSE(fd);
    return res;
}


static int
lxcProcReadCpuinfoParse(virDomainDefPtr def, char *base,
                        virBufferPtr new_cpuinfo)
{
    char *procline = NULL;
    char *saveptr = base;
    size_t cpu;
    size_t nvcpu;
    size_t curcpu = 0;
    bool get_proc = false;

    nvcpu = virDomainDefGetVcpus(def);
    while ((procline = strtok_r(NULL, "\n", &saveptr))) {
        if (sscanf(procline, "processor\t: %zu", &cpu) == 1) {
            virDomainVcpuDefPtr vcpu = virDomainDefGetVcpu(def, cpu);
            /* VCPU is mapped */
            if (vcpu) {
                if (curcpu == nvcpu)
                    break;

                if (curcpu > 0)
                    virBufferAddLit(new_cpuinfo, "\n");

                virBufferAsprintf(new_cpuinfo, "processor\t: %zu\n",
                                  curcpu);
                curcpu++;
                get_proc = true;
            } else {
                get_proc = false;
            }
        } else {
            /* It is not a processor index */
            if (get_proc)
                virBufferAsprintf(new_cpuinfo, "%s\n", procline);
        }
    }

    virBufferAddLit(new_cpuinfo, "\n");

    return strlen(virBufferCurrentContent(new_cpuinfo));
}


static int lxcProcReadCpuinfo(char *hostpath, virDomainDefPtr def,
                              char *buf, size_t size, off_t offset)
{
    virBuffer buffer = VIR_BUFFER_INITIALIZER;
    virBufferPtr new_cpuinfo = &buffer;
    g_autofree char *outbuf = NULL;
    int res = -1;

    /* Gather info from /proc/cpuinfo */
    if (virFileReadAll(hostpath, 1024*1024, &outbuf) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to open %s"), hostpath);
        return -1;
    }

    /* /proc/cpuinfo does not support fseek */
    if (offset > 0)
        return 0;

    res = lxcProcReadCpuinfoParse(def, outbuf, new_cpuinfo);

    if (res > size)
        res = size;
    memcpy(buf, virBufferCurrentContent(new_cpuinfo), res);

    virBufferFreeAndReset(new_cpuinfo);
    return res;
}


static int lxcProcRead(const char *path G_GNUC_UNUSED,
                       char *buf G_GNUC_UNUSED,
                       size_t size G_GNUC_UNUSED,
                       off_t offset G_GNUC_UNUSED,
                       struct fuse_file_info *fi G_GNUC_UNUSED)
{
    int res = -ENOENT;
    g_autofree char *hostpath = NULL;
    struct fuse_context *context = NULL;
    virDomainDefPtr def = NULL;

    hostpath = g_strdup_printf("/proc/%s", path);

    context = fuse_get_context();
    def = (virDomainDefPtr)context->private_data;

    if (STREQ(path, fuse_meminfo_path)) {
        if ((res = lxcProcReadMeminfo(hostpath, def, buf, size, offset)) < 0)
            res = lxcProcHostRead(hostpath, buf, size, offset);
    } else if (STREQ(path, fuse_cpuinfo_path)) {
        if ((res = lxcProcReadCpuinfo(hostpath, def, buf, size, offset)) < 0)
            res = lxcProcHostRead(hostpath, buf, size, offset);
    }

    return res;
}

static struct fuse_operations lxcProcOper = {
    .getattr = lxcProcGetattr,
    .readdir = lxcProcReaddir,
    .open    = lxcProcOpen,
    .read    = lxcProcRead,
};

static void lxcFuseDestroy(virLXCFusePtr fuse)
{
    virMutexLock(&fuse->lock);
    fuse_unmount(fuse->mountpoint, fuse->ch);
    fuse_destroy(fuse->fuse);
    fuse->fuse = NULL;
    virMutexUnlock(&fuse->lock);
}

static void lxcFuseRun(void *opaque)
{
    virLXCFusePtr fuse = opaque;

    if (fuse_loop(fuse->fuse) < 0)
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("fuse_loop failed"));

    lxcFuseDestroy(fuse);
}

int lxcSetupFuse(virLXCFusePtr *f, virDomainDefPtr def)
{
    int ret = -1;
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    virLXCFusePtr fuse = NULL;

    if (VIR_ALLOC(fuse) < 0)
        goto cleanup;

    fuse->def = def;

    if (virMutexInit(&fuse->lock) < 0)
        goto cleanup2;

    fuse->mountpoint = g_strdup_printf("%s/%s.fuse/", LXC_STATE_DIR, def->name);

    if (virFileMakePath(fuse->mountpoint) < 0) {
        virReportSystemError(errno, _("Cannot create %s"),
                             fuse->mountpoint);
        goto cleanup1;
    }

    /* process name is libvirt_lxc */
    if (fuse_opt_add_arg(&args, "libvirt_lxc") == -1 ||
        fuse_opt_add_arg(&args, "-odirect_io") == -1 ||
        fuse_opt_add_arg(&args, "-oallow_other") == -1 ||
        fuse_opt_add_arg(&args, "-ofsname=libvirt") == -1)
        goto cleanup1;

    fuse->ch = fuse_mount(fuse->mountpoint, &args);
    if (fuse->ch == NULL)
        goto cleanup1;

    fuse->fuse = fuse_new(fuse->ch, &args, &lxcProcOper,
                          sizeof(lxcProcOper), fuse->def);
    if (fuse->fuse == NULL) {
        fuse_unmount(fuse->mountpoint, fuse->ch);
        goto cleanup1;
    }

    ret = 0;
 cleanup:
    fuse_opt_free_args(&args);
    *f = fuse;
    return ret;
 cleanup1:
    VIR_FREE(fuse->mountpoint);
    virMutexDestroy(&fuse->lock);
 cleanup2:
    VIR_FREE(fuse);
    goto cleanup;
}

int lxcStartFuse(virLXCFusePtr fuse)
{
    if (virThreadCreate(&fuse->thread, false, lxcFuseRun,
                        (void *)fuse) < 0) {
        lxcFuseDestroy(fuse);
        return -1;
    }

    return 0;
}

void lxcFreeFuse(virLXCFusePtr *f)
{
    virLXCFusePtr fuse = *f;
    /* lxcFuseRun thread create success */
    if (fuse) {
        /* exit fuse_loop, lxcFuseRun thread may try to destroy
         * fuse->fuse at the same time,so add a lock here. */
        virMutexLock(&fuse->lock);
        if (fuse->fuse)
            fuse_exit(fuse->fuse);
        virMutexUnlock(&fuse->lock);

        VIR_FREE(fuse->mountpoint);
        VIR_FREE(*f);
    }
}
#else
int lxcSetupFuse(virLXCFusePtr *f G_GNUC_UNUSED,
                  virDomainDefPtr def G_GNUC_UNUSED)
{
    return 0;
}

int lxcStartFuse(virLXCFusePtr f G_GNUC_UNUSED)
{
    return 0;
}

void lxcFreeFuse(virLXCFusePtr *f G_GNUC_UNUSED)
{
}
#endif
