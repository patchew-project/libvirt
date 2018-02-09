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
#include "virnetdevhostdev.h"
#include "viralloc.h"
#include "virstring.h"
#include "virfile.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.netdevhostdev");

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define IFSWITCHIDSIZ 20

#ifndef SYSFS_NET_DIR
#define SYSFS_NET_DIR "/sys/class/net/"
#endif

#if 0
static int
virNetdevHostdevPCISysfsPath(virDomainHostdevDefPtr hostdev,
                             char **sysfs_path)
{
    virPCIDeviceAddress config_address;

    config_address.domain = hostdev->source.subsys.u.pci.addr.domain;
    config_address.bus = hostdev->source.subsys.u.pci.addr.bus;
    config_address.slot = hostdev->source.subsys.u.pci.addr.slot;
    config_address.function = hostdev->source.subsys.u.pci.addr.function;

    return virPCIDeviceAddressGetSysfsFile(&config_address, sysfs_path);
}


static int
virNetdevHostdevNetDevice(virDomainHostdevDefPtr hostdev,
                          int pfNetDevIdx,
                          char **linkdev,
                          int *vf)
{
    int ret = -1;
    char *sysfs_path = NULL;

    if (virNetdevHostdevPCISysfsPath(hostdev, &sysfs_path) < 0)
        return ret;

    if (virPCIIsVirtualFunction(sysfs_path) == 1) {
        if (virPCIGetVirtualFunctionInfo(sysfs_path, pfNetDevIdx,
                                         linkdev, vf) < 0)
            goto cleanup;
    } else {
        /* In practice this should never happen, since we currently
         * only support assigning SRIOV VFs via <interface
         * type='hostdev'>, and it is only those devices that should
         * end up calling this function.
         */
        if (virPCIGetNetName(sysfs_path, 0, NULL, linkdev) < 0)
            goto cleanup;

        if (!linkdev) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("The device at %s has no network device name"),
                           sysfs_path);
            goto cleanup;
        }

        *vf = -1;
    }

    ret = 0;

 cleanup:
    VIR_FREE(sysfs_path);

    return ret;
}
#endif


/**
 * virNetdevHostdevNetSysfsPath
 *
 * @pf_name: netdev name of the physical function (PF)
 * @vf: virtual function (VF) number for the device of interest
 * @vf_representor: name of the VF representor interface
 *
 * Finds the VF representor name of VF# @vf of SRIOV PF @pfname, and
 * puts it in @vf_representor. The caller must free @vf_representor
 * when it's finished with it
 *
 * Returns 0 on success, -1 on failure
 */
static int
virNetdevHostdevNetSysfsPath(char *pf_name, int vf, char **vf_representor)
{
    char *pf_switch_id = NULL;
    char *pf_switch_id_file = NULL;
    char *pf_subsystem_device_file = NULL;
    char *pf_subsystem_device_switch_id = NULL;
    char *pf_subsystem_device_port_name_file = NULL;
    char *pf_subsystem_dir = NULL;
    char *vf_representor_name = NULL;
    char *vf_num_str = NULL;
    char *vf_suffix = NULL;
    DIR *dirp = NULL;
    struct dirent *dp;
    int ret = -1;


    if (virAsprintf(&pf_switch_id_file, SYSFS_NET_DIR "%s/phys_switch_id",
                    pf_name) < 0)
        goto cleanup;

    if (virAsprintf(&pf_subsystem_dir, SYSFS_NET_DIR "%s/subsystem",
                    pf_name) < 0)
        goto cleanup;

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

        if (virAsprintf(&pf_subsystem_device_file, "%s/%s/phys_switch_id",
                        pf_subsystem_dir, dp->d_name) < 0)
            goto cleanup;

        if (virFileReadAllQuiet(pf_subsystem_device_file, IFSWITCHIDSIZ,
                                &pf_subsystem_device_switch_id) > 0) {
            if (!STREQ(pf_switch_id, pf_subsystem_device_switch_id)) {
                VIR_FREE(pf_subsystem_device_file);
                VIR_FREE(pf_subsystem_device_switch_id);
                continue;
            }
        }

        if (virAsprintf(&pf_subsystem_device_port_name_file,
                        "%s/%s/phys_port_name", pf_subsystem_dir,
                        dp->d_name) < 0)
            goto cleanup;

        if (virFileReadAllQuiet
            (pf_subsystem_device_port_name_file, IFNAMSIZ,
             &vf_representor_name) <= 0) {
            VIR_FREE(pf_subsystem_device_file);
            VIR_FREE(pf_subsystem_device_switch_id);
            VIR_FREE(pf_subsystem_device_port_name_file);
            continue;
        }

        if (virAsprintf(&vf_num_str, "%d", vf) < 0)
            goto cleanup;

        /* phys_port_name may contain just VF number or string with
         * 'vf' or 'VF' followed by VF number at the end.
         */
        if (!(vf_suffix = strcasestr(vf_representor_name, "vf")))
            vf_suffix = vf_representor_name;

        if (strstr(vf_suffix, vf_num_str)) {
            if (virAsprintf(vf_representor, "%s", dp->d_name) < 0)
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
    VIR_FREE(vf_representor_name);
    return ret;
}


/**
 * virNetdevHostdevGetVFRepIFName
 *
 * @hostdev: host device to check
 * @ifname : Contains VF representor name upon successful return.
 *
 * Returns 0 on success, -1 on failure
 */
int
virNetdevHostdevGetVFRepIFName(virDomainHostdevDefPtr hostdev,
                               char **ifname)
{
    char *linkdev = NULL;
    char *vf_representor = NULL;
    int vf = -1;
    int ret = -1;

    if (virHostdevNetDeviceWrapper(hostdev, -1, &linkdev, &vf) < 0)
        goto cleanup;

    if (virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_representor))
        goto cleanup;

    if (VIR_STRDUP(*ifname, vf_representor) > 0)
        ret = 0;

  cleanup:
    VIR_FREE(linkdev);
    VIR_FREE(vf_representor);
    return ret;
}


/**
 * virNetdevHostdevCheckVFRepIFName
 *
 * @hostdev: host device to check
 * @ifname : VF representor name to verify
 *
 * Returns 0 on success, -1 on failure
 */
int
virNetdevHostdevCheckVFRepIFName(virDomainHostdevDefPtr hostdev,
                                 const char *ifname)
{
    char *linkdev = NULL;
    char *vf_representor = NULL;
    int vf = -1;
    int ret = -1;

    if (virHostdevNetDeviceWrapper(hostdev, -1, &linkdev, &vf) < 0)
        goto cleanup;

    if (virNetdevHostdevNetSysfsPath(linkdev, vf, &vf_representor))
        goto cleanup;

    if (STREQ(ifname, vf_representor))
        ret = 0;

  cleanup:
    VIR_FREE(linkdev);
    VIR_FREE(vf_representor);
    return ret;
}
