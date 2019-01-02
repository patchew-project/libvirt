/*
 * virbpf.c: methods for eBPF
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
 */
#include <config.h>

#include <sys/syscall.h>

#include "internal.h"

#include "virbpf.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

VIR_LOG_INIT("util.bpf");

#define VIR_FROM_THIS VIR_FROM_BPF

int
virBPFCreateMap(unsigned int mapType,
                unsigned int keySize,
                unsigned int valSize,
                unsigned int maxEntries)
{
    union bpf_attr attr = {
        .map_type = mapType,
        .key_size = keySize,
        .value_size = valSize,
        .max_entries = maxEntries,
    };

    return syscall(SYS_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

#define LOG_BUF_SIZE (256 * 1024)

int
virBPFLoadProg(struct bpf_insn *insns,
               int progType,
               unsigned int insnCnt)
{
    VIR_AUTOFREE(char *) logbuf = NULL;
    int progfd = -1;

    if (VIR_ALLOC_N(logbuf, LOG_BUF_SIZE) < 0)
        return -1;

    union bpf_attr attr = {
        .prog_type = progType,
        .insn_cnt = (__u32)insnCnt,
        .insns = (__u64)insns,
        .license = (__u64)"GPL",
        .log_buf = (__u64)logbuf,
        .log_size = LOG_BUF_SIZE,
        .log_level = 1,
    };

    progfd = syscall(SYS_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));

    if (progfd < 0)
        VIR_DEBUG("%s", logbuf);

    return progfd;
}

int
virBPFAttachProg(int progfd,
                 int targetfd,
                 int attachType)
{
    union bpf_attr attr = {
        .target_fd = targetfd,
        .attach_bpf_fd = progfd,
        .attach_type = attachType,
    };

    return syscall(SYS_bpf, BPF_PROG_ATTACH, &attr, sizeof(attr));
}

int
virBPFDetachProg(int progfd,
                 int targetfd,
                 int attachType)
{
    union bpf_attr attr = {
        .target_fd = targetfd,
        .attach_bpf_fd = progfd,
        .attach_type = attachType,
    };

    return syscall(SYS_bpf, BPF_PROG_DETACH, &attr, sizeof(attr));
}

int
virBPFQueryProg(int targetfd,
                unsigned int maxprogids,
                int attachType,
                unsigned int *progcnt,
                void *progids)
{
    int rc;

    union bpf_attr attr = {
        .query.target_fd = targetfd,
        .query.attach_type = attachType,
        .query.prog_cnt = maxprogids,
        .query.prog_ids = (__u64)progids,
    };

    rc = syscall(SYS_bpf, BPF_PROG_QUERY, &attr, sizeof(attr));

    if (rc >= 0)
        *progcnt = attr.query.prog_cnt;

    return rc;
}

int
virBPFGetProg(unsigned int id)
{
    union bpf_attr attr = {
        .prog_id = id,
    };

    return syscall(SYS_bpf, BPF_PROG_GET_FD_BY_ID, &attr, sizeof(attr));
}

int
virBPFGetProgInfo(int progfd,
                  struct bpf_prog_info *info,
                  unsigned int **mapIDs)
{
    int rc;

    union bpf_attr attr = {
        .info.bpf_fd = progfd,
        .info.info_len = sizeof(struct bpf_prog_info),
        .info.info = (__u64)info,
    };

    rc = syscall(SYS_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
    if (rc < 0)
        return rc;

    if (mapIDs && info->nr_map_ids > 0) {
        unsigned int maplen = info->nr_map_ids;
        VIR_AUTOFREE(unsigned int *) retmapIDs = NULL;

        if (VIR_ALLOC_N(retmapIDs, maplen) < 0)
            return -1;

        memset(info, 0, sizeof(struct bpf_prog_info));
        info->nr_map_ids = maplen;
        info->map_ids = (__u64)retmapIDs;

        memset(&attr, 0, sizeof(attr));
        attr.info.bpf_fd = progfd;
        attr.info.info_len = sizeof(struct bpf_prog_info);
        attr.info.info = (__u64)info;

        rc = syscall(SYS_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
        if (rc < 0)
            return rc;

        VIR_STEAL_PTR(*mapIDs, retmapIDs);
    }

    return rc;
}

int
virBPFGetMap(unsigned int id)
{
    union bpf_attr attr = {
        .map_id = id,
    };

    return syscall(SYS_bpf, BPF_MAP_GET_FD_BY_ID, &attr, sizeof(attr));
}

int
virBPFGetMapInfo(int mapfd,
                 struct bpf_map_info *info)
{
    union bpf_attr attr = {
        .info.bpf_fd = mapfd,
        .info.info_len = sizeof(struct bpf_map_info),
        .info.info = (__u64)info,
    };

    return syscall(SYS_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
}

int
virBPFLookupElem(int mapfd,
                 void *key,
                 void *val)
{
    union bpf_attr attr = {
        .map_fd = mapfd,
        .key = (__u64)key,
        .value = (__u64)val,
    };

    return syscall(SYS_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int
virBPFGetNextElem(int mapfd,
                  void *key,
                  void *nextKey)
{
    union bpf_attr attr = {
        .map_fd = mapfd,
        .key = (__u64)key,
        .next_key = (__u64)nextKey,
    };

    return syscall(SYS_bpf, BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

int
virBPFUpdateElem(int mapfd,
                 void *key,
                 void *val)
{
    union bpf_attr attr = {
        .map_fd = mapfd,
        .key = (__u64)key,
        .value = (__u64)val,
    };

    return syscall(SYS_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int
virBPFDeleteElem(int mapfd,
                 void *key)
{
    union bpf_attr attr = {
        .map_fd = mapfd,
        .key = (__u64)key,
    };

    return syscall(SYS_bpf, BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}
