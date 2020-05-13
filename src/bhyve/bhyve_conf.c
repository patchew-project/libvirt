/*
 * bhyve_conf.c: bhyve config file
 *
 * Copyright (C) 2017 Roman Bogorodskiy
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

#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"
#include "bhyve_conf.h"
#include "bhyve_domain.h"
#include "configmake.h"

#define VIR_FROM_THIS VIR_FROM_BHYVE

VIR_LOG_INIT("bhyve.bhyve_conf");

G_DEFINE_TYPE(virBhyveDriverConfig, vir_bhyve_driver_config, G_TYPE_OBJECT);
static void virBhyveDriverConfigFinalize(GObject *obj);

static void
vir_bhyve_driver_config_init(virBhyveDriverConfig *cfg G_GNUC_UNUSED)
{
}

static void
vir_bhyve_driver_config_class_init(virBhyveDriverConfigClass *klass)
{
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    obj->finalize = virBhyveDriverConfigFinalize;
}

virBhyveDriverConfigPtr
virBhyveDriverConfigNew(void)
{
    virBhyveDriverConfigPtr cfg =
        VIR_BHYVE_DRIVER_CONFIG(g_object_new(VIR_TYPE_BHYVE_DRIVER_CONFIG, NULL));

    cfg->firmwareDir = g_strdup(DATADIR "/uefi-firmware");

    return cfg;
}

int
virBhyveLoadDriverConfig(virBhyveDriverConfigPtr cfg,
                         const char *filename)
{
    g_autoptr(virConf) conf = NULL;

    if (access(filename, R_OK) == -1) {
        VIR_INFO("Could not read bhyve config file %s", filename);
        return 0;
    }

    if (!(conf = virConfReadFile(filename, 0)))
        return -1;

    if (virConfGetValueString(conf, "firmware_dir",
                              &cfg->firmwareDir) < 0)
        return -1;

    return 0;
}

virBhyveDriverConfigPtr
virBhyveDriverGetConfig(bhyveConnPtr driver)
{
    virBhyveDriverConfigPtr cfg;
    bhyveDriverLock(driver);
    cfg = g_object_ref(driver->config);
    bhyveDriverUnlock(driver);
    return cfg;
}

static void
virBhyveDriverConfigFinalize(GObject *obj)
{
    virBhyveDriverConfigPtr cfg = VIR_BHYVE_DRIVER_CONFIG(obj);

    VIR_FREE(cfg->firmwareDir);

    G_OBJECT_CLASS(vir_bhyve_driver_config_parent_class)->finalize(obj);
}

void
bhyveDomainCmdlineDefFree(bhyveDomainCmdlineDefPtr def)
{
    size_t i;

    if (!def)
        return;

    for (i = 0; i < def->num_args; i++)
        VIR_FREE(def->args[i]);

    VIR_FREE(def->args);
    VIR_FREE(def);
}
