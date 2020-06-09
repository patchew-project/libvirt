#include <config.h>

#include "internal.h"
#include "testutils.h"
#include "datatypes.h"
#include "node_device/node_device_driver.h"
#include "vircommand.h"
#define LIBVIRT_VIRCOMMANDPRIV_H_ALLOW
#include "vircommandpriv.h"

#define VIR_FROM_THIS VIR_FROM_NODEDEV

struct testInfo {
    bool shouldFail;
    const char *virt_type;
    int create;
    const char *filename;
};

// capture stdin passed to command
static void
testCommandDryRunCallback(const char *const*args G_GNUC_UNUSED,
                          const char *const*env G_GNUC_UNUSED,
                          const char *input,
                          char **output G_GNUC_UNUSED,
                          char **error G_GNUC_UNUSED,
                          int *status G_GNUC_UNUSED,
                          void *opaque G_GNUC_UNUSED)
{
    char **stdinbuf = opaque;

    *stdinbuf = g_strdup(input);
}

static int
testMdevctlStart(bool shouldFail G_GNUC_UNUSED,
                 const char *virt_type,
                 int create,
                 const char *mdevxml,
                 const char *startcmdfile,
                 const char *startjsonfile)
{
    virNodeDeviceDefPtr def = NULL;
    virNodeDeviceObjPtr obj = NULL;
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    const char *actualCmdline = NULL;
    int ret = -1;
    g_autofree char *uuid = NULL;
    g_autofree char *stdinbuf = NULL;
    g_autoptr(virCommand) cmd = NULL;

    if (!(def = virNodeDeviceDefParseFile(mdevxml, create, virt_type)))
        goto cleanup;

    // this function will set a stdin buffer containing the json configuration
    // of the device. This json value is captured in the mock wrapper above
    cmd = nodeDeviceGetMdevctlStartCommand(def, false, &uuid);

    if (!cmd)
        goto cleanup;

    virCommandSetDryRun(&buf, testCommandDryRunCallback, &stdinbuf);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    if (!(actualCmdline = virBufferCurrentContent(&buf)))
        goto cleanup;

    if (virTestCompareToFile(actualCmdline, startcmdfile) < 0)
        goto cleanup;

    if (virTestCompareToFile(stdinbuf, startjsonfile) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virBufferFreeAndReset(&buf);
    virCommandSetDryRun(NULL, NULL, NULL);
    virNodeDeviceObjEndAPI(&obj);
    return ret;
}

static int
testMdevctlStartHelper(const void *data)
{
    const struct testInfo *info = data;

    g_autofree char *mdevxml = g_strdup_printf("%s/nodedevschemadata/%s.xml",
                                               abs_srcdir, info->filename);
    g_autofree char *cmdlinefile = g_strdup_printf("%s/nodedevmdevctldata/%s-start.argv",
                                                   abs_srcdir, info->filename);
    g_autofree char *jsonfile = g_strdup_printf("%s/nodedevmdevctldata/%s-start.json",
                                                   abs_srcdir, info->filename);

    return testMdevctlStart(info->shouldFail, info->virt_type,
                            info->create, mdevxml, cmdlinefile,
                            jsonfile);
}

static void
nodedevTestDriverFree(virNodeDeviceDriverStatePtr drv)
{
    if (!drv)
        return;

    virCondDestroy(&drv->initCond);
    virMutexDestroy(&drv->lock);
    VIR_FREE(drv->stateDir);
    VIR_FREE(drv);
}

/* Add a fake root 'computer' device */
static virNodeDeviceDefPtr
fakeRootDevice(void)
{
    virNodeDeviceDefPtr def = NULL;

    if (VIR_ALLOC(def) != 0 || VIR_ALLOC(def->caps) != 0) {
        virNodeDeviceDefFree(def);
        return NULL;
    }

    def->name = g_strdup("computer");

    return def;
}

/* Add a fake pci device that can be used as a parent device for mediated
 * devices. For our purposes, it only needs to have a name that matches the
 * parent of the mdev, and it needs a PCI address
 */
static virNodeDeviceDefPtr
fakeParentDevice(void)
{
    virNodeDeviceDefPtr def = NULL;
    virNodeDevCapPCIDevPtr pci_dev;

    if (VIR_ALLOC(def) != 0 || VIR_ALLOC(def->caps) != 0) {
        virNodeDeviceDefFree(def);
        return NULL;
    }

    def->name = g_strdup("pci_0000_00_02_0");
    def->parent = g_strdup("computer");

    def->caps->data.type = VIR_NODE_DEV_CAP_PCI_DEV;
    pci_dev = &def->caps->data.pci_dev;
    pci_dev->domain = 0;
    pci_dev->bus = 0;
    pci_dev->slot = 2;
    pci_dev->function = 0;

    return def;
}

static int
addDevice(virNodeDeviceDefPtr def)
{
    if (!def)
        return -1;

    virNodeDeviceObjPtr obj = virNodeDeviceObjListAssignDef(driver->devs, def);

    if (!obj) {
        virNodeDeviceDefFree(def);
        return -1;
    }

    virNodeDeviceObjEndAPI(&obj);
    return 0;
}

static int
nodedevTestDriverAddTestDevices(void)
{
    if (addDevice(fakeRootDevice()) < 0 ||
        addDevice(fakeParentDevice()) < 0)
        return -1;

    return 0;
}

/* Bare minimum driver init to be able to test nodedev functionality */
static int
nodedevTestDriverInit(void)
{
    int ret = -1;
    char statedir[] = abs_builddir "/nodedevstatedir-XXXXXX";
    if (VIR_ALLOC(driver) < 0)
        return -1;

    if (!g_mkdtemp(statedir)) {
        fprintf(stderr, "Cannot create fake stateDir");
        goto error;
    }

    driver->stateDir = g_strdup(statedir);
    driver->lockFD = -1;
    if (virMutexInit(&driver->lock) < 0 ||
        virCondInit(&driver->initCond) < 0) {
        VIR_TEST_DEBUG("Unable to initialize test nodedev driver");
        goto error;
    }

    if (!(driver->devs = virNodeDeviceObjListNew()))
        goto error;

    return 0;

 error:
    nodedevTestDriverFree(driver);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;

    if (nodedevTestDriverInit() < 0)
        return EXIT_FAILURE;

    if (nodedevTestDriverAddTestDevices() < 0) {
        ret = EXIT_FAILURE;
        goto done;
    }

    // add a mock device to the device list so it can be used as a parent
    // reference

#define DO_TEST_FULL(shouldFail, virt_type, create, filename) \
    do { \
        struct testInfo info = { shouldFail, virt_type, create, filename, }; \
        if (virTestRun("mdevctl start " filename, \
                       testMdevctlStartHelper, &info) < 0) \
            ret = -1; \
       } \
    while (0);

#define DO_TEST(...) \
    DO_TEST_FULL(false, "QEMU", CREATE_DEVICE, __VA_ARGS__)
#define DO_TEST_FAIL(...) \
    DO_TEST_FULL(true, "QEMU", CREATE_DEVICE, __VA_ARGS__)

    // Test mdevctl start commands
    DO_TEST("mdev_d069d019_36ea_4111_8f0a_8c9a70e21366");
    DO_TEST("mdev_fedc4916_1ca8_49ac_b176_871d16c13076");
    DO_TEST("mdev_d2441d39_495e_4243_ad9f_beb3f14c23d9");

 done:
    nodedevTestDriverFree(driver);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
