/*
 * libxlmock.c: mocking of xenstore/libxs for libxl
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#if defined(WITH_LIBXL) && defined(WITH_YAJL)
# include "virmock.h"
# include <sys/stat.h>
# include <unistd.h>
# include <libxl.h>
# include <xenstore.h>
# include <xenctrl.h>

# include "virfile.h"
# include "virsocket.h"
# include "libxl/libxl_capabilities.h"

VIR_MOCK_IMPL_RET_VOID(xs_daemon_open,
                       struct xs_handle *)
{
    VIR_MOCK_REAL_INIT(xs_daemon_open);
    return (void*)0x1;
}

VIR_MOCK_IMPL_RET_ARGS(xc_interface_open,
                       xc_interface *,
                       xentoollog_logger *, logger,
                       xentoollog_logger *, dombuild_logger,
                       unsigned, open_flags)
{
    VIR_MOCK_REAL_INIT(xc_interface_open);
    return (void*)0x1;
}


VIR_MOCK_IMPL_RET_ARGS(libxl_get_version_info,
                       const libxl_version_info*,
                       libxl_ctx *, ctx)
{
    static libxl_version_info info;

    memset(&info, 0, sizeof(info));

    /* silence gcc warning about unused function */
    if (0)
        real_libxl_get_version_info(ctx);
    return &info;
}

VIR_MOCK_STUB_RET_ARGS(libxl_get_free_memory,
                       int, 0,
                       libxl_ctx *, ctx,
                       uint32_t *, memkb);

VIR_MOCK_STUB_RET_ARGS(xc_interface_close,
                       int, 0,
                       xc_interface *, handle)

VIR_MOCK_STUB_RET_ARGS(xc_physinfo,
                       int, 0,
                       xc_interface *, handle,
                       xc_physinfo_t *, put_info)

VIR_MOCK_STUB_RET_ARGS(xc_sharing_freed_pages,
                       long, 0,
                       xc_interface *, handle)

VIR_MOCK_STUB_RET_ARGS(xc_sharing_used_frames,
                       long, 0,
                       xc_interface *, handle)

VIR_MOCK_STUB_VOID_ARGS(xs_daemon_close,
                        struct xs_handle *, handle)

VIR_MOCK_STUB_RET_ARGS(bind,
                       int, 0,
                       int, sockfd,
                       const struct sockaddr *, addr,
                       socklen_t, addrlen)

VIR_MOCK_IMPL_RET_ARGS(__xstat, int,
                       int, ver,
                       const char *, path,
                       struct stat *, sb)
{
    VIR_MOCK_REAL_INIT(__xstat);

    if (strstr(path, "xenstored.pid")) {
        memset(sb, 0, sizeof(*sb));
        return 0;
    }

    return real___xstat(ver, path, sb);
}

VIR_MOCK_IMPL_RET_ARGS(stat, int,
                       const char *, path,
                       struct stat *, sb)
{
    VIR_MOCK_REAL_INIT(stat);

    if (strstr(path, "xenstored.pid")) {
        memset(sb, 0, sizeof(*sb));
        return 0;
    }

    return real_stat(path, sb);
}

int
libxlDomainGetEmulatorType(const virDomainDef *def G_GNUC_UNUSED)
{
    return LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN;
}

#endif /* WITH_LIBXL && WITH_YAJL */
