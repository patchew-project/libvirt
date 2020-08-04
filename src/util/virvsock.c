/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#if HAVE_DECL_VHOST_VSOCK_SET_GUEST_CID
# include <linux/vhost.h>
#endif

#include "virvsock.h"

#include "virerror.h"
#include "virlog.h"


#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.vsock");

#if HAVE_DECL_VHOST_VSOCK_SET_GUEST_CID
static int
virVsockSetGuestCidQuiet(int fd,
                         unsigned int guest_cid)
{
    uint64_t val = guest_cid;

    return ioctl(fd, VHOST_VSOCK_SET_GUEST_CID, &val);
}

#else
static int
virVsockSetGuestCidQuiet(int fd G_GNUC_UNUSED,
                         unsigned int guest_cid G_GNUC_UNUSED)
{
    errno = ENOSYS;
    return -1;
}
#endif


/**
 * virVsockSetGuestCid:
 * @fd: file descriptor of a vsock interface
 * @guest_cid: guest CID to be set
 *
 * Wrapper for VHOST_VSOCK_SET_GUEST_CID ioctl.
 * Returns: 0 on success, -1 on error.
 */
int
virVsockSetGuestCid(int fd,
                    unsigned int guest_cid)
{
    if (virVsockSetGuestCidQuiet(fd, guest_cid) < 0) {
        virReportSystemError(errno, "%s",
                             _("failed to set guest cid"));
        return -1;
    }

    return 0;
}

#define VIR_VSOCK_GUEST_CID_MIN 3

/**
 * virVsockAcquireGuestCid:
 * @fd: file descriptor of a vsock interface
 * @guest_cid: where to store the guest CID
 *
 * Iterates over usable CIDs until a free one is found.
 * Returns: 0 on success, with the acquired CID stored in guest_cid
 *         -1 on error.
 */
int
virVsockAcquireGuestCid(int fd,
                        unsigned int *guest_cid)
{
    unsigned int cid = VIR_VSOCK_GUEST_CID_MIN;

    for (; virVsockSetGuestCidQuiet(fd, cid) < 0; cid++) {
        if (errno != EADDRINUSE) {
            virReportSystemError(errno, "%s",
                                 _("failed to acquire guest cid"));
            return -1;
        }
    }
    *guest_cid = cid;

    return 0;
}
