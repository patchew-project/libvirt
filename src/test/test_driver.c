/*
 * test_driver.c: A "mock" hypervisor for use by application unit tests
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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
 * Daniel Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libxml/xmlsave.h>
#include <libxml/xpathInternals.h>


#include "virerror.h"
#include "datatypes.h"
#include "test_driver.h"
#include "virbuffer.h"
#include "viruuid.h"
#include "capabilities.h"
#include "configmake.h"
#include "viralloc.h"
#include "network_conf.h"
#include "interface_conf.h"
#include "domain_conf.h"
#include "domain_event.h"
#include "network_event.h"
#include "snapshot_conf.h"
#include "fdstream.h"
#include "storage_conf.h"
#include "storage_event.h"
#include "node_device_conf.h"
#include "node_device_event.h"
#include "virxml.h"
#include "virthread.h"
#include "virlog.h"
#include "virfile.h"
#include "virtypedparam.h"
#include "virrandom.h"
#include "virstring.h"
#include "cpu/cpu.h"
#include "virauth.h"
#include "viratomic.h"
#include "virdomainobjlist.h"
#include "virhostcpu.h"

#include "test_hypervisor_driver.h"
#include "test_storage_driver.h"
#include "test_network_driver.h"
#include "test_interface_driver.h"
#include "test_device_driver.h"

#include "test_private_driver.h"

VIR_LOG_INIT("test.test_driver");


void testDriverLock(testDriverPtr driver)
{
    virMutexLock(&driver->lock);
}

void testDriverUnlock(testDriverPtr driver)
{
    virMutexUnlock(&driver->lock);
}

void testObjectEventQueue(testDriverPtr driver,
                                 virObjectEventPtr event)
{
    if (!event)
        return;

    virObjectEventStateQueue(driver->eventState, event);
}

unsigned long long defaultPoolCap = (100 * 1024 * 1024 * 1024ull);
unsigned long long defaultPoolAlloc;

int testStoragePoolObjSetDefaults(virStoragePoolObjPtr pool);
int testNodeGetInfo(virConnectPtr conn, virNodeInfoPtr info);

/* No shared state between simultaneous test connections initialized
 * from a file.  */

static virConnectDriver testConnectDriver = {
    .hypervisorDriver = &testHypervisorDriver,
    .interfaceDriver = &testInterfaceDriver,
    .networkDriver = &testNetworkDriver,
    .nodeDeviceDriver = &testNodeDeviceDriver,
    .nwfilterDriver = NULL,
    .secretDriver = NULL,
    .storageDriver = &testStorageDriver,
};

/**
 * testRegister:
 *
 * Registers the test driver
 */
int
testRegister(void)
{
    return virRegisterConnectDriver(&testConnectDriver,
                                    false);
}
