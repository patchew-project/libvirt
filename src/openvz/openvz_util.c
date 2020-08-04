/*
 * openvz_util.c: core driver methods for managing OpenVZ VEs
 *
 * Copyright (C) 2013-2014 Red Hat, Inc.
 * Copyright (C) 2012 Guido Günther
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include <unistd.h>

#include "internal.h"

#include "virerror.h"
#include "vircommand.h"
#include "datatypes.h"
#include "viralloc.h"

#include "openvz_conf.h"
#include "openvz_util.h"

#include "virutil.h"

#define VIR_FROM_THIS VIR_FROM_OPENVZ

long
openvzKBPerPages(void)
{
    static long kb_per_pages;

    if (kb_per_pages == 0) {
        if ((kb_per_pages = virGetSystemPageSizeKB()) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Can't determine page size"));
            kb_per_pages = 0;
            return -1;
        }
    }
    return kb_per_pages;
}

char*
openvzVEGetStringParam(virDomainPtr domain, const char* param)
{
    int len;
    char *output = NULL;

    virCommandPtr cmd = virCommandNewArgList(VZLIST,
                                             "-o",
                                             param,
                                             domain->name,
                                             "-H", NULL);

    virCommandSetOutputBuffer(cmd, &output);
    if (virCommandRun(cmd, NULL) < 0) {
        VIR_FREE(output);
        /* virCommandRun sets the virError */
        goto cleanup;
    }

    /* delete trailing newline */
    len = strlen(output);
    if (len && output[len - 1] == '\n')
        output[len - 1] = '\0';

 cleanup:
    virCommandFree(cmd);
    return output;
}
