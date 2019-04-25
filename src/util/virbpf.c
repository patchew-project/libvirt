/*
 * virbpf.c: methods for eBPF
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

#include "viralloc.h"
#include "virbpf.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"

VIR_LOG_INIT("util.bpf");

#define VIR_FROM_THIS VIR_FROM_BPF

#if HAVE_DECL_BPF_PROG_QUERY
int
virBPFCreateMap(unsigned int mapType,
                unsigned int keySize,
                unsigned int valSize,
                unsigned int maxEntries)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_type = mapType;
    attr.key_size = keySize;
    attr.value_size = valSize;
    attr.max_entries = maxEntries;

    return syscall(SYS_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}


# define LOG_BUF_SIZE (256 * 1024)

int
virBPFLoadProg(struct bpf_insn *insns,
               int progType,
               unsigned int insnCnt)
{
    VIR_AUTOFREE(char *) logbuf = NULL;
    int progfd = -1;
    union bpf_attr attr;

    if (VIR_ALLOC_N(logbuf, LOG_BUF_SIZE) < 0)
        return -1;

    memset(&attr, 0, sizeof(attr));

    attr.prog_type = progType;
    attr.insn_cnt = (uint32_t)insnCnt;
    attr.insns = (uint64_t)insns;
    attr.license = (uint64_t)"GPL";
    attr.log_buf = (uint64_t)logbuf;
    attr.log_size = LOG_BUF_SIZE;
    attr.log_level = 1;

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
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.target_fd = targetfd;
    attr.attach_bpf_fd = progfd;
    attr.attach_type = attachType;

    return syscall(SYS_bpf, BPF_PROG_ATTACH, &attr, sizeof(attr));
}


int
virBPFDetachProg(int progfd,
                 int targetfd,
                 int attachType)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.target_fd = targetfd;
    attr.attach_bpf_fd = progfd;
    attr.attach_type = attachType;

    return syscall(SYS_bpf, BPF_PROG_DETACH, &attr, sizeof(attr));
}


int
virBPFQueryProg(int targetfd,
                unsigned int maxprogids,
                int attachType,
                unsigned int *progcnt,
                void *progids)
{
    union bpf_attr attr;
    int rc;

    memset(&attr, 0, sizeof(attr));

    attr.query.target_fd = targetfd;
    attr.query.attach_type = attachType;
    attr.query.prog_cnt = maxprogids;
    attr.query.prog_ids = (uint64_t)progids;

    rc = syscall(SYS_bpf, BPF_PROG_QUERY, &attr, sizeof(attr));

    if (rc >= 0)
        *progcnt = attr.query.prog_cnt;

    return rc;
}


int
virBPFGetProg(unsigned int id)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.prog_id = id;

    return syscall(SYS_bpf, BPF_PROG_GET_FD_BY_ID, &attr, sizeof(attr));
}


int
virBPFGetProgInfo(int progfd,
                  struct bpf_prog_info *info,
                  unsigned int **mapIDs)
{
    union bpf_attr attr;
    int rc;

    memset(&attr, 0, sizeof(attr));

    attr.info.bpf_fd = progfd;
    attr.info.info_len = sizeof(struct bpf_prog_info);
    attr.info.info = (uint64_t)info;

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
        info->map_ids = (uint64_t)retmapIDs;

        memset(&attr, 0, sizeof(attr));
        attr.info.bpf_fd = progfd;
        attr.info.info_len = sizeof(struct bpf_prog_info);
        attr.info.info = (uint64_t)info;

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
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_id = id;

    return syscall(SYS_bpf, BPF_MAP_GET_FD_BY_ID, &attr, sizeof(attr));
}


int
virBPFGetMapInfo(int mapfd,
                 struct bpf_map_info *info)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.info.bpf_fd = mapfd;
    attr.info.info_len = sizeof(struct bpf_map_info);
    attr.info.info = (uint64_t)info;

    return syscall(SYS_bpf, BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
}


int
virBPFLookupElem(int mapfd,
                 void *key,
                 void *val)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_fd = mapfd;
    attr.key = (uint64_t)key;
    attr.value = (uint64_t)val;

    return syscall(SYS_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}


int
virBPFGetNextElem(int mapfd,
                  void *key,
                  void *nextKey)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_fd = mapfd;
    attr.key = (uint64_t)key;
    attr.next_key = (uint64_t)nextKey;

    return syscall(SYS_bpf, BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}


int
virBPFUpdateElem(int mapfd,
                 void *key,
                 void *val)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_fd = mapfd;
    attr.key = (uint64_t)key;
    attr.value = (uint64_t)val;

    return syscall(SYS_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}


int
virBPFDeleteElem(int mapfd,
                 void *key)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));

    attr.map_fd = mapfd;
    attr.key = (uint64_t)key;

    return syscall(SYS_bpf, BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}
#else /* HAVE_DECL_BPF_PROG_QUERY */
int
virBPFCreateMap(unsigned int mapType ATTRIBUTE_UNUSED,
                unsigned int keySize ATTRIBUTE_UNUSED,
                unsigned int valSize ATTRIBUTE_UNUSED,
                unsigned int maxEntries ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFLoadProg(struct bpf_insn *insns ATTRIBUTE_UNUSED,
               int progType ATTRIBUTE_UNUSED,
               unsigned int insnCnt ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFAttachProg(int progfd ATTRIBUTE_UNUSED,
                 int targetfd ATTRIBUTE_UNUSED,
                 int attachType ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFDetachProg(int progfd ATTRIBUTE_UNUSED,
                 int targetfd ATTRIBUTE_UNUSED,
                 int attachType ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFQueryProg(int targetfd ATTRIBUTE_UNUSED,
                unsigned int maxprogids ATTRIBUTE_UNUSED,
                int attachType ATTRIBUTE_UNUSED,
                unsigned int *progcnt ATTRIBUTE_UNUSED,
                void *progids ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFGetProg(unsigned int id ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFGetProgInfo(int progfd ATTRIBUTE_UNUSED,
                  struct bpf_prog_info *info ATTRIBUTE_UNUSED,
                  unsigned int **mapIDs ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFGetMap(unsigned int id ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFGetMapInfo(int mapfd ATTRIBUTE_UNUSED,
                 struct bpf_map_info *info ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFLookupElem(int mapfd ATTRIBUTE_UNUSED,
                 void *key ATTRIBUTE_UNUSED,
                 void *val ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFGetNextElem(int mapfd ATTRIBUTE_UNUSED,
                  void *key ATTRIBUTE_UNUSED,
                  void *nextKey ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFUpdateElem(int mapfd ATTRIBUTE_UNUSED,
                 void *key ATTRIBUTE_UNUSED,
                 void *val ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}


int
virBPFDeleteElem(int mapfd ATTRIBUTE_UNUSED,
                 void *key ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("BPF not supported with this kernel"));
    return -1;
}
#endif /* HAVE_DECL_BPF_PROG_QUERY */
