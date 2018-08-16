/*
 * Copyright (C) 2018 Red Hat, Inc.
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
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <locale.h>

#include "internal.h"
#include "testutils.h"
#include "viralloc.h"
#include "../tools/vsh-table.h"

static int
testVshTableNew(const void *opaque ATTRIBUTE_UNUSED)
{
    int ret = 0;

    if (vshTableNew(NULL)) {
        fprintf(stderr, "expected failure when passing null to"
                        "vshtablenew\n");
        ret = -1;
    }

    return ret;
}

static int
testVshTableHeader(const void *opaque ATTRIBUTE_UNUSED)
{
    int ret = 0;
    char *out;
    const char *exp = "\
 1   fedora28   running  \n\
 2   rhel7.5    running  \n";
    const char *exp2 = "\
 Id   Name       State    \n\
--------------------------\n\
 1    fedora28   running  \n\
 2    rhel7.5    running  \n";

    vshTablePtr table = vshTableNew("Id", "Name", "State",
                                    NULL); //to ask about return
    if (!table)
        goto cleanup;

    vshTableRowAppend(table, "1", "fedora28", "running", NULL);
    vshTableRowAppend(table, "2", "rhel7.5", "running",
                      NULL);

    out = vshTablePrintToString(table, false);
    if (virTestCompareToString(exp, out) < 0)
        ret = -1;

    VIR_FREE(out);
    out = vshTablePrintToString(table, true);
    if (virTestCompareToString(exp2, out) < 0)
        ret = -1;

 cleanup:
    VIR_FREE(out);
    vshTableFree(table);
    return ret;
}

static int
testVshTableNewUnicode(const void *opaque ATTRIBUTE_UNUSED)
{

    int ret = 0;
    char *out;

    char *locale = setlocale(LC_CTYPE, NULL);
    if (!setlocale(LC_CTYPE, "en_US.UTF-8"))
        return EXIT_AM_SKIP;

    const char *exp = "\
 Id   名稱                  государство  \n\
-----------------------------------------\n\
 1    fedora28              running      \n\
 2    🙊🙉🙈rhel7.5🙆🙆🙅   running      \n";
    vshTablePtr table;

    table = vshTableNew("Id", "名稱", "государство", NULL);
    if (!table)
        goto cleanup;

    vshTableRowAppend(table, "1", "fedora28", "running", NULL);
    vshTableRowAppend(table, "2", "🙊🙉🙈rhel7.5🙆🙆🙅", "running",
                      NULL);

    out = vshTablePrintToString(table, true);
    if (virTestCompareToString(exp, out) < 0)
        ret = -1;

 cleanup:
    setlocale(LC_CTYPE, locale);
    VIR_FREE(out);
    vshTableFree(table);
    return ret;
}

static int
testVshTableRowAppend(const void *opaque ATTRIBUTE_UNUSED)
{
    int ret = 0;

    vshTablePtr table = vshTableNew("Id", "Name", NULL);
    if (!table)
        goto cleanup;

    if (vshTableRowAppend(table, NULL) >= 0) {
        fprintf(stderr, "Appending NULL shouldn't work\n");
        ret = -1;
    }

    if (vshTableRowAppend(table, "2", NULL) >= 0) {
        fprintf(stderr, "Appending less items than in header\n");
        ret = -1;
    }

    if (vshTableRowAppend(table, "2", "rhel7.5", "running",
                      NULL) >= 0) {
        fprintf(stderr, "Appending more items than in header\n");
        ret = -1;
    }

    if (vshTableRowAppend(table, "2", "rhel7.5", NULL) < 0) {
        fprintf(stderr, "Appending same number of items as in header"
                        " should not return NULL\n");
        ret = -1;
    }

 cleanup:
    vshTableFree(table);
    return ret;
}

static int
testNTables(const void *opaque ATTRIBUTE_UNUSED)
{
    int ret = 0;
    vshTablePtr table1;
    vshTablePtr table2;
    vshTablePtr table3;
    const char *exp1 = "\
 Id   Name       Status   \n\
--------------------------\n\
 1    fedora28   running  \n\
 2    rhel7.5    running  \n";
    const char *exp2 = "\
 Id   Name   Status  \n\
---------------------\n";
    const char *exp3 = "\
 Id  \n\
-----\n\
 1   \n\
 2   \n\
 3   \n\
 4   \n";
    char *out1;
    char *out2;
    char *out3;

    table1 = vshTableNew("Id", "Name", "Status", NULL);
    if (!table1)
        goto cleanup;
    vshTableRowAppend(table1, "1", "fedora28", "running", NULL);
    vshTableRowAppend(table1, "2", "rhel7.5", "running", NULL);
    out1 = vshTablePrintToString(table1, true);

    table2 = vshTableNew("Id", "Name", "Status", NULL);
    if (!table2)
        goto cleanup;
    out2 = vshTablePrintToString(table2, true);

    table3 = vshTableNew("Id", NULL);
    if (!table3)
        goto cleanup;
    vshTableRowAppend(table3, "1", NULL);
    vshTableRowAppend(table3, "2", NULL);
    vshTableRowAppend(table3, "3", NULL);
    vshTableRowAppend(table3, "4", NULL);
    out3 = vshTablePrintToString(table3, true);

    if (virTestCompareToString(exp1, out1) < 0)
        ret = -1;
    if (virTestCompareToString(exp2, out2) < 0)
        ret = -1;
    if (virTestCompareToString(exp3, out3) < 0)
        ret = -1;

 cleanup:
    VIR_FREE(out1);
    VIR_FREE(out2);
    VIR_FREE(out3);
    vshTableFree(table1);
    vshTableFree(table2);
    vshTableFree(table3);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("testVshTableNew", testVshTableNew,
                   NULL) < 0)
        ret = -1;

    if (virTestRun("testVshTableHeader", testVshTableHeader,
                   NULL) < 0)
        ret = -1;

    if (virTestRun("testVshTableRowAppend", testVshTableRowAppend,
                   NULL) < 0)
        ret = -1;

    if (virTestRun("testVshTableWithUnicode", testVshTableNewUnicode,
                   NULL) < 0)
        ret = -1;

    if (virTestRun("testNTables", testNTables,
                   NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
