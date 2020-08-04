/*
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"
#include "internal.h"
#include "virarch.h"
#include "virthread.h"
#include "qemu/qemu_capabilities.h"
#define LIBVIRT_QEMU_CAPSPRIV_H_ALLOW
#include "qemu/qemu_capspriv.h"

#define VIR_FROM_THIS VIR_FROM_NONE


static void
eventLoop(void *opaque G_GNUC_UNUSED)
{
    while (1) {
        if (virEventRunDefaultImpl() < 0) {
            fprintf(stderr, "Failed to run event loop: %s\n",
                    virGetLastErrorMessage());
        }
    }
}


int
main(int argc, char **argv)
{
    virThread thread;
    virQEMUCapsPtr caps;
    const char *mock = VIR_TEST_MOCK("qemucapsprobe");

    if (!virFileIsExecutable(mock)) {
        perror(mock);
        return EXIT_FAILURE;
    }

    VIR_TEST_PRELOAD(mock);

    virFileActivateDirOverrideForProg(argv[0]);

    if (argc != 2) {
        fprintf(stderr, "%s QEMU_binary\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (virInitialize() < 0) {
        fprintf(stderr, "Failed to initialize libvirt");
        return EXIT_FAILURE;
    }

    if (virEventRegisterDefaultImpl() < 0) {
        fprintf(stderr, "Failed to register event implementation: %s\n",
                virGetLastErrorMessage());
        return EXIT_FAILURE;
    }

    if (virThreadCreate(&thread, false, eventLoop, NULL) < 0)
        return EXIT_FAILURE;

    if (!(caps = virQEMUCapsNewForBinaryInternal(VIR_ARCH_NONE, argv[1], "/tmp",
                                                 -1, -1, NULL, 0, NULL)))
        return EXIT_FAILURE;

    virObjectUnref(caps);

    return EXIT_SUCCESS;
}
