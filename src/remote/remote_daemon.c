/*
 * remote_daemon.c: daemon start of day, guest process & i/o management
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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
 */

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <getopt.h>
#include <grp.h>

#include "libvirt_internal.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virpidfile.h"
#include "virprocess.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

#include "remote_daemon.h"
#include "remote_daemon_config.h"

#include "admin/admin_server_dispatch.h"
#include "viruuid.h"
#include "remote_driver.h"
#include "viralloc.h"
#include "virconf.h"
#include "virnetlink.h"
#include "virnetdaemon.h"
#include "remote_daemon_dispatch.h"
#include "virhook.h"
#include "viraudit.h"
#include "virstring.h"
#include "locking/lock_manager.h"
#include "viraccessmanager.h"
#include "virutil.h"
#include "virgettext.h"
#include "util/virnetdevopenvswitch.h"
#include "virsystemd.h"

#include "driver.h"

#include "configmake.h"

#include "virdbus.h"

VIR_LOG_INIT("daemon." DAEMON_NAME);

#ifndef SOCK_PREFIX
# define SOCK_PREFIX DAEMON_NAME
#endif

#if WITH_SASL
virNetSASLContextPtr saslCtxt = NULL;
#endif
virNetServerProgramPtr remoteProgram = NULL;
virNetServerProgramPtr qemuProgram = NULL;

volatile bool driversInitialized = false;

enum {
    VIR_DAEMON_ERR_NONE = 0,
    VIR_DAEMON_ERR_PIDFILE,
    VIR_DAEMON_ERR_RUNDIR,
    VIR_DAEMON_ERR_INIT,
    VIR_DAEMON_ERR_SIGNAL,
    VIR_DAEMON_ERR_PRIVS,
    VIR_DAEMON_ERR_NETWORK,
    VIR_DAEMON_ERR_CONFIG,
    VIR_DAEMON_ERR_HOOKS,
    VIR_DAEMON_ERR_AUDIT,
    VIR_DAEMON_ERR_DRIVER,

    VIR_DAEMON_ERR_LAST
};

VIR_ENUM_DECL(virDaemonErr);
VIR_ENUM_IMPL(virDaemonErr,
              VIR_DAEMON_ERR_LAST,
              "Initialization successful",
              "Unable to obtain pidfile",
              "Unable to create rundir",
              "Unable to initialize libvirt",
              "Unable to setup signal handlers",
              "Unable to drop privileges",
              "Unable to initialize network sockets",
              "Unable to load configuration file",
              "Unable to look for hook scripts",
              "Unable to initialize audit system",
              "Unable to initialize driver",
);

static int daemonForkIntoBackground(const char *argv0)
{
    int statuspipe[2];
    if (pipe(statuspipe) < 0)
        return -1;

    pid_t pid = fork();
    switch (pid) {
    case 0:
        {
            /* intermediate child */
            int stdinfd = -1;
            int stdoutfd = -1;
            int nextpid;

            VIR_FORCE_CLOSE(statuspipe[0]);

            if ((stdinfd = open("/dev/null", O_RDONLY)) <= STDERR_FILENO)
                goto cleanup;
            if ((stdoutfd = open("/dev/null", O_WRONLY)) <= STDERR_FILENO)
                goto cleanup;
            if (dup2(stdinfd, STDIN_FILENO) != STDIN_FILENO)
                goto cleanup;
            if (dup2(stdoutfd, STDOUT_FILENO) != STDOUT_FILENO)
                goto cleanup;
            if (dup2(stdoutfd, STDERR_FILENO) != STDERR_FILENO)
                goto cleanup;
            if (VIR_CLOSE(stdinfd) < 0)
                goto cleanup;
            if (VIR_CLOSE(stdoutfd) < 0)
                goto cleanup;

            if (setsid() < 0)
                goto cleanup;

            nextpid = fork();
            switch (nextpid) {
            case 0: /* grandchild */
                return statuspipe[1];
            case -1: /* error */
                goto cleanup;
            default: /* intermediate child succeeded */
                _exit(EXIT_SUCCESS);
            }

        cleanup:
            VIR_FORCE_CLOSE(stdoutfd);
            VIR_FORCE_CLOSE(stdinfd);
            VIR_FORCE_CLOSE(statuspipe[1]);
            _exit(EXIT_FAILURE);

        }

    case -1: /* error in parent */
        goto error;

    default:
        {
            /* parent */
            int ret;
            char status;

            VIR_FORCE_CLOSE(statuspipe[1]);

            /* We wait to make sure the first child forked successfully */
            if (virProcessWait(pid, NULL, false) < 0)
                goto error;

            /* If we get here, then the grandchild was spawned, so we
             * must exit.  Block until the second child initializes
             * successfully */
        again:
            ret = read(statuspipe[0], &status, 1);
            if (ret == -1 && errno == EINTR)
                goto again;

            VIR_FORCE_CLOSE(statuspipe[0]);

            if (ret != 1) {
                char ebuf[1024];

                fprintf(stderr,
                        _("%s: error: unable to determine if daemon is "
                          "running: %s\n"), argv0,
                        virStrerror(errno, ebuf, sizeof(ebuf)));
                exit(EXIT_FAILURE);
            } else if (status != 0) {
                fprintf(stderr,
                        _("%s: error: %s. Check /var/log/messages or run "
                          "without --daemon for more info.\n"), argv0,
                        virDaemonErrTypeToString(status));
                exit(EXIT_FAILURE);
            }
            _exit(EXIT_SUCCESS);
        }
    }

 error:
    VIR_FORCE_CLOSE(statuspipe[0]);
    VIR_FORCE_CLOSE(statuspipe[1]);
    return -1;
}


static int
daemonUnixSocketPaths(struct daemonConfig *config,
                      bool privileged,
                      char **sockfile,
                      char **rosockfile,
                      char **admsockfile)
{
    int ret = -1;
    char *rundir = NULL;

    if (config->unix_sock_dir) {
        if (virAsprintf(sockfile, "%s/%s-sock",
                        SOCK_PREFIX, config->unix_sock_dir) < 0)
            goto cleanup;

        if (privileged) {
            if (virAsprintf(rosockfile, "%s/%s-sock-ro",
                            SOCK_PREFIX, config->unix_sock_dir) < 0 ||
                virAsprintf(admsockfile, "%s/%s-admin-sock",
                            SOCK_PREFIX, config->unix_sock_dir) < 0)
                goto cleanup;
        }
    } else {
        if (privileged) {
            if (virAsprintf(sockfile, "%s/run/libvirt/%s-sock",
                            LOCALSTATEDIR, SOCK_PREFIX) < 0 ||
                virAsprintf(sockfile, "%s/run/libvirt/%s-sock-ro",
                            LOCALSTATEDIR, SOCK_PREFIX) < 0 ||
                virAsprintf(sockfile, "%s/run/libvirt/%s-admin-sock",
                            LOCALSTATEDIR, SOCK_PREFIX) < 0)
                goto cleanup;
        } else {
            mode_t old_umask;

            if (!(rundir = virGetUserRuntimeDirectory()))
                goto cleanup;

            old_umask = umask(077);
            if (virFileMakePath(rundir) < 0) {
                umask(old_umask);
                goto cleanup;
            }
            umask(old_umask);

            if (virAsprintf(sockfile, "%s/%s-sock",
                            rundir, SOCK_PREFIX) < 0 ||
                virAsprintf(admsockfile, "%s/%s-admin-sock",
                            rundir, SOCK_PREFIX) < 0)
                goto cleanup;
        }
    }

    ret = 0;
 cleanup:
    VIR_FREE(rundir);
    return ret;
}


static void daemonErrorHandler(void *opaque ATTRIBUTE_UNUSED,
                               virErrorPtr err ATTRIBUTE_UNUSED)
{
    /* Don't do anything, since logging infrastructure already
     * took care of reporting the error */
}

static int daemonErrorLogFilter(virErrorPtr err, int priority)
{
    /* These error codes don't really reflect real errors. They
     * are expected events that occur when an app tries to check
     * whether a particular guest already exists. This filters
     * them to a lower log level to prevent pollution of syslog
     */
    switch (err->code) {
    case VIR_ERR_NO_DOMAIN:
    case VIR_ERR_NO_NETWORK:
    case VIR_ERR_NO_STORAGE_POOL:
    case VIR_ERR_NO_STORAGE_VOL:
    case VIR_ERR_NO_NODE_DEVICE:
    case VIR_ERR_NO_INTERFACE:
    case VIR_ERR_NO_NWFILTER:
    case VIR_ERR_NO_NWFILTER_BINDING:
    case VIR_ERR_NO_SECRET:
    case VIR_ERR_NO_DOMAIN_SNAPSHOT:
    case VIR_ERR_OPERATION_INVALID:
    case VIR_ERR_NO_DOMAIN_METADATA:
    case VIR_ERR_NO_SERVER:
    case VIR_ERR_NO_CLIENT:
        return VIR_LOG_DEBUG;
    }

    return priority;
}


static int daemonInitialize(void)
{
#ifndef LIBVIRTD
# ifdef MODULE_NAME
    /* This a dedicated per-driver daemon build */
    if (virDriverLoadModule(MODULE_NAME, MODULE_NAME "Register", true) < 0)
        return -1;
# else
    /* This is virtproxyd which merely proxies to the per-driver
     * daemons for back compat, and also allows IP connectivity.
     */
# endif
#else
    /* This is the legacy monolithic libvirtd built with all drivers
     *
     * Note that the order is important: the first ones have a higher
     * priority when calling virStateInitialize. We must register the
     * network, storage and nodedev drivers before any stateful domain
     * driver, since their resources must be auto-started before any
     * domains can be auto-started.
     */
# ifdef WITH_NETWORK
    if (virDriverLoadModule("network", "networkRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_INTERFACE
    if (virDriverLoadModule("interface", "interfaceRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_SECRETS
    if (virDriverLoadModule("secret", "secretRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_STORAGE
    if (virDriverLoadModule("storage", "storageRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_NODE_DEVICES
    if (virDriverLoadModule("nodedev", "nodedevRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_NWFILTER
    if (virDriverLoadModule("nwfilter", "nwfilterRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_LIBXL
    if (virDriverLoadModule("libxl", "libxlRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_QEMU
    if (virDriverLoadModule("qemu", "qemuRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_LXC
    if (virDriverLoadModule("lxc", "lxcRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_VBOX
    if (virDriverLoadModule("vbox", "vboxRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_BHYVE
    if (virDriverLoadModule("bhyve", "bhyveRegister", false) < 0)
        return -1;
# endif
# ifdef WITH_VZ
    if (virDriverLoadModule("vz", "vzRegister", false) < 0)
        return -1;
# endif
#endif
    return 0;
}


static int ATTRIBUTE_NONNULL(3)
daemonSetupNetworking(virNetServerPtr srv,
                      virNetServerPtr srvAdm,
                      struct daemonConfig *config,
#ifdef ENABLE_IP
                      bool ipsock,
                      bool privileged,
#endif /* ! ENABLE_IP */
                      const char *sock_path,
                      const char *sock_path_ro,
                      const char *sock_path_adm)
{
    gid_t unix_sock_gid = 0;
    int unix_sock_ro_mask = 0;
    int unix_sock_rw_mask = 0;
    int unix_sock_adm_mask = 0;
    int ret = -1;
    VIR_AUTOPTR(virSystemdActivation) act = NULL;
    virSystemdActivationMap actmap[] = {
        { .name = DAEMON_NAME ".socket", .family = AF_UNIX, .path = sock_path },
        { .name = DAEMON_NAME "-ro.socket", .family = AF_UNIX, .path = sock_path_ro },
        { .name = DAEMON_NAME "-admin.socket", .family = AF_UNIX, .path = sock_path_adm },
#ifdef ENABLE_IP
        { .name = DAEMON_NAME "-tcp.socket", .family = AF_INET },
        { .name = DAEMON_NAME "-tls.socket", .family = AF_INET },
#endif /* ! ENABLE_IP */
    };

#ifdef ENABLE_IP
    if ((actmap[3].port = virSocketAddrResolveService(config->tcp_port)) < 0)
        return -1;

    if ((actmap[4].port = virSocketAddrResolveService(config->tls_port)) < 0)
        return -1;
#endif /* ! ENABLE_IP */

    if (virSystemdGetActivation(actmap, ARRAY_CARDINALITY(actmap), &act) < 0)
        return -1;

    if (config->unix_sock_group) {
        if (virGetGroupID(config->unix_sock_group, &unix_sock_gid) < 0)
            return ret;
    }

    if (virStrToLong_i(config->unix_sock_ro_perms, NULL, 8, &unix_sock_ro_mask) != 0) {
        VIR_ERROR(_("Failed to parse mode '%s'"), config->unix_sock_ro_perms);
        goto cleanup;
    }

    if (virStrToLong_i(config->unix_sock_admin_perms, NULL, 8, &unix_sock_adm_mask) != 0) {
        VIR_ERROR(_("Failed to parse mode '%s'"), config->unix_sock_admin_perms);
        goto cleanup;
    }

    if (virStrToLong_i(config->unix_sock_rw_perms, NULL, 8, &unix_sock_rw_mask) != 0) {
        VIR_ERROR(_("Failed to parse mode '%s'"), config->unix_sock_rw_perms);
        goto cleanup;
    }

    if (virNetServerAddServiceUNIX(srv,
                                   act,
                                   DAEMON_NAME ".socket",
                                   sock_path,
                                   unix_sock_rw_mask,
                                   unix_sock_gid,
                                   config->auth_unix_rw,
                                   NULL,
                                   false,
                                   config->max_queued_clients,
                                   config->max_client_requests) < 0)
        goto cleanup;
    if (sock_path_ro &&
        virNetServerAddServiceUNIX(srv,
                                   act,
                                   DAEMON_NAME "-ro.socket",
                                   sock_path_ro,
                                   unix_sock_ro_mask,
                                   unix_sock_gid,
                                   config->auth_unix_ro,
                                   NULL,
                                   true,
                                   config->max_queued_clients,
                                   config->max_client_requests) < 0)
        goto cleanup;

    if (sock_path_adm &&
        virNetServerAddServiceUNIX(srvAdm,
                                   act,
                                   DAEMON_NAME "-admin.socket",
                                   sock_path_adm,
                                   unix_sock_adm_mask,
                                   unix_sock_gid,
                                   REMOTE_AUTH_NONE,
                                   NULL,
                                   false,
                                   config->admin_max_queued_clients,
                                   config->admin_max_client_requests) < 0)
        goto cleanup;

#ifdef ENABLE_IP
    if (((ipsock && config->listen_tcp) || act) &&
        virNetServerAddServiceTCP(srv,
                                  act,
                                  DAEMON_NAME "-tcp.socket",
                                  config->listen_addr,
                                  config->tcp_port,
                                  AF_UNSPEC,
                                  config->auth_tcp,
                                  NULL,
                                  false,
                                  config->max_queued_clients,
                                  config->max_client_requests) < 0)
        goto cleanup;

    if (((ipsock && config->listen_tls) || (act && virSystemdActivationHasName(act, "ip-tls")))) {
        virNetTLSContextPtr ctxt = NULL;

        if (config->ca_file ||
            config->cert_file ||
            config->key_file) {
            if (!config->ca_file) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("No CA certificate path set to match server key/cert"));
                goto cleanup;
            }
            if (!config->cert_file) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("No server certificate path set to match server key"));
                goto cleanup;
            }
            if (!config->key_file) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("No server key path set to match server cert"));
                goto cleanup;
            }
            VIR_DEBUG("Using CA='%s' cert='%s' key='%s'",
                      config->ca_file, config->cert_file, config->key_file);
            if (!(ctxt = virNetTLSContextNewServer(config->ca_file,
                                                   config->crl_file,
                                                   config->cert_file,
                                                   config->key_file,
                                                   (const char *const*)config->tls_allowed_dn_list,
                                                   config->tls_priority,
                                                   config->tls_no_sanity_certificate ? false : true,
                                                   config->tls_no_verify_certificate ? false : true)))
                goto cleanup;
        } else {
            if (!(ctxt = virNetTLSContextNewServerPath(NULL,
                                                       !privileged,
                                                       (const char *const*)config->tls_allowed_dn_list,
                                                       config->tls_priority,
                                                       config->tls_no_sanity_certificate ? false : true,
                                                       config->tls_no_verify_certificate ? false : true)))
                goto cleanup;
        }

        VIR_DEBUG("Registering TLS socket %s:%s",
                  config->listen_addr, config->tls_port);
        if (virNetServerAddServiceTCP(srv,
                                      act,
                                      DAEMON_NAME "-tls.socket",
                                      config->listen_addr,
                                      config->tls_port,
                                      AF_UNSPEC,
                                      config->auth_tls,
                                      ctxt,
                                      false,
                                      config->max_queued_clients,
                                      config->max_client_requests) < 0) {
            virObjectUnref(ctxt);
            goto cleanup;
        }
        virObjectUnref(ctxt);
    }
#endif /* ! ENABLE_IP */

    if (act &&
        virSystemdActivationComplete(act) < 0)
        goto cleanup;

#if WITH_SASL
    if (virNetServerNeedsAuth(srv, REMOTE_AUTH_SASL) &&
        !(saslCtxt = virNetSASLContextNewServer(
              (const char *const*)config->sasl_allowed_username_list)))
        goto cleanup;
#endif

    ret = 0;

 cleanup:
    return ret;
}


/*
 * Set up the openvswitch timeout
 */
static void
daemonSetupNetDevOpenvswitch(struct daemonConfig *config)
{
    virNetDevOpenvswitchSetTimeout(config->ovs_timeout);
}


/*
 * Set up the logging environment
 * By default if daemonized all errors go to journald/a logfile
 * but if verbose or error debugging is asked for then also output
 * informational and debug messages. Default size if 64 kB.
 */
static int
daemonSetupLogging(struct daemonConfig *config,
                   bool privileged,
                   bool verbose,
                   bool godaemon)
{
    virLogReset();

    /*
     * Logging setup order of precedence is:
     * cmdline > environment > config
     *
     * Given the precedence, we must process the variables in the opposite
     * order, each one overriding the previous.
     */
    if (config->log_level != 0)
        virLogSetDefaultPriority(config->log_level);

    /* In case the config is empty, both filters and outputs will become empty,
     * however we can't start with empty outputs, thus we'll need to define and
     * setup a default one.
     */
    ignore_value(virLogSetFilters(config->log_filters));
    ignore_value(virLogSetOutputs(config->log_outputs));

    /* If there are some environment variables defined, use those instead */
    virLogSetFromEnv();

    /*
     * Command line override for --verbose
     */
    if ((verbose) && (virLogGetDefaultPriority() > VIR_LOG_INFO))
        virLogSetDefaultPriority(VIR_LOG_INFO);

    /* Define the default output. This is only applied if there was no setting
     * from either the config or the environment.
     */
    if (virLogSetDefaultOutput(DAEMON_NAME, godaemon, privileged) < 0)
        return -1;

    if (virLogGetNbOutputs() == 0)
        virLogSetOutputs(virLogGetDefaultOutput());

    return 0;
}


static int
daemonSetupAccessManager(struct daemonConfig *config)
{
    virAccessManagerPtr mgr;
    const char *none[] = { "none", NULL };
    const char **drv = (const char **)config->access_drivers;

    if (!drv ||
        !drv[0])
        drv = none;

    if (!(mgr = virAccessManagerNewStack(drv)))
        return -1;

    virAccessManagerSetDefault(mgr);
    virObjectUnref(mgr);
    return 0;
}


/* Display version information. */
static void
daemonVersion(const char *argv0)
{
    printf("%s (%s) %s\n", argv0, PACKAGE_NAME, PACKAGE_VERSION);
}


static void daemonShutdownHandler(virNetDaemonPtr dmn,
                                  siginfo_t *sig ATTRIBUTE_UNUSED,
                                  void *opaque ATTRIBUTE_UNUSED)
{
    virNetDaemonQuit(dmn);
}

static void daemonReloadHandlerThread(void *opague ATTRIBUTE_UNUSED)
{
    VIR_INFO("Reloading configuration on SIGHUP");
    virHookCall(VIR_HOOK_DRIVER_DAEMON, "-",
                VIR_HOOK_DAEMON_OP_RELOAD, SIGHUP, "SIGHUP", NULL, NULL);
    if (virStateReload() < 0)
        VIR_WARN("Error while reloading drivers");
}

static void daemonReloadHandler(virNetDaemonPtr dmn ATTRIBUTE_UNUSED,
                                siginfo_t *sig ATTRIBUTE_UNUSED,
                                void *opaque ATTRIBUTE_UNUSED)
{
    virThread thr;

    if (!driversInitialized) {
        VIR_WARN("Drivers are not initialized, reload ignored");
        return;
    }

    if (virThreadCreate(&thr, false, daemonReloadHandlerThread, NULL) < 0) {
        /*
         * Not much we can do on error here except log it.
         */
        VIR_ERROR(_("Failed to create thread to handle daemon restart"));
    }
}

static int daemonSetupSignals(virNetDaemonPtr dmn)
{
    if (virNetDaemonAddSignalHandler(dmn, SIGINT, daemonShutdownHandler, NULL) < 0)
        return -1;
    if (virNetDaemonAddSignalHandler(dmn, SIGQUIT, daemonShutdownHandler, NULL) < 0)
        return -1;
    if (virNetDaemonAddSignalHandler(dmn, SIGTERM, daemonShutdownHandler, NULL) < 0)
        return -1;
    if (virNetDaemonAddSignalHandler(dmn, SIGHUP, daemonReloadHandler, NULL) < 0)
        return -1;
    return 0;
}


static void daemonInhibitCallback(bool inhibit, void *opaque)
{
    virNetDaemonPtr dmn = opaque;

    if (inhibit)
        virNetDaemonAddShutdownInhibition(dmn);
    else
        virNetDaemonRemoveShutdownInhibition(dmn);
}


#ifdef WITH_DBUS
static DBusConnection *sessionBus;
static DBusConnection *systemBus;

static void daemonStopWorker(void *opaque)
{
    virNetDaemonPtr dmn = opaque;

    VIR_DEBUG("Begin stop dmn=%p", dmn);

    ignore_value(virStateStop());

    VIR_DEBUG("Completed stop dmn=%p", dmn);

    /* Exit daemon cleanly */
    virNetDaemonQuit(dmn);
}


/* We do this in a thread to not block the main loop */
static void daemonStop(virNetDaemonPtr dmn)
{
    virThread thr;
    virObjectRef(dmn);
    if (virThreadCreate(&thr, false, daemonStopWorker, dmn) < 0)
        virObjectUnref(dmn);
}


static DBusHandlerResult
handleSessionMessageFunc(DBusConnection *connection ATTRIBUTE_UNUSED,
                         DBusMessage *message,
                         void *opaque)
{
    virNetDaemonPtr dmn = opaque;

    VIR_DEBUG("dmn=%p", dmn);

    if (dbus_message_is_signal(message,
                               DBUS_INTERFACE_LOCAL,
                               "Disconnected"))
        daemonStop(dmn);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static DBusHandlerResult
handleSystemMessageFunc(DBusConnection *connection ATTRIBUTE_UNUSED,
                        DBusMessage *message,
                        void *opaque)
{
    virNetDaemonPtr dmn = opaque;

    VIR_DEBUG("dmn=%p", dmn);

    if (dbus_message_is_signal(message,
                               "org.freedesktop.login1.Manager",
                               "PrepareForShutdown"))
        daemonStop(dmn);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
#endif


static void daemonRunStateInit(void *opaque)
{
    virNetDaemonPtr dmn = opaque;
    virIdentityPtr sysident = virIdentityGetSystem();
#ifdef MODULE_NAME
    bool mandatory = true;
#else /* ! MODULE_NAME */
    bool mandatory = false;
#endif /* ! MODULE_NAME */

    virIdentitySetCurrent(sysident);

    /* Since driver initialization can take time inhibit daemon shutdown until
       we're done so clients get a chance to connect */
    daemonInhibitCallback(true, dmn);

    /* Start the stateful HV drivers
     * This is deliberately done after telling the parent process
     * we're ready, since it can take a long time and this will
     * seriously delay OS bootup process */
    if (virStateInitialize(virNetDaemonIsPrivileged(dmn),
                           mandatory,
                           daemonInhibitCallback,
                           dmn) < 0) {
        VIR_ERROR(_("Driver state initialization failed"));
        /* Ensure the main event loop quits */
        kill(getpid(), SIGTERM);
        goto cleanup;
    }

    driversInitialized = true;

#ifdef WITH_DBUS
    /* Tie the non-privileged daemons to the session/shutdown lifecycle */
    if (!virNetDaemonIsPrivileged(dmn)) {

        sessionBus = virDBusGetSessionBus();
        if (sessionBus != NULL)
            dbus_connection_add_filter(sessionBus,
                                       handleSessionMessageFunc, dmn, NULL);

        systemBus = virDBusGetSystemBus();
        if (systemBus != NULL) {
            dbus_connection_add_filter(systemBus,
                                       handleSystemMessageFunc, dmn, NULL);
            dbus_bus_add_match(systemBus,
                               "type='signal',sender='org.freedesktop.login1', interface='org.freedesktop.login1.Manager'",
                               NULL);
        }
    }
#endif
    /* Only now accept clients from network */
    virNetDaemonUpdateServices(dmn, true);
 cleanup:
    daemonInhibitCallback(false, dmn);
    virObjectUnref(dmn);
    virObjectUnref(sysident);
    virIdentitySetCurrent(NULL);
}

static int daemonStateInit(virNetDaemonPtr dmn)
{
    virThread thr;
    virObjectRef(dmn);
    if (virThreadCreate(&thr, false, daemonRunStateInit, dmn) < 0) {
        virObjectUnref(dmn);
        return -1;
    }
    return 0;
}

static int
daemonSetupHostUUID(const struct daemonConfig *config)
{
    static const char *machine_id = "/etc/machine-id";
    char buf[VIR_UUID_STRING_BUFLEN];
    const char *uuid;

    if (config->host_uuid) {
        uuid = config->host_uuid;
    } else if (!config->host_uuid_source ||
               STREQ(config->host_uuid_source, "smbios")) {
        /* smbios UUID is fetched on demand in virGetHostUUID */
        return 0;
    } else if (STREQ(config->host_uuid_source, "machine-id")) {
        if (virFileReadBufQuiet(machine_id, buf, sizeof(buf)) < 0) {
            VIR_ERROR(_("Can't read %s"), machine_id);
            return -1;
        }

        uuid = buf;
    } else {
        VIR_ERROR(_("invalid UUID source: %s"), config->host_uuid_source);
        return -1;
    }

    if (virSetHostUUIDStr(uuid)) {
        VIR_ERROR(_("invalid host UUID: %s"), uuid);
        return -1;
    }

    return 0;
}

typedef struct {
    const char *opts;
    const char *help;
} virOptionHelp;

/* Print command-line usage. */
static void
daemonUsage(const char *argv0, bool privileged)
{
    size_t i;
    virOptionHelp opthelp[] = {
        { "-h | --help", N_("Display program help") },
        { "-v | --verbose", N_("Verbose messages") },
        { "-d | --daemon", N_("Run as a daemon & write PID file") },
#if defined(ENABLE_IP) && defined(LIBVIRTD)
        { "-l | --listen", N_("Listen for TCP/IP connections") },
#endif /* ENABLE_IP && LIBVIRTD */
        { "-t | --timeout <secs>", N_("Exit after timeout period") },
        { "-f | --config <file>", N_("Configuration file") },
        { "-V | --version", N_("Display version information") },
        { "-p | --pid-file <file>", N_("Change name of PID file") },
    };

    fprintf(stderr, "\n");
    fprintf(stderr, "%s:\n", _("Usage"));
    fprintf(stderr, "  %s [%s]\n", argv0, _("options"));
    fprintf(stderr, "\n");

    fprintf(stderr, "%s:\n", _("Options"));
    for (i = 0; i < ARRAY_CARDINALITY(opthelp); i++)
        fprintf(stderr, "  %-22s %s\n", opthelp[i].opts, N_(opthelp[i].help));
    fprintf(stderr, "\n");

    fprintf(stderr, "%s:\n", _("libvirt management daemon"));

    fprintf(stderr, "\n");
    fprintf(stderr, "  %s:\n", _("Default paths"));
    fprintf(stderr, "\n");

    fprintf(stderr, "    %s:\n", _("Configuration file (unless overridden by -f)"));
    fprintf(stderr, "      %s/libvirt/%s.conf\n",
            privileged ? SYSCONFDIR : "$XDG_CONFIG_HOME", DAEMON_NAME);
    fprintf(stderr, "\n");

    fprintf(stderr, "    %s:\n", _("Sockets"));
    fprintf(stderr, "      %s/libvirt/%s-sock\n",
            privileged ? LOCALSTATEDIR "/run" : "$XDG_RUNTIME_DIR",
            SOCK_PREFIX);
    if (privileged)
        fprintf(stderr, "      %s/run/libvirt/%s-sock-ro\n",
                LOCALSTATEDIR, SOCK_PREFIX);
    fprintf(stderr, "\n");

#ifdef ENABLE_IP
    fprintf(stderr, "    %s:\n", _("TLS"));
    fprintf(stderr, "      %s: %s\n",
            _("CA certificate"),
            privileged ? LIBVIRT_CACERT : "$HOME/.pki/libvirt/cacert.pem");
    fprintf(stderr, "      %s: %s\n",
            _("Server certificate"),
            privileged ? LIBVIRT_SERVERCERT : "$HOME/.pki/libvirt/servercert.pem");
    fprintf(stderr, "      %s: %s\n",
            _("Server private key"),
            privileged ? LIBVIRT_SERVERKEY : "$HOME/.pki/libvirt/serverkey.pem");
    fprintf(stderr, "\n");
#endif /* ENABLE_IP */

    fprintf(stderr, "    %s:\n",
            _("PID file (unless overridden by -p)"));
    fprintf(stderr, "      %s/%s.pid\n",
            privileged ? LOCALSTATEDIR "/run" : "$XDG_RUNTIME_DIR/libvirt",
            DAEMON_NAME);
    fprintf(stderr, "\n");
}

int main(int argc, char **argv) {
    virNetDaemonPtr dmn = NULL;
    virNetServerPtr srv = NULL;
    virNetServerPtr srvAdm = NULL;
    virNetServerProgramPtr adminProgram = NULL;
    virNetServerProgramPtr lxcProgram = NULL;
    char *remote_config_file = NULL;
    int statuswrite = -1;
    int ret = 1;
    int pid_file_fd = -1;
    char *pid_file = NULL;
    char *sock_file = NULL;
    char *sock_file_ro = NULL;
    char *sock_file_adm = NULL;
    int timeout = -1;        /* -t: Shutdown timeout */
    int verbose = 0;
    int godaemon = 0;
#ifdef ENABLE_IP
# ifdef LIBVIRTD
    int ipsock = 0;
# else
    int ipsock = 1; /* listen_tcp/listen_tls default to 0 */
# endif
#endif /* ! ENABLE_IP */
    struct daemonConfig *config;
    bool privileged = geteuid() == 0 ? true : false;
    bool implicit_conf = false;
    char *run_dir = NULL;
    mode_t old_umask;

    struct option opts[] = {
        { "verbose", no_argument, &verbose, 'v'},
        { "daemon", no_argument, &godaemon, 'd'},
#if defined(ENABLE_IP) && defined(LIBVIRTD)
        { "listen", no_argument, &ipsock, 'l'},
#endif /* ENABLE_IP && LIBVIRTD */
        { "config", required_argument, NULL, 'f'},
        { "timeout", required_argument, NULL, 't'},
        { "pid-file", required_argument, NULL, 'p'},
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        {0, 0, 0, 0}
    };

    if (virGettextInitialize() < 0 ||
        virInitialize() < 0) {
        fprintf(stderr, _("%s: initialization failed\n"), argv[0]);
        exit(EXIT_FAILURE);
    }

    virUpdateSelfLastChanged(argv[0]);

    virFileActivateDirOverride(argv[0]);

    while (1) {
        int optidx = 0;
        int c;
        char *tmp;
#if defined(ENABLE_IP) && defined(LIBVIRTD)
        const char *optstr = "ldf:p:t:vVh";
#else /* ! ENABLE_IP && ! LIBVIRTD */
        const char *optstr = "df:p:t:vVh";
#endif /* ! ENABLE_IP && ! LIBVIRTD */

        c = getopt_long(argc, argv, optstr, opts, &optidx);

        if (c == -1)
            break;

        switch (c) {
        case 0:
            /* Got one of the flags */
            break;
        case 'v':
            verbose = 1;
            break;
        case 'd':
            godaemon = 1;
            break;

#if defined(ENABLE_IP) && defined(LIBVIRTD)
        case 'l':
            ipsock = 1;
            break;
#endif /* ! ENABLE_IP */

        case 't':
            if (virStrToLong_i(optarg, &tmp, 10, &timeout) != 0
                || timeout <= 0
                /* Ensure that we can multiply by 1000 without overflowing.  */
                || timeout > INT_MAX / 1000) {
                VIR_ERROR(_("Invalid value for timeout"));
                exit(EXIT_FAILURE);
            }
            break;

        case 'p':
            VIR_FREE(pid_file);
            if (VIR_STRDUP_QUIET(pid_file, optarg) < 0) {
                VIR_ERROR(_("Can't allocate memory"));
                exit(EXIT_FAILURE);
            }
            break;

        case 'f':
            VIR_FREE(remote_config_file);
            if (VIR_STRDUP_QUIET(remote_config_file, optarg) < 0) {
                VIR_ERROR(_("Can't allocate memory"));
                exit(EXIT_FAILURE);
            }
            break;

        case 'V':
            daemonVersion(argv[0]);
            exit(EXIT_SUCCESS);

        case 'h':
            daemonUsage(argv[0], privileged);
            exit(EXIT_SUCCESS);

        case '?':
        default:
            daemonUsage(argv[0], privileged);
            exit(EXIT_FAILURE);
        }
    }

    if (optind != argc) {
        fprintf(stderr, "%s: unexpected, non-option, command line arguments\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    if (!(config = daemonConfigNew(privileged))) {
        VIR_ERROR(_("Can't create initial configuration"));
        exit(EXIT_FAILURE);
    }

    /* No explicit config, so try and find a default one */
    if (remote_config_file == NULL) {
        implicit_conf = true;
        if (daemonConfigFilePath(privileged,
                                 &remote_config_file) < 0) {
            VIR_ERROR(_("Can't determine config path"));
            exit(EXIT_FAILURE);
        }
    }

    /* Read the config file if it exists*/
    if (remote_config_file &&
        daemonConfigLoadFile(config, remote_config_file, implicit_conf) < 0) {
        VIR_ERROR(_("Can't load config file: %s: %s"),
                  virGetLastErrorMessage(), remote_config_file);
        exit(EXIT_FAILURE);
    }

    if (daemonSetupHostUUID(config) < 0) {
        VIR_ERROR(_("Can't setup host uuid"));
        exit(EXIT_FAILURE);
    }

    if (daemonSetupLogging(config, privileged, verbose, godaemon) < 0) {
        VIR_ERROR(_("Can't initialize logging"));
        exit(EXIT_FAILURE);
    }

    daemonSetupNetDevOpenvswitch(config);

    if (daemonSetupAccessManager(config) < 0) {
        VIR_ERROR(_("Can't initialize access manager"));
        exit(EXIT_FAILURE);
    }

    if (!pid_file &&
        virPidFileConstructPath(privileged,
                                LOCALSTATEDIR,
                                DAEMON_NAME,
                                &pid_file) < 0) {
        VIR_ERROR(_("Can't determine pid file path."));
        exit(EXIT_FAILURE);
    }
    VIR_DEBUG("Decided on pid file path '%s'", NULLSTR(pid_file));

    if (daemonUnixSocketPaths(config,
                              privileged,
                              &sock_file,
                              &sock_file_ro,
                              &sock_file_adm) < 0) {
        VIR_ERROR(_("Can't determine socket paths"));
        exit(EXIT_FAILURE);
    }
    VIR_DEBUG("Decided on socket paths '%s', '%s' and '%s'",
              sock_file,
              NULLSTR(sock_file_ro),
              NULLSTR(sock_file_adm));

    if (godaemon) {
        char ebuf[1024];

        if (chdir("/") < 0) {
            VIR_ERROR(_("cannot change to root directory: %s"),
                      virStrerror(errno, ebuf, sizeof(ebuf)));
            goto cleanup;
        }

        if ((statuswrite = daemonForkIntoBackground(argv[0])) < 0) {
            VIR_ERROR(_("Failed to fork as daemon: %s"),
                      virStrerror(errno, ebuf, sizeof(ebuf)));
            goto cleanup;
        }
    }

    /* Try to claim the pidfile, exiting if we can't */
    if ((pid_file_fd = virPidFileAcquirePath(pid_file, false, getpid())) < 0) {
        ret = VIR_DAEMON_ERR_PIDFILE;
        goto cleanup;
    }

    /* Ensure the rundir exists (on tmpfs on some systems) */
    if (privileged) {
        if (VIR_STRDUP_QUIET(run_dir, LOCALSTATEDIR "/run/libvirt") < 0) {
            VIR_ERROR(_("Can't allocate memory"));
            goto cleanup;
        }
    } else {
        run_dir = virGetUserRuntimeDirectory();

        if (!run_dir) {
            VIR_ERROR(_("Can't determine user directory"));
            goto cleanup;
        }
    }
    if (privileged)
        old_umask = umask(022);
    else
        old_umask = umask(077);
    VIR_DEBUG("Ensuring run dir '%s' exists", run_dir);
    if (virFileMakePath(run_dir) < 0) {
        char ebuf[1024];
        VIR_ERROR(_("unable to create rundir %s: %s"), run_dir,
                  virStrerror(errno, ebuf, sizeof(ebuf)));
        ret = VIR_DAEMON_ERR_RUNDIR;
        goto cleanup;
    }
    umask(old_umask);

    if (virNetlinkStartup() < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (!(dmn = virNetDaemonNew())) {
        ret = VIR_DAEMON_ERR_DRIVER;
        goto cleanup;
    }

    if (!(srv = virNetServerNew(DAEMON_NAME, 1,
                                config->min_workers,
                                config->max_workers,
                                config->prio_workers,
                                config->max_clients,
                                config->max_anonymous_clients,
                                config->keepalive_interval,
                                config->keepalive_count,
                                remoteClientNew,
                                NULL,
                                remoteClientFree,
                                NULL))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (virNetDaemonAddServer(dmn, srv) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (daemonInitialize() < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    remoteProcs[REMOTE_PROC_AUTH_LIST].needAuth = false;
    remoteProcs[REMOTE_PROC_AUTH_SASL_INIT].needAuth = false;
    remoteProcs[REMOTE_PROC_AUTH_SASL_STEP].needAuth = false;
    remoteProcs[REMOTE_PROC_AUTH_SASL_START].needAuth = false;
    remoteProcs[REMOTE_PROC_AUTH_POLKIT].needAuth = false;
    if (!(remoteProgram = virNetServerProgramNew(REMOTE_PROGRAM,
                                                 REMOTE_PROTOCOL_VERSION,
                                                 remoteProcs,
                                                 remoteNProcs))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }
    if (virNetServerAddProgram(srv, remoteProgram) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (!(lxcProgram = virNetServerProgramNew(LXC_PROGRAM,
                                              LXC_PROTOCOL_VERSION,
                                              lxcProcs,
                                              lxcNProcs))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }
    if (virNetServerAddProgram(srv, lxcProgram) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (!(qemuProgram = virNetServerProgramNew(QEMU_PROGRAM,
                                               QEMU_PROTOCOL_VERSION,
                                               qemuProcs,
                                               qemuNProcs))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }
    if (virNetServerAddProgram(srv, qemuProgram) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (!(srvAdm = virNetServerNew("admin", 1,
                                   config->admin_min_workers,
                                   config->admin_max_workers,
                                   0,
                                   config->admin_max_clients,
                                   0,
                                   config->admin_keepalive_interval,
                                   config->admin_keepalive_count,
                                   remoteAdmClientNew,
                                   NULL,
                                   remoteAdmClientFree,
                                   dmn))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (virNetDaemonAddServer(dmn, srvAdm) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (!(adminProgram = virNetServerProgramNew(ADMIN_PROGRAM,
                                                ADMIN_PROTOCOL_VERSION,
                                                adminProcs,
                                                adminNProcs))) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }
    if (virNetServerAddProgram(srvAdm, adminProgram) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

    if (timeout != -1) {
        VIR_DEBUG("Registering shutdown timeout %d", timeout);
        virNetDaemonAutoShutdown(dmn, timeout);
    }

    if ((daemonSetupSignals(dmn)) < 0) {
        ret = VIR_DAEMON_ERR_SIGNAL;
        goto cleanup;
    }

    if (config->audit_level) {
        VIR_DEBUG("Attempting to configure auditing subsystem");
        if (virAuditOpen(config->audit_level) < 0) {
            if (config->audit_level > 1) {
                ret = VIR_DAEMON_ERR_AUDIT;
                goto cleanup;
            }
            VIR_DEBUG("Proceeding without auditing");
        }
    }
    virAuditLog(config->audit_logging > 0);

    /* setup the hooks if any */
    if (virHookInitialize() < 0) {
        ret = VIR_DAEMON_ERR_HOOKS;
        goto cleanup;
    }

    /* Disable error func, now logging is setup */
    virSetErrorFunc(NULL, daemonErrorHandler);
    virSetErrorLogPriorityFunc(daemonErrorLogFilter);

    /*
     * Call the daemon startup hook
     * TODO: should we abort the daemon startup if the script returned
     *       an error ?
     */
    virHookCall(VIR_HOOK_DRIVER_DAEMON, "-", VIR_HOOK_DAEMON_OP_START,
                0, "start", NULL, NULL);

    if (daemonSetupNetworking(srv, srvAdm,
                              config,
#ifdef ENABLE_IP
                              ipsock,
                              privileged,
#endif /* !ENABLE_IP */
                              sock_file,
                              sock_file_ro,
                              sock_file_adm) < 0) {
        ret = VIR_DAEMON_ERR_NETWORK;
        goto cleanup;
    }

    /* Tell parent of daemon that basic initialization is complete
     * In particular we're ready to accept net connections & have
     * written the pidfile
     */
    if (statuswrite != -1) {
        char status = 0;
        ignore_value(safewrite(statuswrite, &status, 1));
        VIR_FORCE_CLOSE(statuswrite);
    }

    /* Initialize drivers & then start accepting new clients from network */
    if (daemonStateInit(dmn) < 0) {
        ret = VIR_DAEMON_ERR_INIT;
        goto cleanup;
    }

#if defined(__linux__) && defined(NETLINK_ROUTE)
    /* Register the netlink event service for NETLINK_ROUTE */
    if (virNetlinkEventServiceStart(NETLINK_ROUTE, 0) < 0) {
        ret = VIR_DAEMON_ERR_NETWORK;
        goto cleanup;
    }
#endif

#if defined(__linux__) && defined(NETLINK_KOBJECT_UEVENT)
    /* Register the netlink event service for NETLINK_KOBJECT_UEVENT */
    if (virNetlinkEventServiceStart(NETLINK_KOBJECT_UEVENT, 1) < 0) {
        ret = VIR_DAEMON_ERR_NETWORK;
        goto cleanup;
    }
#endif

    /* Run event loop. */
    virNetDaemonRun(dmn);

    ret = 0;

    virHookCall(VIR_HOOK_DRIVER_DAEMON, "-", VIR_HOOK_DAEMON_OP_SHUTDOWN,
                0, "shutdown", NULL, NULL);

 cleanup:
    /* Keep cleanup order in inverse order of startup */
    virNetDaemonClose(dmn);

    virNetlinkEventServiceStopAll();

    if (driversInitialized) {
        /* NB: Possible issue with timing window between driversInitialized
         * setting if virNetlinkEventServerStart fails */
        driversInitialized = false;
        virStateCleanup();
    }

    virObjectUnref(adminProgram);
    virObjectUnref(srvAdm);
    virObjectUnref(qemuProgram);
    virObjectUnref(lxcProgram);
    virObjectUnref(remoteProgram);
    virObjectUnref(srv);
    virObjectUnref(dmn);

    virNetlinkShutdown();

    if (pid_file_fd != -1)
        virPidFileReleasePath(pid_file, pid_file_fd);

    VIR_FREE(run_dir);

    if (statuswrite != -1) {
        if (ret != 0) {
            /* Tell parent of daemon what failed */
            char status = ret;
            ignore_value(safewrite(statuswrite, &status, 1));
        }
        VIR_FORCE_CLOSE(statuswrite);
    }

    VIR_FREE(sock_file);
    VIR_FREE(sock_file_ro);
    VIR_FREE(sock_file_adm);

    VIR_FREE(pid_file);

    VIR_FREE(remote_config_file);
    daemonConfigFree(config);

    return ret;
}
