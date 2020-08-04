/*
 * virdbusmock.c: mocking of dbus message send/reply
 *
 * Copyright (C) 2013-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#if defined(WITH_DBUS) && !defined(WIN32)
# include "virmock.h"
# include <dbus/dbus.h>

VIR_MOCK_STUB_VOID_ARGS(dbus_connection_set_change_sigpipe,
                        dbus_bool_t, will_modify_sigpipe)


VIR_MOCK_STUB_RET_ARGS(dbus_bus_get,
                       DBusConnection *, (DBusConnection *)0x1,
                       DBusBusType, type,
                       DBusError *, error)

VIR_MOCK_STUB_VOID_ARGS(dbus_connection_set_exit_on_disconnect,
                        DBusConnection *, connection,
                        dbus_bool_t, exit_on_disconnect)

VIR_MOCK_STUB_RET_ARGS(dbus_connection_set_watch_functions,
                       dbus_bool_t, 1,
                       DBusConnection *, connection,
                       DBusAddWatchFunction, add_function,
                       DBusRemoveWatchFunction, remove_function,
                       DBusWatchToggledFunction, toggled_function,
                       void *, data,
                       DBusFreeFunction, free_data_function)

VIR_MOCK_STUB_RET_ARGS(dbus_message_set_reply_serial,
                       dbus_bool_t, 1,
                       DBusMessage *, message,
                       dbus_uint32_t, serial)


VIR_MOCK_LINK_RET_ARGS(dbus_connection_send_with_reply_and_block,
                       DBusMessage *,
                       DBusConnection *, connection,
                       DBusMessage *, message,
                       int, timeout_milliseconds,
                       DBusError *, error)

#endif /* WITH_DBUS && !WIN32 */
