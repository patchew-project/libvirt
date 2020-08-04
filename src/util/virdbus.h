/*
 * virdbus.h: helper for using DBus
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#ifdef WITH_DBUS
# undef interface /* Work around namespace pollution in mingw's rpc.h */
# include <dbus/dbus.h>
#else
# define DBusConnection void
# define DBusMessage void
#endif
#include "internal.h"

#include <stdarg.h>

void virDBusSetSharedBus(bool shared);

DBusConnection *virDBusGetSystemBus(void);
bool virDBusHasSystemBus(void);
void virDBusCloseSystemBus(void);
DBusConnection *virDBusGetSessionBus(void);

int virDBusCreateMethod(DBusMessage **call,
                        const char *destination,
                        const char *path,
                        const char *iface,
                        const char *member,
                        const char *types, ...);
int virDBusCreateMethodV(DBusMessage **call,
                         const char *destination,
                         const char *path,
                         const char *iface,
                         const char *member,
                         const char *types,
                         va_list args);
int virDBusCreateReply(DBusMessage **reply,
                       const char *types, ...);
int virDBusCreateReplyV(DBusMessage **reply,
                        const char *types,
                        va_list args);

int virDBusCallMethod(DBusConnection *conn,
                      DBusMessage **reply,
                      virErrorPtr error,
                      const char *destination,
                      const char *path,
                      const char *iface,
                      const char *member,
                      const char *types, ...);
int virDBusMessageDecode(DBusMessage *msg,
                         const char *types, ...);
void virDBusMessageUnref(DBusMessage *msg);

int virDBusIsServiceEnabled(const char *name);
int virDBusIsServiceRegistered(const char *name);

bool virDBusErrorIsUnknownMethod(virErrorPtr err);
