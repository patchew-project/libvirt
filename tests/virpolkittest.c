/*
 * Copyright (C) 2013, 2014, 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "testutils.h"

#if defined(__ELF__)

# include <dbus/dbus.h>

# include "virpolkit.h"
# include "virdbus.h"
# include "virlog.h"
# include "virmock.h"
# define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.systemdtest");

/* Some interesting numbers */
# define THE_PID 1458
# define THE_TIME 11011000001
# define THE_UID 1729

VIR_MOCK_WRAP_RET_ARGS(dbus_connection_send_with_reply_and_block,
                       DBusMessage *,
                       DBusConnection *, connection,
                       DBusMessage *, message,
                       int, timeout_milliseconds,
                       DBusError *, error)
{
    DBusMessage *reply = NULL;
    const char *service = dbus_message_get_destination(message);
    const char *member = dbus_message_get_member(message);

    VIR_MOCK_REAL_INIT(dbus_connection_send_with_reply_and_block);

    if (STREQ(service, "org.freedesktop.PolicyKit1") &&
        STREQ(member, "CheckAuthorization")) {
        char *type;
        char *pidkey;
        unsigned int pidval;
        char *timekey;
        unsigned long long timeval;
        char *uidkey;
        int uidval;
        char *actionid;
        char **details;
        size_t detailslen;
        int allowInteraction;
        char *cancellationId;
        const char **retdetails = NULL;
        size_t retdetailslen = 0;
        const char *retdetailscancelled[] = {
            "polkit.dismissed", "true",
        };
        int is_authorized = 1;
        int is_challenge = 0;

        if (virDBusMessageDecode(message,
                                 "(sa{sv})sa&{ss}us",
                                 &type,
                                 3,
                                 &pidkey, "u", &pidval,
                                 &timekey, "t", &timeval,
                                 &uidkey, "i", &uidval,
                                 &actionid,
                                 &detailslen,
                                 &details,
                                 &allowInteraction,
                                 &cancellationId) < 0)
            goto error;

        if (STREQ(actionid, "org.libvirt.test.success")) {
            is_authorized = 1;
            is_challenge = 0;
        } else if (STREQ(actionid, "org.libvirt.test.challenge")) {
            is_authorized = 0;
            is_challenge = 1;
        } else if (STREQ(actionid, "org.libvirt.test.cancelled")) {
            is_authorized = 0;
            is_challenge = 0;
            retdetails = retdetailscancelled;
            retdetailslen = G_N_ELEMENTS(retdetailscancelled) / 2;
        } else if (STREQ(actionid, "org.libvirt.test.details")) {
            size_t i;
            is_authorized = 0;
            is_challenge = 0;
            for (i = 0; i < detailslen / 2; i++) {
                if (STREQ(details[i * 2],
                          "org.libvirt.test.person") &&
                    STREQ(details[(i * 2) + 1],
                          "Fred")) {
                    is_authorized = 1;
                    is_challenge = 0;
                }
            }
        } else {
            is_authorized = 0;
            is_challenge = 0;
        }

        VIR_FREE(type);
        VIR_FREE(pidkey);
        VIR_FREE(timekey);
        VIR_FREE(uidkey);
        VIR_FREE(actionid);
        VIR_FREE(cancellationId);
        virStringListFreeCount(details, detailslen);

        if (virDBusCreateReply(&reply,
                               "(bba&{ss})",
                               is_authorized,
                               is_challenge,
                               retdetailslen,
                               retdetails) < 0)
            goto error;
    } else {
        reply = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    }

    return reply;

 error:
    virDBusMessageUnref(reply);
    return NULL;
}



static int testPolkitAuthSuccess(const void *opaque G_GNUC_UNUSED)
{
    if (virPolkitCheckAuth("org.libvirt.test.success",
                           THE_PID,
                           THE_TIME,
                           THE_UID,
                           NULL,
                           true) < 0)
        return -1;

    return 0;
}


static int testPolkitAuthDenied(const void *opaque G_GNUC_UNUSED)
{
    int rv;
    virErrorPtr err;

    rv = virPolkitCheckAuth("org.libvirt.test.deny",
                            THE_PID,
                            THE_TIME,
                            THE_UID,
                            NULL,
                            true);

    if (rv == 0) {
        fprintf(stderr, "Unexpected auth success\n");
        return -1;
    } else if (rv != -2) {
        return -1;
    }

    err = virGetLastError();
    if (!err || !strstr(err->message,
                        _("access denied by policy"))) {
        fprintf(stderr, "Incorrect error response\n");
        return -1;
    }

    return 0;
}


static int testPolkitAuthChallenge(const void *opaque G_GNUC_UNUSED)
{
    int rv;
    virErrorPtr err;

    rv = virPolkitCheckAuth("org.libvirt.test.challenge",
                            THE_PID,
                            THE_TIME,
                            THE_UID,
                            NULL,
                            true);

    if (rv == 0) {
        fprintf(stderr, "Unexpected auth success\n");
        return -1;
    } else if (rv != -2) {
        return -1;
    }

    err = virGetLastError();
    if (!err || err->domain != VIR_FROM_POLKIT ||
        err->code != VIR_ERR_AUTH_UNAVAILABLE ||
        !strstr(err->message, _("no polkit agent available to authenticate"))) {
        fprintf(stderr, "Incorrect error response\n");
        return -1;
    }

    return 0;
}


static int testPolkitAuthCancelled(const void *opaque G_GNUC_UNUSED)
{
    int rv;
    virErrorPtr err;

    rv = virPolkitCheckAuth("org.libvirt.test.cancelled",
                            THE_PID,
                            THE_TIME,
                            THE_UID,
                            NULL,
                            true);

    if (rv == 0) {
        fprintf(stderr, "Unexpected auth success\n");
        return -1;
    } else if (rv != -2) {
        return -1;
    }

    err = virGetLastError();
    if (!err || !strstr(err->message,
                       _("user cancelled authentication process"))) {
        fprintf(stderr, "Incorrect error response\n");
        return -1;
    }

    return 0;
}


static int testPolkitAuthDetailsSuccess(const void *opaque G_GNUC_UNUSED)
{
    const char *details[] = {
        "org.libvirt.test.person", "Fred",
        NULL,
    };

    if (virPolkitCheckAuth("org.libvirt.test.details",
                           THE_PID,
                           THE_TIME,
                           THE_UID,
                           details,
                           true) < 0)
        return -1;

    return 0;
}


static int testPolkitAuthDetailsDenied(const void *opaque G_GNUC_UNUSED)
{
    int rv;
    virErrorPtr err;
    const char *details[] = {
        "org.libvirt.test.person", "Joe",
        NULL,
    };

    rv = virPolkitCheckAuth("org.libvirt.test.details",
                            THE_PID,
                            THE_TIME,
                            THE_UID,
                            details,
                            true);

    if (rv == 0) {
        fprintf(stderr, "Unexpected auth success\n");
        return -1;
    } else if (rv != -2) {
        return -1;
    }

    err = virGetLastError();
    if (!err || !strstr(err->message,
                        _("access denied by policy"))) {
        fprintf(stderr, "Incorrect error response\n");
        return -1;
    }

    return 0;
}


static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("Polkit auth success ", testPolkitAuthSuccess, NULL) < 0)
        ret = -1;
    if (virTestRun("Polkit auth deny ", testPolkitAuthDenied, NULL) < 0)
        ret = -1;
    if (virTestRun("Polkit auth challenge ", testPolkitAuthChallenge, NULL) < 0)
        ret = -1;
    if (virTestRun("Polkit auth cancel ", testPolkitAuthCancelled, NULL) < 0)
        ret = -1;
    if (virTestRun("Polkit auth details success ", testPolkitAuthDetailsSuccess, NULL) < 0)
        ret = -1;
    if (virTestRun("Polkit auth details deny ", testPolkitAuthDetailsDenied, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain, VIR_TEST_MOCK("virdbus"))

#else /* ! __ELF__ */
int
main(void)
{
    return EXIT_AM_SKIP;
}
#endif /* ! __ELF__ */
