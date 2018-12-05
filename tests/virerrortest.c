/*
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
 * License along with this library;  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "testutils.h"

#define __VIR_ERROR_ALLOW_INCLUDE_PRIV_H__
#include "virerrorpriv.h"

static int
virErrorTestMsgFormatInfoOne(const char *msg)
{
    bool found = false;
    char *next;
    int ret = 0;

    for (next = (char *)msg; (next = strchr(next, '%')); next++) {
        if (next[1] != 's') {
            VIR_TEST_VERBOSE("\nerror message '%s' contains disallowed printf modifiers\n", msg);
            ret = -1;
        } else {
            if (found) {
                VIR_TEST_VERBOSE("\nerror message '%s' contains multiple %%s modifiers\n", msg);
                ret = -1;
            } else {
                found = true;
            }
        }
    }

    if (!found) {
        VIR_TEST_VERBOSE("\nerror message '%s' does not contain any %%s modifiers\n", msg);
        ret = -1;
    }

    return ret;
}


static int
virErrorTestMsgs(const void *opaque ATTRIBUTE_UNUSED)
{
    const char *err_noinfo;
    const char *err_info;
    size_t i;
    int ret = 0;

    for (i = 1; i < VIR_ERR_NUMBER_LAST; i++) {
        err_noinfo = virErrorMsg(i, NULL);
        err_info = virErrorMsg(i, "");

        if (!err_noinfo) {
            VIR_TEST_VERBOSE("\nmissing string without info for error id %zu\n", i);
            ret = -1;
        }

        if (!err_info) {
            VIR_TEST_VERBOSE("\nmissing string with info for error id %zu\n", i);
            ret = -1;
        }

        if (strchr(err_noinfo, '%')) {
            VIR_TEST_VERBOSE("\nerror message id %zu contains formatting characters: '%s'\n",
                             i, err_noinfo);
            ret = -1;
        }

        if (virErrorTestMsgFormatInfoOne(err_info) < 0)
            ret = -1;
    }

    return ret;
}


static int
virErrorTestMsgsStable(const void *opaque ATTRIBUTE_UNUSED)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    char *actual = NULL;
    size_t i;
    int ret = 0;

    for (i = 1; i < VIR_ERR_NUMBER_LAST; i++) {
        virBufferAsprintf(&buf, "%zu-", i);
        virBufferStrcat(&buf, virErrorMsg(i, NULL), "-", virErrorMsg(i, ""), "\n", NULL);
    }

    actual = virBufferContentAndReset(&buf);

    if (virTestCompareToFile(actual, abs_srcdir "/virerrormessages.txt") < 0)
        ret = -1;

    VIR_FREE(actual);
    return ret;
}


static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("error message strings ", virErrorTestMsgs, NULL) < 0)
        ret = -1;
    if (virTestRun("error message strings stability ", virErrorTestMsgsStable, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
