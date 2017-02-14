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

#include <poll.h>

#include "viralloc.h"
#include "virlog.h"
#include "vireventpoll.h"
#include "vireventpollinternal.h"

#define EVENT_DEBUG(fmt, ...) VIR_DEBUG(fmt, __VA_ARGS__)

#define VIR_FROM_THIS VIR_FROM_EVENT

VIR_LOG_INIT("util.eventpoll");

int virEventPollAddHandleInternal(int watch ATTRIBUTE_UNUSED,
                                  int fd ATTRIBUTE_UNUSED,
                                  int nativeevents ATTRIBUTE_UNUSED)
{
    virEventPollInterruptLocked();
    return 0;
}

int virEventPollUpdateHandleInternal(int watch ATTRIBUTE_UNUSED,
                                     int fd ATTRIBUTE_UNUSED,
                                     int nativeevents ATTRIBUTE_UNUSED)
{
    virEventPollInterruptLocked();
    return 0;
}

int virEventPollRemoveHandleInternal(int watch ATTRIBUTE_UNUSED,
                                     int fd ATTRIBUTE_UNUSED)
{
    // virEventPollInterruptLocked() is called in common code.
    return 0;
}

/*
 * Allocate a pollfd array containing data for all registered
 * file handles. The caller must free the returned data struct
 * returns: the pollfd array, or NULL on error
 */
static struct pollfd *virEventPollMakePollFDs(int *nfds) {
    struct pollfd *fds;
    size_t i;

    *nfds = 0;
    for (i = 0; i < eventLoop.handlesCount; i++) {
        if (eventLoop.handles[i].events && !eventLoop.handles[i].deleted)
            (*nfds)++;
    }

    /* Setup the poll file handle data structs */
    if (VIR_ALLOC_N(fds, *nfds) < 0)
        return NULL;

    *nfds = 0;
    for (i = 0; i < eventLoop.handlesCount; i++) {
        EVENT_DEBUG("Prepare n=%zu w=%d, f=%d e=%d d=%d", i,
                    eventLoop.handles[i].watch,
                    eventLoop.handles[i].fd,
                    eventLoop.handles[i].events,
                    eventLoop.handles[i].deleted);
        if (!eventLoop.handles[i].events || eventLoop.handles[i].deleted)
            continue;
        fds[*nfds].fd = eventLoop.handles[i].fd;
        fds[*nfds].events = eventLoop.handles[i].events;
        fds[*nfds].revents = 0;
        (*nfds)++;
    }

    return fds;
}

int virEventPollInitInternal(void)
{
    return 0;
}

void virEventPollDeinitInternal(void)
{
    return;
}

int
virEventPollToNativeEvents(int events)
{
    int ret = 0;
    if (events & VIR_EVENT_HANDLE_READABLE)
        ret |= POLLIN;
    if (events & VIR_EVENT_HANDLE_WRITABLE)
        ret |= POLLOUT;
    if (events & VIR_EVENT_HANDLE_ERROR)
        ret |= POLLERR;
    if (events & VIR_EVENT_HANDLE_HANGUP)
        ret |= POLLHUP;
    return ret;
}

int
virEventPollFromNativeEvents(int events)
{
    int ret = 0;
    if (events & POLLIN)
        ret |= VIR_EVENT_HANDLE_READABLE;
    if (events & POLLOUT)
        ret |= VIR_EVENT_HANDLE_WRITABLE;
    if (events & POLLERR)
        ret |= VIR_EVENT_HANDLE_ERROR;
    if (events & POLLNVAL) /* Treat NVAL as error, since libvirt doesn't distinguish */
        ret |= VIR_EVENT_HANDLE_ERROR;
    if (events & POLLHUP)
        ret |= VIR_EVENT_HANDLE_HANGUP;
    return ret;
}

int virEventPollWait(int timeout, void **opaque)
{
    int ret, nfds;
    struct pollfd *fds = NULL;

    if (!(fds = virEventPollMakePollFDs(&nfds)))
        return -1;
    *opaque = fds;

 retry:
    ret = poll(fds, nfds, timeout);
    if (ret < 0) {
        EVENT_DEBUG("Poll got error event %d", errno);
        if (errno == EINTR || errno == EAGAIN)
            goto retry;
        virReportSystemError(errno, "%s",
                             _("Unable to poll on file handles"));
    }
    return nfds;
}

void virEventPollOpaqueFree(void *opaque)
{
    VIR_FREE(opaque);
}

int VirWokenFD(void *opaque, size_t n)
{
    return ((struct pollfd *)opaque)[n].fd;
}

int VirWokenEvents(void *opaque, size_t n)
{
    return ((struct pollfd *)opaque)[n].revents;
}
