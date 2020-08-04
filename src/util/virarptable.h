/*
 * virarptable.h Linux ARP table handling
 *
 * Copyright (C) 2018 Chen Hanxiao
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virArpTableEntry virArpTableEntry;
typedef virArpTableEntry *virArpTableEntryPtr;
typedef struct _virArpTable virArpTable;
typedef virArpTable *virArpTablePtr;

struct _virArpTableEntry{
    char *ipaddr;
    char *mac;
};

struct _virArpTable {
    int n;
    virArpTableEntryPtr t;
};

virArpTablePtr virArpTableGet(void);
void virArpTableFree(virArpTablePtr table);
