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

#include <sys/epoll.h>

#include "virfile.h"
#include "virlog.h"
#include "vireventpoll.h"
#include "vireventpollinternal.h"

#define EVENT_DEBUG(fmt, ...) VIR_DEBUG(fmt, __VA_ARGS__)

#define VIR_FROM_THIS VIR_FROM_EVENT

VIR_LOG_INIT("util.eventpoll");

/* Maximum number of events that are returned by epoll in virEventPollRunOnce */
#define MAX_POLL_EVENTS_AT_ONCE 10

int epollfd;

int virEventPollAddHandleInternal(int watch ATTRIBUTE_UNUSED,
                                  int fd,
                                  int nativeevents)
{
    size_t i;
    struct epoll_event ev;
    ev.events = nativeevents;
    ev.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            for (i = 0; i < eventLoop.handlesCount; i++) {
                if (eventLoop.handles[i].fd == fd &&
                    !eventLoop.handles[i].deleted) {
                    ev.events |= eventLoop.handles[i].events;
                }
            }
            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
                return -1;
            }
        }
        else {
            return -1;
        }
    }
    return 0;
}

int virEventPollUpdateHandleInternal(int watch, int fd, int nativeevents)
{
    struct epoll_event ev;
    size_t i;

    ev.events = nativeevents;
    ev.data.fd = fd;
    for (i = 0; i < eventLoop.handlesCount; i++) {
        if (eventLoop.handles[i].fd == fd &&
            !eventLoop.handles[i].deleted &&
            eventLoop.handles[i].watch != watch) {
            ev.events |= eventLoop.handles[i].events;
        }
    }

    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return -1;
    }
    return 0;
}

int virEventPollRemoveHandleInternal(int watch, int fd)
{

    struct epoll_event ev;
    size_t i;

    ev.events = 0;
    ev.data.fd = fd;
    for (i = 0; i < eventLoop.handlesCount; i++) {
        if (eventLoop.handles[i].fd == fd &&
            !eventLoop.handles[i].deleted &&
            eventLoop.handles[i].watch != watch) {
            ev.events |= eventLoop.handles[i].events;
        }
    }

    if (ev.events) {
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
            return -1;
        }
    }
    else {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev) < 0) {
            return -1;
        }
    }

    return 0;
}

int virEventPollInitInternal(void)
{
    epollfd = epoll_create1(0);
    if (epollfd < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to initialize epoll"));
        return -1;
    }
    return 0;
}

void virEventPollDeinitInternal(void)
{
    VIR_FORCE_CLOSE(epollfd);
}

int
virEventPollToNativeEvents(int events)
{
    int ret = 0;
    if (events & VIR_EVENT_HANDLE_READABLE)
        ret |= EPOLLIN;
    if (events & VIR_EVENT_HANDLE_WRITABLE)
        ret |= EPOLLOUT;
    if (events & VIR_EVENT_HANDLE_ERROR)
        ret |= EPOLLERR;
    if (events & VIR_EVENT_HANDLE_HANGUP)
        ret |= EPOLLHUP;
    return ret;
}

int
virEventPollFromNativeEvents(int events)
{
    int ret = 0;
    if (events & EPOLLIN)
        ret |= VIR_EVENT_HANDLE_READABLE;
    if (events & EPOLLOUT)
        ret |= VIR_EVENT_HANDLE_WRITABLE;
    if (events & EPOLLERR)
        ret |= VIR_EVENT_HANDLE_ERROR;
    if (events & EPOLLHUP)
        ret |= VIR_EVENT_HANDLE_HANGUP;
    return ret;
}

struct epoll_event events[MAX_POLL_EVENTS_AT_ONCE];

int virEventPollWait(int timeout, void **opaque)
{
    int ret;
    *opaque = events;

 retry:
    ret = epoll_wait(epollfd, events,
                     MAX_POLL_EVENTS_AT_ONCE, timeout);
    if (ret < 0) {
        EVENT_DEBUG("Poll got error event %d", errno);
        if (errno == EINTR || errno == EAGAIN)
            goto retry;
        virReportSystemError(errno, "%s",
                             _("Unable to poll on file handles"));
    }
    return ret;
}

void virEventPollOpaqueFree(void *opaque ATTRIBUTE_UNUSED)
{
    return;
}

int VirWokenFD(void *opaque, size_t n)
{
    return ((struct epoll_event *)opaque)[n].data.fd;
}

int VirWokenEvents(void *opaque, size_t n)
{
    return ((struct epoll_event *)opaque)[n].events;
}
