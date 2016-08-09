/*
 * hyperv_driver.c: core driver functions for managing Microsoft Hyper-V hosts
 *
 * Copyright (C) 2011-2013 Matthias Bolte <matthias.bolte@googlemail.com>
 * Copyright (C) 2009 Michael Sievers <msievers83@googlemail.com>
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

#include "internal.h"
#include "datatypes.h"
#include "virdomainobjlist.h"
#include "virauth.h"
#include "viralloc.h"
#include "virlog.h"
#include "viruuid.h"
#include "hyperv_driver.h"
#include "hyperv_network_driver.h"
#include "hyperv_private.h"
#include "hyperv_util.h"
#include "hyperv_wmi.h"
#include "openwsman.h"
#include "virstring.h"
#include "virtypedparam.h"

#define VIR_FROM_THIS VIR_FROM_HYPERV

VIR_LOG_INIT("hyperv.hyperv_driver");

static void
hypervFreePrivate(hypervPrivate **priv)
{
    if (priv == NULL || *priv == NULL)
        return;

    if ((*priv)->client != NULL) {
        /* FIXME: This leaks memory due to bugs in openwsman <= 2.2.6 */
        wsmc_release((*priv)->client);
    }

    if ((*priv)->caps != NULL)
        virObjectUnref((*priv)->caps);

    if ((*priv)->xmlopt != NULL)
        virObjectUnref((*priv)->xmlopt);

    hypervFreeParsedUri(&(*priv)->parsedUri);
    VIR_FREE(*priv);
}

/* Forward declaration of hypervCapsInit */
static virCapsPtr hypervCapsInit(hypervPrivate *priv);

static virDrvOpenStatus
hypervConnectOpen(virConnectPtr conn, virConnectAuthPtr auth,
                  virConfPtr conf ATTRIBUTE_UNUSED,
                  unsigned int flags)
{
    virDrvOpenStatus result = VIR_DRV_OPEN_ERROR;
    char *plus;
    hypervPrivate *priv = NULL;
    char *username = NULL;
    char *password = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(VIR_CONNECT_RO, VIR_DRV_OPEN_ERROR);

    /* Decline if the URI is NULL or the scheme is NULL */
    if (conn->uri == NULL || conn->uri->scheme == NULL)
        return VIR_DRV_OPEN_DECLINED;

    /* Decline if the scheme is not hyperv */
    plus = strchr(conn->uri->scheme, '+');

    if (plus == NULL) {
        if (STRCASENEQ(conn->uri->scheme, "hyperv"))
            return VIR_DRV_OPEN_DECLINED;
    } else {
        if (plus - conn->uri->scheme != 6 ||
            STRCASENEQLEN(conn->uri->scheme, "hyperv", 6)) {
            return VIR_DRV_OPEN_DECLINED;
        }

        virReportError(VIR_ERR_INVALID_ARG,
                       _("Transport '%s' in URI scheme is not supported, try again "
                         "without the transport part"), plus + 1);
        return VIR_DRV_OPEN_ERROR;
    }

    /* Require server part */
    if (conn->uri->server == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("URI is missing the server part"));
        return VIR_DRV_OPEN_ERROR;
    }

    /* Require auth */
    if (auth == NULL || auth->cb == NULL) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Missing or invalid auth pointer"));
        return VIR_DRV_OPEN_ERROR;
    }

    /* Allocate per-connection private data */
    if (VIR_ALLOC(priv) < 0)
        goto cleanup;

    if (hypervParseUri(&priv->parsedUri, conn->uri) < 0)
        goto cleanup;

    /* Set the port dependent on the transport protocol if no port is
     * specified. This allows us to rely on the port parameter being
     * correctly set when building URIs later on, without the need to
     * distinguish between the situations port == 0 and port != 0 */
    if (conn->uri->port == 0) {
        if (STRCASEEQ(priv->parsedUri->transport, "https")) {
            conn->uri->port = 5986;
        } else {
            conn->uri->port = 5985;
        }
    }

    /* Request credentials */
    if (conn->uri->user != NULL) {
        if (VIR_STRDUP(username, conn->uri->user) < 0)
            goto cleanup;
    } else {
        username = virAuthGetUsername(conn, auth, "hyperv", "administrator", conn->uri->server);

        if (username == NULL) {
            virReportError(VIR_ERR_AUTH_FAILED, "%s", _("Username request failed"));
            goto cleanup;
        }
    }

    password = virAuthGetPassword(conn, auth, "hyperv", username, conn->uri->server);

    if (password == NULL) {
        virReportError(VIR_ERR_AUTH_FAILED, "%s", _("Password request failed"));
        goto cleanup;
    }

    /* Initialize the openwsman connection */
    priv->client = wsmc_create(conn->uri->server, conn->uri->port, "/wsman",
                               priv->parsedUri->transport, username, password);

    if (priv->client == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not create openwsman client"));
        goto cleanup;
    }

    if (wsmc_transport_init(priv->client, NULL) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not initialize openwsman transport"));
        goto cleanup;
    }

    /* FIXME: Currently only basic authentication is supported  */
    wsman_transport_set_auth_method(priv->client, "basic");

    /* Check if the connection can be established and if the server has the
     * Hyper-V role installed. If the call to hypervGetMsvmComputerSystemList
     * succeeds than the connection has been established. If the returned list
     * is empty than the server isn't a Hyper-V server. */
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_PHYSICAL);

    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("%s is not a Hyper-V server"), conn->uri->server);
        goto cleanup;
    }

    /* Setup capabilities */
    priv->caps = hypervCapsInit(priv);
    if (priv->caps == NULL) {
        goto cleanup;
    }

    /* Init xmlopt to parse Domain XML */
    priv->xmlopt = virDomainXMLOptionNew(NULL, NULL, NULL);

    conn->privateData = priv;
    priv = NULL;
    result = VIR_DRV_OPEN_SUCCESS;

 cleanup:
    hypervFreePrivate(&priv);
    VIR_FREE(username);
    VIR_FREE(password);
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervConnectClose(virConnectPtr conn)
{
    hypervPrivate *priv = conn->privateData;

    hypervFreePrivate(&priv);

    conn->privateData = NULL;

    return 0;
}



static const char *
hypervConnectGetType(virConnectPtr conn ATTRIBUTE_UNUSED)
{
    return "Hyper-V";
}



static char *
hypervConnectGetHostname(virConnectPtr conn)
{
    char *hostname = NULL;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Win32_ComputerSystem *computerSystem = NULL;

    virBufferAddLit(&query, WIN32_COMPUTERSYSTEM_WQL_SELECT);

    if (hypervGetWin32ComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s"),
                       "Win32_ComputerSystem");
        goto cleanup;
    }

    ignore_value(VIR_STRDUP(hostname, computerSystem->data->DNSHostName));

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return hostname;
}



static int
hypervNodeGetInfo(virConnectPtr conn, virNodeInfoPtr info)
{
    int result = -1;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Win32_ComputerSystem *computerSystem = NULL;
    Win32_Processor *processorList = NULL;
    Win32_Processor *processor = NULL;
    char *tmp;

    memset(info, 0, sizeof(*info));

    virBufferAddLit(&query, WIN32_COMPUTERSYSTEM_WQL_SELECT);

    /* Get Win32_ComputerSystem */
    if (hypervGetWin32ComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s"),
                       "Win32_ComputerSystem");
        goto cleanup;
    }

    /* Get Win32_Processor list */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Win32_ComputerSystem.Name=\"%s\"} "
                      "where AssocClass = Win32_ComputerSystemProcessor "
                      "ResultClass = Win32_Processor",
                      computerSystem->data->Name);

    if (hypervGetWin32ProcessorList(priv, &query, &processorList) < 0)
        goto cleanup;

    if (processorList == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s"),
                       "Win32_Processor");
        goto cleanup;
    }

    /* Strip the string to fit more relevant information in 32 chars */
    tmp = processorList->data->Name;

    while (*tmp != '\0') {
        if (STRPREFIX(tmp, "  ")) {
            memmove(tmp, tmp + 1, strlen(tmp + 1) + 1);
            continue;
        } else if (STRPREFIX(tmp, "(R)") || STRPREFIX(tmp, "(C)")) {
            memmove(tmp, tmp + 3, strlen(tmp + 3) + 1);
            continue;
        } else if (STRPREFIX(tmp, "(TM)")) {
            memmove(tmp, tmp + 4, strlen(tmp + 4) + 1);
            continue;
        }

        ++tmp;
    }

    /* Fill struct */
    if (virStrncpy(info->model, processorList->data->Name,
                   sizeof(info->model) - 1, sizeof(info->model)) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU model %s too long for destination"),
                       processorList->data->Name);
        goto cleanup;
    }

    info->memory = computerSystem->data->TotalPhysicalMemory / 1024; /* byte to kilobyte */
    info->mhz = processorList->data->MaxClockSpeed;
    info->nodes = 1;
    info->sockets = 0;

    for (processor = processorList; processor != NULL;
         processor = processor->next) {
        ++info->sockets;
    }

    info->cores = processorList->data->NumberOfCores;
    info->threads = info->cores / processorList->data->NumberOfLogicalProcessors;
    info->cpus = info->sockets * info->cores;

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    hypervFreeObject(priv, (hypervObject *)processorList);

    return result;
}



static int
hypervConnectListDomains(virConnectPtr conn, int *ids, int maxids)
{
    bool success = false;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystemList = NULL;
    Msvm_ComputerSystem *computerSystem = NULL;
    int count = 0;

    if (maxids == 0)
        return 0;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAddLit(&query, "and ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_ACTIVE);

    if (hypervGetMsvmComputerSystemList(priv, &query,
                                        &computerSystemList) < 0) {
        goto cleanup;
    }

    for (computerSystem = computerSystemList; computerSystem != NULL;
         computerSystem = computerSystem->next) {
        ids[count++] = computerSystem->data->ProcessID;

        if (count >= maxids)
            break;
    }

    success = true;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystemList);

    return success ? count : -1;
}



static int
hypervConnectNumOfDomains(virConnectPtr conn)
{
    bool success = false;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystemList = NULL;
    Msvm_ComputerSystem *computerSystem = NULL;
    int count = 0;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAddLit(&query, "and ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_ACTIVE);

    if (hypervGetMsvmComputerSystemList(priv, &query,
                                        &computerSystemList) < 0) {
        goto cleanup;
    }

    for (computerSystem = computerSystemList; computerSystem != NULL;
         computerSystem = computerSystem->next) {
        ++count;
    }

    success = true;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystemList);

    return success ? count : -1;
}



static virDomainPtr
hypervDomainLookupByID(virConnectPtr conn, int id)
{
    virDomainPtr domain = NULL;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAsprintf(&query, "and ProcessID = %d", id);

    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN, _("No domain with ID %d"), id);
        goto cleanup;
    }

    hypervMsvmComputerSystemToDomain(conn, computerSystem, &domain);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return domain;
}



static virDomainPtr
hypervDomainLookupByUUID(virConnectPtr conn, const unsigned char *uuid)
{
    virDomainPtr domain = NULL;
    hypervPrivate *priv = conn->privateData;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;

    virUUIDFormat(uuid, uuid_string);

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAsprintf(&query, "and Name = \"%s\"", uuid_string);

    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("No domain with UUID %s"), uuid_string);
        goto cleanup;
    }

    hypervMsvmComputerSystemToDomain(conn, computerSystem, &domain);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return domain;
}



static virDomainPtr
hypervDomainLookupByName(virConnectPtr conn, const char *name)
{
    virDomainPtr domain = NULL;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAsprintf(&query, "and ElementName = \"%s\"", name);

    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("No domain with name %s"), name);
        goto cleanup;
    }

    hypervMsvmComputerSystemToDomain(conn, computerSystem, &domain);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return domain;
}



static int
hypervDomainSuspend(virDomainPtr domain)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem->data->EnabledState !=
        MSVM_COMPUTERSYSTEM_ENABLEDSTATE_ENABLED) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is not active"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_PAUSED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainResume(virDomainPtr domain)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem->data->EnabledState !=
        MSVM_COMPUTERSYSTEM_ENABLEDSTATE_PAUSED) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is not paused"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_ENABLED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainDestroyFlags(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;
    bool in_transition = false;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (!hypervIsMsvmComputerSystemActive(computerSystem, &in_transition) ||
        in_transition) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is not active or is in state transition"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_DISABLED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainDestroy(virDomainPtr domain)
{
    return hypervDomainDestroyFlags(domain, 0);
}



static char *
hypervDomainGetOSType(virDomainPtr domain ATTRIBUTE_UNUSED)
{
    char *osType;

    ignore_value(VIR_STRDUP(osType, "hvm"));
    return osType;
}



static int
hypervDomainGetInfo(virDomainPtr domain, virDomainInfoPtr info)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_ProcessorSettingData *processorSettingData = NULL;
    Msvm_MemorySettingData *memorySettingData = NULL;

    memset(info, 0, sizeof(*info));

    virUUIDFormat(domain->uuid, uuid_string);

    /* Get Msvm_ComputerSystem */
    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query,
                                                  &virtualSystemSettingData) < 0) {
        goto cleanup;
    }

    if (virtualSystemSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_VirtualSystemSettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_ProcessorSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ProcessorSettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmProcessorSettingDataList(priv, &query,
                                              &processorSettingData) < 0) {
        goto cleanup;
    }

    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_ProcessorSettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_MemorySettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_MemorySettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmMemorySettingDataList(priv, &query,
                                           &memorySettingData) < 0) {
        goto cleanup;
    }


    if (memorySettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_MemorySettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Fill struct */
    info->state = hypervMsvmComputerSystemEnabledStateToDomainState(computerSystem);
    info->maxMem = memorySettingData->data->Limit * 1024; /* megabyte to kilobyte */
    info->memory = memorySettingData->data->VirtualQuantity * 1024; /* megabyte to kilobyte */
    info->nrVirtCpu = processorSettingData->data->VirtualQuantity;
    info->cpuTime = 0;

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)processorSettingData);
    hypervFreeObject(priv, (hypervObject *)memorySettingData);

    return result;
}



static int
hypervDomainGetState(virDomainPtr domain, int *state, int *reason,
                     unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    *state = hypervMsvmComputerSystemEnabledStateToDomainState(computerSystem);

    if (reason != NULL)
        *reason = 0;

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static char *
hypervDomainGetXMLDesc(virDomainPtr domain, unsigned int flags)
{
    char *xml = NULL;
    hypervPrivate *priv = domain->conn->privateData;
    virDomainDefPtr def = NULL;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_ProcessorSettingData *processorSettingData = NULL;
    Msvm_MemorySettingData *memorySettingData = NULL;

    /* Flags checked by virDomainDefFormat */

    if (!(def = virDomainDefNew()))
        goto cleanup;

    virUUIDFormat(domain->uuid, uuid_string);

    /* Get Msvm_ComputerSystem */
    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query,
                                                  &virtualSystemSettingData) < 0) {
        goto cleanup;
    }

    if (virtualSystemSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_VirtualSystemSettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_ProcessorSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ProcessorSettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmProcessorSettingDataList(priv, &query,
                                              &processorSettingData) < 0) {
        goto cleanup;
    }

    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_ProcessorSettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_MemorySettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_MemorySettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmMemorySettingDataList(priv, &query,
                                           &memorySettingData) < 0) {
        goto cleanup;
    }


    if (memorySettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_MemorySettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Fill struct */
    def->virtType = VIR_DOMAIN_VIRT_HYPERV;

    if (hypervIsMsvmComputerSystemActive(computerSystem, NULL)) {
        def->id = computerSystem->data->ProcessID;
    } else {
        def->id = -1;
    }

    if (virUUIDParse(computerSystem->data->Name, def->uuid) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not parse UUID from string '%s'"),
                       computerSystem->data->Name);
        return NULL;
    }

    if (VIR_STRDUP(def->name, computerSystem->data->ElementName) < 0)
        goto cleanup;

    if (VIR_STRDUP(def->description, virtualSystemSettingData->data->Notes) < 0)
        goto cleanup;

    virDomainDefSetMemoryTotal(def, memorySettingData->data->Limit * 1024); /* megabyte to kilobyte */
    def->mem.cur_balloon = memorySettingData->data->VirtualQuantity * 1024; /* megabyte to kilobyte */

    if (virDomainDefSetVcpusMax(def,
                                processorSettingData->data->VirtualQuantity,
                                NULL) < 0)
        goto cleanup;

    if (virDomainDefSetVcpus(def,
                             processorSettingData->data->VirtualQuantity) < 0)
        goto cleanup;

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;

    /* FIXME: devices section is totally missing */

    xml = virDomainDefFormat(def, NULL,
                             virDomainDefFormatConvertXMLFlags(flags));

 cleanup:
    virDomainDefFree(def);
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)processorSettingData);
    hypervFreeObject(priv, (hypervObject *)memorySettingData);

    return xml;
}



static int
hypervConnectListDefinedDomains(virConnectPtr conn, char **const names, int maxnames)
{
    bool success = false;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystemList = NULL;
    Msvm_ComputerSystem *computerSystem = NULL;
    int count = 0;
    size_t i;

    if (maxnames == 0)
        return 0;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAddLit(&query, "and ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_INACTIVE);

    if (hypervGetMsvmComputerSystemList(priv, &query,
                                        &computerSystemList) < 0) {
        goto cleanup;
    }

    for (computerSystem = computerSystemList; computerSystem != NULL;
         computerSystem = computerSystem->next) {
        if (VIR_STRDUP(names[count], computerSystem->data->ElementName) < 0)
            goto cleanup;

        ++count;

        if (count >= maxnames)
            break;
    }

    success = true;

 cleanup:
    if (!success) {
        for (i = 0; i < count; ++i)
            VIR_FREE(names[i]);

        count = -1;
    }

    hypervFreeObject(priv, (hypervObject *)computerSystemList);

    return count;
}



static int
hypervConnectNumOfDefinedDomains(virConnectPtr conn)
{
    bool success = false;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystemList = NULL;
    Msvm_ComputerSystem *computerSystem = NULL;
    int count = 0;

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);
    virBufferAddLit(&query, "and ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_INACTIVE);

    if (hypervGetMsvmComputerSystemList(priv, &query,
                                        &computerSystemList) < 0) {
        goto cleanup;
    }

    for (computerSystem = computerSystemList; computerSystem != NULL;
         computerSystem = computerSystem->next) {
        ++count;
    }

    success = true;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystemList);

    return success ? count : -1;
}



static int
hypervDomainCreateWithFlags(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (hypervIsMsvmComputerSystemActive(computerSystem, NULL)) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is already active or is in state transition"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_ENABLED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainCreate(virDomainPtr domain)
{
    return hypervDomainCreateWithFlags(domain, 0);
}



static int
hypervConnectIsEncrypted(virConnectPtr conn)
{
    hypervPrivate *priv = conn->privateData;

    if (STRCASEEQ(priv->parsedUri->transport, "https")) {
        return 1;
    } else {
        return 0;
    }
}



static int
hypervConnectIsSecure(virConnectPtr conn)
{
    hypervPrivate *priv = conn->privateData;

    if (STRCASEEQ(priv->parsedUri->transport, "https")) {
        return 1;
    } else {
        return 0;
    }
}



static int
hypervConnectIsAlive(virConnectPtr conn)
{
    hypervPrivate *priv = conn->privateData;

    /* XXX we should be able to do something better than this is simple, safe,
     * and good enough for now. In worst case, the function will return true
     * even though the connection is not alive.
     */
    if (priv->client)
        return 1;
    else
        return 0;
}



static int
hypervDomainIsActive(virDomainPtr domain)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    result = hypervIsMsvmComputerSystemActive(computerSystem, NULL) ? 1 : 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainIsPersistent(virDomainPtr domain ATTRIBUTE_UNUSED)
{
    /* Hyper-V has no concept of transient domains, so all of them are persistent */
    return 1;
}



static int
hypervDomainIsUpdated(virDomainPtr domain ATTRIBUTE_UNUSED)
{
    return 0;
}



static int
hypervDomainManagedSave(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;
    bool in_transition = false;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (!hypervIsMsvmComputerSystemActive(computerSystem, &in_transition) ||
        in_transition) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is not active or is in state transition"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_SUSPENDED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainHasManagedSaveImage(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    result = computerSystem->data->EnabledState ==
             MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SUSPENDED ? 1 : 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}



static int
hypervDomainManagedSaveRemove(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0)
        goto cleanup;

    if (computerSystem->data->EnabledState !=
        MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SUSPENDED) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain has no managed save image"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange
               (domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_DISABLED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);

    return result;
}


#define MATCH(FLAG) (flags & (FLAG))
static int
hypervConnectListAllDomains(virConnectPtr conn,
                            virDomainPtr **domains,
                            unsigned int flags)
{
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystemList = NULL;
    Msvm_ComputerSystem *computerSystem = NULL;
    size_t ndoms;
    virDomainPtr domain;
    virDomainPtr *doms = NULL;
    int count = 0;
    int ret = -1;
    size_t i;

    virCheckFlags(VIR_CONNECT_LIST_DOMAINS_FILTERS_ALL, -1);

    /* check for filter combinations that return no results:
     * persistent: all hyperv guests are persistent
     * snapshot: the driver does not support snapshot management
     * autostart: the driver does not support autostarting guests
     */
    if ((MATCH(VIR_CONNECT_LIST_DOMAINS_TRANSIENT) &&
         !MATCH(VIR_CONNECT_LIST_DOMAINS_PERSISTENT)) ||
        (MATCH(VIR_CONNECT_LIST_DOMAINS_AUTOSTART) &&
         !MATCH(VIR_CONNECT_LIST_DOMAINS_NO_AUTOSTART)) ||
        (MATCH(VIR_CONNECT_LIST_DOMAINS_HAS_SNAPSHOT) &&
         !MATCH(VIR_CONNECT_LIST_DOMAINS_NO_SNAPSHOT))) {
        if (domains && VIR_ALLOC_N(*domains, 1) < 0)
            goto cleanup;

        ret = 0;
        goto cleanup;
    }

    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAddLit(&query, "where ");
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_VIRTUAL);

    /* construct query with filter depending on flags */
    if (!(MATCH(VIR_CONNECT_LIST_DOMAINS_ACTIVE) &&
          MATCH(VIR_CONNECT_LIST_DOMAINS_INACTIVE))) {
        if (MATCH(VIR_CONNECT_LIST_DOMAINS_ACTIVE)) {
            virBufferAddLit(&query, "and ");
            virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_ACTIVE);
        }

        if (MATCH(VIR_CONNECT_LIST_DOMAINS_INACTIVE)) {
            virBufferAddLit(&query, "and ");
            virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_INACTIVE);
        }
    }

    if (hypervGetMsvmComputerSystemList(priv, &query,
                                        &computerSystemList) < 0)
        goto cleanup;

    if (domains) {
        if (VIR_ALLOC_N(doms, 1) < 0)
            goto cleanup;
        ndoms = 1;
    }

    for (computerSystem = computerSystemList; computerSystem != NULL;
         computerSystem = computerSystem->next) {

        /* filter by domain state */
        if (MATCH(VIR_CONNECT_LIST_DOMAINS_FILTERS_STATE)) {
            int st = hypervMsvmComputerSystemEnabledStateToDomainState(computerSystem);
            if (!((MATCH(VIR_CONNECT_LIST_DOMAINS_RUNNING) &&
                   st == VIR_DOMAIN_RUNNING) ||
                  (MATCH(VIR_CONNECT_LIST_DOMAINS_PAUSED) &&
                   st == VIR_DOMAIN_PAUSED) ||
                  (MATCH(VIR_CONNECT_LIST_DOMAINS_SHUTOFF) &&
                   st == VIR_DOMAIN_SHUTOFF) ||
                  (MATCH(VIR_CONNECT_LIST_DOMAINS_OTHER) &&
                   (st != VIR_DOMAIN_RUNNING &&
                    st != VIR_DOMAIN_PAUSED &&
                    st != VIR_DOMAIN_SHUTOFF))))
                continue;
        }

        /* managed save filter */
        if (MATCH(VIR_CONNECT_LIST_DOMAINS_FILTERS_MANAGEDSAVE)) {
            bool mansave = computerSystem->data->EnabledState ==
                           MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SUSPENDED;

            if (!((MATCH(VIR_CONNECT_LIST_DOMAINS_MANAGEDSAVE) && mansave) ||
                  (MATCH(VIR_CONNECT_LIST_DOMAINS_NO_MANAGEDSAVE) && !mansave)))
                continue;
        }

        if (!doms) {
            count++;
            continue;
        }

        if (VIR_RESIZE_N(doms, ndoms, count, 2) < 0)
            goto cleanup;

        domain = NULL;

        if (hypervMsvmComputerSystemToDomain(conn, computerSystem,
                                             &domain) < 0)
            goto cleanup;

        doms[count++] = domain;
    }

    if (doms)
        *domains = doms;
    doms = NULL;
    ret = count;

 cleanup:
    if (doms) {
        for (i = 0; i < count; ++i)
            virObjectUnref(doms[i]);

        VIR_FREE(doms);
    }

    hypervFreeObject(priv, (hypervObject *)computerSystemList);

    return ret;
}
#undef MATCH

static char*
hypervConnectGetCapabilities(virConnectPtr conn)
{
    hypervPrivate *priv = conn->privateData;
    char *xml = virCapabilitiesFormatXML(priv->caps);

    if (xml == NULL) {
        virReportOOMError();
        return NULL;
    }

    return xml;
}

static int
hypervConnectGetVersion(virConnectPtr conn, unsigned long *version)
{
    int result = -1;
    hypervPrivate *priv = conn->privateData;
    CIM_DataFile  *datafile = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    char *p;

    virBufferAddLit(&query, " Select * from CIM_DataFile where Name='c:\\\\windows\\\\system32\\\\vmms.exe' ");
    if (hypervGetCIMDataFileList(priv, &query, &datafile) < 0) {
        goto cleanup;
    }

    /* Check the result of convertion */
    if (datafile == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_VirtualSystemSettingData",
                       datafile->data->Version);
        goto cleanup;
    }

    /* Delete release number and last digit of build number 1.1.111x.xxxx */
    p = strrchr(datafile->data->Version,'.');
    if (p == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not parse version number from '%s'"),
                       datafile->data->Version);
        goto cleanup;
    }
    p--;
    *p = '\0';

    /* Parse Version String to Long */
    if (virParseVersionString(datafile->data->Version,
                              version, true) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not parse version number from '%s'"),
                       datafile->data->Version);
        goto cleanup;
    }

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)datafile);
    virBufferFreeAndReset(&query);

    return result;
}

static int
hypervConnectGetMaxVcpus(virConnectPtr conn, const char *type ATTRIBUTE_UNUSED)
{
    int result = -1;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ProcessorSettingData *processorSettingData = NULL;

    /* Get Msvm_ProcessorSettingData maximum definition */
    virBufferAddLit(&query, "SELECT * FROM Msvm_ProcessorSettingData "
                    "WHERE InstanceID LIKE 'Microsoft:Definition%Maximum'");

    if (hypervGetMsvmProcessorSettingDataList(priv, &query, &processorSettingData) < 0) {
        goto cleanup;
    }

    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not get maximum definition of Msvm_ProcessorSettingData"));
        goto cleanup;
    }

    result = processorSettingData->data->SocketCount * processorSettingData->data->ProcessorsPerSocket;

 cleanup:
    hypervFreeObject(priv, (hypervObject *) processorSettingData);
    virBufferFreeAndReset(&query);

    return result;
}

static int
hypervDomainGetVcpusFlags(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;
    Msvm_ProcessorSettingData *processorSettingData = NULL;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;

    virCheckFlags(VIR_DOMAIN_VCPU_LIVE | VIR_DOMAIN_VCPU_CONFIG | VIR_DOMAIN_VCPU_MAXIMUM, -1);

    virUUIDFormat(domain->uuid, uuid_string);

    /* Get Msvm_ComputerSystem */
    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0) {
        goto cleanup;
    }

    /* If @flags includes VIR_DOMAIN_VCPU_LIVE,
       this will query a running domain (which will fail if domain is not active) */
    if (flags & VIR_DOMAIN_VCPU_LIVE) {
        if (computerSystem->data->EnabledState != MSVM_COMPUTERSYSTEM_ENABLEDSTATE_ENABLED) {
            virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("Domain is not active"));
            goto cleanup;
        }
    }

    /* If @flags includes VIR_DOMAIN_VCPU_MAXIMUM, then the maximum virtual CPU limit is queried */
    if (flags & VIR_DOMAIN_VCPU_MAXIMUM) {
        result = hypervConnectGetMaxVcpus(domain->conn, NULL);
        goto cleanup;
    }

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);
    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query, &virtualSystemSettingData) < 0) {
        goto cleanup;
    }
    if (virtualSystemSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Could not lookup %s for domain %s"),
                       "Msvm_VirtualSystemSettingData", computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_ProcessorSettingData */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ProcessorSettingData",
                      virtualSystemSettingData->data->InstanceID);
    if (hypervGetMsvmProcessorSettingDataList(priv, &query, &processorSettingData) < 0) {
        goto cleanup;
    }
    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Could not lookup %s for domain %s"),
                       "Msvm_ProcessorSettingData", computerSystem->data->ElementName);
        goto cleanup;
    }

    result = processorSettingData->data->VirtualQuantity;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)processorSettingData);
    virBufferFreeAndReset(&query);

    return result;
}

static int
hypervDomainGetMaxVcpus(virDomainPtr dom)
{
    /* If the guest is inactive, this is basically the same as virConnectGetMaxVcpus() */
    return (hypervDomainIsActive(dom)) ?
        hypervDomainGetVcpusFlags(dom, (VIR_DOMAIN_VCPU_LIVE | VIR_DOMAIN_VCPU_MAXIMUM))
        : hypervConnectGetMaxVcpus(dom->conn, NULL);
}

static int
hypervDomainGetVcpus(virDomainPtr domain, virVcpuInfoPtr info, int maxinfo,
                     unsigned char *cpumaps, int maplen)
{
    int count = 0, i;
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Win32_PerfRawData_HvStats_HyperVHypervisorVirtualProcessor *hypervVirtualProcessor = NULL;

    /* FIXME: no information stored in cpumaps */
    if ((cpumaps != NULL) && (maplen > 0))
        memset(cpumaps, 0, maxinfo * maplen);

    /* Loop for each vCPU */
    for (i = 0; i < maxinfo; i++) {

        /* Get vCPU stats */
        hypervFreeObject(priv, (hypervObject *)hypervVirtualProcessor);
        hypervVirtualProcessor = NULL;
        virBufferFreeAndReset(&query);
        virBufferAddLit(&query, WIN32_PERFRAWDATA_HVSTATS_HYPERVHYPERVISORVIRTUALPROCESSOR_WQL_SELECT);
        /* Attribute Name format : <domain_name>:Hv VP <vCPU_number> */
        virBufferAsprintf(&query, "where Name = \"%s:Hv VP %d\"", domain->name, i);

        if (hypervGetWin32PerfRawDataHvStatsHyperVHypervisorVirtualProcessorList(
                priv, &query, &hypervVirtualProcessor) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not get stats on vCPU #%d"), i);
            continue;
        }

        /* Fill structure info */
        info[i].number = i;
        if (hypervVirtualProcessor == NULL) {
            info[i].state = VIR_VCPU_OFFLINE;
            info[i].cpuTime = 0LLU;
            info[i].cpu = -1;
        } else {
            info[i].state = VIR_VCPU_RUNNING;
            info[i].cpuTime = hypervVirtualProcessor->data->PercentTotalRunTime;
            info[i].cpu = i;
        }

        count++;
    }

    hypervFreeObject(priv, (hypervObject *)hypervVirtualProcessor);
    virBufferFreeAndReset(&query);

    return count;
}

static unsigned long long
hypervNodeGetFreeMemory(virConnectPtr conn)
{
    unsigned long long res = 0;
    hypervPrivate *priv = conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Win32_OperatingSystem *operatingSystem = NULL;

    /* Get Win32_OperatingSystem */
    virBufferAddLit(&query, WIN32_OPERATINGSYSTEM_WQL_SELECT);

    if (hypervGetWin32OperatingSystemList(priv, &query, &operatingSystem) < 0) {
        goto cleanup;
    }

    if (operatingSystem == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not get Win32_OperatingSystem"));
        goto cleanup;
    }

    /* Return free memory in bytes */
    res = operatingSystem->data->FreePhysicalMemory * 1024;

 cleanup:
    hypervFreeObject(priv, (hypervObject *) operatingSystem);
    virBufferFreeAndReset(&query);

    return res;
}

static int
hypervDomainSetAutostart(virDomainPtr domain, int autostart)
{
    int result = -1;
    invokeXmlParam *params = NULL;
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    virBuffer queryVssd = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    properties_t *tab_props = NULL;
    eprParam eprparam;
    embeddedParam embeddedparam;
    int nb_params;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";

    virUUIDFormat(domain->uuid, uuid_string);

    /* Prepare EPR param */
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", uuid_string);
    eprparam.query = &query;
    eprparam.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param */
    virBufferAsprintf(&queryVssd,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &queryVssd, &virtualSystemSettingData) < 0)
        goto cleanup;

    embeddedparam.nbProps = 2;
    if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "AutomaticStartupAction";
    (*tab_props).val = autostart ? "2" : "0";
    (*(tab_props+1)).name = "InstanceID";
    (*(tab_props+1)).val = virtualSystemSettingData->data->InstanceID;

    embeddedparam.instanceName =  "Msvm_VirtualSystemGlobalSettingData";
    embeddedparam.prop_t = tab_props;

    /* Create invokeXmlParam tab */
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "ComputerSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam;
    (*(params+1)).name = "SystemSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam;

    result = hypervInvokeMethod(priv, params, nb_params, "ModifyVirtualSystem",
                             MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector);

 cleanup:
    hypervFreeObject(priv, (hypervObject *) virtualSystemSettingData);
    VIR_FREE(tab_props);
    VIR_FREE(params);
    virBufferFreeAndReset(&query);
    virBufferFreeAndReset(&queryVssd);

    return result;
}



static int
hypervDomainGetAutostart(virDomainPtr domain, int *autostart)
{
    int result = -1;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemGlobalSettingData *vsgsd = NULL;

    virUUIDFormat(domain->uuid, uuid_string);
    virBufferAddLit(&query, MSVM_VIRTUALSYSTEMGLOBALSETTINGDATA_WQL_SELECT);
    virBufferAsprintf(&query, "where SystemName = \"%s\"", uuid_string);

    if (hypervGetMsvmVirtualSystemGlobalSettingDataList(priv, &query, &vsgsd) < 0)
        goto cleanup;

    *autostart = vsgsd->data->AutomaticStartupAction;
    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *) vsgsd);
    virBufferFreeAndReset(&query);

    return result;
}

static int
hypervDomainShutdownFlags(virDomainPtr domain, unsigned int flags)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;
    bool in_transition = false;

    virCheckFlags(0, -1);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0) {
        goto cleanup;
    }

    if (!hypervIsMsvmComputerSystemActive(computerSystem, &in_transition) || in_transition) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("Domain is not active or is in state transition"));
        goto cleanup;
    }

    result = hypervInvokeMsvmComputerSystemRequestStateChange(domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_DISABLED);

 cleanup:
    hypervFreeObject(priv, (hypervObject *) computerSystem);
    return result;
}

static int
hypervDomainShutdown(virDomainPtr dom)
{
    return hypervDomainShutdownFlags(dom, 0);
}

static int
hypervDomainGetSchedulerParametersFlags(virDomainPtr dom, virTypedParameterPtr params,
                                        int *nparams, unsigned int flags)
{
    hypervPrivate *priv = dom->conn->privateData;
    Msvm_ComputerSystem *computerSystem = NULL;
    Msvm_ProcessorSettingData *processorSettingData = NULL;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    virBuffer query = VIR_BUFFER_INITIALIZER;
    int saved_nparams = 0;
    int result = -1;

    virCheckFlags(VIR_DOMAIN_AFFECT_LIVE |VIR_DOMAIN_AFFECT_CONFIG |VIR_TYPED_PARAM_STRING_OKAY, -1);

    /* We don't return strings, and thus trivially support this flag */
    flags &= ~VIR_TYPED_PARAM_STRING_OKAY;

    virUUIDFormat(dom->uuid, uuid_string);

    /* Get Msvm_ComputerSystem */
    if (hypervMsvmComputerSystemFromDomain(dom, &computerSystem) < 0) {
        goto cleanup;
    }

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query, &virtualSystemSettingData) < 0) {
        goto cleanup;
    }

    if (virtualSystemSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup %s for domain %s"),
                       "Msvm_VirtualSystemSettingData",
                       computerSystem->data->ElementName);
        goto cleanup;
    }

    /* Get Msvm_ProcessorSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ProcessorSettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmProcessorSettingDataList(priv, &query, &processorSettingData) < 0) {
        goto cleanup;
    }

    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Could not lookup %s for domain %s"),
                       "Msvm_ProcessorSettingData",computerSystem->data->ElementName);
        goto cleanup;
    }

    if (virTypedParameterAssign(&params[0], VIR_DOMAIN_SCHEDULER_LIMIT,
                                VIR_TYPED_PARAM_LLONG, processorSettingData->data->Limit) < 0)
        goto cleanup;
    saved_nparams++;

    if (*nparams > saved_nparams) {
        if (virTypedParameterAssign(&params[1],VIR_DOMAIN_SCHEDULER_RESERVATION,
                                    VIR_TYPED_PARAM_LLONG, processorSettingData->data->Reservation) < 0)
            goto cleanup;
        saved_nparams++;
    }

    if (*nparams > saved_nparams) {
        if (virTypedParameterAssign(&params[2],VIR_DOMAIN_SCHEDULER_WEIGHT,
                                    VIR_TYPED_PARAM_UINT, processorSettingData->data->Weight) < 0)
            goto cleanup;
        saved_nparams++;
    }

    *nparams = saved_nparams;

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)processorSettingData);
    virBufferFreeAndReset(&query);

    return result;
}



static int
hypervDomainGetSchedulerParameters(virDomainPtr dom, virTypedParameterPtr params, int *nparams)
{
    return hypervDomainGetSchedulerParametersFlags(dom, params, nparams, VIR_DOMAIN_AFFECT_CURRENT);
}



static char*
hypervDomainGetSchedulerType(virDomainPtr domain ATTRIBUTE_UNUSED, int *nparams)
{
    char *type;

    if (VIR_STRDUP(type, "allocation") < 0) {
        virReportOOMError();
        return NULL;
    }

    if (nparams != NULL) {
        *nparams = 3; /* reservation, limit, weight */
    }

    return type;
}

/* Format a number as a string value */
static char *num2str(unsigned long value)
{
    int sz;
    char *result;

    sz = snprintf (NULL, 0, "%lu", value);
    if (VIR_ALLOC_N(result, sz + 1) < 0) {
      return NULL;
    }

    sprintf(result, "%lu", value);
    return result;
}



static int
hypervDomainSetMaxMemory(virDomainPtr domain, unsigned long memory)
{
    int result = -1;
    invokeXmlParam *params = NULL;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    properties_t *tab_props = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    virBuffer query2 = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_MemorySettingData *memorySettingData = NULL;
    eprParam eprparam;
    embeddedParam embeddedparam;
    int nb_params;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    unsigned long memory_mb = memory/1024;
    char *memory_str = NULL;

    /* Memory value must be a multiple of 2 MB; round up it accordingly if necessary */
    if (memory_mb % 2) memory_mb++;

    /* Convert the memory value as a string */
    memory_str = num2str(memory_mb);
    if (memory_str == NULL)
        goto cleanup;

    virUUIDFormat(domain->uuid, uuid_string);

    VIR_DEBUG("memory=%sMb, uuid=%s", memory_str, uuid_string);

    /* Prepare EPR param */
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"",uuid_string);
    eprparam.query = &query;
    eprparam.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param 1 */
    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query2,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query2, &virtualSystemSettingData) < 0)
        goto cleanup;

    /* Get Msvm_MemorySettingData */
    virBufferFreeAndReset(&query2);
    virBufferAsprintf(&query2,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_MemorySettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmMemorySettingDataList(priv, &query2, &memorySettingData) < 0)
        goto cleanup;

    embeddedparam.nbProps = 2;
    if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "Limit";
    (*tab_props).val = memory_str;
    (*(tab_props+1)).name = "InstanceID";
    (*(tab_props+1)).val = memorySettingData->data->InstanceID;
    embeddedparam.instanceName =  "Msvm_MemorySettingData";
    embeddedparam.prop_t = tab_props;
    embeddedparam.nbProps = 2;

    /* Create invokeXmlParam */
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "ComputerSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam;

    result = hypervInvokeMethod(priv, params, nb_params, "ModifyVirtualSystemResources",
                             MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector);

 cleanup:
    VIR_FREE(tab_props);
    VIR_FREE(params);
    VIR_FREE(memory_str);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)memorySettingData);
    virBufferFreeAndReset(&query);
    virBufferFreeAndReset(&query2);

    return result;
}

static int
hypervDomainSetMemoryFlags(virDomainPtr domain, unsigned long memory,
                           unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1, nb_params;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    invokeXmlParam *params = NULL;
    properties_t *tab_props = NULL;
    eprParam eprparam;
    embeddedParam embeddedparam;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_MemorySettingData *memorySettingData = NULL;
    unsigned long memory_mb = memory / 1024;   /* Memory converted in MB */
    char *memory_str = NULL;

    /* Memory value must be a multiple of 2 MB; round up it accordingly if necessary */
    if (memory_mb % 2) memory_mb++;

    /* Convert the memory value as a string */
    memory_str = num2str(memory_mb);
    if (memory_str == NULL)
        goto cleanup;

    virUUIDFormat(domain->uuid, uuid_string);

    VIR_DEBUG("memory=%sMb, uuid=%s", memory_str, uuid_string);

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);
    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query, &virtualSystemSettingData) < 0)
        goto cleanup;

    /* Get Msvm_MemorySettingData */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_MemorySettingData",
                      virtualSystemSettingData->data->InstanceID);
    if (hypervGetMsvmMemorySettingDataList(priv, &query, &memorySettingData) < 0)
        goto cleanup;

    /* Prepare EPR param */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"",uuid_string);
    eprparam.query = &query;
    eprparam.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param */
    embeddedparam.nbProps = 2;
    if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "VirtualQuantity";
    (*tab_props).val = memory_str;
    (*(tab_props+1)).name = "InstanceID";
    (*(tab_props+1)).val = memorySettingData->data->InstanceID;
    embeddedparam.instanceName =  "Msvm_MemorySettingData";
    embeddedparam.prop_t = tab_props;

    /* Create invokeXmlParam */
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "ComputerSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam;

    if (hypervInvokeMethod(priv, params, nb_params, "ModifyVirtualSystemResources",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Could not set domain memory"));
        goto cleanup;
    }

    result = 0;

 cleanup:
    VIR_FREE(tab_props);
    VIR_FREE(params);
    VIR_FREE(memory_str);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)memorySettingData);
    virBufferFreeAndReset(&query);

    return result;
}



static int
hypervDomainSetMemory(virDomainPtr domain, unsigned long memory)
{
    return hypervDomainSetMemoryFlags(domain, memory, 0);
}


static int
hypervDomainSetVcpusFlags(virDomainPtr domain, unsigned int nvcpus,
                          unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1;
    invokeXmlParam *params = NULL;
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    properties_t *tab_props = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_ProcessorSettingData *processorSettingData = NULL;
    eprParam eprparam;
    embeddedParam embeddedparam;
    int nb_params;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    char *nvcpus_str = NULL;

    /* Convert nvcpus as a string value */
    nvcpus_str = num2str(nvcpus);
    if (nvcpus_str == NULL)
    goto cleanup;

    virUUIDFormat(domain->uuid, uuid_string);

    VIR_DEBUG("nvcpus=%s, uuid=%s", nvcpus_str, uuid_string);

    /* Get Msvm_VirtualSystemSettingData */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);

    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query, &virtualSystemSettingData) < 0)
        goto cleanup;

    /* Get Msvm_ProcessorSettingData */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ProcessorSettingData",
                      virtualSystemSettingData->data->InstanceID);

    if (hypervGetMsvmProcessorSettingDataList(priv, &query, &processorSettingData) < 0)
        goto cleanup;

    if (processorSettingData == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not lookup Msvm_ProcessorSettingData for domain %s"),
                       virtualSystemSettingData->data->ElementName);
        goto cleanup;
    }

    /* Prepare EPR param */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"",uuid_string);
    eprparam.query = &query;
    eprparam.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param */
    embeddedparam.nbProps = 2;
    if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "VirtualQuantity";
    (*tab_props).val = nvcpus_str;
    (*(tab_props+1)).name = "InstanceID";
    (*(tab_props+1)).val = processorSettingData->data->InstanceID;
    embeddedparam.instanceName =  "Msvm_ProcessorSettingData";
    embeddedparam.prop_t = tab_props;

    /* Create invokeXmlParam */
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "ComputerSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam;

    if (hypervInvokeMethod(priv, params, nb_params, "ModifyVirtualSystemResources",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Could not set domain vcpus"));
        goto cleanup;
    }

    result = 0;

 cleanup:
    VIR_FREE(tab_props);
    VIR_FREE(params);
    VIR_FREE(nvcpus_str);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)processorSettingData);
    virBufferFreeAndReset(&query);

    return result;
}



static int
hypervDomainSetVcpus(virDomainPtr domain, unsigned int nvcpus)
{
    return hypervDomainSetVcpusFlags(domain, nvcpus, 0);
}


static int
hypervDomainUndefineFlags(virDomainPtr domain, unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1, nb_params;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    invokeXmlParam *params = NULL;
    eprParam eprparam;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;

    virCheckFlags(0, -1);

    virUUIDFormat(domain->uuid, uuid_string);

    if (hypervMsvmComputerSystemFromDomain(domain, &computerSystem) < 0) {
        goto cleanup;
    }

    /* Shutdown the VM if not disabled */
    if (computerSystem->data->EnabledState != MSVM_COMPUTERSYSTEM_ENABLEDSTATE_DISABLED) {
        if (hypervDomainShutdown(domain) < 0) {
            goto cleanup;
        }
    }

    /* Deleting the VM */

    /* Prepare EPR param */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", uuid_string);
    eprparam.query = &query;
    eprparam.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Create invokeXmlParam tab */
    nb_params = 1;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "ComputerSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam;

    /* Destroy VM */
    if (hypervInvokeMethod(priv, params, nb_params, "DestroyVirtualSystem",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Could not delete domain"));
        goto cleanup;
    }

    result = 0;

 cleanup:
    VIR_FREE(params);
    hypervFreeObject(priv, (hypervObject *) computerSystem);
    virBufferFreeAndReset(&query);

    return result;
}



static int
hypervDomainUndefine(virDomainPtr domain)
{
    return hypervDomainUndefineFlags(domain, 0);
}

/*
 * Creates the attribute __PATH for the RASD object
 * The attribute is build like this:
 *   \\<host_name>\root\virtualization:Msvm_ResourceAllocationSettingData.InstanceID="<rasdInstanceID>"
 *   where backslashes in rasdInstanceID are doubled
 */
static int
hypervGetResourceAllocationSettingDataPATH(virDomainPtr domain, char *rasdInstanceID, char **__path)
{
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;
    char *strTemp = NULL;
    int result = -1, i = 0, j = 0, n = 0;

    virUUIDFormat(domain->uuid, uuid_string);

    /* Get host name */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_HostedDependency "
                      "ResultClass = Msvm_ComputerSystem",
                      uuid_string);
    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0) {
        goto cleanup;
    }
    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN, _("No domain with UUID %s"), uuid_string);
        goto cleanup;
    }

    /* Count the number of backslash character */
    strTemp = strchr(rasdInstanceID, '\\');
    while (strTemp != NULL) {
        n++;
        strTemp = strchr(++strTemp, '\\');
    }
    /* Double the blackslashes */
    if (VIR_ALLOC_N(strTemp, strlen(rasdInstanceID) + 1 + n) < 0)
        goto cleanup;
    while (rasdInstanceID[i] != '\0') {
        strTemp[j] = rasdInstanceID[i];
        if (rasdInstanceID[i] == '\\') {
            j++;
            strTemp[j] = '\\';
        }
        i++;
        j++;
    }
    strTemp[j] = '\0';

    /* Create the attribute __PATH */
    /* FIXME: *__path allocated with 255 characters (static value) */
    if (VIR_ALLOC_N(*__path, 255) < 0)
        goto cleanup;
    sprintf(*__path, "\\\\");
    strcat(*__path, computerSystem->data->ElementName);
    strcat(*__path, "\\root\\virtualization:Msvm_ResourceAllocationSettingData.InstanceID=\"");
    strcat(*__path, strTemp);
    strcat(*__path, "\"");

    result = 0;

 cleanup:
    VIR_FREE(strTemp);
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    virBufferFreeAndReset(&query);

    return result;
}



/* hypervDomainAttachDisk
 * FIXME:
 *   - added ressources must me removed in case of error
 *   - allow attaching disks on iSCSI (implemented only on IDE)
 *   - allow attaching ISO images (on DVD devices)
 *   - implement associated detach method
 */
ATTRIBUTE_UNUSED static int
hypervDomainAttachDisk(virDomainPtr domain, virDomainDiskDefPtr disk)
{
    int result = -1, nb_params;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    char *ideRasdPath = NULL, *newDiskDrivePath = NULL;
    char ideControler[2], ideControlerAddr[2];
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_VirtualSystemSettingData *virtualSystemSettingData = NULL;
    Msvm_ResourceAllocationSettingData *resourceAllocationSettingData = NULL;
    Msvm_ResourceAllocationSettingData *resourceAllocationSettingData2 = NULL;
    Msvm_ResourceAllocationSettingData *resourceAllocationSettingData3 = NULL;
    Msvm_ResourceAllocationSettingData *resourceAllocationSettingData4 = NULL;
    Msvm_ResourceAllocationSettingData *ideRasd = NULL;  /* resourceAllocationSettingData subtree -> do not disallocate */
    Msvm_ResourceAllocationSettingData *diskRasd = NULL;  /* resourceAllocationSettingData2 subtree -> do not disallocate */
    Msvm_ResourceAllocationSettingData *newDiskDrive = NULL;  /* resourceAllocationSettingData3 subtree -> do not disallocate */
    Msvm_AllocationCapabilities *allocationCapabilities  = NULL;
    Msvm_AllocationCapabilities *allocationCapabilities2  = NULL;
    invokeXmlParam *params = NULL;
    properties_t *tab_props = NULL;
    eprParam eprparam1, eprparam2;
    embeddedParam embeddedparam1, embeddedparam2;

    /* Initialization */
    virUUIDFormat(domain->uuid, uuid_string);

    /* Set IDE Controler 0 or 1 and address 0 or 1 */
    if (STREQ(disk->dst, "hda")) {
        sprintf(ideControler, "%d", 0);
        sprintf(ideControlerAddr, "%d", 0);
    } else if (STREQ(disk->dst, "hdb")) {
        sprintf(ideControler, "%d", 0);
        sprintf(ideControlerAddr, "%d", 1);
    } else if (STREQ(disk->dst, "hdc")) {
        sprintf(ideControler, "%d", 1);
        sprintf(ideControlerAddr, "%d", 0);
    } else if (STREQ(disk->dst, "hdd")) {
        sprintf(ideControler, "%d", 1);
        sprintf(ideControlerAddr, "%d", 1);
    } else {
        /* IDE Controler 0 and address 0 choosen by default */
        sprintf(ideControler, "%d", 0);
        sprintf(ideControlerAddr, "%d", 0);
    }

    VIR_DEBUG("src=%s, dst=IDE Controller %s:%s, uuid=%s",
              disk->src->path, ideControler, ideControlerAddr, uuid_string);

    /* Get the current VM settings object */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineState "
                      "ResultClass = Msvm_VirtualSystemSettingData",
                      uuid_string);
    if (hypervGetMsvmVirtualSystemSettingDataList(priv, &query, &virtualSystemSettingData) < 0) {
        goto cleanup;
    }

    /* Get the settings for IDE Controller on the VM */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ResourceAllocationSettingData",
                      virtualSystemSettingData->data->InstanceID);
    if (hypervGetMsvmResourceAllocationSettingDataList(priv, &query, &resourceAllocationSettingData) < 0) {
        goto cleanup;
    }
    ideRasd = resourceAllocationSettingData;
    while (ideRasd != NULL) {
        if (ideRasd->data->ResourceType == 5 && STREQ(ideRasd->data->Address, ideControler)) {
            /* IDE Controller 0 or 1 */
            break;
        }
        ideRasd = ideRasd->next;
    }
    if (ideRasd == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not find IDE Controller %s"), ideControler);
        goto cleanup;
    }

    /* Get the settings for 'Microsoft Synthetic Disk Drive' */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_ALLOCATIONCAPABILITIES_WQL_SELECT);
    virBufferAddLit(&query, "WHERE ResourceSubType = 'Microsoft Synthetic Disk Drive'");
    if (hypervGetMsvmAllocationCapabilitiesList(priv, &query, &allocationCapabilities) < 0) {
        goto cleanup;
    }

    /* Get default values for 'Microsoft Synthetic Disk Drive' */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_AllocationCapabilities.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineCapabilities "
                      "ResultClass = Msvm_ResourceAllocationSettingData",
                      allocationCapabilities->data->InstanceID);
    if (hypervGetMsvmResourceAllocationSettingDataList(priv, &query, &resourceAllocationSettingData2) < 0) {
        goto cleanup;
    }
    diskRasd = resourceAllocationSettingData2;
    while (diskRasd != NULL) {
        if (strstr(diskRasd->data->InstanceID, "Default") != NULL) {
            /* Default values */
            break;
        }
        diskRasd = diskRasd->next;
    }
    if (diskRasd == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not get default values for 'Microsoft Synthetic Disk Drive'"));
        goto cleanup;
    }

    /* Create the attribute _PATH for the RASD object */
    if (hypervGetResourceAllocationSettingDataPATH(domain, ideRasd->data->InstanceID, &ideRasdPath) < 0) {
        goto cleanup;
    }

    /* Add default disk drive */
    /* Prepare EPR param */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", uuid_string);
    eprparam1.query = &query;
    eprparam1.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param 1 */
    embeddedparam1.nbProps = 4;
    if (VIR_ALLOC_N(tab_props, embeddedparam1.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "Parent";
    (*tab_props).val = ideRasdPath;
    (*(tab_props+1)).name = "Address";
    (*(tab_props+1)).val = ideControlerAddr;
    (*(tab_props+2)).name = "ResourceType";
    (*(tab_props+2)).val = "22";
    (*(tab_props+3)).name = "ResourceSubType";
    (*(tab_props+3)).val = diskRasd->data->ResourceSubType;
    embeddedparam1.instanceName =  MSVM_RESOURCEALLOCATIONSETTINGDATA_CLASSNAME;
    embeddedparam1.prop_t = tab_props;

    /* Create invokeXmlParam tab */
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "TargetSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam1;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam1;

    if (hypervInvokeMethod(priv, params, nb_params, "AddVirtualSystemResources",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Could not add default disk drive"));
        goto cleanup;
    }

    /* Get the instance of the new default drive disk */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_VirtualSystemSettingData.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_VirtualSystemSettingDataComponent "
                      "ResultClass = Msvm_ResourceAllocationSettingData",
                      virtualSystemSettingData->data->InstanceID);
    if (hypervGetMsvmResourceAllocationSettingDataList(priv, &query, &resourceAllocationSettingData3) < 0) {
        goto cleanup;
    }
    newDiskDrive = resourceAllocationSettingData3;
    while (newDiskDrive != NULL) {
        if (newDiskDrive->data->ResourceType == 22 &&
            STREQ(newDiskDrive->data->ResourceSubType, "Microsoft Synthetic Disk Drive") &&
            STREQ(newDiskDrive->data->Parent, ideRasdPath) &&
            STREQ(newDiskDrive->data->Address, ideControlerAddr)) {
            break;
        }
        newDiskDrive = newDiskDrive->next;
    }
    if (newDiskDrive == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not find 'Microsoft Synthetic Disk Drive'"));
        goto cleanup;
    }

    /* Get the settings for 'Microsoft Virtual Hard Disk' */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_ALLOCATIONCAPABILITIES_WQL_SELECT);
    virBufferAddLit(&query, "WHERE ResourceSubType = 'Microsoft Virtual Hard Disk'");
    if (hypervGetMsvmAllocationCapabilitiesList(priv, &query, &allocationCapabilities2) < 0) {
        goto cleanup;
    }

    /* Get default values for 'Microsoft Virtual Hard Drive' */
    virBufferFreeAndReset(&query);
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_AllocationCapabilities.InstanceID=\"%s\"} "
                      "where AssocClass = Msvm_SettingsDefineCapabilities "
                      "ResultClass = Msvm_ResourceAllocationSettingData",
                      allocationCapabilities2->data->InstanceID);
    if (hypervGetMsvmResourceAllocationSettingDataList(priv, &query, &resourceAllocationSettingData4) < 0) {
        goto cleanup;
    }
    diskRasd = resourceAllocationSettingData4;
    while (diskRasd != NULL) {
        if (strstr(diskRasd->data->InstanceID, "Default") != NULL) {
            /* Default values */
            break;
        }
        diskRasd = diskRasd->next;
    }
    if (diskRasd == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not get default values for 'Microsoft Virtual Hard Drive'"));
        goto cleanup;
    }

    /* Create the attribute _PATH for the RASD object */
    if (hypervGetResourceAllocationSettingDataPATH(domain, newDiskDrive->data->InstanceID, &newDiskDrivePath) < 0) {
        goto cleanup;
    }

    /* Add the new VHD */
    /* Prepare EPR param 2 */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", uuid_string);
    eprparam2.query = &query;
    eprparam2.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param 2 */
    VIR_FREE(tab_props);
    embeddedparam2.nbProps = 4;
    if (VIR_ALLOC_N(tab_props, embeddedparam2.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "Parent";
    (*tab_props).val = newDiskDrivePath;
    (*(tab_props+1)).name = "Connection";
    (*(tab_props+1)).val = disk->src->path;
    (*(tab_props+2)).name = "ResourceType";
    (*(tab_props+2)).val = "21";
    (*(tab_props+3)).name = "ResourceSubType";
    (*(tab_props+3)).val = diskRasd->data->ResourceSubType;
    embeddedparam2.instanceName = MSVM_RESOURCEALLOCATIONSETTINGDATA_CLASSNAME;
    embeddedparam2.prop_t = tab_props;

    /* Create invokeXmlParam tab */
    VIR_FREE(params);
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "TargetSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam2;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam2;

    if (hypervInvokeMethod(priv, params, nb_params, "AddVirtualSystemResources",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not attach hard disk drive"));
        goto cleanup;
    }

    result = 0;

 cleanup:
    VIR_FREE(ideRasdPath);
    VIR_FREE(newDiskDrivePath);
    VIR_FREE(tab_props);
    VIR_FREE(params);
    hypervFreeObject(priv, (hypervObject *)virtualSystemSettingData);
    hypervFreeObject(priv, (hypervObject *)resourceAllocationSettingData);
    hypervFreeObject(priv, (hypervObject *)resourceAllocationSettingData2);
    hypervFreeObject(priv, (hypervObject *)resourceAllocationSettingData3);
    hypervFreeObject(priv, (hypervObject *)resourceAllocationSettingData4);
    hypervFreeObject(priv, (hypervObject *)allocationCapabilities);
    hypervFreeObject(priv, (hypervObject *)allocationCapabilities2);
    virBufferFreeAndReset(&query);

    return result;
}


/*
 * Create the attribute __PATH for the SwitchPort object.
 * The attribute is build like this:
 *   \\<host_name>\root\virtualization:Msvm_SwitchPort.CreationClassName="Msvm_SwitchPort",
 *   Name="<switchPortName>",SystemCreationClassName="Msvm_VirtualSwitch",
 *   SystemName="<virtualSwitchSystemName>"
 */
static int
hypervGetSwitchPortPATH(virDomainPtr domain, char *switchPortName, char *virtualSwitchSystemName, char **__path)
{
    char uuid_string[VIR_UUID_STRING_BUFLEN];
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    Msvm_ComputerSystem *computerSystem = NULL;
    int result = -1;

    virUUIDFormat(domain->uuid, uuid_string);

    /* Get host name */
    virBufferAsprintf(&query,
                      "associators of "
                      "{Msvm_ComputerSystem.CreationClassName=\"Msvm_ComputerSystem\","
                      "Name=\"%s\"} "
                      "where AssocClass = Msvm_HostedDependency "
                      "ResultClass = Msvm_ComputerSystem",
                      uuid_string);
    if (hypervGetMsvmComputerSystemList(priv, &query, &computerSystem) < 0) {
        goto cleanup;
    }
    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("No domain with UUID %s"), uuid_string);
        goto cleanup;
    }

    /* Create the attribute __PATH */
    /* FIXME: *__path is allocated with 512 characters (static value) */
    if (VIR_ALLOC_N(*__path, 512) < 0)
        goto cleanup;
    sprintf(*__path,
            "\\\\%s\\root\\virtualization:Msvm_SwitchPort.CreationClassName=\"Msvm_SwitchPort\","
            "Name=\"%s\",SystemCreationClassName=\"Msvm_VirtualSwitch\",SystemName=\"%s\"",
            computerSystem->data->ElementName, switchPortName, virtualSwitchSystemName);

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *) computerSystem);
    virBufferFreeAndReset(&query);
    return result;
}



/* hypervDomainAttachNetwork
 * FIXME:
 *   - implement associated detach method
 */
ATTRIBUTE_UNUSED static int
hypervDomainAttachNetwork(virDomainPtr domain, virDomainNetDefPtr net)
{
    int result = -1, nb_params;
    const char *selector1 = "CreationClassName=Msvm_VirtualSwitchManagementService";
    const char *selector2 = "CreationClassName=Msvm_VirtualSystemManagementService";
    char uuid_string[VIR_UUID_STRING_BUFLEN], guid_string[VIR_UUID_STRING_BUFLEN];
    unsigned char guid[VIR_UUID_BUFLEN];
    char *virtualSystemIdentifiers = NULL, *switchPortPATH = NULL;
    hypervPrivate *priv = domain->conn->privateData;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    eprParam eprparam1, eprparam2;
    simpleParam simpleparam1, simpleparam2, simpleparam3;
    embeddedParam embeddedparam;
    properties_t *tab_props = NULL;
    invokeXmlParam *params = NULL;
    Msvm_SwitchPort *switchPort = NULL;
    Msvm_VirtualSwitch *virtualSwitch = NULL;

    /* Initialization */
    virUUIDFormat(domain->uuid, uuid_string);

    VIR_DEBUG("network=%s, uuid=%s", net->data.network.name, uuid_string);

    /* Create virtual switch port */
    /* Prepare EPR param 1 */
    virBufferAddLit(&query, MSVM_VIRTUALSWITCH_WQL_SELECT);
    virBufferAsprintf(&query, "where ElementName = \"%s\"", net->data.network.name);
    eprparam1.query = &query;
    eprparam1.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare SIMPLE params */
    virUUIDGenerate(guid);
    virUUIDFormat(guid, guid_string);
    simpleparam1.value = guid_string;
    simpleparam2.value = "Dynamic Ethernet Switch Port";
    simpleparam3.value = "";

    /* Create invokeXmlParam tab */
    nb_params = 4;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "VirtualSwitch";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam1;
    (*(params+1)).name = "Name";
    (*(params+1)).type = SIMPLE_PARAM;
    (*(params+1)).param = &simpleparam1;
    (*(params+2)).name = "FriendlyName";
    (*(params+2)).type = SIMPLE_PARAM;
    (*(params+2)).param = &simpleparam2;
    (*(params+3)).name = "ScopeOfResidence";
    (*(params+3)).type = SIMPLE_PARAM;
    (*(params+3)).param = &simpleparam3;

    if (hypervInvokeMethod(priv, params, nb_params, "CreateSwitchPort",
                           MSVM_VIRTUALSWITCHMANAGEMENTSERVICE_RESOURCE_URI, selector1) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not create port for virtual switch '%s'"), net->data.network.name);
        goto cleanup;
    }

    /* Get a reference of the switch port created previously */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_SWITCHPORT_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", guid_string);
    if (hypervGetMsvmSwitchPortList(priv, &query, &switchPort) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Method hypervGetMsvmSwitchPortList failed with query=%s"), query.e);
        goto cleanup;
    }
    if (switchPort == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not get switch port with Name=%s"), guid_string);
        goto cleanup;
    }

    /* Get a reference of the given virtual switch */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_VIRTUALSWITCH_WQL_SELECT);
    virBufferAsprintf(&query, "where ElementName = \"%s\"", net->data.network.name);
    if (hypervGetMsvmVirtualSwitchList(priv, &query, &virtualSwitch) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Method hypervGetMsvmVirtualSwitchList failed with query=%s"), query.e);
        goto cleanup;
    }
    if (virtualSwitch == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not get virtual switch '%s'"), net->data.network.name);
        goto cleanup;
    }

    /* Add the synthetic ethernet port to the VM */
    /* Prepare EPR param 2 */
    virBufferFreeAndReset(&query);
    virBufferAddLit(&query, MSVM_COMPUTERSYSTEM_WQL_SELECT);
    virBufferAsprintf(&query, "where Name = \"%s\"", uuid_string);
    eprparam2.query = &query;
    eprparam2.wmiProviderURI = ROOT_VIRTUALIZATION;

    /* Prepare EMBEDDED param */
    virUUIDGenerate(guid);
    virUUIDFormat(guid, guid_string);
    virtualSystemIdentifiers = (char *) malloc((strlen(guid_string)+3) * sizeof(char));
    sprintf(virtualSystemIdentifiers, "{%s}", guid_string);
    if (hypervGetSwitchPortPATH(domain, switchPort->data->Name, virtualSwitch->data->Name, &switchPortPATH) < 0) {
        goto cleanup;
    }

    embeddedparam.nbProps = 5;
    if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
        goto cleanup;
    (*tab_props).name = "Connection";
    (*tab_props).val = switchPortPATH;
    (*(tab_props+1)).name = "ElementName";
    (*(tab_props+1)).val = "Network Adapter";
    (*(tab_props+2)).name = "VirtualSystemIdentifiers";
    (*(tab_props+2)).val = virtualSystemIdentifiers;
    (*(tab_props+3)).name = "ResourceType";
    (*(tab_props+3)).val = "10";
    (*(tab_props+4)).name = "ResourceSubType";
    (*(tab_props+4)).val = "Microsoft Synthetic Ethernet Port";
    embeddedparam.instanceName =  MSVM_SYNTHETICETHERNETPORTSETTINGDATA_CLASSNAME;
    embeddedparam.prop_t = tab_props;

    /* Create invokeXmlParam tab */
    VIR_FREE(params);
    nb_params = 2;
    if (VIR_ALLOC_N(params, nb_params) < 0)
        goto cleanup;
    (*params).name = "TargetSystem";
    (*params).type = EPR_PARAM;
    (*params).param = &eprparam2;
    (*(params+1)).name = "ResourceSettingData";
    (*(params+1)).type = EMBEDDED_PARAM;
    (*(params+1)).param = &embeddedparam;

    if (hypervInvokeMethod(priv, params, nb_params, "AddVirtualSystemResources",
                           MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector2) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not attach the network"));
        goto cleanup;
    }

    result = 0;

 cleanup:
    VIR_FREE(virtualSystemIdentifiers);
    VIR_FREE(switchPortPATH);
    VIR_FREE(tab_props);
    VIR_FREE(params);
    hypervFreeObject(priv, (hypervObject *)switchPort);
    hypervFreeObject(priv, (hypervObject *)virtualSwitch);
    virBufferFreeAndReset(&query);

    return result;
}


static int
hypervDomainAttachDeviceFlags(virDomainPtr domain, const char *xml,
                              unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1;
    hypervPrivate *priv = domain->conn->privateData;
    virDomainDefPtr def = NULL;
    virDomainDeviceDefPtr dev = NULL;
    char *xmlDomain = NULL;

    /* Get domain definition */
    if ((xmlDomain = hypervDomainGetXMLDesc(domain, 0)) == NULL) {
        goto cleanup;
    }
    if ((def = virDomainDefParseString(xmlDomain, priv->caps, priv->xmlopt,
                                       1 << VIR_DOMAIN_VIRT_HYPERV | VIR_DOMAIN_XML_INACTIVE)) == NULL) {
        goto cleanup;
    }

    /* Get domain device definition */
    if ((dev = virDomainDeviceDefParse(xml, def, priv->caps,
                                       priv->xmlopt, VIR_DOMAIN_XML_INACTIVE)) == NULL) {
        goto cleanup;
    }

    switch (dev->type) {
        /* Device = disk */
        case VIR_DOMAIN_DEVICE_DISK:
            if (hypervDomainAttachDisk(domain, dev->data.disk) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Could not attach disk"));
                goto cleanup;
            }
            VIR_DEBUG("Disk attached");
            break;

        /* Device = network */
        case VIR_DOMAIN_DEVICE_NET:
            if (hypervDomainAttachNetwork(domain, dev->data.net) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Could not attach network"));
                goto cleanup;
            }
            VIR_DEBUG("Network attached");
            break;

        /* Unsupported device type */
        default:
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Device attachment of type %d is not implemented"), dev->type);
            goto cleanup;
    }

    result = 0;

 cleanup:
    virDomainDefFree(def);
    virDomainDeviceDefFree(dev);
    VIR_FREE(xmlDomain);

    return result;
}



static int
hypervDomainAttachDevice(virDomainPtr domain, const char *xml)
{
    return hypervDomainAttachDeviceFlags(domain, xml, 0);
}

static virDomainPtr
hypervDomainDefineXML(virConnectPtr conn, const char *xml)
{
    hypervPrivate *priv = conn->privateData;
    virDomainDefPtr def = NULL;
    virDomainPtr domain = NULL;
    invokeXmlParam *params = NULL;
    properties_t *tab_props = NULL;
    embeddedParam embeddedparam;
    int nb_params, i;
    const char *selector = "CreationClassName=Msvm_VirtualSystemManagementService";
    char uuid_string[VIR_UUID_STRING_BUFLEN];

    /* Parse XML domain description */
    if ((def = virDomainDefParseString(xml, priv->caps, priv->xmlopt,
                                       1 << VIR_DOMAIN_VIRT_HYPERV | VIR_DOMAIN_XML_INACTIVE)) == NULL) {
        goto cleanup;
    }

    /* Create the domain if does not exist */
    if (def->uuid == NULL || (domain = hypervDomainLookupByUUID(conn, def->uuid)) == NULL) {
        /* Prepare EMBEDDED param */
        /* Edit only VM name */
        /* FIXME: cannot edit VM UUID */
        embeddedparam.nbProps = 1;
        if (VIR_ALLOC_N(tab_props, embeddedparam.nbProps) < 0)
            goto cleanup;
        (*tab_props).name = "ElementName";
        (*tab_props).val = def->name;
        embeddedparam.instanceName = "Msvm_VirtualSystemGlobalSettingData";
        embeddedparam.prop_t = tab_props;

        /* Create invokeXmlParam */
        nb_params = 1;
        if (VIR_ALLOC_N(params, nb_params) < 0)
            goto cleanup;
        (*params).name = "SystemSettingData";
        (*params).type = EMBEDDED_PARAM;
        (*params).param = &embeddedparam;

        /* Create VM */
        if (hypervInvokeMethod(priv, params, nb_params, "DefineVirtualSystem",
                               MSVM_VIRTUALSYSTEMMANAGEMENTSERVICE_RESOURCE_URI, selector) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not create new domain %s"), def->name);
            goto cleanup;
        }

        /* Get domain pointer */
        domain = hypervDomainLookupByName(conn, def->name);

        VIR_DEBUG("Domain created: name=%s, uuid=%s",
                  domain->name, virUUIDFormat(domain->uuid, uuid_string));
    }

    /* Set VM maximum memory */
    if (def->mem.max_memory > 0) {
        if (hypervDomainSetMaxMemory(domain, def->mem.max_memory) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not set VM maximum memory"));
        }
    }

    /* Set VM memory */
    if (def->mem.cur_balloon > 0) {
        if (hypervDomainSetMemory(domain, def->mem.cur_balloon) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not set VM memory"));
        }
    }

    /* Set VM vcpus */
    /*if ((int)def->vcpus > 0) {*/
        /*if (hypervDomainSetVcpus(domain, def->vcpus) < 0) {*/
            /*virReportError(VIR_ERR_INTERNAL_ERROR,*/
                           /*_("Could not set VM vCPUs"));*/
        /*}*/
    /*}*/

    /* Attach networks */
    for (i = 0; i < def->nnets; i++) {
        if (hypervDomainAttachNetwork(domain, def->nets[i]) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not attach network"));
        }
    }

    /* Attach disks */
    for (i = 0; i < def->ndisks; i++) {
        if (hypervDomainAttachDisk(domain, def->disks[i]) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not attach disk"));
        }
    }

 cleanup:
    virDomainDefFree(def);
    VIR_FREE(tab_props);
    VIR_FREE(params);

    return domain;
}



static virDomainPtr
hypervDomainCreateXML(virConnectPtr conn, const char *xmlDesc, unsigned int flags)
{
    virDomainPtr domain;

    virCheckFlags(VIR_DOMAIN_START_PAUSED | VIR_DOMAIN_START_AUTODESTROY, NULL);

    /* Create the new domain */
    domain = hypervDomainDefineXML(conn, xmlDesc);
    if (domain == NULL)
        return NULL;

    /* Start the domain */
    if (hypervInvokeMsvmComputerSystemRequestStateChange(domain, MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_ENABLED) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not start the domain %s"), domain->name);
        return domain;
    }

    /* If the VIR_DOMAIN_START_PAUSED flag is set,
       the guest domain will be started, but its CPUs will remain paused */
    if (flags & VIR_DOMAIN_START_PAUSED) {
        if (hypervDomainSuspend(domain) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not pause the domain %s"), domain->name);
        }
    }

    /* FIXME: process autodestroy flag */

    return domain;
}


static virHypervisorDriver hypervHypervisorDriver = {
    .name = "Hyper-V",
    .connectOpen = hypervConnectOpen, /* 0.9.5 */
    .connectClose = hypervConnectClose, /* 0.9.5 */
    .connectGetType = hypervConnectGetType, /* 0.9.5 */
    .connectGetHostname = hypervConnectGetHostname, /* 0.9.5 */
    .nodeGetInfo = hypervNodeGetInfo, /* 0.9.5 */
    .nodeGetFreeMemory = hypervNodeGetFreeMemory, /* 1.2.10 */
    .connectListDomains = hypervConnectListDomains, /* 0.9.5 */
    .connectNumOfDomains = hypervConnectNumOfDomains, /* 0.9.5 */
    .connectListAllDomains = hypervConnectListAllDomains, /* 0.10.2 */
    .domainLookupByID = hypervDomainLookupByID, /* 0.9.5 */
    .domainLookupByUUID = hypervDomainLookupByUUID, /* 0.9.5 */
    .domainLookupByName = hypervDomainLookupByName, /* 0.9.5 */
    .domainSuspend = hypervDomainSuspend, /* 0.9.5 */
    .domainResume = hypervDomainResume, /* 0.9.5 */
    .domainDestroy = hypervDomainDestroy, /* 0.9.5 */
    .domainDestroyFlags = hypervDomainDestroyFlags, /* 0.9.5 */
    .domainGetOSType = hypervDomainGetOSType, /* 0.9.5 */
    .domainGetInfo = hypervDomainGetInfo, /* 0.9.5 */
    .domainGetState = hypervDomainGetState, /* 0.9.5 */
    .domainGetXMLDesc = hypervDomainGetXMLDesc, /* 0.9.5 */
    .connectListDefinedDomains = hypervConnectListDefinedDomains, /* 0.9.5 */
    .connectNumOfDefinedDomains = hypervConnectNumOfDefinedDomains, /* 0.9.5 */
    .domainCreate = hypervDomainCreate, /* 0.9.5 */
    .domainCreateWithFlags = hypervDomainCreateWithFlags, /* 0.9.5 */
    .connectIsEncrypted = hypervConnectIsEncrypted, /* 0.9.5 */
    .connectIsSecure = hypervConnectIsSecure, /* 0.9.5 */
    .domainIsActive = hypervDomainIsActive, /* 0.9.5 */
    .domainIsPersistent = hypervDomainIsPersistent, /* 0.9.5 */
    .domainIsUpdated = hypervDomainIsUpdated, /* 0.9.5 */
    .domainManagedSave = hypervDomainManagedSave, /* 0.9.5 */
    .domainHasManagedSaveImage = hypervDomainHasManagedSaveImage, /* 0.9.5 */
    .domainManagedSaveRemove = hypervDomainManagedSaveRemove, /* 0.9.5 */
    .connectIsAlive = hypervConnectIsAlive, /* 0.9.8 */
    .connectGetCapabilities = hypervConnectGetCapabilities, /* 1.2.10 */
    .connectGetVersion = hypervConnectGetVersion, /* 1.2.10 */
    .connectGetMaxVcpus = hypervConnectGetMaxVcpus, /* 1.2.10 */
    .domainGetMaxVcpus = hypervDomainGetMaxVcpus, /* 1.2.10 */
    .domainGetVcpusFlags = hypervDomainGetVcpusFlags, /* 1.2.10 */
    .domainGetVcpus = hypervDomainGetVcpus, /* 1.2.10 */
    .domainSetAutostart = hypervDomainSetAutostart, /* 1.2.10 */
    .domainGetAutostart = hypervDomainGetAutostart, /* 1.2.10 */
    .domainShutdownFlags = hypervDomainShutdownFlags, /* 1.2.10 */
    .domainShutdown = hypervDomainShutdown, /* 1.2.10 */
    .domainGetSchedulerParametersFlags = hypervDomainGetSchedulerParametersFlags, /* 1.2.10 */
    .domainGetSchedulerParameters = hypervDomainGetSchedulerParameters, /* 1.2.10 */
    .domainGetSchedulerType = hypervDomainGetSchedulerType, /* 1.2.10 */
    .domainSetMaxMemory = hypervDomainSetMaxMemory, /* 1.2.10 */
    .domainSetMemory = hypervDomainSetMemory, /* 1.2.10 */
    .domainSetMemoryFlags = hypervDomainSetMemoryFlags, /* 1.2.10 */
    .domainSetVcpus = hypervDomainSetVcpus, /* 1.2.10 */
    .domainSetVcpusFlags = hypervDomainSetVcpusFlags, /* 1.2.10 */
    .domainUndefine = hypervDomainUndefine, /* 1.2.10 */
    .domainUndefineFlags = hypervDomainUndefineFlags, /* 1.2.10 */
    .domainAttachDevice = hypervDomainAttachDevice, /* 1.2.10 */
    .domainAttachDeviceFlags = hypervDomainAttachDeviceFlags, /* 1.2.10 */
    .domainDefineXML = hypervDomainDefineXML, /* 1.2.10 */
    .domainCreateXML = hypervDomainCreateXML, /* 1.2.10 */
};

/* Retrieves host system UUID  */
static int
hypervLookupHostSystemBiosUuid(hypervPrivate *priv, unsigned char *uuid)
{
    Win32_ComputerSystemProduct *computerSystem = NULL;
    virBuffer query = VIR_BUFFER_INITIALIZER;
    int result = -1;

    virBufferAddLit(&query, WIN32_COMPUTERSYSTEMPRODUCT_WQL_SELECT);

    if (hypervGetWin32ComputerSystemProductList(priv, &query, &computerSystem) < 0) {
        goto cleanup;
    }

    if (computerSystem == NULL) {
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("Unable to get Win32_ComputerSystemProduct"));
        goto cleanup;
    }

    if (virUUIDParse(computerSystem->data->UUID, uuid) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not parse UUID from string '%s'"),
                       computerSystem->data->UUID);
        goto cleanup;
    }

    result = 0;

 cleanup:
    hypervFreeObject(priv, (hypervObject *)computerSystem);
    virBufferFreeAndReset(&query);

    return result;
}



static virCapsPtr hypervCapsInit(hypervPrivate *priv)
{
    virCapsPtr caps = NULL;
    virCapsGuestPtr guest = NULL;

    caps = virCapabilitiesNew(VIR_ARCH_X86_64, 1, 1);

    if (caps == NULL) {
        virReportOOMError();
        return NULL;
    }

    /* virCapabilitiesSetMacPrefix(caps, (unsigned char[]){ 0x00, 0x0c, 0x29 }); */

    if (hypervLookupHostSystemBiosUuid(priv,caps->host.host_uuid) < 0) {
        goto failure;
    }

    /* i686 */
    guest = virCapabilitiesAddGuest(caps, VIR_DOMAIN_OSTYPE_HVM, VIR_ARCH_I686, NULL, NULL, 0, NULL);
    if (guest == NULL) {
        goto failure;
    }
    if (virCapabilitiesAddGuestDomain(guest, VIR_DOMAIN_VIRT_HYPERV, NULL, NULL, 0, NULL) == NULL) {
        goto failure;
    }

    /* x86_64 */
    guest = virCapabilitiesAddGuest(caps, VIR_DOMAIN_OSTYPE_HVM, VIR_ARCH_X86_64, NULL, NULL, 0, NULL);
    if (guest == NULL) {
        goto failure;
    }
    if (virCapabilitiesAddGuestDomain(guest, VIR_DOMAIN_VIRT_HYPERV, NULL, NULL, 0, NULL) == NULL) {
        goto failure;
    }

    return caps;

 failure:
    virObjectUnref(caps);
    return NULL;
}


static void
hypervDebugHandler(const char *message, debug_level_e level,
                   void *user_data ATTRIBUTE_UNUSED)
{
    switch (level) {
      case DEBUG_LEVEL_ERROR:
      case DEBUG_LEVEL_CRITICAL:
        VIR_ERROR(_("openwsman error: %s"), message);
        break;

      case DEBUG_LEVEL_WARNING:
        VIR_WARN("openwsman warning: %s", message);
        break;

      default:
        /* Ignore the rest */
        break;
    }
}


static virConnectDriver hypervConnectDriver = {
    .hypervisorDriver = &hypervHypervisorDriver,
    .networkDriver = &hypervNetworkDriver,
};

int
hypervRegister(void)
{
    /* Forward openwsman errors and warnings to libvirt's logging */
    debug_add_handler(hypervDebugHandler, DEBUG_LEVEL_WARNING, NULL);

    return virRegisterConnectDriver(&hypervConnectDriver,
                                    false);
}
