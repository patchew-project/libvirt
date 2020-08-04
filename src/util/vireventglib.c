/*
 * vireventglib.c: GMainContext based event loop
 *
 * Copyright (C) 2008 Daniel P. Berrange
 * Copyright (C) 2010-2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vireventglib.h"
#include "vireventglibwatch.h"
#include "virerror.h"
#include "virlog.h"
#include "virprobe.h"

#ifdef G_OS_WIN32
# include <io.h>
#endif

#define VIR_FROM_THIS VIR_FROM_EVENT

VIR_LOG_INIT("util.eventglib");

struct virEventGLibHandle
{
    int watch;
    int fd;
    int events;
    int removed;
    guint source;
    virEventHandleCallback cb;
    void *opaque;
    virFreeCallback ff;
};

struct virEventGLibTimeout
{
    int timer;
    int interval;
    int removed;
    guint source;
    virEventTimeoutCallback cb;
    void *opaque;
    virFreeCallback ff;
};

static GMutex *eventlock;

static int nextwatch = 1;
static GPtrArray *handles;

static int nexttimer = 1;
static GPtrArray *timeouts;

static GIOCondition
virEventGLibEventsToCondition(int events)
{
    GIOCondition cond = 0;
    if (events & VIR_EVENT_HANDLE_READABLE)
        cond |= G_IO_IN;
    if (events & VIR_EVENT_HANDLE_WRITABLE)
        cond |= G_IO_OUT;
    if (events & VIR_EVENT_HANDLE_ERROR)
        cond |= G_IO_ERR;
    if (events & VIR_EVENT_HANDLE_HANGUP)
        cond |= G_IO_HUP;
    return cond;
}

static int
virEventGLibConditionToEvents(GIOCondition cond)
{
    int events = 0;
    if (cond & G_IO_IN)
        events |= VIR_EVENT_HANDLE_READABLE;
    if (cond & G_IO_OUT)
        events |= VIR_EVENT_HANDLE_WRITABLE;
    if (cond & G_IO_ERR)
        events |= VIR_EVENT_HANDLE_ERROR;
    if (cond & G_IO_NVAL) /* Treat NVAL as error, since libvirt doesn't distinguish */
        events |= VIR_EVENT_HANDLE_ERROR;
    if (cond & G_IO_HUP)
        events |= VIR_EVENT_HANDLE_HANGUP;
    return events;
}

static gboolean
virEventGLibHandleDispatch(int fd G_GNUC_UNUSED,
                           GIOCondition condition,
                           gpointer opaque)
{
    struct virEventGLibHandle *data = opaque;
    int events = virEventGLibConditionToEvents(condition);

    VIR_DEBUG("Dispatch handler data=%p watch=%d fd=%d events=%d opaque=%p",
              data, data->watch, data->fd, events, data->opaque);

    PROBE(EVENT_GLIB_DISPATCH_HANDLE,
          "watch=%d events=%d cb=%p opaque=%p",
          data->watch, events, data->cb, data->opaque);

    (data->cb)(data->watch, data->fd, events, data->opaque);

    return TRUE;
}


static int
virEventGLibHandleAdd(int fd,
                      int events,
                      virEventHandleCallback cb,
                      void *opaque,
                      virFreeCallback ff)
{
    struct virEventGLibHandle *data;
    GIOCondition cond = virEventGLibEventsToCondition(events);
    int ret;

    g_mutex_lock(eventlock);

    data = g_new0(struct virEventGLibHandle, 1);

    data->watch = nextwatch++;
    data->fd = fd;
    data->events = events;
    data->cb = cb;
    data->opaque = opaque;
    data->ff = ff;

    VIR_DEBUG("Add handle data=%p watch=%d fd=%d events=%d opaque=%p",
              data, data->watch, data->fd, events, data->opaque);

    if (events != 0) {
        data->source = virEventGLibAddSocketWatch(
            fd, cond, NULL, virEventGLibHandleDispatch, data, NULL);
    }

    g_ptr_array_add(handles, data);

    ret = data->watch;

    PROBE(EVENT_GLIB_ADD_HANDLE,
          "watch=%d fd=%d events=%d cb=%p opaque=%p ff=%p",
          ret, fd, events, cb, opaque, ff);
    g_mutex_unlock(eventlock);

    return ret;
}

static struct virEventGLibHandle *
virEventGLibHandleFind(int watch)
{
    guint i;

    for (i = 0; i < handles->len; i++) {
        struct virEventGLibHandle *h = g_ptr_array_index(handles, i);

        if (h == NULL) {
            g_warn_if_reached();
            continue;
        }

        if ((h->watch == watch) && !h->removed)
            return h;
    }

    return NULL;
}

static void
virEventGLibHandleUpdate(int watch,
                         int events)
{
    struct virEventGLibHandle *data;

    PROBE(EVENT_GLIB_UPDATE_HANDLE,
          "watch=%d events=%d",
          watch, events);
    g_mutex_lock(eventlock);

    data = virEventGLibHandleFind(watch);
    if (!data) {
        VIR_DEBUG("Update for missing handle watch=%d", watch);
        goto cleanup;
    }

    VIR_DEBUG("Update handle data=%p watch=%d fd=%d events=%d",
              data, watch, data->fd, events);

    if (events != 0) {
        GIOCondition cond = virEventGLibEventsToCondition(events);
        if (events == data->events)
            goto cleanup;

        if (data->source != 0) {
            VIR_DEBUG("Removed old handle watch=%d", data->source);
            g_source_remove(data->source);
        }

        data->source = virEventGLibAddSocketWatch(
            data->fd, cond, NULL, virEventGLibHandleDispatch, data, NULL);

        data->events = events;
        VIR_DEBUG("Added new handle watch=%d", data->source);
    } else {
        if (data->source == 0)
            goto cleanup;

        VIR_DEBUG("Removed old handle watch=%d", data->source);
        g_source_remove(data->source);
        data->source = 0;
        data->events = 0;
    }

 cleanup:
    g_mutex_unlock(eventlock);
}

static gboolean
virEventGLibHandleRemoveIdle(gpointer data)
{
    struct virEventGLibHandle *h = data;

    PROBE(EVENT_GLIB_REMOVE_HANDLE_IDLE,
          "watch=%d ff=%p opaque=%p",
          h->watch, h->ff, h->opaque);
    if (h->ff)
        (h->ff)(h->opaque);

    g_mutex_lock(eventlock);
    g_ptr_array_remove_fast(handles, h);
    g_mutex_unlock(eventlock);

    return FALSE;
}

static int
virEventGLibHandleRemove(int watch)
{
    struct virEventGLibHandle *data;
    int ret = -1;

    PROBE(EVENT_GLIB_REMOVE_HANDLE,
          "watch=%d",
          watch);
    g_mutex_lock(eventlock);

    data = virEventGLibHandleFind(watch);
    if (!data) {
        VIR_DEBUG("Remove of missing handle watch=%d", watch);
        goto cleanup;
    }

    VIR_DEBUG("Remove handle data=%p watch=%d fd=%d",
              data, watch, data->fd);

    if (data->source != 0) {
        g_source_remove(data->source);
        data->source = 0;
        data->events = 0;
    }

    /* since the actual watch deletion is done asynchronously, a handleUpdate call may
     * reschedule the watch before it's fully deleted, that's why we need to mark it as
     * 'removed' to prevent reuse
     */
    data->removed = TRUE;
    g_idle_add(virEventGLibHandleRemoveIdle, data);

    ret = 0;

 cleanup:
    g_mutex_unlock(eventlock);
    return ret;
}


static gboolean
virEventGLibTimeoutDispatch(void *opaque)
{
    struct virEventGLibTimeout *data = opaque;

    VIR_DEBUG("Dispatch timeout data=%p cb=%p timer=%d opaque=%p",
              data, data->cb, data->timer, data->opaque);

    PROBE(EVENT_GLIB_DISPATCH_TIMEOUT,
          "timer=%d cb=%p opaque=%p",
          data->timer, data->cb, data->opaque);
    (data->cb)(data->timer, data->opaque);

    return TRUE;
}

static int
virEventGLibTimeoutAdd(int interval,
                       virEventTimeoutCallback cb,
                       void *opaque,
                       virFreeCallback ff)
{
    struct virEventGLibTimeout *data;
    int ret;

    g_mutex_lock(eventlock);

    data = g_new0(struct virEventGLibTimeout, 1);
    data->timer = nexttimer++;
    data->interval = interval;
    data->cb = cb;
    data->opaque = opaque;
    data->ff = ff;
    if (interval >= 0)
        data->source = g_timeout_add(interval,
                                     virEventGLibTimeoutDispatch,
                                     data);

    g_ptr_array_add(timeouts, data);

    VIR_DEBUG("Add timeout data=%p interval=%d ms cb=%p opaque=%p timer=%d",
              data, interval, cb, opaque, data->timer);

    ret = data->timer;

    PROBE(EVENT_GLIB_ADD_TIMEOUT,
          "timer=%d interval=%d cb=%p opaque=%p ff=%p",
          ret, interval, cb, opaque, ff);
    g_mutex_unlock(eventlock);

    return ret;
}


static struct virEventGLibTimeout *
virEventGLibTimeoutFind(int timer)
{
    guint i;

    g_return_val_if_fail(timeouts != NULL, NULL);

    for (i = 0; i < timeouts->len; i++) {
        struct virEventGLibTimeout *t = g_ptr_array_index(timeouts, i);

        if (t == NULL) {
            g_warn_if_reached();
            continue;
        }

        if ((t->timer == timer) && !t->removed)
            return t;
    }

    return NULL;
}


static void
virEventGLibTimeoutUpdate(int timer,
                          int interval)
{
    struct virEventGLibTimeout *data;

    PROBE(EVENT_GLIB_UPDATE_TIMEOUT,
          "timer=%d interval=%d",
          timer, interval);
    g_mutex_lock(eventlock);

    data = virEventGLibTimeoutFind(timer);
    if (!data) {
        VIR_DEBUG("Update of missing timeout timer=%d", timer);
        goto cleanup;
    }

    VIR_DEBUG("Update timeout data=%p timer=%d interval=%d ms", data, timer, interval);

    if (interval >= 0) {
        if (data->source != 0)
            g_source_remove(data->source);

        data->interval = interval;
        data->source = g_timeout_add(data->interval,
                                     virEventGLibTimeoutDispatch,
                                     data);
    } else {
        if (data->source == 0)
            goto cleanup;

        g_source_remove(data->source);
        data->source = 0;
    }

 cleanup:
    g_mutex_unlock(eventlock);
}

static gboolean
virEventGLibTimeoutRemoveIdle(gpointer data)
{
    struct virEventGLibTimeout *t = data;

    PROBE(EVENT_GLIB_REMOVE_TIMEOUT_IDLE,
          "timer=%d ff=%p opaque=%p",
          t->timer, t->ff, t->opaque);

    if (t->ff)
        (t->ff)(t->opaque);

    g_mutex_lock(eventlock);
    g_ptr_array_remove_fast(timeouts, t);
    g_mutex_unlock(eventlock);

    return FALSE;
}

static int
virEventGLibTimeoutRemove(int timer)
{
    struct virEventGLibTimeout *data;
    int ret = -1;

    PROBE(EVENT_GLIB_REMOVE_TIMEOUT,
          "timer=%d",
          timer);
    g_mutex_lock(eventlock);

    data = virEventGLibTimeoutFind(timer);
    if (!data) {
        VIR_DEBUG("Remove of missing timeout timer=%d", timer);
        goto cleanup;
    }

    VIR_DEBUG("Remove timeout data=%p timer=%d",
              data, timer);

    if (data->source != 0) {
        g_source_remove(data->source);
        data->source = 0;
    }

    /* since the actual timeout deletion is done asynchronously, a timeoutUpdate call may
     * reschedule the timeout before it's fully deleted, that's why we need to mark it as
     * 'removed' to prevent reuse
     */
    data->removed = TRUE;
    g_idle_add(virEventGLibTimeoutRemoveIdle, data);

    ret = 0;

 cleanup:
    g_mutex_unlock(eventlock);
    return ret;
}


static gpointer virEventGLibRegisterOnce(gpointer data G_GNUC_UNUSED)
{
    eventlock = g_new0(GMutex, 1);
    timeouts = g_ptr_array_new_with_free_func(g_free);
    handles = g_ptr_array_new_with_free_func(g_free);
    virEventRegisterImpl(virEventGLibHandleAdd,
                         virEventGLibHandleUpdate,
                         virEventGLibHandleRemove,
                         virEventGLibTimeoutAdd,
                         virEventGLibTimeoutUpdate,
                         virEventGLibTimeoutRemove);
    return NULL;
}


void virEventGLibRegister(void)
{
    static GOnce once = G_ONCE_INIT;

    g_once(&once, virEventGLibRegisterOnce, NULL);
}


int virEventGLibRunOnce(void)
{
    g_main_context_iteration(NULL, TRUE);

    return 0;
}
