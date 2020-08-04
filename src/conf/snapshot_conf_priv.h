/*
 * snapshot_conf_priv.h: domain snapshot XML processing (private)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_SNAPSHOT_CONF_PRIV_H_ALLOW
# error "snapshot_conf_priv.h may only be included by snapshot_conf.c or test suites"
#endif /* LIBVIRT_SNAPSHOT_CONF_PRIV_H_ALLOW */

#pragma once

#include "snapshot_conf.h"

int
virDomainSnapshotDiskDefParseXML(xmlNodePtr node,
                                 xmlXPathContextPtr ctxt,
                                 virDomainSnapshotDiskDefPtr def,
                                 unsigned int flags,
                                 virDomainXMLOptionPtr xmlopt);

void
virDomainSnapshotDiskDefFree(virDomainSnapshotDiskDefPtr disk);
