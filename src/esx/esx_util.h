/*
 * esx_util.h: utility functions for the VMware ESX driver
 *
 * Copyright (C) 2009 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "viruri.h"

#define ESX_VI_CHECK_ARG_LIST(val) \
    do { \
        if (!val || *val) { \
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Invalid argument")); \
            return -1; \
        } \
    } while (0)

typedef struct _esxUtil_ParsedUri esxUtil_ParsedUri;

struct _esxUtil_ParsedUri {
    char *transport;
    char *vCenter;
    bool noVerify;
    bool autoAnswer;
    bool proxy;
    int proxy_type;
    char *proxy_hostname;
    int proxy_port;
    char *path;
};

int esxUtil_ParseUri(esxUtil_ParsedUri **parsedUri, virURIPtr uri);

void esxUtil_FreeParsedUri(esxUtil_ParsedUri **parsedUri);

int esxUtil_ParseVirtualMachineIDString(const char *id_string, int *id);

int esxUtil_ParseDatastorePath(const char *datastorePath, char **datastoreName,
                               char **directoryName, char **directoryAndFileName);

int esxUtil_ResolveHostname(const char *hostname,
                            char *ipAddress, size_t ipAddress_length);

int esxUtil_ReformatUuid(const char *input, char *output);

char *esxUtil_EscapeBase64(const char *string);

void esxUtil_ReplaceSpecialWindowsPathChars(char *string);

char *esxUtil_EscapeDatastoreItem(const char *string);

char *esxUtil_EscapeForXml(const char *string);
