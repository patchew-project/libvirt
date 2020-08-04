/*
 * virsysinfopriv.h: Header for functions tested in the test suite
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_VIRSYSINFOPRIV_H_ALLOW
# error "virsysinfopriv.h may only be included by virsysinfo.c or test suites"
#endif /* LIBVIRT_VIRSYSINFOPRIV_H_ALLOW */

#pragma once

void
virSysinfoSetup(const char *sysinfo,
                const char *cpuinfo);

virSysinfoDefPtr
virSysinfoReadPPC(void);

virSysinfoDefPtr
virSysinfoReadARM(void);

virSysinfoDefPtr
virSysinfoReadS390(void);

virSysinfoDefPtr
virSysinfoReadDMI(void);
