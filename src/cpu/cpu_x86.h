/*
 * cpu_x86.h: CPU driver for CPUs with x86 compatible CPUID instruction
 *
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "cpu.h"
#include "cpu_x86_data.h"

extern struct cpuArchDriver cpuDriverX86;

int virCPUx86DataAdd(virCPUDataPtr cpuData,
                     const virCPUx86DataItem *cpuid);

int virCPUx86DataSetSignature(virCPUDataPtr cpuData,
                              unsigned int family,
                              unsigned int model,
                              unsigned int stepping);

uint32_t virCPUx86DataGetSignature(virCPUDataPtr cpuData,
                                   unsigned int *family,
                                   unsigned int *model,
                                   unsigned int *stepping);

int virCPUx86DataSetVendor(virCPUDataPtr cpuData,
                           const char *vendor);

bool virCPUx86FeatureFilterSelectMSR(const char *name,
                                     virCPUFeaturePolicy policy,
                                     void *opaque);

bool virCPUx86FeatureFilterDropMSR(const char *name,
                                   virCPUFeaturePolicy policy,
                                   void *opaque);
