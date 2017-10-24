/*
 * qemu_event.c:
 *    optimize qemu async event handling.
 *
 * Copyright (C) 2017-2026 Nutanix, Inc.
 * Copyright (C) 2017 Prerna Saxena
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
 * Author: Prerna Saxena <prerna.saxena@nutanix.com>
 */

#include "config.h"
#include "internal.h"
# include "qemu_monitor.h"
# include "qemu_conf.h"
# include "qemu_event.h"
#include "qemu_process.h"

#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"
#include "virobject.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_event");

VIR_ENUM_IMPL(qemuMonitorEvent,
              QEMU_EVENT_LAST,
              "ACPI Event", "Balloon Change", "Block IO Error",
              "Block Job Event",
              "Block Write Threshold", "Device Deleted",
              "Device Tray Moved", "Graphics", "Guest Panicked",
              "Migration", "Migration pass",
              "Nic RX Filter Changed", "Powerdown", "Reset", "Resume",
              "RTC Change", "Shutdown", "Stop",
              "Suspend", "Suspend To Disk",
              "Virtual Serial Port Change",
              "Wakeup", "Watchdog");

virQemuEventList* virQemuEventListInit(void)
{
    virQemuEventList *ev_list;
    if (VIR_ALLOC(ev_list) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Unable to allocate virQemuEventList");
        return NULL;
    }

    if (virMutexInit(&ev_list->lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("cannot initialize mutex"));
        VIR_FREE(ev_list);
        return NULL;
    }

    ev_list->head = NULL;
    ev_list->last = NULL;

    return ev_list;
}
