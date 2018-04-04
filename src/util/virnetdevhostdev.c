/*
 * virnetdevhostdev.c: utilities to get/verify Switchdev VF Representor
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

#include "virhostdev.h"
#include "virnetdev.h"
#include "virnetdevhostdev.h"
#include "viralloc.h"
#include "virstring.h"
#include "virfile.h"
#include "virerror.h"
#include "virlog.h"
#include "c-ctype.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.netdevhostdev");

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define IFSWITCHIDSIZ 20

#ifdef __linux__
/**
 * virNetdevHostdevNetSysfsPath
 *
 * @pf_name: netdev name of the physical function (PF)
 * @vf: virtual function (VF) number for the device of interest
 * @vf_ifname: name of the VF representor interface
 *
 * Finds the VF representor name of VF# @vf of SRIOV PF @pfname,
 * and puts it in @vf_ifname. The caller must free @vf_ifname
 * when it's finished with it
 *
 * Returns 0 on success, -1 on failure
 */
static int
virNetdevHostdevNetSysfsPath(char *pf_name,
                             int vf,
                             char **vf_ifname)
{
    size_t i;
    char *pf_switch_id = NULL;
    char *pf_switch_id_file = NULL;
    char *pf_subsystem_device_file = NULL;
    char *pf_subsystem_device_switch_id = NULL;
    char *pf_subsystem_device_port_name_file = NULL;
    char *pf_subsystem_dir = NULL;
    char *vf_rep_ifname = NULL;
    char *vf_num_str = NULL;
    DIR *dirp = NULL;
    struct dirent *dp;
    int ret = -1;

    if (virAsprintf(&pf_switch_id_file, SYSFS_NET_DIR "%s/phys_switch_id",
                    pf_name) < 0)
        goto cleanup;

    if (virAsprintf(&pf_subsystem_dir, SYSFS_NET_DIR "%s/subsystem",
                    pf_name) < 0)
        goto cleanup;

    /* a failure to read just means the driver doesn't support
     * phys_switch_id, so ignoring the error from
     * virFileReadAllQuiet().
     */
    if (virFileReadAllQuiet(pf_switch_id_file, IFSWITCHIDSIZ,
                            &pf_switch_id) <= 0) {
        goto cleanup;
    }

    if (virDirOpen(&dirp, pf_subsystem_dir) < 0)
        goto cleanup;

    /* Iterate over the PFs subsystem devices to find entry with matching
     * switch_id with that of PF.
     */
    while (virDirRead(dirp, &dp, pf_subsystem_dir) > 0) {
        if (STREQ(dp->d_name, pf_name))
            continue;

        VIR_FREE(pf_subsystem_device_file);
        if (virAsprintf(&pf_subsystem_device_file, "%s/%s/phys_switch_id",
                        pf_subsystem_dir, dp->d_name) < 0)
            goto cleanup;

    /* a failure to read just means the driver doesn't support the entry
     * being probed for current device in subsystem dir, so ignoring the
     * error in the following calls to virFileReadAllQuiet() and continue
     * the loop to find device which supports this and is a  match.
     */
        VIR_FREE(pf_subsystem_device_switch_id);
        if (virFileReadAllQuiet(pf_subsystem_device_file, IFSWITCHIDSIZ,
                                &pf_subsystem_device_switch_id) > 0) {
            if (STRNEQ(pf_switch_id, pf_subsystem_device_switch_id))
                continue;
        }

        if (virAsprintf(&pf_subsystem_device_port_name_file,
                        "%s/%s/phys_port_name", pf_subsystem_dir,
                        dp->d_name) < 0)
            goto cleanup;

        VIR_FREE(vf_rep_ifname);
        vf_rep_ifname = NULL;

        if (virFileReadAllQuiet
            (pf_subsystem_device_port_name_file, IFNAMSIZ,
             &vf_rep_ifname) <= 0)
            continue;

        if (virAsprintf(&vf_num_str, "%d", vf) < 0)
            goto cleanup;

        /* phys_port_name may contain just VF number or string in format
         * as pf'X'vf'Y' or vf'Y', where X and Y are PF and VF numbers.
         * As at this point, we are already with correct PF, just need
         * to verify VF number now.
         */

        /* vf_rep_ifname read from file may contain new line,replace with '\0'
           for string comparison below */
        i = strlen(vf_rep_ifname);
        if (c_isspace(vf_rep_ifname[i-1])) {
            vf_rep_ifname[i-1] = '\0';
            i -= 1;
        }

        while (c_isdigit(vf_rep_ifname[i-1]))
              i -= 1;

        if ((ret = STREQ((vf_rep_ifname + i), vf_num_str))) {
            if (VIR_STRDUP(*vf_ifname, dp->d_name) < 0)
                goto cleanup;
            ret = 0;
            break;
        }
    }

 cleanup:
    VIR_DIR_CLOSE(dirp);
    VIR_FREE(pf_switch_id);
    VIR_FREE(pf_switch_id_file);
    VIR_FREE(pf_subsystem_dir);
    VIR_FREE(pf_subsystem_device_file);
    VIR_FREE(pf_subsystem_device_switch_id);
    VIR_FREE(pf_subsystem_device_port_name_file);
    VIR_FREE(vf_num_str);
    VIR_FREE(vf_rep_ifname);
    return ret;
}


/**
 * virNetdevHostdevGetVFRepIFName
 *
 * @hostdev: host device to check
 *
 * Returns VF string with VF representor name upon success else NULL
 */
char *
virNetdevHostdevGetVFRIfName(virDomainHostdevDefPtr hostdev)
{
    char *linkdev = NULL;
    char *ifname = NULL;
    char *vf_ifname = NULL;
    int vf = -1;

    if (virHostdevNetDevice(hostdev, -1, &linkdev, &vf) < 0)
        goto cleanup;

    if (virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_ifname))
        goto cleanup;

    ignore_value(VIR_STRDUP(ifname, vf_ifname));

 cleanup:
    VIR_FREE(linkdev);
    VIR_FREE(vf_ifname);
    return ifname;
}


/**
 * virNetdevHostdevCheckVFRepIFName
 *
 * @hostdev: host device to check
 * @ifname : VF representor name to verify
 *
 * Returns true on success, false on failure
 */
bool
virNetdevHostdevCheckVFRIfName(virDomainHostdevDefPtr hostdev,
                               const char *ifname)
{
    char *linkdev = NULL;
    char *vf_ifname = NULL;
    int vf = -1;
    bool ret = false;

    if (virHostdevNetDevice(hostdev, -1, &linkdev, &vf) < 0)
        goto cleanup;

    if (virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_ifname))
        goto cleanup;

    if (STREQ(ifname, vf_ifname))
        ret = true;

 cleanup:
    VIR_FREE(linkdev);
    VIR_FREE(vf_ifname);
    return ret;
}


/*-------------------- interface stats --------------------*/
/* Copy of virNetDevTapInterfaceStats for linux */
/**
 * virNetdevHostdevVFRepInterfaceStats:
 * @ifname: interface
 * @stats: where to store statistics
 * @swapped: whether to swap RX/TX fields
 *
 * Fetch RX/TX statistics for given named interface (@ifname) and
 * store them at @stats. The returned statistics are always from
 * domain POV. Because in some cases this means swapping RX/TX in
 * the stats and in others this means no swapping (consider TAP
 * vs macvtap) caller might choose if the returned stats should
 * be @swapped or not.
 *
 * Returns 0 on success, -1 otherwise (with error reported).
 */
int
virNetdevHostdevVFRIfStats(const char *ifname,
                           virDomainInterfaceStatsPtr stats,
                           bool swapped)
{
    int ifname_len;
    FILE *fp;
    char line[256], *colon;

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        virReportSystemError(errno, "%s",
                             _("Could not open /proc/net/dev"));
        return -1;
    }

    ifname_len = strlen(ifname);

    while (fgets(line, sizeof(line), fp)) {
        long long dummy;
        long long rx_bytes;
        long long rx_packets;
        long long rx_errs;
        long long rx_drop;
        long long tx_bytes;
        long long tx_packets;
        long long tx_errs;
        long long tx_drop;

        /* The line looks like:
         *   "   eth0:..."
         * Split it at the colon.
         */
        colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        if (colon-ifname_len >= line &&
            STREQ(colon-ifname_len, ifname)) {
            if (sscanf(colon+1,
                       "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
                       &rx_bytes, &rx_packets, &rx_errs, &rx_drop,
                       &dummy, &dummy, &dummy, &dummy,
                       &tx_bytes, &tx_packets, &tx_errs, &tx_drop,
                       &dummy, &dummy, &dummy, &dummy) != 16)
                continue;

            if (swapped) {
                stats->rx_bytes = tx_bytes;
                stats->rx_packets = tx_packets;
                stats->rx_errs = tx_errs;
                stats->rx_drop = tx_drop;
                stats->tx_bytes = rx_bytes;
                stats->tx_packets = rx_packets;
                stats->tx_errs = rx_errs;
                stats->tx_drop = rx_drop;
            } else {
                stats->rx_bytes = rx_bytes;
                stats->rx_packets = rx_packets;
                stats->rx_errs = rx_errs;
                stats->rx_drop = rx_drop;
                stats->tx_bytes = tx_bytes;
                stats->tx_packets = tx_packets;
                stats->tx_errs = tx_errs;
                stats->tx_drop = tx_drop;
            }

            VIR_FORCE_FCLOSE(fp);
            return 0;
        }
    }
    VIR_FORCE_FCLOSE(fp);

    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("/proc/net/dev: Interface not found"));
    return -1;
}
#else
int
virNetdevHostdevVFRIfStats(const char *ifname ATTRIBUTE_UNUSED,
                           virDomainInterfaceStatsPtr stats ATTRIBUTE_UNUSED,
                           bool swapped ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                   _("interface stats not implemented on this platform"));
    return -1;
}


static const char *unsupported = N_("not supported on non-linux platforms");


static int
virNetdevHostdevNetSysfsPath(char *pf_name ATTRIBUTE_UNUSED,
                             int vf ATTRIBUTE_UNUSED,
                             char **vf_ifname ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return -1;
}


char *
virNetdevHostdevGetVFRIfName(virDomainHostdevDefPtr hostdev ATTRIBUTE_UNUSED,
                             char **ifname ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return NULL;
}


static bool
virNetdevHostdevCheckVFRIfName(virDomainHostdevDefPtr hostdev ATTRIBUTE_UNUSED,
                               const char *ifname ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return false;
}
#endif
