/*
 * commandhelper.c: Auxiliary program for commandtest
 *
 * Copyright (C) 2010-2014 Red Hat, Inc.
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define VIR_NO_GLIB_STDIO /* This file intentionally does not link to libvirt/glib */
#include "testutils.h"

#ifndef WIN32
# include <poll.h>

/* Some UNIX lack it in headers & it doesn't hurt to redeclare */
extern char **environ;

# define VIR_FROM_THIS VIR_FROM_NONE

typedef struct Arguments {
    int *readfds;
    int numreadfds;
    bool daemonize_check;
    bool close_stdin;
} Arguments;

static void cleanupArguments(struct Arguments* args) {
    if (args)
        free(args->readfds);

    free(args);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Arguments, cleanupArguments);

static struct Arguments *parseArguments(int argc, char** argv)
{
    g_autoptr(Arguments) args = NULL;
    size_t i;

    if (!(args = calloc(1, sizeof(*args))))
        return NULL;

    if (!(args->readfds = calloc(1, sizeof(*args->readfds))))
        return NULL;

    args->numreadfds = 1;
    args->readfds[0] = STDIN_FILENO;

    for (i = 1; i < argc; i++) {
        if (STREQ(argv[i - 1], "--readfd")) {
            char c;

            args->readfds = realloc(args->readfds,
                                    (args->numreadfds + 1) *
                                    sizeof(*args->readfds));
            if (!args->readfds)
                return NULL;

            if (1 != sscanf(argv[i], "%u%c",
                            &args->readfds[args->numreadfds++], &c)) {
                printf("Could not parse fd %s\n", argv[i]);
                return NULL;
            }
        } else if (STREQ(argv[i], "--check-daemonize")) {
            args->daemonize_check = true;
        } else if (STREQ(argv[i], "--close-stdin")) {
            args->close_stdin = true;
        }
    }

    return g_steal_pointer(&args);
}

static void printArguments(FILE *log, int argc, char** argv)
{
    size_t i;

    for (i = 1; i < argc; i++) {
        fprintf(log, "ARG:%s\n", argv[i]);
    }
}

static int envsort(const void *a, const void *b)
{
    const char *const*astrptr = a;
    const char *const*bstrptr = b;
    return strcmp(*astrptr, *bstrptr);
}

static int printEnvironment(FILE *log)
{
    g_autofree char **newenv = NULL;
    size_t length;
    size_t i;

    for (length = 0; environ[length]; length++) {
    }

    if (!(newenv = malloc(sizeof(*newenv) * length)))
        return -1;

    for (i = 0; i < length; i++) {
        newenv[i] = environ[i];
    }

    qsort(newenv, length, sizeof(newenv[0]), envsort);

    for (i = 0; i < length; i++) {
        /* Ignore the variables used to instruct the loader into
         * behaving differently, as they could throw the tests off. */
        if (!STRPREFIX(newenv[i], "LD_"))
            fprintf(log, "ENV:%s\n", newenv[i]);
    }

    return 0;
}

static int printFds(FILE *log)
{
    long int open_max = sysconf(_SC_OPEN_MAX);
    size_t i;

    if (open_max < 0)
        return -1;

    for (i = 0; i < open_max; i++) {
        int ignore;

        if (i == fileno(log))
            continue;

        if (fcntl(i, F_GETFD, &ignore) == -1 && errno == EBADF)
            continue;

        fprintf(log, "FD:%zu\n", i);
    }

    return 0;
}

static void printDaemonization(FILE *log, struct Arguments *args)
{
    int retries = 3;

    if (args->daemonize_check) {
        while ((getpgrp() == getppid()) && (retries-- > 0)) {
            usleep(100 * 1000);
        }
    }

    fprintf(log, "DAEMON:%s\n", getpgrp() != getppid() ? "yes" : "no");
}

static int printCwd(FILE *log)
{
    g_autofree char *cwd = NULL;
    char *display;

    if (!(cwd = getcwd(NULL, 0)))
        return -1;

    if ((strlen(cwd) > strlen(".../commanddata")) &&
        (STREQ(cwd + strlen(cwd) - strlen("/commanddata"), "/commanddata"))) {
        strcpy(cwd, ".../commanddata");
    }

    display = cwd;

# ifdef __APPLE__
    if (strstr(cwd, "/private"))
        display = cwd + strlen("/private");
# endif

    fprintf(log, "CWD:%s\n", display);
    return 0;
}

static int printInput(struct Arguments *args)
{
    char buf[1024];
    struct pollfd *fds = NULL;
    char **buffers = NULL;
    size_t *buflen = NULL;
    int ret = -1;
    size_t i;
    ssize_t got;

    if (!(fds = calloc(args->numreadfds, sizeof(*fds))))
        goto cleanup;

    if (!(buffers = calloc(args->numreadfds, sizeof(*buffers))))
        goto cleanup;

    if (!(buflen = calloc(args->numreadfds, sizeof(*buflen))))
        goto cleanup;

    if (args->close_stdin) {
        if (freopen("/dev/null", "r", stdin) != stdin)
            goto cleanup;
        usleep(100 * 1000);
    }

    fprintf(stdout, "BEGIN STDOUT\n");
    fflush(stdout);
    fprintf(stderr, "BEGIN STDERR\n");
    fflush(stderr);

    for (i = 0; i < args->numreadfds; i++) {
        fds[i].fd = args->readfds[i];
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }

    for (;;) {
        unsigned ctr = 0;

        if (poll(fds, args->numreadfds, -1) < 0) {
            printf("poll failed: %s\n", strerror(errno));
            goto cleanup;
        }

        for (i = 0; i < args->numreadfds; i++) {
            short revents = POLLIN | POLLHUP | POLLERR;

# ifdef __APPLE__
            /*
             * poll() on /dev/null will return POLLNVAL
             * Apple-Feedback: FB8785208
             */
            revents |= POLLNVAL;
# endif

            if (fds[i].revents & revents) {
                fds[i].revents = 0;

                got = read(fds[i].fd, buf, sizeof(buf));
                if (got < 0)
                    goto cleanup;
                if (got == 0) {
                    /* do not want to hear from this fd anymore */
                    fds[i].events = 0;
                } else {
                    buffers[i] = realloc(buffers[i], buflen[i] + got);
                    if (!buf[i]) {
                        fprintf(stdout, "Out of memory!\n");
                        goto cleanup;
                    }
                    memcpy(buffers[i] + buflen[i], buf, got);
                    buflen[i] += got;
                }
            }
        }
        for (i = 0; i < args->numreadfds; i++) {
            if (fds[i].events) {
                ctr++;
                break;
            }
        }
        if (ctr == 0)
            break;
    }

    for (i = 0; i < args->numreadfds; i++) {
        if (fwrite(buffers[i], 1, buflen[i], stdout) != buflen[i])
            goto cleanup;
        if (fwrite(buffers[i], 1, buflen[i], stderr) != buflen[i])
            goto cleanup;
    }

    fprintf(stdout, "END STDOUT\n");
    fflush(stdout);
    fprintf(stderr, "END STDERR\n");
    fflush(stderr);

    ret = 0;

 cleanup:
    if (buffers) {
        for (i = 0; i < args->numreadfds; i++)
            free(buffers[i]);
    }
    free(fds);
    free(buflen);
    free(buffers);

    return ret;
}

int main(int argc, char **argv) {
    g_autoptr(Arguments) args = parseArguments(argc, argv);
    g_autoptr(FILE) log = fopen(abs_builddir "/commandhelper.log", "w");

    if (!log || !args)
        return EXIT_FAILURE;

    printArguments(log, argc, argv);

    if (printEnvironment(log) != 0)
        return EXIT_FAILURE;

    if (printFds(log) != 0)
        return EXIT_FAILURE;

    printDaemonization(log, args);

    if (printCwd(log) != 0)
        return EXIT_FAILURE;

    fprintf(log, "UMASK:%04o\n", umask(0));

    if (printInput(args) != 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif
