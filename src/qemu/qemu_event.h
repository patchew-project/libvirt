/*
 * qemu_event.h: interaction with QEMU JSON monitor event layer
 * Carve out improved interactions with qemu.
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


#ifndef QEMU_EVENT_H
# define QEMU_EVENT_H

# include "internal.h"
# include "virobject.h"

typedef enum {
    QEMU_EVENT_ACPI_OST,
    QEMU_EVENT_BALLOON_CHANGE,
    QEMU_EVENT_BLOCK_IO_ERROR,
    QEMU_EVENT_BLOCK_JOB,
    QEMU_EVENT_BLOCK_WRITE_THRESHOLD,
    QEMU_EVENT_DEVICE_DELETED,
    QEMU_EVENT_DEVICE_TRAY_MOVED,
    QEMU_EVENT_GRAPHICS,
    QEMU_EVENT_GUEST_PANICKED,
    QEMU_EVENT_MIGRATION,
    QEMU_EVENT_MIGRATION_PASS,
    QEMU_EVENT_NIC_RX_FILTER_CHANGED,
    QEMU_EVENT_POWERDOWN,
    QEMU_EVENT_RESET,
    QEMU_EVENT_RESUME,
    QEMU_EVENT_RTC_CHANGE,
    QEMU_EVENT_SHUTDOWN,
    QEMU_EVENT_STOP,
    QEMU_EVENT_SUSPEND,
    QEMU_EVENT_SUSPEND_DISK,
    QEMU_EVENT_SERIAL_CHANGE,
    QEMU_EVENT_WAKEUP,
    QEMU_EVENT_WATCHDOG,

    QEMU_EVENT_LAST,

} qemuMonitorEventType;

VIR_ENUM_DECL(qemuMonitorEvent);

struct _qemuEvent;
typedef struct _qemuEvent * qemuEventPtr;

struct qemuEventAcpiOstInfoData {
    char *alias;
    char *slotType;
    char *slot;
    unsigned int source;
    unsigned int status;
};

struct qemuEventBalloonChangeData {
    unsigned long long actual;
};

struct qemuEventIOErrorData {
    char *device;
    int action;
    char *reason;
};

struct qemuEventBlockJobData {
    int status;
    char *device;
    int type;
};

struct qemuEventBlockThresholdData {
    char *nodename;
    unsigned long long threshold;
    unsigned long long excess;
};

struct qemuEventDeviceDeletedData {
    char *device;
};

struct qemuEventTrayChangeData {
    char *devAlias;
    int reason;
};

struct qemuEventGuestPanicData {
};

struct qemuEventMigrationStatusData {
    int status;
};

struct qemuEventMigrationPassData {
    int pass;
};

struct qemuEventNicRxFilterChangeData {
    char *devAlias;
};

struct qemuEventRTCChangeData {
    long long offset;
};

struct qemuEventGraphicsData {
    int phase;
    int localFamilyID;
    int remoteFamilyID;

    char *localNode;
    char *localService;
    char *remoteNode;
    char *remoteService;
    char *authScheme;
    char *x509dname;
    char *saslUsername;
};

struct qemuEventSerialChangeData {
    char *devAlias;
    bool connected;
};

struct qemuEventWatchdogData {
    int action;
};

struct _qemuEvent {
    qemuMonitorEventType ev_type;
    unsigned long ev_id;
    long long seconds;
    unsigned int micros;
    virDomainObjPtr vm;
    void (*handler)(qemuEventPtr ev, void *opaque);
    union qemuEventData {
        struct qemuEventAcpiOstInfoData ev_acpi;
        struct qemuEventBalloonChangeData ev_balloon;
        struct qemuEventIOErrorData ev_IOErr;
        struct qemuEventBlockJobData ev_blockJob;
        struct qemuEventBlockThresholdData ev_threshold;
        struct qemuEventDeviceDeletedData ev_deviceDel;
        struct qemuEventTrayChangeData ev_tray;
        struct qemuEventGuestPanicData ev_panic;
        struct qemuEventMigrationStatusData ev_migStatus;
        struct qemuEventMigrationPassData ev_migPass;
        struct qemuEventNicRxFilterChangeData ev_nic;
        struct qemuEventRTCChangeData ev_rtc;
        struct qemuEventGraphicsData ev_graphics;
        struct qemuEventSerialChangeData ev_serial;
        struct qemuEventWatchdogData ev_watchdog;
    } evData;
};



// Define a Global event queue.
// This is a double LL with qemuEventPtr embedded.

struct _qemuGlobalEventListElement {
    unsigned long ev_id;
    virDomainObjPtr vm;
    struct _qemuGlobalEventListElement *prev;
    struct _qemuGlobalEventListElement *next;
};

struct _qemuGlobalEventList {
    virMutex lock;
    struct _qemuGlobalEventListElement *head;
    struct _qemuGlobalEventListElement *last;
};

/* Global list of event entries of all VM */
typedef struct _qemuGlobalEventList virQemuEventList;

struct _qemuVmEventQueueElement {
    qemuEventPtr ev;
    struct _qemuVmEventQueueElement *next;
};

// Define a Per-VM event queue. 
struct _qemuVmEventQueue {
    struct _qemuVmEventQueueElement *head;
    struct _qemuVmEventQueueElement *last;
    virMutex lock;
 };

typedef struct _qemuVmEventQueue virQemuVmEventQueue;



virQemuEventList* virQemuEventListInit(void);
int virQemuVmEventListInit(virDomainObjPtr vm);
/**
 * viEnqueueVMEvent()
 * Adds a new event to :
 *  - the global event queue.
 *  - the event queue for this VM
 *
 */
int virEnqueueVMEvent(virQemuEventList *qlist, qemuEventPtr ev);
qemuEventPtr virDequeueVMEvent(virQemuEventList *qlist, virDomainObjPtr vm);
void virEventWorkerScanQueue(void *dummy, void *opaque);
void virEventRunHandler(qemuEventPtr ev, void *opaque);
void virDomainConsumeVMEvents(virDomainObjPtr vm, void *opaque);
#endif
