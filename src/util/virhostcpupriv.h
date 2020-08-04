/*
 * virhostcpupriv.h: helper APIs for host CPU info
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_VIRHOSTCPUPRIV_H_ALLOW
# error "virhostcpupriv.h may only be included by virhostcpu.c or test suites"
#endif /* LIBVIRT_VIRHOSTCPUPRIV_H_ALLOW */

#pragma once

#include "virhostcpu.h"

#ifdef __linux__
int virHostCPUGetInfoPopulateLinux(FILE *cpuinfo,
                                   virArch arch,
                                   unsigned int *cpus,
                                   unsigned int *mhz,
                                   unsigned int *nodes,
                                   unsigned int *sockets,
                                   unsigned int *cores,
                                   unsigned int *threads);

int virHostCPUGetStatsLinux(FILE *procstat,
                            int cpuNum,
                            virNodeCPUStatsPtr params,
                            int *nparams);
#endif

int virHostCPUReadSignature(virArch arch,
                            FILE *cpuinfo,
                            char **signature);
