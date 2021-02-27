/*
 * bhyve_firmware.c: bhyve firmware management
 *
 * Copyright (C) 2021 Roman Bogorodskiy
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
 */

#include <config.h>
#include <dirent.h>

#include "viralloc.h"
#include "virlog.h"
#include "virfile.h"
#include "bhyve_conf.h"
#include "bhyve_firmware.h"

#define VIR_FROM_THIS   VIR_FROM_BHYVE

VIR_LOG_INIT("bhyve.bhyve_firmware");


#define BHYVE_DEFAULT_FIRMWARE  "BHYVE_UEFI.fd"

int
bhyveFirmwareFillDomain(bhyveConnPtr driver,
                        virDomainDefPtr def,
                        unsigned int flags)
{
    g_autoptr(DIR) dir = NULL;
    virBhyveDriverConfigPtr cfg = virBhyveDriverGetConfig(driver);
    const char *firmware_dir_cfg = cfg->firmwareDir;
    const char *firmware_dir_env = NULL, *firmware_dir = NULL;
    struct dirent *entry;
    char *matching_firmware = NULL;
    char *first_found = NULL;

    virCheckFlags(0, -1);

    if (def->os.firmware == VIR_DOMAIN_OS_DEF_FIRMWARE_NONE)
        return 0;

    if (virDirOpenIfExists(&dir, firmware_dir_cfg) > 0) {
        while ((virDirRead(dir, &entry, firmware_dir)) > 0) {
            if (STREQ(entry->d_name, BHYVE_DEFAULT_FIRMWARE)) {
                matching_firmware = g_strdup(entry->d_name);
                break;
            }
            if (!first_found)
                first_found = g_strdup(entry->d_name);
        }
    }

    if (!matching_firmware) {
        if (!first_found) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("no firmwares found in %s"),
                           firmware_dir_cfg);
            return -1;
        } else {
            matching_firmware = first_found;
        }
    }

    if (!def->os.loader)
        def->os.loader = g_new0(virDomainLoaderDef, 1);

    def->os.loader->type = VIR_DOMAIN_LOADER_TYPE_PFLASH;
    def->os.loader->readonly = VIR_TRISTATE_BOOL_YES;

    VIR_FREE(def->os.loader->path);

    firmware_dir_env = g_getenv("LIBVIRT_BHYVE_FIRMWARE_DIR_OVERRIDE");
    firmware_dir = firmware_dir_env ? firmware_dir_env : firmware_dir_cfg;
    def->os.loader->path = g_build_filename(firmware_dir, matching_firmware, NULL);

    return 0;
}
