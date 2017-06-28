/*
 * lxc_docker.c: LXC native docker configuration import
 *
 * Copyright (C) 2017 Venkat Datta N H
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
 * Author: Venkat Datta N H <nhvenkatdatta@gmail.com>
 */

#include <config.h>

#include "util/viralloc.h"
#include "util/virfile.h"
#include "util/virstring.h"
#include "util/virconf.h"
#include "util/virjson.h"
#include "util/virutil.h"
#include "virerror.h"
#include "virlog.h"
#include "conf/domain_conf.h"
#include "lxc_docker.h"
#include "secret_conf.h"

#define VIR_FROM_THIS VIR_FROM_LXC

VIR_LOG_INIT("lxc.lxc_docker");

static int virLXCDockerParseCpu(virDomainDefPtr dom,
                            virDomainXMLOptionPtr xmlopt,
                            virJSONValuePtr prop)
{
    int vcpus;

    if (virJSONValueObjectGetNumberInt(prop, "CpuShares", &vcpus)  != 0)
        return -1;

    if (virDomainDefSetVcpusMax(dom, vcpus, xmlopt) < 0)
        return -1;

    if (virDomainDefSetVcpus(dom, vcpus) < 0)
        return -1;

    return 0;
}

static int virLXCDockerParseMem(virDomainDefPtr dom,
                   virJSONValuePtr prop)
{
    unsigned long long mem;

    if (virJSONValueObjectGetNumberUlong(prop, "Memory", &mem) != 0)
        return -1;

    virDomainDefSetMemoryTotal(dom, mem / 1024);
    dom->mem.cur_balloon = mem / 1024;

    return 0;
}

virDomainDefPtr virLXCDockerParseJSONConfig(virCapsPtr caps ATTRIBUTE_UNUSED,
                                            virDomainXMLOptionPtr xmlopt,
                                            const char *config)
{
    virJSONValuePtr json_obj;
    virJSONValuePtr host_config;

    if (!(json_obj = virJSONValueFromString(config)))
        return NULL;

    virDomainDefPtr def;

    if (!(def = virDomainDefNew()))
        goto error;

    def->id = -1;
    def->mem.cur_balloon = 64*1024;
    virDomainDefSetMemoryTotal(def, def->mem.cur_balloon);

    if ((host_config = virJSONValueObjectGetObject(json_obj, "HostConfig")) != NULL) {
        if (virLXCDockerParseCpu(def, xmlopt, host_config) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("failed to parse VCpu"));
            goto error;
        }

        if (virLXCDockerParseMem(def, host_config) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("failed to parse Memory"));
            goto error;
        }
    }

    def->clock.offset = VIR_DOMAIN_CLOCK_OFFSET_UTC;
    def->onReboot = VIR_DOMAIN_LIFECYCLE_RESTART;
    def->onCrash = VIR_DOMAIN_LIFECYCLE_CRASH_DESTROY;
    def->onPoweroff = VIR_DOMAIN_LIFECYCLE_DESTROY;
    def->virtType = VIR_DOMAIN_VIRT_LXC;
    def->os.type = VIR_DOMAIN_OSTYPE_EXE;

    return def;

 error:
    virDomainDefFree(def);
    return NULL;
}
