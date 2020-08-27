/*
 * Copyright Intel Corp. 2020
 *
 * ch_driver.h: header file for Cloud-Hypervisor driver functions
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

#include "ch_conf.h"
#include "ch_domain.h"
#include "ch_driver.h"
#include "ch_monitor.h"
#include "ch_process.h"
#include "datatypes.h"
#include "driver.h"
#include "viraccessapicheck.h"
#include "viralloc.h"
#include "virbuffer.h"
#include "vircommand.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virnetdevtap.h"
#include "virobject.h"
#include "virstring.h"
#include "virtypedparam.h"
#include "viruri.h"
#include "virutil.h"
#include "viruuid.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_driver");

static int chStateInitialize(bool privileged,
                             const char *root,
                             virStateInhibitCallback callback,
                             void *opaque);
static int chStateCleanup(void);
virCHDriverPtr ch_driver = NULL;

static virDomainObjPtr
chDomObjFromDomain(virDomainPtr domain)
{
    virDomainObjPtr vm;
    virCHDriverPtr driver = domain->conn->privateData;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    vm = virDomainObjListFindByUUID(driver->domains, domain->uuid);
    if (!vm) {
        virUUIDFormat(domain->uuid, uuidstr);
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching uuid '%s' (%s)"),
                       uuidstr, domain->name);
        return NULL;
    }

    return vm;
}

/* Functions */
static int
chConnectURIProbe(char **uri)
{
    if (ch_driver == NULL)
        return 0;

    *uri = g_strdup("ch:///system");
    return 1;
}

static virDrvOpenStatus chConnectOpen(virConnectPtr conn,
                                      virConnectAuthPtr auth G_GNUC_UNUSED,
                                      virConfPtr conf G_GNUC_UNUSED,
                                      unsigned int flags)
{
    virCheckFlags(VIR_CONNECT_RO, VIR_DRV_OPEN_ERROR);

    /* URI was good, but driver isn't active */
    if (ch_driver == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("Cloud-Hypervisor state driver is not active"));
        return VIR_DRV_OPEN_ERROR;
    }

    if (virConnectOpenEnsureACL(conn) < 0)
        return VIR_DRV_OPEN_ERROR;

    conn->privateData = ch_driver;

    return VIR_DRV_OPEN_SUCCESS;
}

static int chConnectClose(virConnectPtr conn)
{
    conn->privateData = NULL;
    return 0;
}

static const char *chConnectGetType(virConnectPtr conn)
{
    if (virConnectGetTypeEnsureACL(conn) < 0)
        return NULL;

    return "CH";
}

static int chConnectGetVersion(virConnectPtr conn,
                               unsigned long *version)
{
    virCHDriverPtr driver = conn->privateData;

    if (virConnectGetVersionEnsureACL(conn) < 0)
        return -1;

    chDriverLock(driver);
    *version = driver->version;
    chDriverUnlock(driver);
    return 0;
}

static char *chConnectGetHostname(virConnectPtr conn)
{
    if (virConnectGetHostnameEnsureACL(conn) < 0)
        return NULL;

    return virGetHostname();
}

static int chConnectNumOfDomains(virConnectPtr conn)
{
    virCHDriverPtr driver = conn->privateData;

    if (virConnectNumOfDomainsEnsureACL(conn) < 0)
        return -1;

    return virDomainObjListNumOfDomains(driver->domains, true,
                                        virConnectNumOfDomainsCheckACL, conn);
}

static int chConnectListDomains(virConnectPtr conn, int *ids, int nids)
{
    virCHDriverPtr driver = conn->privateData;

    if (virConnectListDomainsEnsureACL(conn) < 0)
        return -1;

    return virDomainObjListGetActiveIDs(driver->domains, ids, nids,
                                     virConnectListDomainsCheckACL, conn);
}

static int
chConnectListAllDomains(virConnectPtr conn,
                        virDomainPtr **domains,
                        unsigned int flags)
{
    virCHDriverPtr driver = conn->privateData;

    virCheckFlags(VIR_CONNECT_LIST_DOMAINS_FILTERS_ALL, -1);

    if (virConnectListAllDomainsEnsureACL(conn) < 0)
        return -1;

    return virDomainObjListExport(driver->domains, conn, domains,
                                 virConnectListAllDomainsCheckACL, flags);
}

static int chNodeGetInfo(virConnectPtr conn,
                         virNodeInfoPtr nodeinfo)
{
    if (virNodeGetInfoEnsureACL(conn) < 0)
        return -1;

    return virCapabilitiesGetNodeInfo(nodeinfo);
}

static char *chConnectGetCapabilities(virConnectPtr conn)
{
    virCHDriverPtr driver = conn->privateData;
    virCapsPtr caps;
    char *xml;

    if (virConnectGetCapabilitiesEnsureACL(conn) < 0)
        return NULL;

    if (!(caps = virCHDriverGetCapabilities(driver, true)))
        return NULL;

    xml = virCapabilitiesFormatXML(caps);

    virObjectUnref(caps);
    return xml;
}

/**
 * chDomainCreateXML:
 * @conn: pointer to connection
 * @xml: XML definition of domain
 * @flags: bitwise-OR of supported virDomainCreateFlags
 *
 * Creates a domain based on xml and starts it
 *
 * Returns a new domain object or NULL in case of failure.
 */
static virDomainPtr
chDomainCreateXML(virConnectPtr conn,
                           const char *xml,
                           unsigned int flags)
{
    virCHDriverPtr driver = conn->privateData;
    virDomainDefPtr vmdef = NULL;
    virDomainObjPtr vm = NULL;
    virDomainPtr dom = NULL;
    unsigned int parse_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;

    virCheckFlags(VIR_DOMAIN_START_VALIDATE, NULL);

    if (flags & VIR_DOMAIN_START_VALIDATE)
        parse_flags |= VIR_DOMAIN_DEF_PARSE_VALIDATE_SCHEMA;


    if ((vmdef = virDomainDefParseString(xml, driver->xmlopt,
                                         NULL, parse_flags)) == NULL)
        goto cleanup;

    if (virDomainCreateXMLEnsureACL(conn, vmdef) < 0)
        goto cleanup;

    if (!(vm = virDomainObjListAdd(driver->domains,
                                   vmdef,
                                   driver->xmlopt,
                                   VIR_DOMAIN_OBJ_LIST_ADD_LIVE |
                                       VIR_DOMAIN_OBJ_LIST_ADD_CHECK_LIVE,
                                   NULL)))
        goto cleanup;

    vmdef = NULL;
    vm->persistent = 1;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    if (virCHProcessStart(driver, vm, VIR_DOMAIN_RUNNING_BOOTED) < 0)
        goto cleanup;

    dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainDefFree(vmdef);
    virDomainObjEndAPI(&vm);
    chDriverUnlock(driver);
    return dom;
}

static int
chDomainCreateWithFlags(virDomainPtr dom, unsigned int flags)
{
    virCHDriverPtr driver = dom->conn->privateData;
    virDomainObjPtr vm;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainCreateWithFlagsEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    ret = virCHProcessStart(driver, vm, VIR_DOMAIN_RUNNING_BOOTED);

    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainCreate(virDomainPtr dom)
{
    return chDomainCreateWithFlags(dom, 0);
}

static virDomainPtr
chDomainDefineXMLFlags(virConnectPtr conn, const char *xml, unsigned int flags)
{
    virCHDriverPtr driver = conn->privateData;
    virDomainDefPtr vmdef = NULL;
    virDomainObjPtr vm = NULL;
    virDomainPtr dom = NULL;
    unsigned int parse_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;

    virCheckFlags(VIR_DOMAIN_DEFINE_VALIDATE, NULL);

    if (flags & VIR_DOMAIN_START_VALIDATE)
        parse_flags |= VIR_DOMAIN_DEF_PARSE_VALIDATE_SCHEMA;

    if ((vmdef = virDomainDefParseString(xml, driver->xmlopt,
                                         NULL, parse_flags)) == NULL)
        goto cleanup;

    if (virXMLCheckIllegalChars("name", vmdef->name, "\n") < 0)
        goto cleanup;

    if (virDomainDefineXMLFlagsEnsureACL(conn, vmdef) < 0)
        goto cleanup;

    if (!(vm = virDomainObjListAdd(driver->domains, vmdef,
                                   driver->xmlopt,
                                   0, NULL)))
        goto cleanup;

    vmdef = NULL;
    vm->persistent = 1;

    dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

 cleanup:
    virDomainDefFree(vmdef);
    virDomainObjEndAPI(&vm);
    return dom;
}

static virDomainPtr
chDomainDefineXML(virConnectPtr conn, const char *xml)
{
    return chDomainDefineXMLFlags(conn, xml, 0);
}

static int
chDomainUndefineFlags(virDomainPtr dom,
                      unsigned int flags)
{
    virCHDriverPtr driver = dom->conn->privateData;
    virDomainObjPtr vm;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainUndefineFlagsEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    if (!vm->persistent) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       "%s", _("Cannot undefine transient domain"));
        goto cleanup;
    }

    if (virDomainObjIsActive(vm)) {
        vm->persistent = 0;
    } else {
        virDomainObjListRemove(driver->domains, vm);
    }

    ret = 0;

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainUndefine(virDomainPtr dom)
{
    return chDomainUndefineFlags(dom, 0);
}

static int chDomainIsActive(virDomainPtr dom)
{
    virCHDriverPtr driver = dom->conn->privateData;
    virDomainObjPtr vm;
    int ret = -1;

    chDriverLock(driver);
    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainIsActiveEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    ret = virDomainObjIsActive(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    chDriverUnlock(driver);
    return ret;
}

static int
chDomainShutdownFlags(virDomainPtr dom,
                      unsigned int flags)
{
    virCHDomainObjPrivatePtr priv;
    virDomainObjPtr vm;
    virDomainState state;
    int ret = -1;

    virCheckFlags(VIR_DOMAIN_SHUTDOWN_INITCTL |
                  VIR_DOMAIN_SHUTDOWN_SIGNAL, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    priv = vm->privateData;

    if (virDomainShutdownFlagsEnsureACL(dom->conn, vm->def, flags) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    if (virDomainObjCheckActive(vm) < 0)
        goto endjob;

    state = virDomainObjGetState(vm, NULL);
    if (state != VIR_DOMAIN_RUNNING && state != VIR_DOMAIN_PAUSED) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("only can shutdown running/paused domain"));
        goto endjob;
    } else {
        if (virCHMonitorShutdownVM(priv->monitor) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                        _("failed to shutdown guest VM"));
            goto endjob;
        }
    }

    virDomainObjSetState(vm, VIR_DOMAIN_SHUTDOWN, VIR_DOMAIN_SHUTDOWN_USER);

    ret = 0;

 endjob:
    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainShutdown(virDomainPtr dom)
{
    return chDomainShutdownFlags(dom, 0);
}


static int
chDomainReboot(virDomainPtr dom, unsigned int flags)
{
    virCHDomainObjPrivatePtr priv;
    virDomainObjPtr vm;
    virDomainState state;
    int ret = -1;

    virCheckFlags(VIR_DOMAIN_REBOOT_INITCTL |
                  VIR_DOMAIN_REBOOT_SIGNAL, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    priv = vm->privateData;

    if (virDomainRebootEnsureACL(dom->conn, vm->def, flags) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    if (virDomainObjCheckActive(vm) < 0)
        goto endjob;

    state = virDomainObjGetState(vm, NULL);
    if (state != VIR_DOMAIN_RUNNING && state != VIR_DOMAIN_PAUSED) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("only can reboot running/paused domain"));
        goto endjob;
    } else {
        if (virCHMonitorRebootVM(priv->monitor) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("failed to reboot domain"));
            goto endjob;
        }
    }

    if (state == VIR_DOMAIN_RUNNING)
        virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_BOOTED);
    else
        virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_UNPAUSED);

    ret = 0;

 endjob:
    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainSuspend(virDomainPtr dom)
{
    virCHDomainObjPrivatePtr priv;
    virDomainObjPtr vm;
    int ret = -1;

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    priv = vm->privateData;

    if (virDomainSuspendEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    if (virDomainObjCheckActive(vm) < 0)
        goto endjob;

    if (virDomainObjGetState(vm, NULL) != VIR_DOMAIN_RUNNING) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("only can suspend running domain"));
        goto endjob;
    } else {
        if (virCHMonitorSuspendVM(priv->monitor) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                        _("failed to suspend domain"));
            goto endjob;
        }
    }

    virDomainObjSetState(vm, VIR_DOMAIN_PAUSED, VIR_DOMAIN_PAUSED_USER);

    ret = 0;

 endjob:
    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainResume(virDomainPtr dom)
{
    virCHDomainObjPrivatePtr priv;
    virDomainObjPtr vm;
    int ret = -1;

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    priv = vm->privateData;

    if (virDomainResumeEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) < 0)
        goto cleanup;

    if (virDomainObjCheckActive(vm) < 0)
        goto endjob;

    if (virDomainObjGetState(vm, NULL) != VIR_DOMAIN_PAUSED) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("only can resume paused domain"));
        goto endjob;
    } else {
        if (virCHMonitorResumeVM(priv->monitor) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                        _("failed to resume domain"));
            goto endjob;
        }
    }

    virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_UNPAUSED);

    ret = 0;

 endjob:
    virCHDomainObjEndJob(vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

/**
 * chDomainDestroyFlags:
 * @dom: pointer to domain to destroy
 * @flags: extra flags; not used yet.
 *
 * Sends SIGKILL to Cloud-Hypervisor process to terminate it
 *
 * Returns 0 on success or -1 in case of error
 */
static int
chDomainDestroyFlags(virDomainPtr dom, unsigned int flags)
{
    virCHDriverPtr driver = dom->conn->privateData;
    virDomainObjPtr vm;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainDestroyFlagsEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, CH_JOB_DESTROY) < 0)
        goto cleanup;

    if (virDomainObjCheckActive(vm) < 0)
        goto endjob;

    ret = virCHProcessStop(driver, vm, VIR_DOMAIN_SHUTOFF_DESTROYED);

 endjob:
    virCHDomainObjEndJob(vm);
    if (!vm->persistent)
        virDomainObjListRemove(driver->domains, vm);

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int
chDomainDestroy(virDomainPtr dom)
{
    return chDomainDestroyFlags(dom, 0);
}

static virDomainPtr chDomainLookupByID(virConnectPtr conn,
                                       int id)
{
    virCHDriverPtr driver = conn->privateData;
    virDomainObjPtr vm;
    virDomainPtr dom = NULL;

    chDriverLock(driver);
    vm = virDomainObjListFindByID(driver->domains, id);
    chDriverUnlock(driver);

    if (!vm) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching id '%d'"), id);
        goto cleanup;
    }

    if (virDomainLookupByIDEnsureACL(conn, vm->def) < 0)
        goto cleanup;

    dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

 cleanup:
    virDomainObjEndAPI(&vm);
    return dom;
}

static virDomainPtr chDomainLookupByName(virConnectPtr conn,
                                         const char *name)
{
    virCHDriverPtr driver = conn->privateData;
    virDomainObjPtr vm;
    virDomainPtr dom = NULL;

    chDriverLock(driver);
    vm = virDomainObjListFindByName(driver->domains, name);
    chDriverUnlock(driver);

    if (!vm) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching name '%s'"), name);
        goto cleanup;
    }

    if (virDomainLookupByNameEnsureACL(conn, vm->def) < 0)
        goto cleanup;

    dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

 cleanup:
    virDomainObjEndAPI(&vm);
    return dom;
}

static virDomainPtr chDomainLookupByUUID(virConnectPtr conn,
                                         const unsigned char *uuid)
{
    virCHDriverPtr driver = conn->privateData;
    virDomainObjPtr vm;
    virDomainPtr dom = NULL;

    chDriverLock(driver);
    vm = virDomainObjListFindByUUID(driver->domains, uuid);
    chDriverUnlock(driver);

    if (!vm) {
        char uuidstr[VIR_UUID_STRING_BUFLEN];
        virUUIDFormat(uuid, uuidstr);
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching uuid '%s'"), uuidstr);
        goto cleanup;
    }

    if (virDomainLookupByUUIDEnsureACL(conn, vm->def) < 0)
        goto cleanup;

    dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

 cleanup:
    virDomainObjEndAPI(&vm);
    return dom;
}

static int
chDomainGetState(virDomainPtr dom,
                 int *state,
                 int *reason,
                 unsigned int flags)
{
    virDomainObjPtr vm;
    int ret = -1;

    virCheckFlags(0, -1);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainGetStateEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    *state = virDomainObjGetState(vm, reason);
    ret = 0;

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static char *chDomainGetXMLDesc(virDomainPtr dom,
                                unsigned int flags)
{
    virCHDriverPtr driver = dom->conn->privateData;
    virDomainObjPtr vm;
    char *ret = NULL;

    virCheckFlags(VIR_DOMAIN_XML_COMMON_FLAGS, NULL);

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainGetXMLDescEnsureACL(dom->conn, vm->def, flags) < 0)
        goto cleanup;

    ret = virDomainDefFormat(vm->def, driver->xmlopt,
                             virDomainDefFormatConvertXMLFlags(flags));

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int chDomainGetInfo(virDomainPtr dom,
                           virDomainInfoPtr info)
{
    virDomainObjPtr vm;
    int ret = -1;

    if (!(vm = chDomObjFromDomain(dom)))
        goto cleanup;

    if (virDomainGetInfoEnsureACL(dom->conn, vm->def) < 0)
        goto cleanup;

    info->state = virDomainObjGetState(vm, NULL);

    info->cpuTime = 0;

    info->maxMem = virDomainDefGetMemoryTotal(vm->def);
    info->memory = vm->def->mem.cur_balloon;
    info->nrVirtCpu = virDomainDefGetVcpus(vm->def);

    ret = 0;

 cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

static int chStateCleanup(void)
{
    if (ch_driver == NULL)
        return -1;

    virObjectUnref(ch_driver->domains);
    virObjectUnref(ch_driver->xmlopt);
    virObjectUnref(ch_driver->caps);
    virObjectUnref(ch_driver->config);
    virMutexDestroy(&ch_driver->lock);
    VIR_FREE(ch_driver);

    return 0;
}

static int chStateInitialize(bool privileged,
                             const char *root,
                             virStateInhibitCallback callback G_GNUC_UNUSED,
                             void *opaque G_GNUC_UNUSED)
{
    if (root != NULL) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Driver does not support embedded mode"));
        return -1;
    }

    /* Check that the user is root, silently disable if not */
    if (!privileged) {
        VIR_INFO("Not running privileged, disabling driver");
        return VIR_DRV_STATE_INIT_SKIPPED;
    }

    if (VIR_ALLOC(ch_driver) < 0)
        return VIR_DRV_STATE_INIT_ERROR;

    if (virMutexInit(&ch_driver->lock) < 0) {
        VIR_FREE(ch_driver);
        return VIR_DRV_STATE_INIT_ERROR;
    }

    if (!(ch_driver->domains = virDomainObjListNew()))
        goto cleanup;

    if (!(ch_driver->caps = virCHDriverCapsInit()))
        goto cleanup;

    if (!(ch_driver->xmlopt = chDomainXMLConfInit(ch_driver)))
        goto cleanup;

    if (!(ch_driver->config = virCHDriverConfigNew()))
        goto cleanup;

    if (chExtractVersion(ch_driver) < 0)
        goto cleanup;

    return VIR_DRV_STATE_INIT_COMPLETE;

 cleanup:
    chStateCleanup();
    return VIR_DRV_STATE_INIT_ERROR;
}

/* Function Tables */
static virHypervisorDriver chHypervisorDriver = {
    .name = "CH",
    .connectURIProbe = chConnectURIProbe,
    .connectOpen = chConnectOpen,                           /* 6.7.0 */
    .connectClose = chConnectClose,                         /* 6.7.0 */
    .connectGetType = chConnectGetType,                     /* 6.7.0 */
    .connectGetVersion = chConnectGetVersion,               /* 6.7.0 */
    .connectGetHostname = chConnectGetHostname,             /* 6.7.0 */
    .connectNumOfDomains = chConnectNumOfDomains,           /* 6.7.0 */
    .connectListAllDomains = chConnectListAllDomains,       /* 6.7.0 */
    .connectListDomains = chConnectListDomains,             /* 6.7.0 */
    .connectGetCapabilities = chConnectGetCapabilities,     /* 6.7.0 */
    .domainCreateXML = chDomainCreateXML,                   /* 6.7.0 */
    .domainCreate = chDomainCreate,                         /* 6.7.0 */
    .domainCreateWithFlags = chDomainCreateWithFlags,       /* 6.7.0 */
    .domainShutdown = chDomainShutdown,                     /* 6.7.0 */
    .domainShutdownFlags = chDomainShutdownFlags,           /* 6.7.0 */
    .domainReboot = chDomainReboot,                         /* 6.7.0 */
    .domainSuspend = chDomainSuspend,                       /* 6.7.0 */
    .domainResume = chDomainResume,                         /* 6.7.0 */
    .domainDestroy = chDomainDestroy,                       /* 6.7.0 */
    .domainDestroyFlags = chDomainDestroyFlags,             /* 6.7.0 */
    .domainDefineXML = chDomainDefineXML,                   /* 6.7.0 */
    .domainDefineXMLFlags = chDomainDefineXMLFlags,         /* 6.7.0 */
    .domainUndefine = chDomainUndefine,                     /* 6.7.0 */
    .domainUndefineFlags = chDomainUndefineFlags,           /* 6.7.0 */
    .domainLookupByID = chDomainLookupByID,                 /* 6.7.0 */
    .domainLookupByUUID = chDomainLookupByUUID,             /* 6.7.0 */
    .domainLookupByName = chDomainLookupByName,             /* 6.7.0 */
    .domainGetState = chDomainGetState,                     /* 6.7.0 */
    .domainGetXMLDesc = chDomainGetXMLDesc,                 /* 6.7.0 */
    .domainGetInfo = chDomainGetInfo,                       /* 6.7.0 */
    .domainIsActive = chDomainIsActive,                     /* 6.7.0 */
    .nodeGetInfo = chNodeGetInfo,                           /* 6.7.0 */
};

static virConnectDriver chConnectDriver = {
    .localOnly = true,
    .uriSchemes = (const char *[]){"CH", "Ch", "ch", "Cloud-Hypervisor", NULL},
    .hypervisorDriver = &chHypervisorDriver,
};

static virStateDriver chStateDriver = {
    .name = "CH",
    .stateInitialize = chStateInitialize,
    .stateCleanup = chStateCleanup,
};

int chRegister(void)
{
    if (virRegisterConnectDriver(&chConnectDriver, false) < 0)
        return -1;
    if (virRegisterStateDriver(&chStateDriver) < 0)
        return -1;
    return 0;
}
