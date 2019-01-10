/*
 * virfirewalld.c: support for firewalld (https://firewalld.org)
 *
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <stdarg.h>

#include "virfirewall.h"
#include "virfirewalld.h"
#include "virerror.h"
#include "virutil.h"
#include "virlog.h"
#include "virdbus.h"

#define VIR_FROM_THIS VIR_FROM_FIREWALLD

VIR_LOG_INIT("util.firewalld");

VIR_ENUM_DECL(virFirewallLayerFirewallD)
VIR_ENUM_IMPL(virFirewallLayerFirewallD, VIR_FIREWALL_LAYER_LAST,
              "eb", "ipv4", "ipv6")

int
virFirewallDStatus(void)
{
    return virDBusIsServiceRegistered(VIR_FIREWALL_FIREWALLD_SERVICE);
}


int
virFirewallDApplyRule(virFirewallLayer layer,
                      char **args, size_t argsLen,
                      bool ignoreErrors,
                      char **output)
{
    const char *ipv = virFirewallLayerFirewallDTypeToString(layer);
    DBusConnection *sysbus = virDBusGetSystemBus();
    DBusMessage *reply = NULL;
    virError error;
    int ret = -1;

    if (!sysbus)
        return -1;

    memset(&error, 0, sizeof(error));

    if (!ipv) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown firewall layer %d"),
                       layer);
        goto cleanup;
    }

    if (virDBusCallMethod(sysbus,
                          &reply,
                          &error,
                          VIR_FIREWALL_FIREWALLD_SERVICE,
                          "/org/fedoraproject/FirewallD1",
                          "org.fedoraproject.FirewallD1.direct",
                          "passthrough",
                          "sa&s",
                          ipv,
                          (int)argsLen,
                          args) < 0)
        goto cleanup;

    if (error.level == VIR_ERR_ERROR) {
        /*
         * As of firewalld-0.3.9.3-1.fc20.noarch the name and
         * message fields in the error look like
         *
         *    name="org.freedesktop.DBus.Python.dbus.exceptions.DBusException"
         * message="COMMAND_FAILED: '/sbin/iptables --table filter --delete
         *          INPUT --in-interface virbr0 --protocol udp --destination-port 53
         *          --jump ACCEPT' failed: iptables: Bad rule (does a matching rule
         *          exist in that chain?)."
         *
         * We'd like to only ignore DBus errors precisely related to the failure
         * of iptables/ebtables commands. A well designed DBus interface would
         * return specific named exceptions not the top level generic python dbus
         * exception name. With this current scheme our only option is todo a
         * sub-string match for 'COMMAND_FAILED' on the message. eg like
         *
         * if (ignoreErrors &&
         *     STREQ(error.name,
         *           "org.freedesktop.DBus.Python.dbus.exceptions.DBusException") &&
         *     STRPREFIX(error.message, "COMMAND_FAILED"))
         *    ...
         *
         * But this risks our error detecting code being broken if firewalld changes
         * ever alter the message string, so we're avoiding doing that.
         */
        if (ignoreErrors) {
            VIR_DEBUG("Ignoring error '%s': '%s'",
                      error.str1, error.message);
        } else {
            virReportErrorObject(&error);
            goto cleanup;
        }
    } else {
        if (virDBusMessageRead(reply, "s", output) < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    virResetError(&error);
    virDBusMessageUnref(reply);
    return ret;
}
