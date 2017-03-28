/*
 * vircommand.h: Child command execution
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
 *
 */

#ifndef __VIR_COMMAND_H__
# define __VIR_COMMAND_H__

# include "internal.h"
# include "virbuffer.h"

typedef struct _virCommand virCommand;
typedef virCommand *virCommandPtr;

/* This will execute in the context of the first child
 * after fork() but before execve().  As such, it is unsafe to
 * call any function that is not async-signal-safe.  */
typedef int (*virExecHook)(void *data);

pid_t virFork(void) ATTRIBUTE_RETURN_CHECK;

int virRun(const char *const*argv, int *status) ATTRIBUTE_RETURN_CHECK;

virCommandPtr virCommandNew(const char *binary);

virCommandPtr virCommandNewArgs(const char *const*args);

virCommandPtr virCommandNewArgList(const char *binary, ...)
    ATTRIBUTE_SENTINEL;

virCommandPtr virCommandNewVAList(const char *binary, va_list list);

/* All error report from these setup APIs is
 * delayed until the Run/RunAsync methods
 */

enum {
    /* Close the FD in the parent */
    VIR_COMMAND_PASS_FD_CLOSE_PARENT = (1 << 0),
};

void virCommandPassFD(virCommandPtr cmd,
                      int fd,
                      unsigned int flags);

void virCommandPassListenFDs(virCommandPtr cmd);

int virCommandPassFDGetFDIndex(virCommandPtr cmd,
                               int fd);

void virCommandSetPidFile(virCommandPtr cmd,
                          const char *pidfile);

void virCommandSetGID(virCommandPtr cmd, gid_t gid);

void virCommandSetUID(virCommandPtr cmd, uid_t uid);

void virCommandSetMaxMemLock(virCommandPtr cmd, unsigned long long bytes);
void virCommandSetMaxProcesses(virCommandPtr cmd, unsigned int procs);
void virCommandSetMaxFiles(virCommandPtr cmd, unsigned int files);
void virCommandSetMaxCoreSize(virCommandPtr cmd, unsigned long long bytes);
void virCommandSetUmask(virCommandPtr cmd, int umask);

void virCommandClearCaps(virCommandPtr cmd);

void virCommandAllowCap(virCommandPtr cmd,
                        int capability);

void virCommandSetSELinuxLabel(virCommandPtr cmd,
                               const char *label);

void virCommandSetAppArmorProfile(virCommandPtr cmd,
                                  const char *profile);

void virCommandDaemonize(virCommandPtr cmd);

void virCommandNonblockingFDs(virCommandPtr cmd);

void virCommandRawStatus(virCommandPtr cmd);

void virCommandAddEnvFormat(virCommandPtr cmd, const char *format, ...)
    ATTRIBUTE_FMT_PRINTF(2, 3);

void virCommandAddEnvPair(virCommandPtr cmd,
                          const char *name,
                          const char *value);

void virCommandAddEnvString(virCommandPtr cmd,
                            const char *str);

void virCommandAddEnvBuffer(virCommandPtr cmd,
                            virBufferPtr buf);

void virCommandAddEnvPassBlockSUID(virCommandPtr cmd,
                                   const char *name,
                                   const char *defvalue);

void virCommandAddEnvPassAllowSUID(virCommandPtr cmd,
                                   const char *name);

void virCommandAddEnvPassCommon(virCommandPtr cmd);

void virCommandAddArg(virCommandPtr cmd,
                      const char *val);

void virCommandAddArgBuffer(virCommandPtr cmd,
                            virBufferPtr buf);

void virCommandAddArgFormat(virCommandPtr cmd,
                            const char *format, ...)
    ATTRIBUTE_FMT_PRINTF(2, 3);

void virCommandAddArgPair(virCommandPtr cmd,
                          const char *name,
                          const char *val);

void virCommandAddArgSet(virCommandPtr cmd,
                         const char *const*vals);

void virCommandAddArgList(virCommandPtr cmd,
                          ... /* const char *arg, ..., NULL */)
    ATTRIBUTE_SENTINEL;

void virCommandSetWorkingDirectory(virCommandPtr cmd,
                                   const char *pwd);

void virCommandSetInputBuffer(virCommandPtr cmd,
                              const char *inbuf);

void virCommandSetOutputBuffer(virCommandPtr cmd,
                               char **outbuf);

void virCommandSetErrorBuffer(virCommandPtr cmd,
                              char **errbuf);

void virCommandSetInputFD(virCommandPtr cmd,
                          int infd);

void virCommandSetOutputFD(virCommandPtr cmd,
                           int *outfd);

void virCommandSetErrorFD(virCommandPtr cmd,
                          int *errfd);

void virCommandSetPreExecHook(virCommandPtr cmd,
                              virExecHook hook,
                              void *opaque);

void virCommandWriteArgLog(virCommandPtr cmd,
                           int logfd);

char *virCommandToString(virCommandPtr cmd) ATTRIBUTE_RETURN_CHECK;

int virCommandExec(virCommandPtr cmd) ATTRIBUTE_RETURN_CHECK;

int virCommandRun(virCommandPtr cmd,
                  int *exitstatus) ATTRIBUTE_RETURN_CHECK;

int virCommandRunAsync(virCommandPtr cmd,
                       pid_t *pid) ATTRIBUTE_RETURN_CHECK;

int virCommandWait(virCommandPtr cmd,
                   int *exitstatus) ATTRIBUTE_RETURN_CHECK;

void virCommandRequireHandshake(virCommandPtr cmd);

int virCommandHandshakeWait(virCommandPtr cmd)
    ATTRIBUTE_RETURN_CHECK;

int virCommandHandshakeNotify(virCommandPtr cmd)
    ATTRIBUTE_RETURN_CHECK;

void virCommandAbort(virCommandPtr cmd);

void virCommandFree(virCommandPtr cmd);

void virCommandDoAsyncIO(virCommandPtr cmd);

typedef int (*virCommandRunRegexFunc)(char **const groups,
                                      void *data);
typedef int (*virCommandRunNulFunc)(size_t n_tokens,
                                    char **const groups,
                                    void *data);

int virCommandRunRegex(virCommandPtr cmd,
                       int nregex,
                       const char **regex,
                       int *nvars,
                       virCommandRunRegexFunc func,
                       void *data,
                       const char *cmd_to_ignore,
                       int *exitstatus);

int virCommandRunNul(virCommandPtr cmd,
                     size_t n_columns,
                     virCommandRunNulFunc func,
                     void *data);


#endif /* __VIR_COMMAND_H__ */
