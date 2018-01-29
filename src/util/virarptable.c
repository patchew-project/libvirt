/*
 * virarptable.c Linux ARP table handling
 *
 * Copyright (C) 2018 Chen Hanxiao
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
 * Authors:
 *     Chen Hanxiao <chenhanxiao@gmail.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>

#include "viralloc.h"
#include "virarptable.h"
#include "virfile.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

#ifdef __linux__

virArpTablePtr virArpTableGet(void)
{
    FILE *fp = NULL;
    char line[1024];
    int num = 0;
    virArpTablePtr table = NULL;

    if (VIR_ALLOC(table) < 0)
        return NULL;

    if (!(fp = fopen("/proc/net/arp", "r")))
        goto cleanup;

    while (fgets(line, sizeof(line), fp)) {
        char ip[32], mac[32], dev_name[32], hwtype[32],
             flags[32], mask[32], nouse[32];

        if (STRPREFIX(line, "IP address"))
            continue;

        if (VIR_REALLOC_N(table->t, num + 1) < 0)
            goto cleanup;

        table->n = num + 1;

        /* /proc/net/arp looks like:
         * 172.16.17.254  0x1 0x2  e4:68:a3:8d:ed:d3  *   enp3s0
         */
        sscanf(line, "%[0-9.]%[ ]%[^ ]%[ ]%[^ ]%[ ]%[^ ]%[ ]%[^ ]%[ ]%[^ \t\n]",
               ip, nouse,
               hwtype, nouse,
               flags, nouse,
               mac, nouse,
               mask, nouse,
               dev_name);

        if (VIR_STRDUP(table->t[num].ipaddr, ip) < 0)
            goto cleanup;

        if (VIR_STRDUP(table->t[num].mac, mac) < 0)
            goto cleanup;

        if (VIR_STRDUP(table->t[num].dev_name, dev_name) < 0)
            goto cleanup;

        num++;
    }

    return table;

 cleanup:
    VIR_FORCE_FCLOSE(fp);
    return NULL;
}

#else

virArpTablePtr virArpTableGet(void)
{
    virReportError(VIR_ERR_NO_SUPPORT, "%s",
                   _("get arp table not implemented on this platform"));
    return NULL;
}

#endif /* __linux__ */

void
virArpTableFree(virArpTablePtr table)
{
    size_t i;
    for (i = 0; i < table->n; i++) {
        VIR_FREE(table->t[i].ipaddr);
        VIR_FREE(table->t[i].mac);
        VIR_FREE(table->t[i].dev_name);
    }
    VIR_FREE(table);
}
