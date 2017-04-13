/*
 * virfdstream.c: generic streams impl for file descriptors
 *
 * Copyright (C) 2009-2012, 2014 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#if HAVE_SYS_UN_H
# include <sys/un.h>
#endif
#include <netinet/in.h>
#include <termios.h>

#include "virfdstream.h"
#include "virerror.h"
#include "datatypes.h"
#include "virlog.h"
#include "viralloc.h"
#include "virutil.h"
#include "virfile.h"
#include "configmake.h"
#include "virstring.h"
#include "virtime.h"
#include "virprocess.h"

#define VIR_FROM_THIS VIR_FROM_STREAMS

VIR_LOG_INIT("fdstream");

typedef enum {
    VIR_FDSTREAM_MSG_TYPE_DATA,
} virFDStreamMsgType;

typedef struct _virFDStreamMsg virFDStreamMsg;
typedef virFDStreamMsg *virFDStreamMsgPtr;
struct _virFDStreamMsg {
    virFDStreamMsgPtr next;

    virFDStreamMsgType type;

    union {
        struct {
            char *buf;
            size_t len;
            size_t offset;
        } data;
    } stream;
};


/* Tunnelled migration stream support */
typedef struct virFDStreamData virFDStreamData;
typedef virFDStreamData *virFDStreamDataPtr;
struct virFDStreamData {
    virObjectLockable parent;

    int fd;
    unsigned long long offset;
    unsigned long long length;

    int watch;
    int events;         /* events the stream callback is subscribed for */
    bool cbRemoved;
    bool dispatching;
    bool closed;
    virStreamEventCallback cb;
    void *opaque;
    virFreeCallback ff;

    /* don't call the abort callback more than once */
    bool abortCallbackCalled;
    bool abortCallbackDispatching;

    /* internal callback, as the regular one (from generic streams) gets
     * eaten up by the server stream driver */
    virFDStreamInternalCloseCb icbCb;
    virFDStreamInternalCloseCbFreeOpaque icbFreeOpaque;
    void *icbOpaque;

    /* Thread data */
    virThreadPtr thread;
    virCond threadCond;
    int threadErr;
    bool threadQuit;
    bool threadAbort;
    bool threadDoRead;
    virFDStreamMsgPtr msg;
};

static virClassPtr virFDStreamDataClass;

static void virFDStreamMsgQueueFree(virFDStreamMsgPtr *queue);

static void
virFDStreamDataDispose(void *obj)
{
    virFDStreamDataPtr fdst = obj;

    VIR_DEBUG("obj=%p", fdst);
    virFDStreamMsgQueueFree(&fdst->msg);
}

static int virFDStreamDataOnceInit(void)
{
    if (!(virFDStreamDataClass = virClassNew(virClassForObjectLockable(),
                                             "virFDStreamData",
                                             sizeof(virFDStreamData),
                                             virFDStreamDataDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virFDStreamData)


static void
virFDStreamMsgQueuePush(virFDStreamDataPtr fdst,
                        virFDStreamMsgPtr msg)
{
    virFDStreamMsgPtr *tmp = &fdst->msg;

    while (*tmp)
        tmp = &(*tmp)->next;

    *tmp = msg;
    virCondSignal(&fdst->threadCond);
}


static virFDStreamMsgPtr
virFDStreamMsgQueuePop(virFDStreamDataPtr fdst)
{
    virFDStreamMsgPtr tmp = fdst->msg;

    if (tmp) {
        fdst->msg = tmp->next;
        tmp->next = NULL;
    }

    virCondSignal(&fdst->threadCond);
    return tmp;
}


static void
virFDStreamMsgFree(virFDStreamMsgPtr msg)
{
    if (!msg)
        return;

    switch (msg->type) {
    case VIR_FDSTREAM_MSG_TYPE_DATA:
        VIR_FREE(msg->stream.data.buf);
        break;
    }

    VIR_FREE(msg);
}


static void
virFDStreamMsgQueueFree(virFDStreamMsgPtr *queue)
{
    virFDStreamMsgPtr tmp = *queue;

    while (tmp) {
        virFDStreamMsgPtr next = tmp->next;
        virFDStreamMsgFree(tmp);
        tmp = next;
    }

    *queue = NULL;
}


static int virFDStreamRemoveCallback(virStreamPtr stream)
{
    virFDStreamDataPtr fdst = stream->privateData;
    int ret = -1;

    if (!fdst) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream is not open"));
        return -1;
    }

    virObjectLock(fdst);
    if (fdst->watch == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream does not have a callback registered"));
        goto cleanup;
    }

    virEventRemoveHandle(fdst->watch);
    if (fdst->dispatching)
        fdst->cbRemoved = true;
    else if (fdst->ff)
        (fdst->ff)(fdst->opaque);

    fdst->watch = 0;
    fdst->ff = NULL;
    fdst->cb = NULL;
    fdst->events = 0;
    fdst->opaque = NULL;

    ret = 0;

 cleanup:
    virObjectUnlock(fdst);
    return ret;
}

static int virFDStreamUpdateCallback(virStreamPtr stream, int events)
{
    virFDStreamDataPtr fdst = stream->privateData;
    int ret = -1;

    if (!fdst) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream is not open"));
        return -1;
    }

    virObjectLock(fdst);
    if (fdst->watch == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream does not have a callback registered"));
        goto cleanup;
    }

    virEventUpdateHandle(fdst->watch, events);
    fdst->events = events;

    ret = 0;

 cleanup:
    virObjectUnlock(fdst);
    return ret;
}

static void virFDStreamEvent(int watch ATTRIBUTE_UNUSED,
                             int fd ATTRIBUTE_UNUSED,
                             int events,
                             void *opaque)
{
    virStreamPtr stream = opaque;
    virFDStreamDataPtr fdst = stream->privateData;
    virStreamEventCallback cb;
    void *cbopaque;
    virFreeCallback ff;
    bool closed;

    if (!fdst)
        return;

    virObjectLock(fdst);
    if (!fdst->cb) {
        virObjectUnlock(fdst);
        return;
    }

    cb = fdst->cb;
    cbopaque = fdst->opaque;
    ff = fdst->ff;
    fdst->dispatching = true;
    virObjectUnlock(fdst);

    cb(stream, events, cbopaque);

    virObjectLock(fdst);
    fdst->dispatching = false;
    if (fdst->cbRemoved && ff)
        (ff)(cbopaque);
    closed = fdst->closed;
    virObjectUnlock(fdst);

    if (closed)
        virObjectUnref(fdst);
}

static void virFDStreamCallbackFree(void *opaque)
{
    virObjectUnref(opaque);
}


static int
virFDStreamAddCallback(virStreamPtr st,
                       int events,
                       virStreamEventCallback cb,
                       void *opaque,
                       virFreeCallback ff)
{
    virFDStreamDataPtr fdst = st->privateData;
    int ret = -1;

    if (!fdst) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream is not open"));
        return -1;
    }

    virObjectLock(fdst);
    if (fdst->watch != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream already has a callback registered"));
        goto cleanup;
    }

    if ((fdst->watch = virEventAddHandle(fdst->fd,
                                         events,
                                         virFDStreamEvent,
                                         st,
                                         virFDStreamCallbackFree)) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("cannot register file watch on stream"));
        goto cleanup;
    }

    fdst->cbRemoved = false;
    fdst->cb = cb;
    fdst->opaque = opaque;
    fdst->ff = ff;
    fdst->events = events;
    fdst->abortCallbackCalled = false;
    virStreamRef(st);

    ret = 0;

 cleanup:
    virObjectUnlock(fdst);
    return ret;
}


typedef struct _virFDStreamThreadData virFDStreamThreadData;
typedef virFDStreamThreadData *virFDStreamThreadDataPtr;
struct _virFDStreamThreadData {
    virStreamPtr st;
    size_t length;
    int fdin;
    char *fdinname;
    int fdout;
    char *fdoutname;
};


static void
virFDStreamThreadDataFree(virFDStreamThreadDataPtr data)
{
    if (!data)
        return;

    virObjectUnref(data->st);
    VIR_FREE(data->fdinname);
    VIR_FREE(data->fdoutname);
    VIR_FREE(data);
}


static ssize_t
virFDStreamThreadDoRead(virFDStreamDataPtr fdst,
                        const int fdin,
                        const char *fdinname,
                        size_t buflen)
{
    virFDStreamMsgPtr msg = NULL;
    char *buf = NULL;
    ssize_t got;

    if (VIR_ALLOC(msg) < 0)
        goto error;

    if (VIR_ALLOC_N(buf, buflen) < 0)
        goto error;

    if ((got = saferead(fdin, buf, buflen)) < 0) {
        virReportSystemError(errno,
                             _("Unable to read %s"),
                             fdinname);
        goto error;
    }

    msg->type = VIR_FDSTREAM_MSG_TYPE_DATA;
    msg->stream.data.buf = buf;
    msg->stream.data.len = got;
    buf = NULL;

    virFDStreamMsgQueuePush(fdst, msg);
    msg = NULL;

    return got;

 error:
    VIR_FREE(buf);
    virFDStreamMsgFree(msg);
    return -1;
}


static ssize_t
virFDStreamThreadDoWrite(virFDStreamDataPtr fdst,
                         const int fdout,
                         const char *fdoutname)
{
    ssize_t got;
    virFDStreamMsgPtr msg = fdst->msg;
    bool pop = false;

    switch (msg->type) {
    case VIR_FDSTREAM_MSG_TYPE_DATA:
        got = safewrite(fdout,
                        msg->stream.data.buf + msg->stream.data.offset,
                        msg->stream.data.len - msg->stream.data.offset);
        if (got < 0) {
            virReportSystemError(errno,
                                 _("Unable to write %s"),
                                 fdoutname);
            return -1;
        }

        msg->stream.data.offset += got;

        pop = msg->stream.data.offset == msg->stream.data.len;
        break;
    }

    if (pop) {
        virFDStreamMsgQueuePop(fdst);
        virFDStreamMsgFree(msg);
    }

    return got;
}


static void
virFDStreamThread(void *opaque)
{
    virFDStreamThreadDataPtr data = opaque;
    virStreamPtr st = data->st;
    size_t length = data->length;
    int fdin = data->fdin;
    char *fdinname = data->fdinname;
    int fdout = data->fdout;
    char *fdoutname = data->fdoutname;
    virFDStreamDataPtr fdst = st->privateData;
    bool doRead = fdst->threadDoRead;
    size_t buflen = 256 * 1024;
    size_t total = 0;

    virObjectRef(fdst);
    virObjectLock(fdst);

    while (1) {
        ssize_t got;

        if (length &&
            (length - total) < buflen)
            buflen = length - total;

        if (buflen == 0)
            break; /* End of requested data from client */

        while (doRead == (fdst->msg != NULL) &&
               !fdst->threadQuit) {
            if (virCondWait(&fdst->threadCond, &fdst->parent.lock)) {
                virReportSystemError(errno, "%s",
                                     _("failed to wait on condition"));
                goto error;
            }
        }

        if (fdst->threadQuit) {
            /* If stream abort was requested, quit early. */
            if (fdst->threadAbort)
                goto cleanup;

            /* Otherwise flush buffers and quit gracefully. */
            if (doRead == (fdst->msg != NULL))
                break;
        }

        if (doRead)
            got = virFDStreamThreadDoRead(fdst, fdin, fdinname, buflen);
        else
            got = virFDStreamThreadDoWrite(fdst, fdout, fdoutname);

        if (got < 0)
            goto error;

        if (got == 0)
            break;

        total += got;
    }

 cleanup:
    fdst->threadQuit = true;
    virObjectUnlock(fdst);
    if (!virObjectUnref(fdst))
        st->privateData = NULL;
    virFDStreamThreadDataFree(data);
    return;

 error:
    fdst->threadErr = errno;
    goto cleanup;
}


static int
virFDStreamJoinWorker(virFDStreamDataPtr fdst, bool streamAbort)
{
    int ret = -1;
    if (!fdst->thread)
        return 0;

    fdst->threadAbort = streamAbort;
    fdst->threadQuit = true;
    virCondSignal(&fdst->threadCond);

    /* Give the thread a chance to lock the FD stream object. */
    virObjectUnlock(fdst);
    virThreadJoin(fdst->thread);
    virObjectLock(fdst);

    if (fdst->threadErr && !streamAbort) {
        /* errors are expected on streamAbort */
        goto cleanup;
    }

    ret = 0;
 cleanup:
    VIR_FREE(fdst->thread);
    virCondDestroy(&fdst->threadCond);
    return ret;
}


static int
virFDStreamCloseInt(virStreamPtr st, bool streamAbort)
{
    virFDStreamDataPtr fdst;
    virStreamEventCallback cb;
    void *opaque;
    int ret;

    VIR_DEBUG("st=%p", st);

    if (!st || !(fdst = st->privateData) || fdst->abortCallbackDispatching)
        return 0;

    virObjectLock(fdst);

    /* aborting the stream, ensure the callback is called if it's
     * registered for stream error event */
    if (streamAbort &&
        fdst->cb &&
        (fdst->events & (VIR_STREAM_EVENT_READABLE |
                         VIR_STREAM_EVENT_WRITABLE))) {
        /* don't enter this function accidentally from the callback again */
        if (fdst->abortCallbackCalled) {
            virObjectUnlock(fdst);
            return 0;
        }

        fdst->abortCallbackCalled = true;
        fdst->abortCallbackDispatching = true;

        /* cache the pointers */
        cb = fdst->cb;
        opaque = fdst->opaque;
        virObjectUnlock(fdst);

        /* call failure callback, poll reports nothing on closed fd */
        (cb)(st, VIR_STREAM_EVENT_ERROR, opaque);

        virObjectLock(fdst);
        fdst->abortCallbackDispatching = false;
    }

    if (virFDStreamJoinWorker(fdst, streamAbort) < 0)
        ret = -1;

    /* mutex locked */
    if ((ret = VIR_CLOSE(fdst->fd)) < 0)
        virReportSystemError(errno, "%s",
                             _("Unable to close"));

    st->privateData = NULL;

    /* call the internal stream closing callback */
    if (fdst->icbCb) {
        /* the mutex is not accessible anymore, as private data is null */
        (fdst->icbCb)(st, fdst->icbOpaque);
        if (fdst->icbFreeOpaque)
            (fdst->icbFreeOpaque)(fdst->icbOpaque);
    }

    if (fdst->dispatching) {
        fdst->closed = true;
        virObjectUnlock(fdst);
    } else {
        virObjectUnlock(fdst);
        virObjectUnref(fdst);
    }

    return ret;
}

static int
virFDStreamClose(virStreamPtr st)
{
    return virFDStreamCloseInt(st, false);
}

static int
virFDStreamAbort(virStreamPtr st)
{
    return virFDStreamCloseInt(st, true);
}

static int virFDStreamWrite(virStreamPtr st, const char *bytes, size_t nbytes)
{
    virFDStreamDataPtr fdst = st->privateData;
    virFDStreamMsgPtr msg = NULL;
    int ret = -1;

    if (nbytes > INT_MAX) {
        virReportSystemError(ERANGE, "%s",
                             _("Too many bytes to write to stream"));
        return -1;
    }

    if (!fdst) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream is not open"));
        return -1;
    }

    virObjectLock(fdst);

    if (fdst->length) {
        if (fdst->length == fdst->offset) {
            virReportSystemError(ENOSPC, "%s",
                                 _("cannot write to stream"));
            virObjectUnlock(fdst);
            return -1;
        }

        if ((fdst->length - fdst->offset) < nbytes)
            nbytes = fdst->length - fdst->offset;
    }

    if (fdst->thread) {
        char *buf;

        if (fdst->threadQuit) {
            virReportSystemError(EBADF, "%s",
                                 _("cannot write to stream"));
            return -1;
        }

        if (VIR_ALLOC(msg) < 0 ||
            VIR_ALLOC_N(buf, nbytes) < 0)
            goto cleanup;

        memcpy(buf, bytes, nbytes);
        msg->type = VIR_FDSTREAM_MSG_TYPE_DATA;
        msg->stream.data.buf = buf;
        msg->stream.data.len = nbytes;

        virFDStreamMsgQueuePush(fdst, msg);
        msg = NULL;
        ret = nbytes;
    } else {
     retry:
        ret = write(fdst->fd, bytes, nbytes);
        if (ret < 0) {
            VIR_WARNINGS_NO_WLOGICALOP_EQUAL_EXPR
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
            VIR_WARNINGS_RESET
                ret = -2;
            } else if (errno == EINTR) {
                goto retry;
            } else {
                ret = -1;
                virReportSystemError(errno, "%s",
                                     _("cannot write to stream"));
            }
        }
    }

    if (fdst->length)
        fdst->offset += ret;

 cleanup:
    virObjectUnlock(fdst);
    virFDStreamMsgFree(msg);
    return ret;
}


static int virFDStreamRead(virStreamPtr st, char *bytes, size_t nbytes)
{
    virFDStreamDataPtr fdst = st->privateData;
    int ret = -1;

    if (nbytes > INT_MAX) {
        virReportSystemError(ERANGE, "%s",
                             _("Too many bytes to read from stream"));
        return -1;
    }

    if (!fdst) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("stream is not open"));
        return -1;
    }

    virObjectLock(fdst);

    if (fdst->length) {
        if (fdst->length == fdst->offset) {
            virObjectUnlock(fdst);
            return 0;
        }

        if ((fdst->length - fdst->offset) < nbytes)
            nbytes = fdst->length - fdst->offset;
    }

    if (fdst->thread) {
        virFDStreamMsgPtr msg = NULL;

        while (!(msg = fdst->msg)) {
            if (fdst->threadQuit) {
                if (nbytes) {
                    virReportSystemError(EBADF, "%s",
                                         _("stream is not open"));
                } else {
                    ret = 0;
                }
                goto cleanup;
            } else {
                virObjectUnlock(fdst);
                virCondSignal(&fdst->threadCond);
                virObjectLock(fdst);
            }
        }

        if (msg->type != VIR_FDSTREAM_MSG_TYPE_DATA) {
            /* Nope, nope, I'm outta here */
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("unexpected message type"));
            goto cleanup;
        }

        if (nbytes > msg->stream.data.len - msg->stream.data.offset)
            nbytes = msg->stream.data.len - msg->stream.data.offset;

        memcpy(bytes,
               msg->stream.data.buf + msg->stream.data.offset,
               nbytes);

        msg->stream.data.offset += nbytes;
        if (msg->stream.data.offset == msg->stream.data.len) {
            virFDStreamMsgQueuePop(fdst);
            virFDStreamMsgFree(msg);
        }

        ret = nbytes;

    } else {
     retry:
        ret = read(fdst->fd, bytes, nbytes);
        if (ret < 0) {
            VIR_WARNINGS_NO_WLOGICALOP_EQUAL_EXPR
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
            VIR_WARNINGS_RESET
                ret = -2;
            } else if (errno == EINTR) {
                goto retry;
            } else {
                ret = -1;
                virReportSystemError(errno, "%s",
                                     _("cannot read from stream"));
            }
            goto cleanup;
        }
    }

    if (fdst->length)
        fdst->offset += ret;

 cleanup:
    virObjectUnlock(fdst);
    return ret;
}


static virStreamDriver virFDStreamDrv = {
    .streamSend = virFDStreamWrite,
    .streamRecv = virFDStreamRead,
    .streamFinish = virFDStreamClose,
    .streamAbort = virFDStreamAbort,
    .streamEventAddCallback = virFDStreamAddCallback,
    .streamEventUpdateCallback = virFDStreamUpdateCallback,
    .streamEventRemoveCallback = virFDStreamRemoveCallback
};

static int virFDStreamOpenInternal(virStreamPtr st,
                                   int fd,
                                   virFDStreamThreadDataPtr threadData,
                                   unsigned long long length)
{
    virFDStreamDataPtr fdst;

    VIR_DEBUG("st=%p fd=%d threadData=%p length=%llu",
              st, fd, threadData, length);

    if (virFDStreamDataInitialize() < 0)
        return -1;

    if ((st->flags & VIR_STREAM_NONBLOCK) && !threadData &&
        virSetNonBlock(fd) < 0) {
        virReportSystemError(errno, "%s", _("Unable to set non-blocking mode"));
        return -1;
    }

    if (!(fdst = virObjectLockableNew(virFDStreamDataClass)))
        return -1;

    fdst->fd = fd;
    fdst->length = length;

    st->driver = &virFDStreamDrv;
    st->privateData = fdst;

    if (threadData) {
        /* The thread is going to do reads if fdin is set and fdout is not. */
        fdst->threadDoRead = threadData->fdout == -1;

        /* Create the thread after fdst and st were initialized.
         * The thread worker expects them to be that way. */
        if (VIR_ALLOC(fdst->thread) < 0)
            goto error;

        if (virCondInit(&fdst->threadCond) < 0) {
            virReportSystemError(errno, "%s",
                                 _("cannot initialize condition variable"));
            goto error;
        }

        if (virThreadCreate(fdst->thread,
                            true,
                            virFDStreamThread,
                            threadData) < 0)
            goto error;
    }

    return 0;

 error:
    VIR_FREE(fdst->thread);
    st->driver = NULL;
    st->privateData = NULL;
    virObjectUnref(fdst);
    return -1;
}


int virFDStreamOpen(virStreamPtr st,
                    int fd)
{
    return virFDStreamOpenInternal(st, fd, NULL, 0);
}


#if HAVE_SYS_UN_H
int virFDStreamConnectUNIX(virStreamPtr st,
                           const char *path,
                           bool abstract)
{
    struct sockaddr_un sa;
    virTimeBackOffVar timeout;
    int ret;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        virReportSystemError(errno, "%s", _("Unable to open UNIX socket"));
        goto error;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (abstract) {
        if (virStrcpy(sa.sun_path+1, path, sizeof(sa.sun_path)-1) == NULL)
            goto error;
        sa.sun_path[0] = '\0';
    } else {
        if (virStrcpy(sa.sun_path, path, sizeof(sa.sun_path)) == NULL)
            goto error;
    }

    if (virTimeBackOffStart(&timeout, 1, 3*1000 /* ms */) < 0)
        goto error;
    while (virTimeBackOffWait(&timeout)) {
        ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
        if (ret == 0)
            break;

        if (errno == ENOENT || errno == ECONNREFUSED) {
            /* ENOENT       : Socket may not have shown up yet
             * ECONNREFUSED : Leftover socket hasn't been removed yet */
            continue;
        }

        goto error;
    }

    if (virFDStreamOpenInternal(st, fd, NULL, 0) < 0)
        goto error;
    return 0;

 error:
    VIR_FORCE_CLOSE(fd);
    return -1;
}
#else
int virFDStreamConnectUNIX(virStreamPtr st ATTRIBUTE_UNUSED,
                           const char *path ATTRIBUTE_UNUSED,
                           bool abstract ATTRIBUTE_UNUSED)
{
    virReportSystemError(ENOSYS, "%s",
                         _("UNIX domain sockets are not supported on this platform"));
    return -1;
}
#endif

static int
virFDStreamOpenFileInternal(virStreamPtr st,
                            const char *path,
                            unsigned long long offset,
                            unsigned long long length,
                            int oflags,
                            int mode,
                            bool forceIOHelper)
{
    int fd = -1;
    struct stat sb;
    virFDStreamThreadDataPtr threadData = NULL;

    VIR_DEBUG("st=%p path=%s oflags=%x offset=%llu length=%llu mode=%o",
              st, path, oflags, offset, length, mode);

    oflags |= O_NOCTTY | O_BINARY;

    if (oflags & O_CREAT)
        fd = open(path, oflags, mode);
    else
        fd = open(path, oflags);
    if (fd < 0) {
        virReportSystemError(errno,
                             _("Unable to open stream for '%s'"),
                             path);
        return -1;
    }

    if (fstat(fd, &sb) < 0) {
        virReportSystemError(errno,
                             _("Unable to access stream for '%s'"),
                             path);
        goto error;
    }

    if (offset &&
        lseek(fd, offset, SEEK_SET) < 0) {
        virReportSystemError(errno,
                             _("Unable to seek %s to %llu"),
                             path, offset);
        goto error;
    }

    /* Thanks to the POSIX i/o model, we can't reliably get
     * non-blocking I/O on block devs/regular files. To
     * support those we need to fork a helper process to do
     * the I/O so we just have a fifo. Or use AIO :-(
     */
    if ((st->flags & VIR_STREAM_NONBLOCK) &&
        ((!S_ISCHR(sb.st_mode) &&
          !S_ISFIFO(sb.st_mode)) || forceIOHelper)) {
        if ((oflags & O_ACCMODE) == O_RDWR) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("%s: Cannot request read and write flags together"),
                           path);
            goto error;
        }

        if (VIR_ALLOC(threadData) < 0)
            goto error;

        threadData->st = virObjectRef(st);
        threadData->length = length;

        if ((oflags & O_ACCMODE) == O_RDONLY) {
            threadData->fdin = fd;
            threadData->fdout = -1;
            if (VIR_STRDUP(threadData->fdinname, path) < 0)
                goto error;
        } else {
            threadData->fdin = -1;
            threadData->fdout = fd;
            if (VIR_STRDUP(threadData->fdoutname, path) < 0)
                goto error;
        }
    }

    if (virFDStreamOpenInternal(st, fd, threadData, length) < 0)
        goto error;

    return 0;

 error:
    VIR_FORCE_CLOSE(fd);
    if (oflags & O_CREAT)
        unlink(path);
    virFDStreamThreadDataFree(threadData);
    return -1;
}

int virFDStreamOpenFile(virStreamPtr st,
                        const char *path,
                        unsigned long long offset,
                        unsigned long long length,
                        int oflags)
{
    if (oflags & O_CREAT) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Attempt to create %s without specifying mode"),
                       path);
        return -1;
    }
    return virFDStreamOpenFileInternal(st, path,
                                       offset, length,
                                       oflags, 0, false);
}

int virFDStreamCreateFile(virStreamPtr st,
                          const char *path,
                          unsigned long long offset,
                          unsigned long long length,
                          int oflags,
                          mode_t mode)
{
    return virFDStreamOpenFileInternal(st, path,
                                       offset, length,
                                       oflags | O_CREAT, mode,
                                       false);
}

#ifdef HAVE_CFMAKERAW
int virFDStreamOpenPTY(virStreamPtr st,
                       const char *path,
                       unsigned long long offset,
                       unsigned long long length,
                       int oflags)
{
    virFDStreamDataPtr fdst = NULL;
    struct termios rawattr;

    if (virFDStreamOpenFileInternal(st, path,
                                    offset, length,
                                    oflags | O_CREAT, 0,
                                    false) < 0)
        return -1;

    fdst = st->privateData;

    if (tcgetattr(fdst->fd, &rawattr) < 0) {
        virReportSystemError(errno,
                             _("unable to get tty attributes: %s"),
                             path);
        goto cleanup;
    }

    cfmakeraw(&rawattr);

    if (tcsetattr(fdst->fd, TCSANOW, &rawattr) < 0) {
        virReportSystemError(errno,
                             _("unable to set tty attributes: %s"),
                             path);
        goto cleanup;
    }

    return 0;

 cleanup:
    virFDStreamClose(st);
    return -1;
}
#else /* !HAVE_CFMAKERAW */
int virFDStreamOpenPTY(virStreamPtr st,
                       const char *path,
                       unsigned long long offset,
                       unsigned long long length,
                       int oflags)
{
    return virFDStreamOpenFileInternal(st, path,
                                       offset, length,
                                       oflags | O_CREAT, 0,
                                       false);
}
#endif /* !HAVE_CFMAKERAW */

int virFDStreamOpenBlockDevice(virStreamPtr st,
                               const char *path,
                               unsigned long long offset,
                               unsigned long long length,
                               int oflags)
{
    return virFDStreamOpenFileInternal(st, path,
                                       offset, length,
                                       oflags, 0, true);
}

int virFDStreamSetInternalCloseCb(virStreamPtr st,
                                  virFDStreamInternalCloseCb cb,
                                  void *opaque,
                                  virFDStreamInternalCloseCbFreeOpaque fcb)
{
    virFDStreamDataPtr fdst = st->privateData;

    virObjectLock(fdst);

    if (fdst->icbFreeOpaque)
        (fdst->icbFreeOpaque)(fdst->icbOpaque);

    fdst->icbCb = cb;
    fdst->icbOpaque = opaque;
    fdst->icbFreeOpaque = fcb;

    virObjectUnlock(fdst);
    return 0;
}
