#include <config.h>
#ifdef WITH_LXC

# include "testutilslxc.h"
# include "testutils.h"
# include "viralloc.h"
# include "domain_conf.h"

# define VIR_FROM_THIS VIR_FROM_LXC

virCapsPtr
testLXCCapsInit(void)
{
    g_autoptr(virCaps) caps = NULL;
    virCapsGuestPtr guest;

    if ((caps = virCapabilitiesNew(VIR_ARCH_X86_64,
                                   false, false)) == NULL)
        return NULL;

    if ((guest = virCapabilitiesAddGuest(caps, VIR_DOMAIN_OSTYPE_EXE,
                                         VIR_ARCH_I686,
                                         "/usr/libexec/libvirt_lxc", NULL,
                                         0, NULL)) == NULL)
        return NULL;

    if (!virCapabilitiesAddGuestDomain(guest, VIR_DOMAIN_VIRT_LXC, NULL, NULL, 0, NULL))
        return NULL;


    if ((guest = virCapabilitiesAddGuest(caps, VIR_DOMAIN_OSTYPE_EXE,
                                         VIR_ARCH_X86_64,
                                         "/usr/libexec/libvirt_lxc", NULL,
                                         0, NULL)) == NULL)
        return NULL;

    if (!virCapabilitiesAddGuestDomain(guest, VIR_DOMAIN_VIRT_LXC, NULL, NULL, 0, NULL))
        return NULL;


    if (virTestGetDebug()) {
        char *caps_str;

        caps_str = virCapabilitiesFormatXML(caps);
        if (!caps_str)
            return NULL;

        VIR_TEST_DEBUG("LXC driver capabilities:\n%s", caps_str);

        VIR_FREE(caps_str);
    }

    return g_steal_pointer(&caps);
}


virLXCDriverPtr
testLXCDriverInit(void)
{
    virLXCDriverPtr driver = g_new0(virLXCDriver, 1);

    if (virMutexInit(&driver->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", "cannot initialize mutex");
        g_free(driver);
        return NULL;
    }

    driver->caps = testLXCCapsInit();
    driver->xmlopt = lxcDomainXMLConfInit(driver);

    return driver;
}


void
testLXCDriverFree(virLXCDriverPtr driver)
{
    g_clear_object(&driver->xmlopt);
    g_clear_object(&driver->caps);
    virMutexDestroy(&driver->lock);
    g_free(driver);
}

#endif
