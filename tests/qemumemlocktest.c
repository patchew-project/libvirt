#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>

#include "testutils.h"

#ifdef WITH_QEMU

# include "datatypes.h"
# include "internal.h"
# include "virstring.h"
# include "conf/domain_conf.h"
# include "qemu/qemu_capabilities.h"
# include "qemu/qemu_process.h"

# include "testutilsqemu.h"

# define VIR_FROM_THIS VIR_FROM_QEMU

static const char *abs_top_srcdir;
static virQEMUDriver driver;

struct testInfo {
    const char *name;
    unsigned long long memlock;
};

static int
testCompareMemLock(const void *data)
{
    const struct testInfo *info = data;
    virConnectPtr conn = NULL;
    virDomainObjPtr vm = NULL;
    virCommandPtr cmd = NULL;
    char *xml = NULL;
    int ret = -1;

    if (!(conn = virGetConnect()))
        goto cleanup;

    if (virAsprintf(&xml, "%s/qemumemlockdata/qemumemlock-%s.xml",
                    abs_srcdir, info->name) < 0)
        goto cleanup;

    if (!(vm = virDomainObjNew(driver.xmlopt)))
        goto cleanup;

    if (!(vm->def = virDomainDefParseFile(xml, driver.caps, driver.xmlopt, NULL,
                                          VIR_DOMAIN_DEF_PARSE_INACTIVE))) {
        goto cleanup;
    }

    if (!(cmd = qemuProcessCreatePretendCmd(conn, &driver, vm, NULL, 0, false,
                                            VIR_QEMU_PROCESS_START_COLD))) {
        goto cleanup;
    }

    if (virTestCompareToULL(info->memlock, virCommandGetMaxMemLock(cmd)) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virCommandFree(cmd);
    virObjectUnref(vm);
    virObjectUnref(conn);
    VIR_FREE(xml);

    return ret;
}


int
main(void)
{
    virQEMUCapsPtr qemuCaps = NULL;
    int ret = 0;

    abs_top_srcdir = getenv("abs_top_srcdir");
    if (!abs_top_srcdir)
        abs_top_srcdir = abs_srcdir "/..";

    if (qemuTestDriverInit(&driver) < 0)
        return EXIT_FAILURE;

    driver.privileged = true;

    /* QEMU capabilites are the same for all tests */
    if (!(qemuCaps = virQEMUCapsNew()))
        return EXIT_FAILURE;

    virQEMUCapsSetList(qemuCaps,
                       QEMU_CAPS_KVM, QEMU_CAPS_REALTIME_MLOCK,
                       QEMU_CAPS_DEVICE_VFIO_PCI);

    if (qemuTestCapsCacheInsert(driver.qemuCapsCache, "qemumemlock", qemuCaps) < 0)
        return EXIT_FAILURE;

    virObjectUnref(qemuCaps);

# define DO_TEST(name, memlock) \
    do { \
        static struct testInfo info = { \
            name, memlock \
        }; \
        if (virTestRun("QEMU MEMLOCK " name, testCompareMemLock, &info) < 0) \
            ret = -1; \
    } while (0)

    /* The tests below make sure that the memory locking limit is being
     * calculated correctly in a number of situations. Each test is
     * performed both on x86_64/pc and ppc64/pseries in order to account
     * for some architecture-specific details.
     *
     * kvm: simple KMV guest
     * tcg: simple TCG guest
     *
     * hardlimit: guest where <memtune><hard_limit> has been configured
     * locked:    guest where <memoryBacking><locked> has been enabled
     * hostdev:   guest that has some hostdev assigned
     *
     * The remaining tests cover different combinations of the above to
     * ensure settings are prioritized as expected.
     */

    qemuTestSetHostArch(driver.caps, VIR_ARCH_X86_64);

    DO_TEST("pc-kvm", 0);
    DO_TEST("pc-tcg", 0);

    DO_TEST("pc-hardlimit", 2147483648);
    DO_TEST("pc-locked", VIR_DOMAIN_MEMORY_PARAM_UNLIMITED);
    DO_TEST("pc-hostdev", 2147483648);

    DO_TEST("pc-hardlimit+locked", 2147483648);
    DO_TEST("pc-hardlimit+hostdev", 2147483648);
    DO_TEST("pc-hardlimit+locked+hostdev", 2147483648);
    DO_TEST("pc-locked+hostdev", VIR_DOMAIN_MEMORY_PARAM_UNLIMITED);

    qemuTestSetHostArch(driver.caps, VIR_ARCH_PPC64);

    DO_TEST("pseries-kvm", 20971520);
    DO_TEST("pseries-tcg", 0);

    DO_TEST("pseries-hardlimit", 2147483648);
    DO_TEST("pseries-locked", VIR_DOMAIN_MEMORY_PARAM_UNLIMITED);
    DO_TEST("pseries-hostdev", 2168455168);

    DO_TEST("pseries-hardlimit+locked", 2147483648);
    DO_TEST("pseries-hardlimit+hostdev", 2147483648);
    DO_TEST("pseries-hardlimit+locked+hostdev", 2147483648);
    DO_TEST("pseries-locked+hostdev", VIR_DOMAIN_MEMORY_PARAM_UNLIMITED);

    qemuTestDriverFree(&driver);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#else

int main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_QEMU */
