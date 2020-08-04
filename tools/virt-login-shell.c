/*
 * virt-login-shell.c: a setuid shell to connect to a container
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "configmake.h"

#define VIR_INT64_STR_BUFLEN 21

int main(int argc, char **argv) {
    char uidstr[VIR_INT64_STR_BUFLEN];
    char gidstr[VIR_INT64_STR_BUFLEN];
    const char * newargv[6];
    size_t nargs = 0;
    char *newenv[] = {
        NULL,
        NULL,
    };
    char *term = getenv("TERM");

    if (getuid() == 0 || getgid() == 0) {
        fprintf(stderr, "%s: must not be run as root\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (geteuid() != 0) {
        fprintf(stderr, "%s: must be run as setuid root\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    newargv[nargs++] = LIBEXECDIR "/virt-login-shell-helper";
    if (argc == 3) {
        if (strcmp(argv[1], "-c") != 0) {
            fprintf(stderr, "%s: syntax: %s [-c CMDSTR]\n", argv[0], argv[0]);
            exit(EXIT_FAILURE);
        }
        newargv[nargs++] = argv[1];
        newargv[nargs++] = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "%s: syntax: %s [-c CMDSTR]\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }
    newargv[nargs++] = uidstr;
    newargv[nargs++] = gidstr;
    newargv[nargs++] = NULL;

    assert(nargs <= (sizeof(newargv)/sizeof(newargv[0])));

    if (term &&
        asprintf(&(newenv[0]), "TERM=%s", term) < 0) {
        fprintf(stderr, "%s: cannot set TERM env variable: %s\n",
                argv[0], strerror(errno));
        exit(EXIT_FAILURE);
    }

    assert(snprintf(uidstr, sizeof(uidstr), "%d", getuid()) < sizeof(uidstr));
    assert(snprintf(gidstr, sizeof(gidstr), "%d", getgid()) < sizeof(gidstr));

    if (setuid(0) < 0) {
        fprintf(stderr, "%s: unable to set real UID to root: %s\n",
                argv[0], strerror(errno));
        exit(EXIT_FAILURE);
    }

    execve(newargv[0], (char *const*)newargv, newenv);
    fprintf(stderr, "%s: failed to run %s/virt-login-shell-helper: %s\n",
            argv[0], LIBEXECDIR, strerror(errno));
    exit(EXIT_FAILURE);
}
