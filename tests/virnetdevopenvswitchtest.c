/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"
#include "virnetdevopenvswitch.h"

#define VIR_FROM_THIS VIR_FROM_NONE

typedef struct _InterfaceParseStatsData InterfaceParseStatsData;
struct _InterfaceParseStatsData {
    const char *filename;
    const virDomainInterfaceStatsStruct stats;
};


static int
testInterfaceParseStats(const void *opaque)
{
    const InterfaceParseStatsData *data = opaque;
    g_autofree char *filename = NULL;
    g_autofree char *buf = NULL;
    virDomainInterfaceStatsStruct actual;

    filename = g_strdup_printf("%s/virnetdevopenvswitchdata/%s", abs_srcdir,
                               data->filename);

    if (virFileReadAll(filename, 1024, &buf) < 0)
        return -1;

    if (virNetDevOpenvswitchInterfaceParseStats(buf, &actual) < 0)
        return -1;

    if (memcmp(&actual, &data->stats, sizeof(actual)) != 0) {
        fprintf(stderr,
                "Expected stats: %lld %lld %lld %lld %lld %lld %lld %lld\n"
                "Actual stats: %lld %lld %lld %lld %lld %lld %lld %lld",
                data->stats.rx_bytes,
                data->stats.rx_packets,
                data->stats.rx_errs,
                data->stats.rx_drop,
                data->stats.tx_bytes,
                data->stats.tx_packets,
                data->stats.tx_errs,
                data->stats.tx_drop,
                actual.rx_bytes,
                actual.rx_packets,
                actual.rx_errs,
                actual.rx_drop,
                actual.tx_bytes,
                actual.tx_packets,
                actual.tx_errs,
                actual.tx_drop);

        return -1;
    }

    return 0;
}


static int
mymain(void)
{
    int ret = 0;

#define TEST_INTERFACE_STATS(file, \
                             rxBytes, rxPackets, rxErrs, rxDrop, \
                             txBytes, txPackets, txErrs, txDrop) \
    do { \
        const InterfaceParseStatsData data = {.filename = file, .stats = { \
                             rxBytes, rxPackets, rxErrs, rxDrop, \
                             txBytes, txPackets, txErrs, txDrop}}; \
        if (virTestRun("Interface stats " file, testInterfaceParseStats, &data) < 0) \
            ret = -1; \
    } while (0)

    TEST_INTERFACE_STATS("stats1.json", 9, 12, 11, 10, 2, 8, 5, 4);
    TEST_INTERFACE_STATS("stats2.json", 12406, 173, 0, 0, 0, 0, 0, 0);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain);
