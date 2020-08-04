/*
 * lxc_hostdev.h: VIRLXC hostdev management
 *
 * Copyright (C) 2006-2007, 2009-2010 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "lxc_conf.h"
#include "domain_conf.h"

int virLXCUpdateActiveUSBHostdevs(virLXCDriverPtr driver,
                                  virDomainDefPtr def);
int virLXCFindHostdevUSBDevice(virDomainHostdevDefPtr hostdev,
                               bool mandatory,
                               virUSBDevicePtr *usb);
int virLXCPrepareHostdevUSBDevices(virLXCDriverPtr driver,
                                   const char *name,
                                   virUSBDeviceListPtr list);
int virLXCPrepareHostDevices(virLXCDriverPtr driver,
                             virDomainDefPtr def);
void virLXCDomainReAttachHostDevices(virLXCDriverPtr driver,
                                     virDomainDefPtr def);
