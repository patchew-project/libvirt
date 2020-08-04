/*
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <config.h>

#include "virerror.h"
#include "virlog.h"

#include "security_driver.h"
#ifdef WITH_SECDRIVER_SELINUX
# include "security_selinux.h"
#endif

#ifdef WITH_SECDRIVER_APPARMOR
# include "security_apparmor.h"
#endif

#include "security_nop.h"

#define VIR_FROM_THIS VIR_FROM_SECURITY

VIR_LOG_INIT("security.security_driver");

static virSecurityDriverPtr security_drivers[] = {
#ifdef WITH_SECDRIVER_SELINUX
    &virSecurityDriverSELinux,
#endif
#ifdef WITH_SECDRIVER_APPARMOR
    &virAppArmorSecurityDriver,
#endif
    &virSecurityDriverNop, /* Must always be last, since it will always probe */
};

virSecurityDriverPtr virSecurityDriverLookup(const char *name,
                                             const char *virtDriver)
{
    virSecurityDriverPtr drv = NULL;
    size_t i;

    VIR_DEBUG("name=%s", NULLSTR(name));

    for (i = 0; i < G_N_ELEMENTS(security_drivers) && !drv; i++) {
        virSecurityDriverPtr tmp = security_drivers[i];

        if (name &&
            STRNEQ(tmp->name, name))
            continue;

        switch (tmp->probe(virtDriver)) {
        case SECURITY_DRIVER_ENABLE:
            VIR_DEBUG("Probed name=%s", tmp->name);
            drv = tmp;
            break;

        case SECURITY_DRIVER_DISABLE:
            VIR_DEBUG("Not enabled name=%s", tmp->name);
            if (name && STREQ(tmp->name, name)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Security driver %s not enabled"),
                               name);
                return NULL;
            }
            break;

        case SECURITY_DRIVER_ERROR:
        default:
            return NULL;
        }
    }

    if (!drv) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Security driver %s not found"),
                       NULLSTR(name));
        return NULL;
    }

    return drv;
}
