/*
 * vireventpoll.h: Poll based event loop for monitoring file handles
 *
 * Copyright (C) 2007 Daniel P. Berrange
 * Copyright (C) 2007 Red Hat, Inc.
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

#ifndef __VIR_EVENT_POLL_INTERNAL_H__
# define __VIR_EVENT_POLL_INTERNAL_H__

# include "internal.h"
#include "virthread.h"

int virEventPollInterruptLocked(void);

/* State for a single file handle being monitored */
struct virEventPollHandle {
    int watch;
    int fd;
    int events;
    virEventHandleCallback cb;
    virFreeCallback ff;
    void *opaque;
    int deleted;
};

/* State for a single timer being generated */
struct virEventPollTimeout {
    int timer;
    int frequency;
    unsigned long long expiresAt;
    virEventTimeoutCallback cb;
    virFreeCallback ff;
    void *opaque;
    int deleted;
};

/* Allocate extra slots for virEventPollHandle/virEventPollTimeout
   records in this multiple */
#define EVENT_ALLOC_EXTENT 10

/* State for the main event loop */
struct virEventPollLoop {
    virMutex lock;
    int running;
    virThread leader;
    int wakeupfd[2];
    size_t handlesCount;
    size_t handlesAlloc;
    struct virEventPollHandle *handles;
    size_t timeoutsCount;
    size_t timeoutsAlloc;
    struct virEventPollTimeout *timeouts;
};

/* Only have one event loop */
extern struct virEventPollLoop eventLoop;

/* Unique ID for the next FD watch to be registered */
extern int nextWatch;

/* Unique ID for the next timer to be registered */
extern int nextTimer;

int virEventPollAddHandleInternal(int watch, int fd, int nativeevents);
int virEventPollUpdateHandleInternal(int watch, int fd, int nativeevents);
int virEventPollRemoveHandleInternal(int watch, int fd);
int virEventPollInitInternal(void);
void virEventPollDeinitInternal(void);
int virEventPollWait(int timeout, void **opaque);
void virEventPollOpaqueFree(void *opaque);
int VirWokenFD(void *opaque, size_t n);
int VirWokenEvents(void *opaque, size_t n);

#endif /* __VIR_EVENT_POLL_INTERNAL_H__ */
