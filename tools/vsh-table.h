/*
 * vsh-table.h: table printing helper
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

typedef struct _vshTable vshTable;
typedef struct _vshTableRow vshTableRow;
typedef vshTable *vshTablePtr;
typedef vshTableRow *vshTableRowPtr;

void vshTableFree(vshTablePtr table);
vshTablePtr vshTableNew(const char *format, ...);
int vshTableRowAppend(vshTablePtr table, const char *arg, ...);
void vshTablePrintToStdout(vshTablePtr table, vshControl *ctl);
char *vshTablePrintToString(vshTablePtr table, bool header);
