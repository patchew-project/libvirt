/*
 * virthread.c: basic thread synchronization primitives
 *
 * Copyright (C) 2009-2010, 2014 Red Hat, Inc.
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
 *
 */

#include <config.h>

#include "virthread.h"

#ifdef __FreeBSD__
# include <pthread_np.h>
#endif

#include <unistd.h>
#include <inttypes.h>
#if HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
#endif

#include "viralloc.h"
#include "virthreadjob.h"


int virOnce(virOnceControlPtr once, virOnceFunc init)
{
    return pthread_once(&once->once, init);
}


struct virThreadArgs {
    virThreadFunc func;
    char *name;
    bool worker;
    void *opaque;
};

size_t virThreadMaxName(void)
{
#if defined(__FreeBSD__) || defined(__APPLE__)
    return 63;
#else
# ifdef __linux__
    return 15;
# else
    return 0; /* unlimited */
# endif
#endif
}

static void *virThreadHelper(void *data)
{
    struct virThreadArgs *args = data;
    struct virThreadArgs local = *args;
    g_autofree char *thname = NULL;
    size_t maxname = virThreadMaxName();

    /* Free args early, rather than tying it up during the entire thread.  */
    g_free(args);

    if (local.worker)
        virThreadJobSetWorker(local.name);
    else
        virThreadJobSet(local.name);

    if (maxname) {
        thname = g_strndup(local.name, maxname);
    } else {
        thname = g_strdup(local.name);
    }

#if defined(__linux__) || defined(WIN32)
    pthread_setname_np(pthread_self(), thname);
#else
# ifdef __FreeBSD__
    pthread_set_name_np(pthread_self(), thname);
# else
#  ifdef __APPLE__
    pthread_setname_np(thname);
#  endif
# endif
#endif

    local.func(local.opaque);

    if (!local.worker)
        virThreadJobClear(0);

    g_free(local.name);
    return NULL;
}

int virThreadCreateFull(virThreadPtr thread,
                        bool joinable,
                        virThreadFunc func,
                        const char *name,
                        bool worker,
                        void *opaque)
{
    struct virThreadArgs *args;
    pthread_attr_t attr;
    int ret = -1;
    int err;

    if ((err = pthread_attr_init(&attr)) != 0)
        goto cleanup;
    if (VIR_ALLOC_QUIET(args) < 0) {
        err = ENOMEM;
        goto cleanup;
    }

    args->func = func;
    args->name = g_strdup(name);
    args->worker = worker;
    args->opaque = opaque;

    if (!joinable)
        pthread_attr_setdetachstate(&attr, 1);

    err = pthread_create(&thread->thread, &attr, virThreadHelper, args);
    if (err != 0) {
        g_free(args->name);
        g_free(args);
        goto cleanup;
    }
    /* New thread owns 'args' in success case, so don't free */

    ret = 0;
 cleanup:
    pthread_attr_destroy(&attr);
    if (ret < 0)
        errno = err;
    return ret;
}

void virThreadSelf(virThreadPtr thread)
{
    thread->thread = pthread_self();
}

bool virThreadIsSelf(virThreadPtr thread)
{
    return pthread_equal(pthread_self(), thread->thread) ? true : false;
}

/* For debugging use only; this result is not guaranteed unique if
 * pthread_t is larger than a 64-bit pointer, nor does it always match
 * the pthread_self() id on Linux.  */
unsigned long long virThreadSelfID(void)
{
#if defined(HAVE_SYS_SYSCALL_H) && defined(SYS_gettid) && defined(__linux__)
    pid_t tid = syscall(SYS_gettid);
    return tid;
#else
    union {
        unsigned long long l;
        pthread_t t;
    } u;
    u.t = pthread_self();
    return u.l;
#endif
}

/* For debugging use only; this result is not guaranteed unique if
 * pthread_t is larger than a 64-bit pointer, nor does it always match
 * the thread id of virThreadSelfID on Linux.  */
unsigned long long virThreadID(virThreadPtr thread)
{
    union {
        unsigned long long l;
        pthread_t t;
    } u;
    u.t = thread->thread;
    return u.l;
}

void virThreadJoin(virThreadPtr thread)
{
    pthread_join(thread->thread, NULL);
}

void virThreadCancel(virThreadPtr thread)
{
    pthread_cancel(thread->thread);
}

int virThreadLocalInit(virThreadLocalPtr l,
                       virThreadLocalCleanup c)
{
    int ret;
    if ((ret = pthread_key_create(&l->key, c)) != 0) {
        errno = ret;
        return -1;
    }
    return 0;
}

void *virThreadLocalGet(virThreadLocalPtr l)
{
    return pthread_getspecific(l->key);
}

int virThreadLocalSet(virThreadLocalPtr l, void *val)
{
    int err = pthread_setspecific(l->key, val);
    if (err) {
        errno = err;
        return -1;
    }
    return 0;
}
