/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

#include "qemu/qemu_alias.h"
#include "qemu/qemu_conf.h"
#include "qemu/qemu_hotplug.h"
#include "qemu/qemu_hotplugpriv.h"
#include "qemu/qemu_driverpriv.h"
#include "qemumonitortestutils.h"
#include "testutils.h"
#include "testutilsqemu.h"
#include "virerror.h"
#include "virstring.h"
#include "virthread.h"
#include "virfile.h"

#define VIR_FROM_THIS VIR_FROM_NONE

static virQEMUDriver driver;

enum {
    ATTACH,
    DETACH,
    UPDATE
};

#define QEMU_HOTPLUG_TEST_DOMAIN_ID 7

struct qemuHotplugTestData {
    const char *domain_filename;
    const char *device_filename;
    bool fail;
    const char *const *mon;
    int action;
    bool keep;
    virDomainObjPtr vm;
    bool deviceDeletedEvent;
    virDomainModificationImpact impact;
};

static int
qemuHotplugCreateObjects(virDomainXMLOptionPtr xmlopt,
                         virDomainObjPtr *vm,
                         const char *domxml,
                         bool event, const char *testname,
                         virDomainModificationImpact impact)
{
    int ret = -1;
    qemuDomainObjPrivatePtr priv = NULL;

    if (!(*vm = virDomainObjNew(xmlopt)))
        goto cleanup;

    priv = (*vm)->privateData;

    if (!(priv->qemuCaps = virQEMUCapsNew()))
        goto cleanup;

    virQEMUCapsSet(priv->qemuCaps, QEMU_CAPS_VIRTIO_SCSI);
    virQEMUCapsSet(priv->qemuCaps, QEMU_CAPS_DEVICE_USB_STORAGE);
    virQEMUCapsSet(priv->qemuCaps, QEMU_CAPS_VIRTIO_CCW);
    if (event)
        virQEMUCapsSet(priv->qemuCaps, QEMU_CAPS_DEVICE_DEL_EVENT);

    if (qemuTestCapsCacheInsert(driver.qemuCapsCache, testname,
                                priv->qemuCaps) < 0)
        goto cleanup;

    if (!((*vm)->def = virDomainDefParseString(domxml,
                                               driver.caps,
                                               driver.xmlopt,
                                               VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        goto cleanup;

    if (qemuDomainAssignAddresses((*vm)->def, priv->qemuCaps, *vm, true) < 0)
        goto cleanup;

    if (qemuAssignDeviceAliases((*vm)->def, priv->qemuCaps) < 0)
        goto cleanup;

    if (impact == VIR_DOMAIN_AFFECT_LIVE)
        (*vm)->def->id = QEMU_HOTPLUG_TEST_DOMAIN_ID;

    if (qemuDomainSetPrivatePaths(&driver, *vm) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    return ret;
}

static int
testQemuHotplugAttach(virDomainObjPtr vm,
                      virDomainDeviceDefPtr dev,
                      const char *device_xml,
                      virDomainModificationImpact impact)
{
    int ret = -1;

    switch (dev->type) {
    case VIR_DOMAIN_DEVICE_DISK:
    case VIR_DOMAIN_DEVICE_CHR:
        /* conn in only used for storage pool and secrets lookup so as long
         * as we don't use any of them, passing NULL should be safe
         */
        ret = qemuDomainAttachDeviceLiveAndConfig(NULL, vm, &driver,
                                                  device_xml, impact);
        break;
    default:
        VIR_TEST_VERBOSE("device type '%s' cannot be attached\n",
                virDomainDeviceTypeToString(dev->type));
        break;
    }

    return ret;
}

static int
testQemuHotplugDetach(virDomainObjPtr vm,
                      virDomainDeviceDefPtr dev,
                      const char *device_xml,
                      virDomainModificationImpact impact)
{
    int ret = -1;

    switch (dev->type) {
    case VIR_DOMAIN_DEVICE_DISK:
    case VIR_DOMAIN_DEVICE_CHR:
        ret = qemuDomainDetachDeviceLiveAndConfig(&driver, vm,
                                                  device_xml, impact);
        break;
    default:
        VIR_TEST_VERBOSE("device type '%s' cannot be detached\n",
                virDomainDeviceTypeToString(dev->type));
        break;
    }

    return ret;
}

static int
testQemuHotplugUpdate(virDomainObjPtr vm,
                      virDomainDeviceDefPtr dev,
                      const char *device_xml,
                      virDomainModificationImpact impact)
{
    int ret = -1;

    /* XXX Ideally, we would call qemuDomainUpdateDeviceLive here.  But that
     * would require us to provide virConnectPtr and virDomainPtr (they're used
     * in case of updating a disk device. So for now, we will proceed with
     * breaking the function into pieces. If we ever learn how to fake those
     * required object, we can replace this code then. */
    switch (dev->type) {
    case VIR_DOMAIN_DEVICE_GRAPHICS:
        /* conn is only used for storage lookup, so passing NULL should be safe. */
        ret = qemuDomainUpdateDeviceLiveAndConfig(NULL, vm, &driver,
                                                  device_xml, impact);
        break;
    default:
        VIR_TEST_VERBOSE("device type '%s' cannot be updated\n",
                virDomainDeviceTypeToString(dev->type));
        break;
    }

    return ret;
}

static int
testQemuHotplugCheckResult(virDomainObjPtr vm,
                           const char *expected,
                           const char *expectedFile,
                           bool fail,
                           virDomainModificationImpact impact)
{
    char *actual = NULL;
    int ret;

    switch (impact) {
    case VIR_DOMAIN_AFFECT_LIVE:
        actual = virDomainDefFormat(vm->def, driver.caps,
                                    VIR_DOMAIN_DEF_FORMAT_SECURE);
        vm->def->id = QEMU_HOTPLUG_TEST_DOMAIN_ID;
        break;
    case VIR_DOMAIN_AFFECT_CONFIG:
        actual = virDomainDefFormat(vm->def, driver.caps,
                                    VIR_DOMAIN_DEF_FORMAT_SECURE);
        break;
    case VIR_DOMAIN_AFFECT_CURRENT:
        VIR_TEST_VERBOSE("Please specify either VIR_DOMAIN_AFFECT_LIVE or"
                         "VIR_DOMAIN_AFFECT_CONFIG");
        break;
    }

    if (!actual)
        return -1;

    if (STREQ(expected, actual)) {
        if (fail)
            VIR_TEST_VERBOSE("domain XML should not match the expected result\n");
        ret = 0;
    } else {
        if (!fail)
            virTestDifferenceFull(stderr,
                                  expected, expectedFile,
                                  actual, NULL);
        ret = -1;
    }

    VIR_FREE(actual);
    return ret;
}

static int
testQemuHotplug(const void *data)
{
    int ret = -1;
    struct qemuHotplugTestData *test = (struct qemuHotplugTestData *) data;
    char *domain_filename = NULL;
    char *device_filename = NULL;
    char *result_filename = NULL;
    char *domain_xml = NULL;
    char *device_xml = NULL;
    char *result_xml = NULL;
    const char *const *tmp;
    bool fail = test->fail;
    bool keep = test->keep;
    unsigned int device_parse_flags = 0;
    virDomainObjPtr vm = NULL;
    virDomainDeviceDefPtr dev = NULL;
    virCapsPtr caps = NULL;
    qemuMonitorTestPtr test_mon = NULL;
    qemuDomainObjPrivatePtr priv = NULL;
    virDomainModificationImpact impact = test->impact;

    if (virAsprintf(&domain_filename, "%s/qemuhotplugtestdomains/qemuhotplug-%s.xml",
                    abs_srcdir, test->domain_filename) < 0 ||
        virAsprintf(&device_filename, "%s/qemuhotplugtestdevices/qemuhotplug-%s.xml",
                    abs_srcdir, test->device_filename) < 0)
        goto cleanup;

    switch (impact) {
    case VIR_DOMAIN_AFFECT_LIVE:
        if (virAsprintf(&result_filename,
                        "%s/qemuhotplugtestdomains/qemuhotplug-%s+%s.xml",
                        abs_srcdir, test->domain_filename,
                        test->device_filename) < 0)
            goto cleanup;
        break;
    case VIR_DOMAIN_AFFECT_CONFIG:
        if (virAsprintf(&result_filename,
                        "%s/qemuhotplugtestdomains/qemuhotplug-%s+%s+config.xml",
                        abs_srcdir, test->domain_filename,
                        test->device_filename) < 0)
            goto cleanup;
        break;
    default:
        VIR_TEST_VERBOSE("Impact can either be VIR_DOMAIN_AFFECT_LIVE"
                         " or VIR_DOMAIN_AFFECT_CONFIG\n");
        goto cleanup;
    }

    if (virTestLoadFile(domain_filename, &domain_xml) < 0 ||
        virTestLoadFile(device_filename, &device_xml) < 0)
        goto cleanup;

    if (test->action != UPDATE &&
        virTestLoadFile(result_filename, &result_xml) < 0)
        goto cleanup;

    if (!(caps = virQEMUDriverGetCapabilities(&driver, false)))
        goto cleanup;

    if (test->vm) {
        vm = test->vm;
    } else {
        if (qemuHotplugCreateObjects(driver.xmlopt, &vm, domain_xml,
                                     test->deviceDeletedEvent,
                                     test->domain_filename,
                                     impact) < 0)
            goto cleanup;
    }

    if (test->action == ATTACH)
        device_parse_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;

    if (!(dev = virDomainDeviceDefParse(device_xml, vm->def,
                                        caps, driver.xmlopt,
                                        device_parse_flags)))
        goto cleanup;

    /* Now is the best time to feed the spoofed monitor with predefined
     * replies. */
    if (!(test_mon = qemuMonitorTestNew(true, driver.xmlopt, vm, &driver, NULL)))
        goto cleanup;

    tmp = test->mon;
    while (tmp && *tmp) {
        const char *command_name;
        const char *response;

        if (!(command_name = *tmp++) ||
            !(response = *tmp++))
            break;
        if (qemuMonitorTestAddItem(test_mon, command_name, response) < 0)
            goto cleanup;
    }

    priv = vm->privateData;
    priv->mon = qemuMonitorTestGetMonitor(test_mon);
    priv->monJSON = true;

    /* XXX We need to unlock the monitor here, as
     * qemuDomainObjEnterMonitorInternal (called from qemuDomainChangeGraphics)
     * tries to lock it again */
    virObjectUnlock(priv->mon);

    switch (test->action) {
    case ATTACH:
        ret = testQemuHotplugAttach(vm, dev, device_xml, impact);
        if (ret == 0) {
            /* vm->def stolen dev->data.* so we just need to free the dev
             * envelope */
            VIR_FREE(dev);
        }
        if (ret == 0 || fail)
            ret = testQemuHotplugCheckResult(vm, result_xml, result_filename,
                                             fail, impact);
        break;

    case DETACH:
        ret = testQemuHotplugDetach(vm, dev, device_xml, impact);
        if (ret == 0 || fail)
            ret = testQemuHotplugCheckResult(vm, domain_xml, domain_filename,
                                             fail, impact);
        break;

    case UPDATE:
        ret = testQemuHotplugUpdate(vm, dev, device_xml, impact);
    }

 cleanup:
    VIR_FREE(domain_filename);
    VIR_FREE(device_filename);
    VIR_FREE(result_filename);
    VIR_FREE(domain_xml);
    VIR_FREE(device_xml);
    VIR_FREE(result_xml);
    /* don't dispose test monitor with VM */
    if (priv)
        priv->mon = NULL;
    if (keep) {
        test->vm = vm;
    } else {
        virObjectUnref(vm);
        test->vm = NULL;
    }
    virDomainDeviceDefFree(dev);
    virObjectUnref(caps);
    qemuMonitorTestFree(test_mon);
    return ((ret < 0 && fail) || (!ret && !fail)) ? 0 : -1;
}

static int
mymain(void)
{
    int ret = 0;
    struct qemuHotplugTestData data = {0};

#if !WITH_YAJL
    fputs("libvirt not compiled with yajl, skipping this test\n", stderr);
    return EXIT_AM_SKIP;
#endif

    if (virThreadInitialize() < 0 ||
        qemuTestDriverInit(&driver) < 0)
        return EXIT_FAILURE;

    virEventRegisterDefaultImpl();

    VIR_FREE(driver.config->spiceListen);
    VIR_FREE(driver.config->vncListen);
    /* some dummy values from 'config file' */
    if (VIR_STRDUP_QUIET(driver.config->spicePassword, "123456") < 0)
        return EXIT_FAILURE;

    if (!(driver.domainEventState = virObjectEventStateNew()))
        return EXIT_FAILURE;

    driver.lockManager = virLockManagerPluginNew("nop", "qemu",
                                                 driver.config->configBaseDir,
                                                 0);
    if (!driver.lockManager)
        return EXIT_FAILURE;

    /* wait only 100ms for DEVICE_DELETED event */
    qemuDomainRemoveDeviceWaitTime = 100;

#define DO_TEST(file, ACTION, dev, event, fial, kep, impct, ...)            \
    do {                                                                    \
        const char *my_mon[] = { __VA_ARGS__, NULL};                        \
        const char *name = file " " #ACTION " " dev;                        \
        data.action = ACTION;                                               \
        data.domain_filename = file;                                        \
        data.device_filename = dev;                                         \
        data.fail = fial;                                                   \
        data.mon = my_mon;                                                  \
        data.keep = kep;                                                    \
        data.deviceDeletedEvent = event;                                    \
        data.impact = impct;                                                \
        if (virTestRun(name, testQemuHotplug, &data) < 0)                   \
            ret = -1;                                                       \
    } while (0)

#define DO_TEST_ATTACH_LIVE(file, dev, fial, kep, ...)                       \
    DO_TEST(file, ATTACH, dev, false, fial, kep,                             \
        VIR_DOMAIN_AFFECT_LIVE, __VA_ARGS__)

#define DO_TEST_DETACH_LIVE(file, dev, fial, kep, ...)                       \
    DO_TEST(file, DETACH, dev, false, fial, kep,                             \
        VIR_DOMAIN_AFFECT_LIVE, __VA_ARGS__)

#define DO_TEST_ATTACH_EVENT_LIVE(file, dev, fial, kep, ...)                 \
    DO_TEST(file, ATTACH, dev, true, fial, kep,                              \
        VIR_DOMAIN_AFFECT_LIVE, __VA_ARGS__)

#define DO_TEST_DETACH_EVENT_LIVE(file, dev, fial, kep, ...)                 \
    DO_TEST(file, DETACH, dev, true, fial, kep,                              \
        VIR_DOMAIN_AFFECT_LIVE, __VA_ARGS__)

#define DO_TEST_UPDATE_LIVE(file, dev, fial, kep, ...)                       \
    DO_TEST(file, UPDATE, dev, false, fial, kep,                             \
        VIR_DOMAIN_AFFECT_LIVE, __VA_ARGS__)


#define DO_TEST_ATTACH_CONFIG(file, dev, fial, kep, ...)                     \
    DO_TEST(file, ATTACH, dev, false, fial, kep,                             \
        VIR_DOMAIN_AFFECT_CONFIG, __VA_ARGS__)

#define DO_TEST_DETACH_CONFIG(file, dev, fial, kep, ...)                     \
    DO_TEST(file, DETACH, dev, false, fial, kep,                             \
        VIR_DOMAIN_AFFECT_CONFIG, __VA_ARGS__)


#define QMP_OK      "{\"return\": {}}"
#define HMP(msg)    "{\"return\": \"" msg "\"}"
#define QOM_OK      "{ \"return\": []}"

#define QMP_DEVICE_DELETED(dev) \
    "{"                                                     \
    "    \"timestamp\": {"                                  \
    "        \"seconds\": 1374137171,"                      \
    "        \"microseconds\": 2659"                        \
    "    },"                                                \
    "    \"event\": \"DEVICE_DELETED\","                    \
    "    \"data\": {"                                       \
    "        \"device\": \"" dev "\","                      \
    "        \"path\": \"/machine/peripheral/" dev "\""     \
    "    }"                                                 \
    "}\r\n"

    DO_TEST_UPDATE_LIVE("graphics-spice", "graphics-spice-nochange", false, false, NULL);
    DO_TEST_UPDATE_LIVE("graphics-spice-timeout", "graphics-spice-timeout-nochange", false, false,
                        "set_password", QMP_OK, "expire_password", QMP_OK);
    DO_TEST_UPDATE_LIVE("graphics-spice-timeout", "graphics-spice-timeout-password", false, false,
                        "set_password", QMP_OK, "expire_password", QMP_OK);
    DO_TEST_UPDATE_LIVE("graphics-spice", "graphics-spice-listen", true, false, NULL);
    DO_TEST_UPDATE_LIVE("graphics-spice-listen-network", "graphics-spice-listen-network-password", false, false,
                        "set_password", QMP_OK, "expire_password", QMP_OK);
    /* Strange huh? Currently, only graphics can be updated :-P */
    DO_TEST_UPDATE_LIVE("disk-cdrom", "disk-cdrom-nochange", true, false, NULL);

    DO_TEST_ATTACH_LIVE("console-compat-2-live", "console-virtio", false, true,
                        "chardev-add", "{\"return\": {\"pty\": \"/dev/pts/26\"}}",
                        "device_add", QMP_OK);

    DO_TEST_DETACH_LIVE("console-compat-2-live", "console-virtio", false, false,
                        "device_del", QMP_OK,
                        "chardev-remove", QMP_OK);

    DO_TEST_ATTACH_LIVE("base-live", "disk-virtio", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-virtio", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_EVENT_LIVE("base-live", "disk-virtio", false, true,
                              "human-monitor-command", HMP("OK\\r\\n"),
                              "device_add", QMP_OK,
                              "qom-list", QOM_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-virtio", true, true,
                        "device_del", QMP_OK,
                        "qom-list", QOM_OK,
                        "human-monitor-command", HMP(""));
    DO_TEST_DETACH_LIVE("base-live", "disk-virtio", false, false,
                        "device_del", QMP_DEVICE_DELETED("virtio-disk4") QMP_OK,
                        "human-monitor-command", HMP(""),
                        "qom-list", QOM_OK);

    DO_TEST_ATTACH_LIVE("base-live", "disk-usb", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-usb", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_EVENT_LIVE("base-live", "disk-usb", false, true,
                              "human-monitor-command", HMP("OK\\r\\n"),
                              "device_add", QMP_OK,
                              "qom-list", QOM_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-usb", true, true,
                        "device_del", QMP_OK,
                        "qom-list", QOM_OK,
                        "human-monitor-command", HMP(""));
    DO_TEST_DETACH_LIVE("base-live", "disk-usb", false, false,
                        "device_del", QMP_DEVICE_DELETED("usb-disk16") QMP_OK,
                        "human-monitor-command", HMP(""),
                        "qom-list", QOM_OK);

    DO_TEST_ATTACH_LIVE("base-live", "disk-scsi", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-scsi", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_EVENT_LIVE("base-live", "disk-scsi", false, true,
                              "human-monitor-command", HMP("OK\\r\\n"),
                              "device_add", QMP_OK,
                              "qom-list", QOM_OK);
    DO_TEST_DETACH_LIVE("base-live", "disk-scsi", true, true,
                        "device_del", QMP_OK,
                        "qom-list", QOM_OK,
                        "human-monitor-command", HMP(""));
    DO_TEST_DETACH_LIVE("base-live", "disk-scsi", false, false,
                        "device_del", QMP_DEVICE_DELETED("scsi0-0-0-5") QMP_OK,
                        "human-monitor-command", HMP(""),
                        "qom-list", QOM_OK);

    DO_TEST_ATTACH_LIVE("base-without-scsi-controller-live", "disk-scsi-2", false, true,
                        /* Four controllers added */
                        "device_add", QMP_OK,
                        "device_add", QMP_OK,
                        "device_add", QMP_OK,
                        "device_add", QMP_OK,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        /* Disk added */
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-with-scsi-controller-live", "disk-scsi-2", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_EVENT_LIVE("base-without-scsi-controller-live", "disk-scsi-2", false, true,
                              /* Four controllers added */
                              "device_add", QMP_OK,
                              "device_add", QMP_OK,
                              "device_add", QMP_OK,
                              "device_add", QMP_OK,
                              "human-monitor-command", HMP("OK\\r\\n"),
                              /* Disk added */
                              "device_add", QMP_OK,
                              "qom-list", QOM_OK);
    DO_TEST_DETACH_LIVE("base-with-scsi-controller-live", "disk-scsi-2", true, true,
                        "device_del", QMP_OK,
                        "qom-list", QOM_OK,
                        "human-monitor-command", HMP(""));
    DO_TEST_DETACH_LIVE("base-with-scsi-controller-live", "disk-scsi-2", false, false,
                        "device_del", QMP_DEVICE_DELETED("scsi3-0-5-7") QMP_OK,
                        "human-monitor-command", HMP(""),
                        "qom-list", QOM_OK);

    DO_TEST_ATTACH_LIVE("base-live", "qemu-agent", false, true,
                        "chardev-add", QMP_OK,
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-live", "qemu-agent-detach", false, false,
                        "device_del", QMP_OK,
                        "chardev-remove", QMP_OK);

    DO_TEST_ATTACH_LIVE("base-ccw-live", "ccw-virtio", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);
    DO_TEST_DETACH_LIVE("base-ccw-live", "ccw-virtio", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_LIVE("base-ccw-live-with-ccw-virtio", "ccw-virtio-2", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);

    DO_TEST_DETACH_LIVE("base-ccw-live-with-ccw-virtio", "ccw-virtio-2", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_LIVE("base-ccw-live-with-ccw-virtio", "ccw-virtio-2-explicit", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);

    DO_TEST_DETACH_LIVE("base-ccw-live-with-ccw-virtio", "ccw-virtio-2-explicit", false, false,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    /* Attach a second device, then detach the first one. Then attach the first one again. */
    DO_TEST_ATTACH_LIVE("base-ccw-live-with-ccw-virtio", "ccw-virtio-2-explicit", false, true,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);

    DO_TEST_DETACH_LIVE("base-ccw-live-with-2-ccw-virtio", "ccw-virtio-1-explicit", false, true,
                        "device_del", QMP_OK,
                        "human-monitor-command", HMP(""));

    DO_TEST_ATTACH_LIVE("base-ccw-live-with-2-ccw-virtio", "ccw-virtio-1-reverse", false, false,
                        "human-monitor-command", HMP("OK\\r\\n"),
                        "device_add", QMP_OK);

    qemuTestDriverFree(&driver);
    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIRT_TEST_MAIN(mymain)
