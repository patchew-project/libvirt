/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"

#ifdef __linux__

# include "virnetdev.h"

# define VIR_FROM_THIS VIR_FROM_NONE

struct testVirNetDevGetLinkInfoData {
    const char *ifname;         /* ifname to get info on */
    virNetDevIfState state;     /* expected state */
    unsigned int speed;         /* expected speed */
};

static int
testVirNetDevGetLinkInfo(const void *opaque)
{
    const struct testVirNetDevGetLinkInfoData *data = opaque;
    virNetDevIfLink lnk;

    if (virNetDevGetLinkInfo(data->ifname, &lnk) < 0)
        return -1;

    if (lnk.state != data->state) {
        fprintf(stderr,
                "Fetched link state (%s) doesn't match the expected one (%s)",
                virNetDevIfStateTypeToString(lnk.state),
                virNetDevIfStateTypeToString(data->state));
        return -1;
    }

    if (lnk.speed != data->speed) {
        fprintf(stderr,
                "Fetched link speed (%u) doesn't match the expected one (%u)",
                lnk.speed, data->speed);
        return -1;
    }

    return 0;
}

static int
mymain(void)
{
    int ret = 0;

# define DO_TEST_LINK(ifname, state, speed) \
    do { \
        struct testVirNetDevGetLinkInfoData data = {ifname, state, speed}; \
        if (virTestRun("Link info: " # ifname, \
                       testVirNetDevGetLinkInfo, &data) < 0) \
            ret = -1; \
    } while (0)

    DO_TEST_LINK("eth0", VIR_NETDEV_IF_STATE_UP, 1000);
    DO_TEST_LINK("lo", VIR_NETDEV_IF_STATE_UNKNOWN, 0);
    DO_TEST_LINK("eth0-broken", VIR_NETDEV_IF_STATE_DOWN, 0);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain, VIR_TEST_MOCK("virnetdev"))
#else
int
main(void)
{
    return EXIT_AM_SKIP;
}
#endif
