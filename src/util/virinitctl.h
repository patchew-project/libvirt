/*
 * virinitctl.h: API for talking to init systems via initctl
 *
 * Copyright (C) 2012-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

typedef enum {
    VIR_INITCTL_RUNLEVEL_POWEROFF = 0,
    VIR_INITCTL_RUNLEVEL_1 = 1,
    VIR_INITCTL_RUNLEVEL_2 = 2,
    VIR_INITCTL_RUNLEVEL_3 = 3,
    VIR_INITCTL_RUNLEVEL_4 = 4,
    VIR_INITCTL_RUNLEVEL_5 = 5,
    VIR_INITCTL_RUNLEVEL_REBOOT = 6,

    VIR_INITCTL_RUNLEVEL_LAST
} virInitctlRunLevel;


extern const char *virInitctlFifos[];

int virInitctlSetRunLevel(const char *fifo,
                          virInitctlRunLevel level);
