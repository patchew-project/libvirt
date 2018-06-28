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
# define IFNAMSIZ 16
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
 * Returns 1 on success, 0 for no switchdev support for @pfname device
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
    int vf_rep_num;
    const char *vf_num_str;
    DIR *dirp = NULL;
    struct dirent *dp;
    int ret = 0;

    if (virAsprintf(&pf_switch_id_file, SYSFS_NET_DIR "%s/phys_switch_id",
                    pf_name) < 0)
        goto cleanup;

    if (!virFileExists(pf_switch_id_file))
        goto cleanup;

    /* If file exists, failure to read or if it's an empty file just means
     * that driver doesn't support phys_switch_id, therefore ignoring the error
     * from virFileReadAllQuiet().
     */
    if (virFileReadAllQuiet(pf_switch_id_file, IFSWITCHIDSIZ,
                            &pf_switch_id) <= 0)
        goto cleanup;

    if (virAsprintf(&pf_subsystem_dir, SYSFS_NET_DIR "%s/subsystem",
                    pf_name) < 0)
        goto cleanup;

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

        if (!virFileExists(pf_subsystem_device_file))
            goto cleanup;

        /* If file exists, failure to read or if it's an empty file just means
         * the driver doesn't support the entry being probed for current
         * device in subsystem dir, therefore ignoring the error in the
         * following calls to virFileReadAllQuiet() and continue the loop
         * to find device which supports this and is a match.
         */
        VIR_FREE(pf_subsystem_device_switch_id);
        if (virFileReadAllQuiet(pf_subsystem_device_file, IFSWITCHIDSIZ,
                                &pf_subsystem_device_switch_id) > 0) {
            if (STRNEQ(pf_switch_id, pf_subsystem_device_switch_id))
                continue;
        }
        else
            continue;

        VIR_FREE(pf_subsystem_device_port_name_file);
        if (virAsprintf(&pf_subsystem_device_port_name_file,
                        "%s/%s/phys_port_name", pf_subsystem_dir,
                        dp->d_name) < 0)
            goto cleanup;

        if (!virFileExists(pf_subsystem_device_port_name_file))
            goto cleanup;

        VIR_FREE(vf_rep_ifname);
        if (virFileReadAllQuiet
            (pf_subsystem_device_port_name_file, IFNAMSIZ,
             &vf_rep_ifname) <= 0)
            continue;

        /* phys_port_name may contain just VF number or string in format
         * as pf'X'vf'Y' or vf'Y', where X and Y are PF and VF numbers.
         * As at this point, we are already with correct PF, just need
         * to verify VF number which is always at the end.
         */

        /* vf_rep_ifname read from file may contain new line,replace with '\0'
           for string comparison below */
        i = strlen(vf_rep_ifname);
        if (c_isspace(vf_rep_ifname[i-1])) {
            vf_rep_ifname[i-1] = '\0';
            i -= 1;
        }

        /* Locating the start of numeric portion of VF in the string */
        while (c_isdigit(vf_rep_ifname[i-1]))
              i -= 1;

        vf_num_str =  vf_rep_ifname + i;
        vf_rep_num = virParseNumber(&vf_num_str);

        if (vf_rep_num == vf) {
            if (VIR_STRDUP(*vf_ifname, dp->d_name) < 0)
                goto cleanup;
            ret = 1;
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

    if (!virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_ifname)) {
        virResetLastError();
        goto cleanup;
    }

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

    if (!virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_ifname)) {
        virResetLastError();
        goto cleanup;
    }

    if (STREQ(ifname, vf_ifname))
        ret = true;

 cleanup:
    VIR_FREE(linkdev);
    VIR_FREE(vf_ifname);
    return ret;
}


/**
 * virNetdevHostdevVFRepInterfaceStats:
 * @ifname: interface
 * @stats: where to store statistics
 * @swapped: whether to swap RX/TX fields
 *
 * Returns 0 on success, -1 otherwise (with error reported).
 */
int
virNetdevHostdevVFRIfStats(const char *ifname,
                           virDomainInterfaceStatsPtr stats,
                           bool swapped)
{
    return virNetDevGetProcNetdevStats(ifname, stats, swapped);
}
#else
static const char *unsupported = N_("not supported on non-linux platforms");


char *
virNetdevHostdevGetVFRIfName(virDomainHostdevDefPtr hostdev ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return NULL;
}


bool
virNetdevHostdevCheckVFRIfName(virDomainHostdevDefPtr hostdev ATTRIBUTE_UNUSED,
                               const char *ifname ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return false;
}


int
virNetdevHostdevVFRIfStats(const char *ifname ATTRIBUTE_UNUSED,
                           virDomainInterfaceStatsPtr stats ATTRIBUTE_UNUSED,
                           bool swapped ATTRIBUTE_UNUSED)
{
    virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                   _("interface stats not implemented on this platform"));
    return -1;
}
#endif
