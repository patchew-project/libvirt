/*
 * vireventpoll.c: Poll based event loop for monitoring file handles
 *
 * Copyright (C) 2007, 2010-2014 Red Hat, Inc.
 * Copyright (C) 2007 Daniel P. Berrange
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
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "virthread.h"
#include "virlog.h"
#include "vireventpoll.h"
#include "vireventpollinternal.h"
#include "viralloc.h"
#include "virutil.h"
#include "virfile.h"
#include "virerror.h"
#include "virprobe.h"
#include "virtime.h"

#define EVENT_DEBUG(fmt, ...) VIR_DEBUG(fmt, __VA_ARGS__)

#define VIR_FROM_THIS VIR_FROM_EVENT

VIR_LOG_INIT("util.eventpoll");

struct virEventPollLoop eventLoop;
int nextWatch = 1;
int nextTimer = 1;

/*
 * Register a callback for monitoring file handle events.
 * NB, it *must* be safe to call this from within a callback
 * For this reason we only ever append to existing list.
 */
int virEventPollAddHandle(int fd, int events,
                          virEventHandleCallback cb,
                          void *opaque,
                          virFreeCallback ff)
{
    int watch;
    int native = virEventPollToNativeEvents(events);
    virMutexLock(&eventLoop.lock);
    if (eventLoop.handlesCount == eventLoop.handlesAlloc) {
        EVENT_DEBUG("Used %zu handle slots, adding at least %d more",
                    eventLoop.handlesAlloc, EVENT_ALLOC_EXTENT);
        if (VIR_RESIZE_N(eventLoop.handles, eventLoop.handlesAlloc,
                         eventLoop.handlesCount, EVENT_ALLOC_EXTENT) < 0) {
            virMutexUnlock(&eventLoop.lock);
            return -1;
        }
    }

    watch = nextWatch++;

    eventLoop.handles[eventLoop.handlesCount].watch = watch;
    eventLoop.handles[eventLoop.handlesCount].fd = fd;
    eventLoop.handles[eventLoop.handlesCount].events = native;
    eventLoop.handles[eventLoop.handlesCount].cb = cb;
    eventLoop.handles[eventLoop.handlesCount].ff = ff;
    eventLoop.handles[eventLoop.handlesCount].opaque = opaque;
    eventLoop.handles[eventLoop.handlesCount].deleted = 0;

    eventLoop.handlesCount++;

    if (virEventPollAddHandleInternal(watch, fd, native) < 0) {
        virMutexUnlock(&eventLoop.lock);
        return -1;
    }

    PROBE(EVENT_POLL_ADD_HANDLE,
          "watch=%d fd=%d events=%d cb=%p opaque=%p ff=%p",
          watch, fd, events, cb, opaque, ff);
    virMutexUnlock(&eventLoop.lock);

    return watch;
}

void virEventPollUpdateHandle(int watch, int events)
{
    size_t i;
    bool found = false;
    int native = virEventPollToNativeEvents(events);
    PROBE(EVENT_POLL_UPDATE_HANDLE,
          "watch=%d events=%d",
          watch, events);

    if (watch <= 0) {
        VIR_WARN("Ignoring invalid update watch %d", watch);
        return;
    }

    virMutexLock(&eventLoop.lock);
    for (i = 0; i < eventLoop.handlesCount; i++) {
        if (eventLoop.handles[i].watch == watch) {
            eventLoop.handles[i].events = native;
            found = true;
            break;
        }
    }

    if (!found) {
        VIR_WARN("Got update for non-existent handle watch %d", watch);
    }
    else if (virEventPollUpdateHandleInternal(watch,
                                         eventLoop.handles[i].fd, native) < 0) {
        VIR_WARN("Update for existing handle watch %d failed", watch);
    }

    virMutexUnlock(&eventLoop.lock);
}

/*
 * Unregister a callback from a file handle
 * NB, it *must* be safe to call this from within a callback
 * For this reason we only ever set a flag in the existing list.
 * Actual deletion will be done out-of-band
 */
int virEventPollRemoveHandle(int watch)
{
    size_t i;
    PROBE(EVENT_POLL_REMOVE_HANDLE,
          "watch=%d",
          watch);

    if (watch <= 0) {
        VIR_WARN("Ignoring invalid remove watch %d", watch);
        return -1;
    }

    virMutexLock(&eventLoop.lock);
    for (i = 0; i < eventLoop.handlesCount; i++) {
        if (eventLoop.handles[i].deleted)
            continue;

        if (eventLoop.handles[i].watch == watch) {
            break;
        }
    }

    if (i == eventLoop.handlesCount) {
        virMutexUnlock(&eventLoop.lock);
        return -1;
    }

    if (virEventPollRemoveHandleInternal(watch, eventLoop.handles[i].fd) < 0) {
        virMutexUnlock(&eventLoop.lock);
        return -1;
    }

    EVENT_DEBUG("mark delete %zu %d", i, eventLoop.handles[i].fd);
    eventLoop.handles[i].deleted = 1;
    virEventPollInterruptLocked();
    virMutexUnlock(&eventLoop.lock);
    return 0;
}


/*
 * Register a callback for a timer event
 * NB, it *must* be safe to call this from within a callback
 * For this reason we only ever append to existing list.
 */
int virEventPollAddTimeout(int frequency,
                           virEventTimeoutCallback cb,
                           void *opaque,
                           virFreeCallback ff)
{
    unsigned long long now;
    int ret;

    if (virTimeMillisNow(&now) < 0)
        return -1;

    virMutexLock(&eventLoop.lock);
    if (eventLoop.timeoutsCount == eventLoop.timeoutsAlloc) {
        EVENT_DEBUG("Used %zu timeout slots, adding at least %d more",
                    eventLoop.timeoutsAlloc, EVENT_ALLOC_EXTENT);
        if (VIR_RESIZE_N(eventLoop.timeouts, eventLoop.timeoutsAlloc,
                         eventLoop.timeoutsCount, EVENT_ALLOC_EXTENT) < 0) {
            virMutexUnlock(&eventLoop.lock);
            return -1;
        }
    }

    eventLoop.timeouts[eventLoop.timeoutsCount].timer = nextTimer++;
    eventLoop.timeouts[eventLoop.timeoutsCount].frequency = frequency;
    eventLoop.timeouts[eventLoop.timeoutsCount].cb = cb;
    eventLoop.timeouts[eventLoop.timeoutsCount].ff = ff;
    eventLoop.timeouts[eventLoop.timeoutsCount].opaque = opaque;
    eventLoop.timeouts[eventLoop.timeoutsCount].deleted = 0;
    eventLoop.timeouts[eventLoop.timeoutsCount].expiresAt =
        frequency >= 0 ? frequency + now : 0;

    eventLoop.timeoutsCount++;
    ret = nextTimer-1;
    virEventPollInterruptLocked();

    PROBE(EVENT_POLL_ADD_TIMEOUT,
          "timer=%d frequency=%d cb=%p opaque=%p ff=%p",
          ret, frequency, cb, opaque, ff);
    virMutexUnlock(&eventLoop.lock);
    return ret;
}

void virEventPollUpdateTimeout(int timer, int frequency)
{
    unsigned long long now;
    size_t i;
    bool found = false;
    PROBE(EVENT_POLL_UPDATE_TIMEOUT,
          "timer=%d frequency=%d",
          timer, frequency);

    if (timer <= 0) {
        VIR_WARN("Ignoring invalid update timer %d", timer);
        return;
    }

    if (virTimeMillisNow(&now) < 0)
        return;

    virMutexLock(&eventLoop.lock);
    for (i = 0; i < eventLoop.timeoutsCount; i++) {
        if (eventLoop.timeouts[i].timer == timer) {
            eventLoop.timeouts[i].frequency = frequency;
            eventLoop.timeouts[i].expiresAt =
                frequency >= 0 ? frequency + now : 0;
            VIR_DEBUG("Set timer freq=%d expires=%llu", frequency,
                      eventLoop.timeouts[i].expiresAt);
            virEventPollInterruptLocked();
            found = true;
            break;
        }
    }
    virMutexUnlock(&eventLoop.lock);

    if (!found)
        VIR_WARN("Got update for non-existent timer %d", timer);
}

/*
 * Unregister a callback for a timer
 * NB, it *must* be safe to call this from within a callback
 * For this reason we only ever set a flag in the existing list.
 * Actual deletion will be done out-of-band
 */
int virEventPollRemoveTimeout(int timer)
{
    size_t i;
    PROBE(EVENT_POLL_REMOVE_TIMEOUT,
          "timer=%d",
          timer);

    if (timer <= 0) {
        VIR_WARN("Ignoring invalid remove timer %d", timer);
        return -1;
    }

    virMutexLock(&eventLoop.lock);
    for (i = 0; i < eventLoop.timeoutsCount; i++) {
        if (eventLoop.timeouts[i].deleted)
            continue;

        if (eventLoop.timeouts[i].timer == timer) {
            eventLoop.timeouts[i].deleted = 1;
            virEventPollInterruptLocked();
            virMutexUnlock(&eventLoop.lock);
            return 0;
        }
    }
    virMutexUnlock(&eventLoop.lock);
    return -1;
}

/* Iterates over all registered timeouts and determine which
 * will be the first to expire.
 * @timeout: filled with expiry time of soonest timer, or -1 if
 *           no timeout is pending
 * returns: 0 on success, -1 on error
 */
static int virEventPollCalculateTimeout(int *timeout)
{
    unsigned long long then = 0;
    size_t i;
    EVENT_DEBUG("Calculate expiry of %zu timers", eventLoop.timeoutsCount);
    /* Figure out if we need a timeout */
    for (i = 0; i < eventLoop.timeoutsCount; i++) {
        if (eventLoop.timeouts[i].deleted)
            continue;
        if (eventLoop.timeouts[i].frequency < 0)
            continue;

        EVENT_DEBUG("Got a timeout scheduled for %llu", eventLoop.timeouts[i].expiresAt);
        if (then == 0 ||
            eventLoop.timeouts[i].expiresAt < then)
            then = eventLoop.timeouts[i].expiresAt;
    }

    /* Calculate how long we should wait for a timeout if needed */
    if (then > 0) {
        unsigned long long now;

        if (virTimeMillisNow(&now) < 0)
            return -1;

        EVENT_DEBUG("Schedule timeout then=%llu now=%llu", then, now);
        if (then <= now)
            *timeout = 0;
        else
            *timeout = ((then - now) > INT_MAX) ? INT_MAX : (then - now);
    } else {
        *timeout = -1;
    }

    if (*timeout > -1)
        EVENT_DEBUG("Timeout at %llu due in %d ms", then, *timeout);
    else
        EVENT_DEBUG("%s", "No timeout is pending");

    return 0;
}

/*
 * Iterate over all timers and determine if any have expired.
 * Invoke the user supplied callback for each timer whose
 * expiry time is met, and schedule the next timeout. Does
 * not try to 'catch up' on time if the actual expiry time
 * was later than the requested time.
 *
 * This method must cope with new timers being registered
 * by a callback, and must skip any timers marked as deleted.
 *
 * Returns 0 upon success, -1 if an error occurred
 */
static int virEventPollDispatchTimeouts(void)
{
    unsigned long long now;
    size_t i;
    /* Save this now - it may be changed during dispatch */
    int ntimeouts = eventLoop.timeoutsCount;
    VIR_DEBUG("Dispatch %d", ntimeouts);

    if (virTimeMillisNow(&now) < 0)
        return -1;

    for (i = 0; i < ntimeouts; i++) {
        if (eventLoop.timeouts[i].deleted || eventLoop.timeouts[i].frequency < 0)
            continue;

        /* Add 20ms fuzz so we don't pointlessly spin doing
         * <10ms sleeps, particularly on kernels with low HZ
         * it is fine that a timer expires 20ms earlier than
         * requested
         */
        if (eventLoop.timeouts[i].expiresAt <= (now+20)) {
            virEventTimeoutCallback cb = eventLoop.timeouts[i].cb;
            int timer = eventLoop.timeouts[i].timer;
            void *opaque = eventLoop.timeouts[i].opaque;
            eventLoop.timeouts[i].expiresAt =
                now + eventLoop.timeouts[i].frequency;

            PROBE(EVENT_POLL_DISPATCH_TIMEOUT,
                  "timer=%d",
                  timer);
            virMutexUnlock(&eventLoop.lock);
            (cb)(timer, opaque);
            virMutexLock(&eventLoop.lock);
        }
    }
    return 0;
}


/* Iterate over all file handles and dispatch any which
 * have pending events listed in the poll() data. Invoke
 * the user supplied callback for each handle which has
 * pending events
 *
 * This method must cope with new handles being registered
 * by a callback, and must skip any handles marked as deleted.
 *
 * Returns 0 upon success, -1 if an error occurred
 */
static int virEventPollDispatchHandles(int nfds, void *opaque)
{
    size_t i, n;
    VIR_DEBUG("Dispatch %d", nfds);

    /* NB, use nfds not eventLoop.handlesCount, because new
     * fds might be added on end of list, and they're not
     * in the fds array we've got */
    for (n = 0; n < nfds; n++) {
        for (i = 0; i < eventLoop.handlesCount &&
             (eventLoop.handles[i].fd != VirWokenFD(opaque, n) ||
              eventLoop.handles[i].events == 0) ; i++) {
            ;
        }
        if (i == eventLoop.handlesCount)
            continue;

        VIR_DEBUG("i=%zu w=%d", i, eventLoop.handles[i].watch);
        if (eventLoop.handles[i].deleted) {
            EVENT_DEBUG("Skip deleted n=%zu w=%d f=%d", i,
                        eventLoop.handles[i].watch, eventLoop.handles[i].fd);
            continue;
        }

        if (VirWokenEvents(opaque, n)) {
            int hEvents = virEventPollFromNativeEvents(VirWokenEvents(opaque, n));
            virEventHandleCallback cb = eventLoop.handles[i].cb;
            int watch = eventLoop.handles[i].watch;
            void *cbopaque = eventLoop.handles[i].opaque;
            PROBE(EVENT_POLL_DISPATCH_HANDLE,
                  "watch=%d events=%d",
                  watch, hEvents);
            virMutexUnlock(&eventLoop.lock);
            (cb)(watch, VirWokenFD(opaque, n), hEvents, cbopaque);
            virMutexLock(&eventLoop.lock);
        }
    }

    return 0;
}


/* Used post dispatch to actually remove any timers that
 * were previously marked as deleted. This asynchronous
 * cleanup is needed to make dispatch re-entrant safe.
 */
static void virEventPollCleanupTimeouts(void)
{
    size_t i;
    size_t gap;
    VIR_DEBUG("Cleanup %zu", eventLoop.timeoutsCount);

    /* Remove deleted entries, shuffling down remaining
     * entries as needed to form contiguous series
     */
    for (i = 0; i < eventLoop.timeoutsCount;) {
        if (!eventLoop.timeouts[i].deleted) {
            i++;
            continue;
        }

        PROBE(EVENT_POLL_PURGE_TIMEOUT,
              "timer=%d",
              eventLoop.timeouts[i].timer);
        if (eventLoop.timeouts[i].ff) {
            virFreeCallback ff = eventLoop.timeouts[i].ff;
            void *opaque = eventLoop.timeouts[i].opaque;
            virMutexUnlock(&eventLoop.lock);
            ff(opaque);
            virMutexLock(&eventLoop.lock);
        }

        if ((i+1) < eventLoop.timeoutsCount) {
            memmove(eventLoop.timeouts+i,
                    eventLoop.timeouts+i+1,
                    sizeof(struct virEventPollTimeout)*(eventLoop.timeoutsCount
                                                    -(i+1)));
        }
        eventLoop.timeoutsCount--;
    }

    /* Release some memory if we've got a big chunk free */
    gap = eventLoop.timeoutsAlloc - eventLoop.timeoutsCount;
    if (eventLoop.timeoutsCount == 0 ||
        (gap > eventLoop.timeoutsCount && gap > EVENT_ALLOC_EXTENT)) {
        EVENT_DEBUG("Found %zu out of %zu timeout slots used, releasing %zu",
                    eventLoop.timeoutsCount, eventLoop.timeoutsAlloc, gap);
        VIR_SHRINK_N(eventLoop.timeouts, eventLoop.timeoutsAlloc, gap);
    }
}

/* Used post dispatch to actually remove any handles that
 * were previously marked as deleted. This asynchronous
 * cleanup is needed to make dispatch re-entrant safe.
 */
static void virEventPollCleanupHandles(void)
{
    size_t i;
    size_t gap;
    VIR_DEBUG("Cleanup %zu", eventLoop.handlesCount);

    /* Remove deleted entries, shuffling down remaining
     * entries as needed to form contiguous series
     */
    for (i = 0; i < eventLoop.handlesCount;) {
        if (!eventLoop.handles[i].deleted) {
            i++;
            continue;
        }

        PROBE(EVENT_POLL_PURGE_HANDLE,
              "watch=%d",
              eventLoop.handles[i].watch);
        if (eventLoop.handles[i].ff) {
            virFreeCallback ff = eventLoop.handles[i].ff;
            void *opaque = eventLoop.handles[i].opaque;
            virMutexUnlock(&eventLoop.lock);
            ff(opaque);
            virMutexLock(&eventLoop.lock);
        }

        if ((i+1) < eventLoop.handlesCount) {
            memmove(eventLoop.handles+i,
                    eventLoop.handles+i+1,
                    sizeof(struct virEventPollHandle)*(eventLoop.handlesCount
                                                   -(i+1)));
        }
        eventLoop.handlesCount--;
    }

    /* Release some memory if we've got a big chunk free */
    gap = eventLoop.handlesAlloc - eventLoop.handlesCount;
    if (eventLoop.handlesCount == 0 ||
        (gap > eventLoop.handlesCount && gap > EVENT_ALLOC_EXTENT)) {
        EVENT_DEBUG("Found %zu out of %zu handles slots used, releasing %zu",
                    eventLoop.handlesCount, eventLoop.handlesAlloc, gap);
        VIR_SHRINK_N(eventLoop.handles, eventLoop.handlesAlloc, gap);
    }
}

/*
 * Run a single iteration of the event loop, blocking until
 * at least one file handle has an event, or a timer expires
 */
int virEventPollRunOnce(void)
{
    int nfds, timeout;
    void *opaque = NULL;

    virMutexLock(&eventLoop.lock);
    eventLoop.running = 1;
    virThreadSelf(&eventLoop.leader);

    virEventPollCleanupTimeouts();
    virEventPollCleanupHandles();

    if (virEventPollCalculateTimeout(&timeout) < 0)
        goto error;

    virMutexUnlock(&eventLoop.lock);

    if ((nfds = virEventPollWait(timeout, &opaque)) < 0)
        goto error_unlocked;
    EVENT_DEBUG("Poll got %d event(s)", nfds);

    virMutexLock(&eventLoop.lock);
    if (virEventPollDispatchTimeouts() < 0)
        goto error;

    if (nfds > 0 && virEventPollDispatchHandles(nfds, opaque) < 0)
        goto error;

    virEventPollCleanupTimeouts();
    virEventPollCleanupHandles();

    eventLoop.running = 0;
    virMutexUnlock(&eventLoop.lock);
    virEventPollOpaqueFree(opaque);
    return 0;

 error:
    virMutexUnlock(&eventLoop.lock);
 error_unlocked:
    virEventPollOpaqueFree(opaque);
    return -1;
}


static void virEventPollHandleWakeup(int watch ATTRIBUTE_UNUSED,
                                     int fd,
                                     int events ATTRIBUTE_UNUSED,
                                     void *opaque ATTRIBUTE_UNUSED)
{
    char c;
    virMutexLock(&eventLoop.lock);
    ignore_value(saferead(fd, &c, sizeof(c)));
    virMutexUnlock(&eventLoop.lock);
}

int virEventPollInit(void)
{
    if (virMutexInit(&eventLoop.lock) < 0) {
        virReportSystemError(errno, "%s",
                             _("Unable to initialize mutex"));
        return -1;
    }

    if (pipe2(eventLoop.wakeupfd, O_CLOEXEC | O_NONBLOCK) < 0) {
        virReportSystemError(errno, "%s",
                             _("Unable to setup wakeup pipe"));
        return -1;
    }

    if (virEventPollInitInternal() < 0) {
        VIR_FORCE_CLOSE(eventLoop.wakeupfd[0]);
        VIR_FORCE_CLOSE(eventLoop.wakeupfd[1]);
        return -1;
    }

    if (virEventPollAddHandle(eventLoop.wakeupfd[0],
                              VIR_EVENT_HANDLE_READABLE,
                              virEventPollHandleWakeup, NULL, NULL) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to add handle %d to event loop"),
                       eventLoop.wakeupfd[0]);
        VIR_FORCE_CLOSE(eventLoop.wakeupfd[0]);
        VIR_FORCE_CLOSE(eventLoop.wakeupfd[1]);
        virEventPollDeinitInternal();
        return -1;
    }

    return 0;
}

int virEventPollInterruptLocked(void)
{
    char c = '\0';

    if (!eventLoop.running ||
        virThreadIsSelf(&eventLoop.leader)) {
        VIR_DEBUG("Skip interrupt, %d %llu", eventLoop.running,
                  virThreadID(&eventLoop.leader));
        return 0;
    }

    VIR_DEBUG("Interrupting");
    if (safewrite(eventLoop.wakeupfd[1], &c, sizeof(c)) != sizeof(c))
        return -1;
    return 0;
}

int virEventPollInterrupt(void)
{
    int ret;
    virMutexLock(&eventLoop.lock);
    ret = virEventPollInterruptLocked();
    virMutexUnlock(&eventLoop.lock);
    return ret;
}
