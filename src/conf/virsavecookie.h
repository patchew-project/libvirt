/**
 * virsavecookie.h: Save cookie handling
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libxml/xpath.h>

#include "internal.h"
#include "virobject.h"
#include "virbuffer.h"


typedef int (*virSaveCookieParseFunc)(xmlXPathContextPtr ctxt,
                                      virObjectPtr *obj);
typedef int (*virSaveCookieFormatFunc)(virBufferPtr buf,
                                       virObjectPtr obj);

typedef struct _virSaveCookieCallbacks virSaveCookieCallbacks;
typedef virSaveCookieCallbacks *virSaveCookieCallbacksPtr;
struct _virSaveCookieCallbacks {
    virSaveCookieParseFunc parse;
    virSaveCookieFormatFunc format;
};


int
virSaveCookieParse(xmlXPathContextPtr ctxt,
                   virObjectPtr *obj,
                   virSaveCookieCallbacksPtr saveCookie);

int
virSaveCookieParseString(const char *xml,
                         virObjectPtr *obj,
                         virSaveCookieCallbacksPtr saveCookie);

int
virSaveCookieFormatBuf(virBufferPtr buf,
                       virObjectPtr obj,
                       virSaveCookieCallbacksPtr saveCookie);

char *
virSaveCookieFormat(virObjectPtr obj,
                    virSaveCookieCallbacksPtr saveCookie);
