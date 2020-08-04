/*
 * libvirt_nss: Name Service Switch plugin
 *
 * The aim is to enable users and applications to translate
 * domain names into IP addresses. However, this is currently
 * available only for those domains which gets their IP addresses
 * from a libvirt managed network.
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <nss.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>


#if 0
# include <errno.h>
# include <stdio.h>
# define NULLSTR(s) ((s) ? (s) : "<null>")
# define ERROR(...) \
do { \
    char ebuf[1024]; \
    strerror_r(errno, ebuf, sizeof(ebuf)); \
    fprintf(stderr, "ERROR %s:%d : ", __FUNCTION__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, " : %s\n", ebuf); \
    fprintf(stderr, "\n"); \
} while (0)

# define DEBUG(...) \
do { \
    fprintf(stderr, "DEBUG %s:%d : ", __FUNCTION__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)
#else
# define ERROR(...) do { } while (0)
# define DEBUG(...) do { } while (0)
#endif

#if !defined(LIBVIRT_NSS_GUEST)
# define NSS_NAME(s) _nss_libvirt_##s##_r
#else
# define NSS_NAME(s) _nss_libvirt_guest_##s##_r
#endif

enum nss_status
NSS_NAME(gethostbyname)(const char *name, struct hostent *result,
                        char *buffer, size_t buflen, int *errnop,
                        int *herrnop);

enum nss_status
NSS_NAME(gethostbyname2)(const char *name, int af, struct hostent *result,
                         char *buffer, size_t buflen, int *errnop,
                         int *herrnop);

enum nss_status
NSS_NAME(gethostbyname3)(const char *name, int af, struct hostent *result,
                         char *buffer, size_t buflen, int *errnop,
                         int *herrnop, int32_t *ttlp, char **canonp);

#ifdef HAVE_STRUCT_GAIH_ADDRTUPLE
enum nss_status
NSS_NAME(gethostbyname4)(const char *name, struct gaih_addrtuple **pat,
                         char *buffer, size_t buflen, int *errnop,
                         int *herrnop, int32_t *ttlp);
#endif /* HAVE_STRUCT_GAIH_ADDRTUPLE */

#if defined(HAVE_BSD_NSS)
ns_mtab*
nss_module_register(const char *name, unsigned int *size,
                    nss_module_unregister_fn *unregister);
#endif /* HAVE_BSD_NSS */
