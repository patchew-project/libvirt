/*
 * virlog.c: internal logging and debugging
 *
 * Copyright (C) 2008, 2010-2014 Red Hat, Inc.
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <execinfo.h>
#include <regex.h>
#if HAVE_SYSLOG_H
# include <syslog.h>
#endif
#include <sys/socket.h>
#if HAVE_SYS_UN_H
# include <sys/un.h>
#endif

#include "virerror.h"
#include "virlog.h"
#include "viralloc.h"
#include "virutil.h"
#include "virbuffer.h"
#include "virthread.h"
#include "virfile.h"
#include "virtime.h"
#include "intprops.h"
#include "virstring.h"

/* Journald output is only supported on Linux new enough to expose
 * htole64.  */
#if HAVE_SYSLOG_H && defined(__linux__) && HAVE_DECL_HTOLE64
# define USE_JOURNALD 1
#endif

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.log");

static regex_t *virLogRegex;


#define VIR_LOG_DATE_REGEX "[0-9]{4}-[0-9]{2}-[0-9]{2}"
#define VIR_LOG_TIME_REGEX "[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}\\+[0-9]{4}"
#define VIR_LOG_PID_REGEX "[0-9]+"
#define VIR_LOG_LEVEL_REGEX "(debug|info|warning|error)"

#define VIR_LOG_REGEX \
    VIR_LOG_DATE_REGEX " " VIR_LOG_TIME_REGEX ": " \
    VIR_LOG_PID_REGEX ": " VIR_LOG_LEVEL_REGEX " : "

VIR_ENUM_DECL(virLogDestination);
VIR_ENUM_IMPL(virLogDestination, VIR_LOG_TO_OUTPUT_LAST,
              "stderr", "syslog", "file", "journald");

/*
 * Filters are used to refine the rules on what to keep or drop
 * based on a matching pattern (currently a substring)
 */
struct _virLogFilter {
    char *match;
    virLogPriority priority;
    unsigned int flags;
};

static int virLogFiltersSerial = 1;
static virLogFilterPtr *virLogFilters;
static size_t virLogNbFilters;

/*
 * Outputs are used to emit the messages retained
 * after filtering, multiple output can be used simultaneously
 */
struct _virLogOutput {
    bool logInitMessage;
    void *data;
    virLogOutputFunc f;
    virLogCloseFunc c;
    virLogPriority priority;
    virLogDestination dest;
    char *name;
};

static virLogOutputPtr *virLogOutputs;
static size_t virLogNbOutputs;

/*
 * Default priorities
 */
static virLogPriority virLogDefaultPriority = VIR_LOG_DEFAULT;

static void virLogResetFilters(void);
static void virLogResetOutputs(void);
static void virLogOutputToFd(virLogSourcePtr src,
                             virLogPriority priority,
                             const char *filename,
                             int linenr,
                             const char *funcname,
                             const char *timestamp,
                             virLogMetadataPtr metadata,
                             unsigned int flags,
                             const char *rawstr,
                             const char *str,
                             void *data);


/*
 * Logs accesses must be serialized though a mutex
 */
virMutex virLogMutex;

void
virLogLock(void)
{
    virMutexLock(&virLogMutex);
}


void
virLogUnlock(void)
{
    virMutexUnlock(&virLogMutex);
}


static const char *
virLogPriorityString(virLogPriority lvl)
{
    switch (lvl) {
    case VIR_LOG_DEBUG:
        return "debug";
    case VIR_LOG_INFO:
        return "info";
    case VIR_LOG_WARN:
        return "warning";
    case VIR_LOG_ERROR:
        return "error";
    }
    return "unknown";
}


static int
virLogOnceInit(void)
{
    if (virMutexInit(&virLogMutex) < 0)
        return -1;

    virLogLock();
    virLogDefaultPriority = VIR_LOG_DEFAULT;

    if (VIR_ALLOC_QUIET(virLogRegex) >= 0) {
        if (regcomp(virLogRegex, VIR_LOG_REGEX, REG_EXTENDED) != 0)
            VIR_FREE(virLogRegex);
    }

    virLogUnlock();
    return 0;
}

VIR_ONCE_GLOBAL_INIT(virLog)


/**
 * virLogReset:
 *
 * Reset the logging module to its default initial state
 *
 * Returns 0 if successful, and -1 in case or error
 */
int
virLogReset(void)
{
    if (virLogInitialize() < 0)
        return -1;

    virLogLock();
    virLogResetFilters();
    virLogResetOutputs();
    virLogDefaultPriority = VIR_LOG_DEFAULT;
    virLogUnlock();
    return 0;
}

/**
 * virLogSetDefaultPriority:
 * @priority: the default priority level
 *
 * Set the default priority level, i.e. any logged data of a priority
 * equal or superior to this level will be logged, unless a specific rule
 * was defined for the log category of the message.
 *
 * Returns 0 if successful, -1 in case of error.
 */
int
virLogSetDefaultPriority(virLogPriority priority)
{
    if ((priority < VIR_LOG_DEBUG) || (priority > VIR_LOG_ERROR)) {
        VIR_WARN("Ignoring invalid log level setting.");
        return -1;
    }
    if (virLogInitialize() < 0)
        return -1;

    virLogDefaultPriority = priority;
    return 0;
}


/**
 * virLogResetFilters:
 *
 * Removes the set of logging filters defined.
 */
static void
virLogResetFilters(void)
{
    virLogFilterListFree(virLogFilters, virLogNbFilters);
    virLogFilters = NULL;
    virLogNbFilters = 0;
    virLogFiltersSerial++;
}


void
virLogFilterFree(virLogFilterPtr filter)
{
    if (!filter)
        return;

    VIR_FREE(filter->match);
    VIR_FREE(filter);
}


/**
 * virLogFilterFreeList:
 * @list: list of filters to be freed
 * @count: number of elements in the list
 *
 * Frees a list of filters.
 */
void
virLogFilterListFree(virLogFilterPtr *list, int count)
{
    size_t i;

    if (!list || count < 0)
        return;

    for (i = 0; i < count; i++)
        virLogFilterFree(list[i]);
    VIR_FREE(list);
}


/**
 * virLogDefineFilter:
 * @match: the pattern to match
 * @priority: the priority to give to messages matching the pattern
 * @flags: extra flags, see virLogFilterFlags enum
 *
 * Defines a pattern used for log filtering, it allow to select or
 * reject messages independently of the default priority.
 * The filter defines a rules that will apply only to messages matching
 * the pattern (currently if @match is a substring of the message category)
 *
 * Returns -1 in case of failure or the filter number if successful
 */
int
virLogDefineFilter(const char *match,
                   virLogPriority priority,
                   unsigned int flags)
{
    size_t i;
    int ret = -1;
    char *mdup = NULL;
    virLogFilterPtr filter = NULL;

    virCheckFlags(VIR_LOG_STACK_TRACE, -1);

    if (virLogInitialize() < 0)
        return -1;

    if ((match == NULL) || (priority < VIR_LOG_DEBUG) ||
        (priority > VIR_LOG_ERROR))
        return -1;

    virLogLock();
    for (i = 0; i < virLogNbFilters; i++) {
        if (STREQ(virLogFilters[i]->match, match)) {
            virLogFilters[i]->priority = priority;
            ret = i;
            goto cleanup;
        }
    }

    if (VIR_STRDUP_QUIET(mdup, match) < 0)
        goto cleanup;

    if (VIR_ALLOC_QUIET(filter) < 0) {
        VIR_FREE(mdup);
        goto cleanup;
    }

    filter->match = mdup;
    filter->priority = priority;
    filter->flags = flags;

    if (VIR_APPEND_ELEMENT_QUIET(virLogFilters, virLogNbFilters, filter) < 0)
        goto cleanup;

    virLogFiltersSerial++;
 cleanup:
    virLogUnlock();
    if (ret < 0)
        virReportOOMError();
    return virLogNbFilters;
}

/**
 * virLogResetOutputs:
 *
 * Removes the set of logging output defined.
 */
static void
virLogResetOutputs(void)
{
    virLogOutputListFree(virLogOutputs, virLogNbOutputs);
    virLogOutputs = NULL;
    virLogNbOutputs = 0;
}


void
virLogOutputFree(virLogOutputPtr output)
{
    if (!output)
        return;

    if (output->c)
        output->c(output->data);
    VIR_FREE(output->name);
    VIR_FREE(output);
}


/**
 * virLogOutputsFreeList:
 * @list: list of outputs to be freed
 * @count: number of elements in the list
 *
 * Frees a list of outputs.
 */
void
virLogOutputListFree(virLogOutputPtr *list, int count)
{
    size_t i;

    if (!list || count < 0)
        return;

    for (i = 0; i < count; i++)
        virLogOutputFree(list[i]);
    VIR_FREE(list);
}


/**
 * virLogDefineOutput:
 * @f: the function to call to output a message
 * @c: the function to call to close the output (or NULL)
 * @data: extra data passed as first arg to the function
 * @priority: minimal priority for this filter, use 0 for none
 * @dest: where to send output of this priority
 * @name: optional name data associated with an output
 * @flags: extra flag, currently unused
 *
 * Defines an output function for log messages. Each message once
 * gone though filtering is emitted through each registered output.
 *
 * Returns -1 in case of failure or the output number if successful
 */
int
virLogDefineOutput(virLogOutputFunc f,
                   virLogCloseFunc c,
                   void *data,
                   virLogPriority priority,
                   virLogDestination dest,
                   const char *name,
                   unsigned int flags)
{
    char *ndup = NULL;
    virLogOutputPtr output = NULL;

    virCheckFlags(0, -1);

    if (virLogInitialize() < 0)
        return -1;

    if (f == NULL)
        return -1;

    if (dest == VIR_LOG_TO_SYSLOG || dest == VIR_LOG_TO_FILE) {
        if (!name) {
            virReportOOMError();
            return -1;
        }
        if (VIR_STRDUP(ndup, name) < 0)
            return -1;
    }

    if (VIR_ALLOC_QUIET(output) < 0) {
        VIR_FREE(ndup);
        return -1;
    }

    output->logInitMessage = true;
    output->f = f;
    output->c = c;
    output->data = data;
    output->priority = priority;
    output->dest = dest;
    output->name = ndup;

    virLogLock();
    if (VIR_APPEND_ELEMENT_QUIET(virLogOutputs, virLogNbOutputs, output))
        goto cleanup;

 cleanup:
    virLogUnlock();
    return virLogNbOutputs;
}


static int
virLogFormatString(char **msg,
                   int linenr,
                   const char *funcname,
                   virLogPriority priority,
                   const char *str)
{
    int ret;

    /*
     * Be careful when changing the following log message formatting, we rely
     * on it when stripping libvirt debug messages from qemu log files. So when
     * changing this, you might also need to change the code there.
     * virLogFormatString() function name is mentioned there so it's sufficient
     * to just grep for it to find the right place.
     */
    if ((funcname != NULL)) {
        ret = virAsprintfQuiet(msg, "%llu: %s : %s:%d : %s\n",
                               virThreadSelfID(), virLogPriorityString(priority),
                               funcname, linenr, str);
    } else {
        ret = virAsprintfQuiet(msg, "%llu: %s : %s\n",
                               virThreadSelfID(), virLogPriorityString(priority),
                               str);
    }
    return ret;
}


static int
virLogVersionString(const char **rawmsg,
                    char **msg)
{
    *rawmsg = VIR_LOG_VERSION_STRING;
    return virLogFormatString(msg, 0, NULL, VIR_LOG_INFO, VIR_LOG_VERSION_STRING);
}

/* Similar to virGetHostname() but avoids use of error
 * reporting APIs or logging APIs, to prevent recursion
 */
static int
virLogHostnameString(char **rawmsg,
                     char **msg)
{
    char *hostname = virGetHostnameQuiet();
    char *hoststr;

    if (!hostname)
        return -1;

    if (virAsprintfQuiet(&hoststr, "hostname: %s", hostname) < 0) {
        VIR_FREE(hostname);
        return -1;
    }
    VIR_FREE(hostname);

    if (virLogFormatString(msg, 0, NULL, VIR_LOG_INFO, hoststr) < 0) {
        VIR_FREE(hoststr);
        return -1;
    }
    *rawmsg = hoststr;
    return 0;
}


static void
virLogSourceUpdate(virLogSourcePtr source)
{
    virLogLock();
    if (source->serial < virLogFiltersSerial) {
        unsigned int priority = virLogDefaultPriority;
        unsigned int flags = 0;
        size_t i;

        for (i = 0; i < virLogNbFilters; i++) {
            if (strstr(source->name, virLogFilters[i]->match)) {
                priority = virLogFilters[i]->priority;
                flags = virLogFilters[i]->flags;
                break;
            }
        }

        source->priority = priority;
        source->flags = flags;
        source->serial = virLogFiltersSerial;
    }
    virLogUnlock();
}

/**
 * virLogMessage:
 * @source: where is that message coming from
 * @priority: the priority level
 * @filename: file where the message was emitted
 * @linenr: line where the message was emitted
 * @funcname: the function emitting the (debug) message
 * @metadata: NULL or metadata array, terminated by an item with NULL key
 * @fmt: the string format
 * @...: the arguments
 *
 * Call the libvirt logger with some information. Based on the configuration
 * the message may be stored, sent to output or just discarded
 */
void
virLogMessage(virLogSourcePtr source,
              virLogPriority priority,
              const char *filename,
              int linenr,
              const char *funcname,
              virLogMetadataPtr metadata,
              const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    virLogVMessage(source, priority,
                   filename, linenr, funcname,
                   metadata, fmt, ap);
    va_end(ap);
}


/**
 * virLogVMessage:
 * @source: where is that message coming from
 * @priority: the priority level
 * @filename: file where the message was emitted
 * @linenr: line where the message was emitted
 * @funcname: the function emitting the (debug) message
 * @metadata: NULL or metadata array, terminated by an item with NULL key
 * @fmt: the string format
 * @vargs: format args
 *
 * Call the libvirt logger with some information. Based on the configuration
 * the message may be stored, sent to output or just discarded
 */
void
virLogVMessage(virLogSourcePtr source,
               virLogPriority priority,
               const char *filename,
               int linenr,
               const char *funcname,
               virLogMetadataPtr metadata,
               const char *fmt,
               va_list vargs)
{
    static bool logInitMessageStderr = true;
    char *str = NULL;
    char *msg = NULL;
    char timestamp[VIR_TIME_STRING_BUFLEN];
    int ret;
    size_t i;
    int saved_errno = errno;
    unsigned int filterflags = 0;

    if (virLogInitialize() < 0)
        return;

    if (fmt == NULL)
        return;

    /*
     * 3 intentionally non-thread safe variable reads.
     * Since writes to the variable are serialized on
     * virLogLock, worst case result is a log message
     * is accidentally dropped or emitted, if another
     * thread is updating log filter list concurrently
     * with a log message emission.
     */
    if (source->serial < virLogFiltersSerial)
        virLogSourceUpdate(source);
    if (priority < source->priority)
        goto cleanup;
    filterflags = source->flags;

    /*
     * serialize the error message, add level and timestamp
     */
    if (virVasprintfQuiet(&str, fmt, vargs) < 0)
        goto cleanup;

    ret = virLogFormatString(&msg, linenr, funcname, priority, str);
    if (ret < 0)
        goto cleanup;

    if (virTimeStringNowRaw(timestamp) < 0)
        timestamp[0] = '\0';

    virLogLock();

    /*
     * Push the message to the outputs defined, if none exist then
     * use stderr.
     */
    for (i = 0; i < virLogNbOutputs; i++) {
        if (priority >= virLogOutputs[i]->priority) {
            if (virLogOutputs[i]->logInitMessage) {
                const char *rawinitmsg;
                char *hoststr = NULL;
                char *initmsg = NULL;
                if (virLogVersionString(&rawinitmsg, &initmsg) >= 0)
                    virLogOutputs[i]->f(&virLogSelf, VIR_LOG_INFO,
                                       __FILE__, __LINE__, __func__,
                                       timestamp, NULL, 0, rawinitmsg, initmsg,
                                       virLogOutputs[i]->data);
                VIR_FREE(initmsg);
                if (virLogHostnameString(&hoststr, &initmsg) >= 0)
                    virLogOutputs[i]->f(&virLogSelf, VIR_LOG_INFO,
                                       __FILE__, __LINE__, __func__,
                                       timestamp, NULL, 0, hoststr, initmsg,
                                       virLogOutputs[i]->data);
                VIR_FREE(hoststr);
                VIR_FREE(initmsg);
                virLogOutputs[i]->logInitMessage = false;
            }
            virLogOutputs[i]->f(source, priority,
                               filename, linenr, funcname,
                               timestamp, metadata, filterflags,
                               str, msg, virLogOutputs[i]->data);
        }
    }
    if (virLogNbOutputs == 0) {
        if (logInitMessageStderr) {
            const char *rawinitmsg;
            char *hoststr = NULL;
            char *initmsg = NULL;
            if (virLogVersionString(&rawinitmsg, &initmsg) >= 0)
                virLogOutputToFd(&virLogSelf, VIR_LOG_INFO,
                                 __FILE__, __LINE__, __func__,
                                 timestamp, NULL, 0, rawinitmsg, initmsg,
                                 (void *) STDERR_FILENO);
            VIR_FREE(initmsg);
            if (virLogHostnameString(&hoststr, &initmsg) >= 0)
                virLogOutputToFd(&virLogSelf, VIR_LOG_INFO,
                                 __FILE__, __LINE__, __func__,
                                 timestamp, NULL, 0, hoststr, initmsg,
                                 (void *) STDERR_FILENO);
            VIR_FREE(hoststr);
            VIR_FREE(initmsg);
            logInitMessageStderr = false;
        }
        virLogOutputToFd(source, priority,
                         filename, linenr, funcname,
                         timestamp, metadata, filterflags,
                         str, msg, (void *) STDERR_FILENO);
    }
    virLogUnlock();

 cleanup:
    VIR_FREE(str);
    VIR_FREE(msg);
    errno = saved_errno;
}


static void
virLogStackTraceToFd(int fd)
{
    void *array[100];
    int size;
    static bool doneWarning;
    const char *msg = "Stack trace not available on this platform\n";

#define STRIP_DEPTH 3
    size = backtrace(array, ARRAY_CARDINALITY(array));
    if (size) {
        backtrace_symbols_fd(array +  STRIP_DEPTH, size - STRIP_DEPTH, fd);
        ignore_value(safewrite(fd, "\n", 1));
    } else if (!doneWarning) {
        ignore_value(safewrite(fd, msg, strlen(msg)));
        doneWarning = true;
    }
#undef STRIP_DEPTH
}

static void
virLogOutputToFd(virLogSourcePtr source ATTRIBUTE_UNUSED,
                 virLogPriority priority ATTRIBUTE_UNUSED,
                 const char *filename ATTRIBUTE_UNUSED,
                 int linenr ATTRIBUTE_UNUSED,
                 const char *funcname ATTRIBUTE_UNUSED,
                 const char *timestamp,
                 virLogMetadataPtr metadata ATTRIBUTE_UNUSED,
                 unsigned int flags,
                 const char *rawstr ATTRIBUTE_UNUSED,
                 const char *str,
                 void *data)
{
    int fd = (intptr_t) data;
    char *msg;

    if (fd < 0)
        return;

    if (virAsprintfQuiet(&msg, "%s: %s", timestamp, str) < 0)
        return;

    ignore_value(safewrite(fd, msg, strlen(msg)));
    VIR_FREE(msg);

    if (flags & VIR_LOG_STACK_TRACE)
        virLogStackTraceToFd(fd);
}


static void
virLogCloseFd(void *data)
{
    int fd = (intptr_t) data;

    VIR_LOG_CLOSE(fd);
}


static int
virLogAddOutputToStderr(virLogPriority priority)
{
    if (virLogDefineOutput(virLogOutputToFd, NULL, (void *)2L, priority,
                           VIR_LOG_TO_STDERR, NULL, 0) < 0)
        return -1;
    return 0;
}


static int
virLogAddOutputToFile(virLogPriority priority,
                      const char *file)
{
    int fd;

    fd = open(file, O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return -1;
    if (virLogDefineOutput(virLogOutputToFd, virLogCloseFd,
                           (void *)(intptr_t)fd,
                           priority, VIR_LOG_TO_FILE, file, 0) < 0) {
        VIR_FORCE_CLOSE(fd);
        return -1;
    }
    return 0;
}


#if HAVE_SYSLOG_H || USE_JOURNALD

/* Compat in case we build with journald, but no syslog */
# ifndef LOG_DEBUG
#  define LOG_DEBUG 7
# endif
# ifndef LOG_INFO
#  define LOG_INFO 6
# endif
# ifndef LOG_WARNING
#  define LOG_WARNING 4
# endif
# ifndef LOG_ERR
#  define LOG_ERR 3
# endif

static int
virLogPrioritySyslog(virLogPriority priority)
{
    switch (priority) {
    case VIR_LOG_DEBUG:
        return LOG_DEBUG;
    case VIR_LOG_INFO:
        return LOG_INFO;
    case VIR_LOG_WARN:
        return LOG_WARNING;
    case VIR_LOG_ERROR:
        return LOG_ERR;
    default:
        return LOG_ERR;
    }
}
#endif /* HAVE_SYSLOG_H || USE_JOURNALD */


#if HAVE_SYSLOG_H
static void
virLogOutputToSyslog(virLogSourcePtr source ATTRIBUTE_UNUSED,
                     virLogPriority priority,
                     const char *filename ATTRIBUTE_UNUSED,
                     int linenr ATTRIBUTE_UNUSED,
                     const char *funcname ATTRIBUTE_UNUSED,
                     const char *timestamp ATTRIBUTE_UNUSED,
                     virLogMetadataPtr metadata ATTRIBUTE_UNUSED,
                     unsigned int flags,
                     const char *rawstr ATTRIBUTE_UNUSED,
                     const char *str,
                     void *data ATTRIBUTE_UNUSED)
{
    virCheckFlags(VIR_LOG_STACK_TRACE,);

    syslog(virLogPrioritySyslog(priority), "%s", str);
}

static char *current_ident;


static void
virLogCloseSyslog(void *data ATTRIBUTE_UNUSED)
{
    closelog();
    VIR_FREE(current_ident);
}


static int
virLogAddOutputToSyslog(virLogPriority priority,
                        const char *ident)
{
    /*
     * ident needs to be kept around on Solaris
     */
    VIR_FREE(current_ident);
    if (VIR_STRDUP(current_ident, ident) < 0)
        return -1;

    openlog(current_ident, 0, 0);
    if (virLogDefineOutput(virLogOutputToSyslog, virLogCloseSyslog, NULL,
                           priority, VIR_LOG_TO_SYSLOG, ident, 0) < 0) {
        closelog();
        VIR_FREE(current_ident);
        return -1;
    }
    return 0;
}


# if USE_JOURNALD
#  define IOVEC_SET(iov, data, size)            \
    do {                                        \
        struct iovec *_i = &(iov);              \
        _i->iov_base = (void*)(data);           \
        _i->iov_len = (size);                   \
    } while (0)

#  define IOVEC_SET_STRING(iov, str) IOVEC_SET(iov, str, strlen(str))

/* Used for conversion of numbers to strings, and for length of binary data */
#  define JOURNAL_BUF_SIZE (MAX(INT_BUFSIZE_BOUND(int), sizeof(uint64_t)))

struct journalState
{
    struct iovec *iov, *iov_end;
    char (*bufs)[JOURNAL_BUF_SIZE], (*bufs_end)[JOURNAL_BUF_SIZE];
};

static void
journalAddString(struct journalState *state, const char *field,
                 const char *value)
{
    static const char newline = '\n', equals = '=';

    if (strchr(value, '\n') != NULL) {
        uint64_t nstr;

        /* If 'str' contains a newline, then we must
         * encode the string length, since we can't
         * rely on the newline for the field separator
         */
        if (state->iov_end - state->iov < 5 || state->bufs == state->bufs_end)
            return; /* Silently drop */
        nstr = htole64(strlen(value));
        memcpy(state->bufs[0], &nstr, sizeof(nstr));

        IOVEC_SET_STRING(state->iov[0], field);
        IOVEC_SET(state->iov[1], &newline, sizeof(newline));
        IOVEC_SET(state->iov[2], state->bufs[0], sizeof(nstr));
        state->bufs++;
        state->iov += 3;
    } else {
        if (state->iov_end - state->iov < 4)
            return; /* Silently drop */
        IOVEC_SET_STRING(state->iov[0], field);
        IOVEC_SET(state->iov[1], (void *)&equals, sizeof(equals));
        state->iov += 2;
    }
    IOVEC_SET_STRING(state->iov[0], value);
    IOVEC_SET(state->iov[1], (void *)&newline, sizeof(newline));
    state->iov += 2;
}

static void
journalAddInt(struct journalState *state, const char *field, int value)
{
    static const char newline = '\n', equals = '=';

    char *num;

    if (state->iov_end - state->iov < 4 || state->bufs == state->bufs_end)
        return; /* Silently drop */

    num = virFormatIntDecimal(state->bufs[0], sizeof(state->bufs[0]), value);

    IOVEC_SET_STRING(state->iov[0], field);
    IOVEC_SET(state->iov[1], (void *)&equals, sizeof(equals));
    IOVEC_SET_STRING(state->iov[2], num);
    IOVEC_SET(state->iov[3], (void *)&newline, sizeof(newline));
    state->bufs++;
    state->iov += 4;
}

static int journalfd = -1;

static void
virLogOutputToJournald(virLogSourcePtr source,
                       virLogPriority priority,
                       const char *filename,
                       int linenr,
                       const char *funcname,
                       const char *timestamp ATTRIBUTE_UNUSED,
                       virLogMetadataPtr metadata,
                       unsigned int flags,
                       const char *rawstr,
                       const char *str ATTRIBUTE_UNUSED,
                       void *data ATTRIBUTE_UNUSED)
{
    virCheckFlags(VIR_LOG_STACK_TRACE,);
    int buffd = -1;
    struct msghdr mh;
    struct sockaddr_un sa;
    union {
        struct cmsghdr cmsghdr;
        uint8_t buf[CMSG_SPACE(sizeof(int))];
    } control;
    struct cmsghdr *cmsg;
    /* We use /dev/shm instead of /tmp here, since we want this to
     * be a tmpfs, and one that is available from early boot on
     * and where unprivileged users can create files. */
    char path[] = "/dev/shm/journal.XXXXXX";
    size_t nmetadata = 0;

#  define NUM_FIELDS_CORE 6
#  define NUM_FIELDS_META 5
#  define NUM_FIELDS (NUM_FIELDS_CORE + NUM_FIELDS_META)
    struct iovec iov[NUM_FIELDS * 5];
    char iov_bufs[NUM_FIELDS][JOURNAL_BUF_SIZE];
    struct journalState state;

    state.iov = iov;
    state.iov_end = iov + ARRAY_CARDINALITY(iov);
    state.bufs = iov_bufs;
    state.bufs_end = iov_bufs + ARRAY_CARDINALITY(iov_bufs);

    journalAddString(&state, "MESSAGE", rawstr);
    journalAddInt(&state, "PRIORITY",
                  virLogPrioritySyslog(priority));
    journalAddInt(&state, "SYSLOG_FACILITY", LOG_DAEMON);
    journalAddString(&state, "LIBVIRT_SOURCE", source->name);
    if (filename)
        journalAddString(&state, "CODE_FILE", filename);
    journalAddInt(&state, "CODE_LINE", linenr);
    if (funcname)
        journalAddString(&state, "CODE_FUNC", funcname);
    if (metadata != NULL) {
        while (metadata->key != NULL &&
               nmetadata < NUM_FIELDS_META) {
            if (metadata->s != NULL)
                journalAddString(&state, metadata->key, metadata->s);
            else
                journalAddInt(&state, metadata->key, metadata->iv);
            metadata++;
            nmetadata++;
        }
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (!virStrcpy(sa.sun_path, "/run/systemd/journal/socket", sizeof(sa.sun_path)))
        return;

    memset(&mh, 0, sizeof(mh));
    mh.msg_name = &sa;
    mh.msg_namelen = offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path);
    mh.msg_iov = iov;
    mh.msg_iovlen = state.iov - iov;

    if (sendmsg(journalfd, &mh, MSG_NOSIGNAL) >= 0)
        return;

    if (errno != EMSGSIZE && errno != ENOBUFS)
        return;

    /* Message was too large, so dump to temporary file
     * and pass an FD to the journal
     */

    /* NB: mkostemp is not declared async signal safe by
     * POSIX, but this is Linux only code and the GLibc
     * impl is safe enough, only using open() and inline
     * asm to read a timestamp (falling back to gettimeofday
     * on some arches
     */
    if ((buffd = mkostemp(path, O_CLOEXEC|O_RDWR)) < 0)
        return;

    if (unlink(path) < 0)
        goto cleanup;

    if (writev(buffd, iov, state.iov - iov) < 0)
        goto cleanup;

    mh.msg_iov = NULL;
    mh.msg_iovlen = 0;

    memset(&control, 0, sizeof(control));
    mh.msg_control = &control;
    mh.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&mh);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &buffd, sizeof(int));

    mh.msg_controllen = cmsg->cmsg_len;

    ignore_value(sendmsg(journalfd, &mh, MSG_NOSIGNAL));

 cleanup:
    VIR_LOG_CLOSE(buffd);
}


static void virLogCloseJournald(void *data ATTRIBUTE_UNUSED)
{
    VIR_LOG_CLOSE(journalfd);
}


static int virLogAddOutputToJournald(int priority)
{
    if ((journalfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
        return -1;
    if (virSetInherit(journalfd, false) < 0) {
        VIR_LOG_CLOSE(journalfd);
        return -1;
    }
    if (virLogDefineOutput(virLogOutputToJournald, virLogCloseJournald, NULL,
                           priority, VIR_LOG_TO_JOURNALD, NULL, 0) < 0) {
        return -1;
    }
    return 0;
}
# endif /* USE_JOURNALD */

int virLogPriorityFromSyslog(int priority)
{
    switch (priority) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
        return VIR_LOG_ERROR;
    case LOG_WARNING:
    case LOG_NOTICE:
        return VIR_LOG_WARN;
    case LOG_INFO:
        return VIR_LOG_INFO;
    case LOG_DEBUG:
        return VIR_LOG_DEBUG;
    }
    return VIR_LOG_ERROR;
}

#else /* HAVE_SYSLOG_H */
int virLogPriorityFromSyslog(int priority ATTRIBUTE_UNUSED)
{
    return VIR_LOG_ERROR;
}
#endif /* HAVE_SYSLOG_H */

#define IS_SPACE(cur)                                                   \
    ((*cur == ' ') || (*cur == '\t') || (*cur == '\n') ||               \
     (*cur == '\r') || (*cur == '\\'))


static int
virLogParseAndDefineOutput(const char *src)
{
    int ret = -1;
    char **tokens = NULL;
    char *abspath = NULL;
    size_t count = 0;
    virLogPriority prio;
    int dest;
    bool isSUID = virIsSUID();

    if (!src)
        return -1;

    VIR_DEBUG("output=%s", src);

    /* split our format prio:destination:additional_data to tokens and parse
     * them individually
     */
    if (!(tokens = virStringSplitCount(src, ":", 0, &count)))
        return -1;

    if (virStrToLong_uip(tokens[0], NULL, 10, &prio) < 0 ||
        (prio < VIR_LOG_DEBUG) || (prio > VIR_LOG_ERROR))
        goto cleanup;

    if ((dest = virLogDestinationTypeFromString(tokens[1])) < 0)
        goto cleanup;

    if (((dest == VIR_LOG_TO_STDERR ||
          dest == VIR_LOG_TO_JOURNALD) && count != 2) ||
        ((dest == VIR_LOG_TO_FILE ||
          dest == VIR_LOG_TO_SYSLOG) && count != 3))
        goto cleanup;

    /* if running with setuid, only 'stderr' is allowed */
    if (isSUID && dest != VIR_LOG_TO_STDERR)
        goto cleanup;

    switch ((virLogDestination) dest) {
    case VIR_LOG_TO_STDERR:
        ret = virLogAddOutputToStderr(prio);
        break;
    case VIR_LOG_TO_SYSLOG:
#if HAVE_SYSLOG_H
        ret = virLogAddOutputToSyslog(prio, tokens[2]);
#endif
        break;
    case VIR_LOG_TO_FILE:
        if (virFileAbsPath(tokens[2], &abspath) < 0)
            goto cleanup;
        ret = virLogAddOutputToFile(prio, abspath);
        VIR_FREE(abspath);
        break;
    case VIR_LOG_TO_JOURNALD:
#if USE_JOURNALD
        ret = virLogAddOutputToJournald(prio);
#endif
        break;
    case VIR_LOG_TO_OUTPUT_LAST:
        break;
    }

 cleanup:
    if (ret < 0)
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to parse and define log output %s"), src);
    virStringFreeList(tokens);
    return ret;
}


/**
 * virLogParseAndDefineOutputs:
 * @outputs: string defining a (set of) output(s)
 *
 * The format for an output can be:
 *    x:stderr
 *       output goes to stderr
 *    x:syslog:name
 *       use syslog for the output and use the given name as the ident
 *    x:file:file_path
 *       output to a file, with the given filepath
 * In all case the x prefix is the minimal level, acting as a filter
 *    1: DEBUG
 *    2: INFO
 *    3: WARNING
 *    4: ERROR
 *
 * Multiple output can be defined in a single @output, they just need to be
 * separated by spaces.
 *
 * If running in setuid mode, then only the 'stderr' output will
 * be allowed
 *
 * Returns the number of output parsed or -1 in case of error.
 */
int
virLogParseAndDefineOutputs(const char *src)
{
    int ret = -1;
    int count = 0;
    size_t i;
    char **strings = NULL;

    if (!src)
        return -1;

    VIR_DEBUG("outputs=%s", src);

    if (!(strings = virStringSplit(src, " ", 0)))
        goto cleanup;

    for (i = 0; strings[i]; i++) {
        /* virStringSplit may return empty strings */
        if (STREQ(strings[i], ""))
            continue;

        if (virLogParseAndDefineOutput(strings[i]) < 0)
            goto cleanup;

        count++;
    }

    ret = count;
 cleanup:
    virStringFreeList(strings);
    return ret;
}


static int
virLogParseAndDefineFilter(const char *filter)
{
    int ret = -1;
    size_t count = 0;
    virLogPriority prio;
    char **tokens = NULL;
    unsigned int flags = 0;
    char *ref = NULL;

    if (!filter)
        return -1;

    VIR_DEBUG("filter=%s", filter);

    if (!(tokens = virStringSplitCount(filter, ":", 0, &count)))
        return -1;

    if (count != 2)
        goto cleanup;

    if (virStrToLong_uip(tokens[0], NULL, 10, &prio) < 0 ||
        (prio < VIR_LOG_DEBUG) || (prio > VIR_LOG_ERROR))
        goto cleanup;

    ref = tokens[1];
    if (ref[0] == '+') {
        flags |= VIR_LOG_STACK_TRACE;
        ref++;
    }

    if (!*ref)
        goto cleanup;

    if (virLogDefineFilter(ref, prio, flags) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    if (ret < 0)
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to parse and define log filter %s"), filter);
    virStringFreeList(tokens);
    return ret;
}

/**
 * virLogParseAndDefineFilters:
 * @filters: string defining a (set of) filter(s)
 *
 * The format for a filter is:
 *    x:name
 *       where name is a match string
 * the x prefix is the minimal level where the messages should be logged
 *    1: DEBUG
 *    2: INFO
 *    3: WARNING
 *    4: ERROR
 *
 * Multiple filter can be defined in a single @filters, they just need to be
 * separated by spaces.
 *
 * Returns the number of filter parsed or -1 in case of error.
 */
int
virLogParseAndDefineFilters(const char *filters)
{
    int ret = -1;
    int count = 0;
    size_t i;
    char **strings = NULL;

    if (!filters)
        return -1;

    VIR_DEBUG("filters=%s", filters);

    if (!(strings = virStringSplit(filters, " ", 0)))
        goto cleanup;

    for (i = 0; strings[i]; i++) {
        /* virStringSplit may return empty strings */
        if (STREQ(strings[i], ""))
            continue;

        if (virLogParseAndDefineFilter(strings[i]) < 0)
            goto cleanup;

        count++;
    }

    ret = count;
 cleanup:
    virStringFreeList(strings);
    return ret;
}


/**
 * virLogGetDefaultPriority:
 *
 * Returns the current logging priority level.
 */
virLogPriority
virLogGetDefaultPriority(void)
{
    return virLogDefaultPriority;
}


/**
 * virLogGetFilters:
 *
 * Returns a string listing the current filters, in the format originally
 * specified in the config file or environment. Caller must free the
 * result.
 */
char *
virLogGetFilters(void)
{
    size_t i;
    virBuffer filterbuf = VIR_BUFFER_INITIALIZER;

    virLogLock();
    for (i = 0; i < virLogNbFilters; i++) {
        const char *sep = ":";
        if (virLogFilters[i]->flags & VIR_LOG_STACK_TRACE)
            sep = ":+";
        virBufferAsprintf(&filterbuf, "%d%s%s ",
                          virLogFilters[i]->priority,
                          sep,
                          virLogFilters[i]->match);
    }
    virLogUnlock();

    if (virBufferError(&filterbuf)) {
        virBufferFreeAndReset(&filterbuf);
        return NULL;
    }

    return virBufferContentAndReset(&filterbuf);
}


/**
 * virLogGetOutputs:
 *
 * Returns a string listing the current outputs, in the format originally
 * specified in the config file or environment. Caller must free the
 * result.
 */
char *
virLogGetOutputs(void)
{
    size_t i;
    virBuffer outputbuf = VIR_BUFFER_INITIALIZER;

    virLogLock();
    for (i = 0; i < virLogNbOutputs; i++) {
        virLogDestination dest = virLogOutputs[i]->dest;
        if (i)
            virBufferAddChar(&outputbuf, ' ');
        switch (dest) {
            case VIR_LOG_TO_SYSLOG:
            case VIR_LOG_TO_FILE:
                virBufferAsprintf(&outputbuf, "%d:%s:%s",
                                  virLogOutputs[i]->priority,
                                  virLogDestinationTypeToString(dest),
                                  virLogOutputs[i]->name);
                break;
            default:
                virBufferAsprintf(&outputbuf, "%d:%s",
                                  virLogOutputs[i]->priority,
                                  virLogDestinationTypeToString(dest));
        }
    }
    virLogUnlock();

    if (virBufferError(&outputbuf)) {
        virBufferFreeAndReset(&outputbuf);
        return NULL;
    }

    return virBufferContentAndReset(&outputbuf);
}


/**
 * virLogGetNbFilters:
 *
 * Returns the current number of defined log filters.
 */
int
virLogGetNbFilters(void)
{
    return virLogNbFilters;
}


/**
 * virLogGetNbOutputs:
 *
 * Returns the current number of defined log outputs.
 */
int
virLogGetNbOutputs(void)
{
    return virLogNbOutputs;
}


/**
 * virLogParseDefaultPriority:
 * @priority: string defining the desired logging level
 *
 * Parses and sets the default log priority level. It can take a string or
 * number corresponding to the following levels:
 *    1: DEBUG
 *    2: INFO
 *    3: WARNING
 *    4: ERROR
 *
 * Returns 0 if successful, -1 in case of error.
 */
int
virLogParseDefaultPriority(const char *priority)
{
    int ret = -1;

    if (STREQ(priority, "1") || STREQ(priority, "debug"))
        ret = virLogSetDefaultPriority(VIR_LOG_DEBUG);
    else if (STREQ(priority, "2") || STREQ(priority, "info"))
        ret = virLogSetDefaultPriority(VIR_LOG_INFO);
    else if (STREQ(priority, "3") || STREQ(priority, "warning"))
        ret = virLogSetDefaultPriority(VIR_LOG_WARN);
    else if (STREQ(priority, "4") || STREQ(priority, "error"))
        ret = virLogSetDefaultPriority(VIR_LOG_ERROR);
    else
        VIR_WARN("Ignoring invalid log level setting");

    return ret;
}


/**
 * virLogSetFromEnv:
 *
 * Sets virLogDefaultPriority, virLogFilters and virLogOutputs based on
 * environment variables.
 */
void
virLogSetFromEnv(void)
{
    const char *debugEnv;

    if (virLogInitialize() < 0)
        return;

    debugEnv = virGetEnvAllowSUID("LIBVIRT_DEBUG");
    if (debugEnv && *debugEnv)
        virLogParseDefaultPriority(debugEnv);
    debugEnv = virGetEnvAllowSUID("LIBVIRT_LOG_FILTERS");
    if (debugEnv && *debugEnv)
        virLogParseAndDefineFilters(debugEnv);
    debugEnv = virGetEnvAllowSUID("LIBVIRT_LOG_OUTPUTS");
    if (debugEnv && *debugEnv)
        virLogParseAndDefineOutputs(debugEnv);
}


/*
 * Returns a true value if the first line in @str is
 * probably a log message generated by the libvirt
 * logging layer
 */
bool virLogProbablyLogMessage(const char *str)
{
    bool ret = false;
    if (!virLogRegex)
        return false;
    if (regexec(virLogRegex, str, 0, NULL, 0) == 0)
        ret = true;
    return ret;
}


/**
 * virLogOutputNew:
 * @f: the function to call to output a message
 * @c: the function to call to close the output (or NULL)
 * @data: extra data passed as first arg to the function
 * @priority: minimal priority for this filter, use 0 for none
 * @dest: where to send output of this priority (see virLogDestination)
 * @name: optional name data associated with an output
 *
 * Allocates and returns a new log output object. The object has to be later
 * defined, so that the output will be taken into account when emitting a
 * message.
 *
 * Returns reference to a newly created object or NULL in case of failure.
 */
virLogOutputPtr
virLogOutputNew(virLogOutputFunc f,
                virLogCloseFunc c,
                void *data,
                virLogPriority priority,
                virLogDestination dest,
                const char *name)
{
    virLogOutputPtr ret = NULL;
    char *ndup = NULL;

    if (!f)
        return NULL;

    if (dest == VIR_LOG_TO_SYSLOG || dest == VIR_LOG_TO_FILE) {
        if (!name)
            return NULL;

        if (VIR_STRDUP(ndup, name) < 0)
            return NULL;
    }

    if (VIR_ALLOC_QUIET(ret) < 0) {
        VIR_FREE(ndup);
        return NULL;
    }

    ret->logInitMessage = true;
    ret->f = f;
    ret->c = c;
    ret->data = data;
    ret->priority = priority;
    ret->dest = dest;
    ret->name = ndup;

    return ret;
}


/**
 * virLogFilterNew:
 * @match: the pattern to match
 * @priority: the priority to give to messages matching the pattern
 * @flags: extra flags, see virLogFilterFlags enum
 *
 * Allocates and returns a new log filter object. The object has to be later
 * defined, so that the pattern will be taken into account when executing the
 * log filters (to select or reject a particular message) on messages.
 *
 * The filter defines a rules that will apply only to messages matching
 * the pattern (currently if @match is a substring of the message category)
 *
 * Returns a reference to a newly created filter that needs to be defined using
 * virLogDefineFilters, or NULL in case of an error.
 */
virLogFilterPtr
virLogFilterNew(const char *match,
                virLogPriority priority,
                unsigned int flags)
{
    virLogFilterPtr ret = NULL;
    char *mdup = NULL;

    virCheckFlags(VIR_LOG_STACK_TRACE, NULL);

    if ((match == NULL) || (priority < VIR_LOG_DEBUG) ||
        (priority > VIR_LOG_ERROR))
        return NULL;

    if (VIR_STRDUP_QUIET(mdup, match) < 0)
        return NULL;

    if (VIR_ALLOC_QUIET(ret) < 0) {
        VIR_FREE(mdup);
        return NULL;
    }

    ret->match = mdup;
    ret->priority = priority;
    ret->flags = flags;

    return ret;
}


/**
 * virLogFindOutput:
 * @outputs: a list of outputs where to look for the output of type @dest
 * @noutputs: number of elements in @outputs
 * @dest: destination type of an output
 * @opaque: opaque data to the method (only filename at the moment)
 *
 * Looks for an output of destination type @dest in the source list @outputs.
 * If such an output exists, index of the object in the list is returned.
 * In case of the destination being of type FILE also a comparison of the
 * output's filename with @opaque is performed first.
 *
 * Returns the index of the object in the list or -1 if no object matching the
 * specified @dest type and/or @opaque data one was found.
 */
int
virLogFindOutput(virLogOutputPtr *outputs, size_t noutputs,
                 virLogDestination dest, const void *opaque)
{
    size_t i;
    const char *name = opaque;

    for (i = 0; i < noutputs; i++) {
        if (dest == outputs[i]->dest &&
            (dest != VIR_LOG_TO_FILE || STREQ(outputs[i]->name, name)))
                return i;
    }

    return -1;
}


/**
 * virLogDefineOutputs:
 * @outputs: new set of outputs to be defined
 * @noutputs: number of outputs in @outputs
 *
 * Resets any existing set of outputs and defines a completely new one.
 *
 * Returns number of outputs successfully defined or -1 in case of error;
 */
int
virLogDefineOutputs(virLogOutputPtr *outputs, size_t noutputs)
{
    if (virLogInitialize() < 0)
        return -1;

    virLogLock();
    virLogResetOutputs();
    virLogOutputs = outputs;
    virLogNbOutputs = noutputs;
    virLogUnlock();

    return virLogNbOutputs;
}


/**
 * virLogDefineFilters:
 * @filters: new set of filters to be defined
 * @nfilters: number of filters in @filters
 *
 * Resets any existing set of filters and defines a completely new one.
 *
 * Returns number of filters successfully defined or -1 in case of error;
 */
int
virLogDefineFilters(virLogFilterPtr *filters, size_t nfilters)
{
    if (virLogInitialize() < 0)
        return -1;

    virLogLock();
    virLogResetOutputs();
    virLogFilters = filters;
    virLogNbOutputs = nfilters;
    virLogFiltersSerial++;
    virLogUnlock();

    return virLogNbFilters;
}
