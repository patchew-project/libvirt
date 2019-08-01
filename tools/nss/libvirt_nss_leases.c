/*
 * libvirt_nss_leases.c: Name Service Switch plugin lease file parser
 *
 * Copyright (C) 2019 Red Hat, Inc.
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
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>

#include "libvirt_nss_leases.h"
#include "libvirt_nss.h"
#include "virsocketaddr.h"
#include "viralloc.h"

enum {
    FIND_LEASES_STATE_START,
    FIND_LEASES_STATE_LIST,
    FIND_LEASES_STATE_ENTRY,
};


typedef struct {
    const char *name;
    char **macs;
    size_t nmacs;
    int state;
    unsigned long long now;
    int af;
    bool *found;
    leaseAddress **addrs;
    size_t *naddrs;

    char *key;
    struct {
        unsigned long long expiry;
        char *ipaddr;
        char *macaddr;
        char *hostname;
    } entry;
} findLeasesParser;


static int
appendAddr(const char *name ATTRIBUTE_UNUSED,
           leaseAddress **tmpAddress,
           size_t *ntmpAddress,
           const char *ipAddr,
           long long expirytime,
           int af)
{
    virSocketAddr sa;
    int family;
    size_t i;

    DEBUG("IP address: %s", ipAddr);
    if (virSocketAddrParse(&sa, ipAddr, AF_UNSPEC) < 0) {
        ERROR("Unable to parse %s", ipAddr);
        return -1;
    }

    family = VIR_SOCKET_ADDR_FAMILY(&sa);
    if (af != AF_UNSPEC && af != family) {
        DEBUG("Skipping address which family is %d, %d requested", family, af);
        return 0;
    }

    for (i = 0; i < *ntmpAddress; i++) {
        if (family == AF_INET) {
            if (memcmp((*tmpAddress)[i].addr,
                       &sa.data.inet4.sin_addr.s_addr,
                       sizeof(sa.data.inet4.sin_addr.s_addr)) == 0) {
                DEBUG("IP address already in the list");
                return 0;
            }
        } else {
            if (memcmp((*tmpAddress)[i].addr,
                       &sa.data.inet6.sin6_addr.s6_addr,
                       sizeof(sa.data.inet6.sin6_addr.s6_addr)) == 0) {
                DEBUG("IP address already in the list");
                return 0;
            }
        }
    }

    if (VIR_REALLOC_N_QUIET(*tmpAddress, *ntmpAddress + 1) < 0) {
        ERROR("Out of memory");
        return -1;
    }

    (*tmpAddress)[*ntmpAddress].expirytime = expirytime;
    (*tmpAddress)[*ntmpAddress].af = family;
    if (family == AF_INET)
        memcpy((*tmpAddress)[*ntmpAddress].addr,
               &sa.data.inet4.sin_addr.s_addr,
               sizeof(sa.data.inet4.sin_addr.s_addr));
    else
        memcpy((*tmpAddress)[*ntmpAddress].addr,
               &sa.data.inet6.sin6_addr.s6_addr,
               sizeof(sa.data.inet6.sin6_addr.s6_addr));
    (*ntmpAddress)++;
    return 0;
}


static int
findLeasesParserInteger(void *ctx,
                        long long val)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse int state=%d '%lld' (map key '%s')",
          parser->state, val, NULLSTR(parser->key));
    if (!parser->key)
        return 0;

    if (parser->state == FIND_LEASES_STATE_ENTRY) {
        if (STRNEQ(parser->key, "expiry-time"))
            return 0;

        parser->entry.expiry = val;
    } else {
        return 0;
    }
    return 1;
}


static int
findLeasesParserString(void *ctx,
                       const unsigned char *stringVal,
                       size_t stringLen)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse string state=%d '%.*s' (map key '%s')",
          parser->state, (int)stringLen, (const char *)stringVal,
          NULLSTR(parser->key));
    if (!parser->key)
        return 0;

    if (parser->state == FIND_LEASES_STATE_ENTRY) {
        if (STREQ(parser->key, "ip-address")) {
            if (!(parser->entry.ipaddr = strndup((char *)stringVal, stringLen)))
                return 0;
        } else if (STREQ(parser->key, "mac-address")) {
            if (!(parser->entry.macaddr = strndup((char *)stringVal, stringLen)))
                return 0;
        } else if (STREQ(parser->key, "hostname")) {
            if (!(parser->entry.hostname = strndup((char *)stringVal, stringLen)))
                return 0;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
    return 1;
}


static int
findLeasesParserMapKey(void *ctx,
                       const unsigned char *stringVal,
                       size_t stringLen)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse map key state=%d '%.*s'",
          parser->state, (int)stringLen, (const char *)stringVal);

    free(parser->key);
    if (!(parser->key = strndup((char *)stringVal, stringLen)))
        return 0;

    return 1;
}


static int
findLeasesParserStartMap(void *ctx)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse start map state=%d", parser->state);

    if (parser->state != FIND_LEASES_STATE_LIST)
        return 0;

    free(parser->key);
    parser->key = NULL;
    parser->state = FIND_LEASES_STATE_ENTRY;

    return 1;
}


static int
findLeasesParserEndMap(void *ctx)
{
    findLeasesParser *parser = ctx;
    size_t i;
    bool found = false;

    DEBUG("Parse end map state=%d", parser->state);

    if (parser->entry.macaddr == NULL)
        return 0;

    if (parser->state != FIND_LEASES_STATE_ENTRY)
        return 0;

    if (parser->nmacs) {
        DEBUG("Check %zu macs", parser->nmacs);
        for (i = 0; i < parser->nmacs && !found; i++) {
            DEBUG("Check mac '%s' vs '%s'", parser->macs[i], NULLSTR(parser->entry.macaddr));
            if (STREQ_NULLABLE(parser->macs[i], parser->entry.macaddr))
                found = true;
        }
    } else {
        DEBUG("Check name '%s' vs '%s'", parser->name, NULLSTR(parser->entry.hostname));
        if (STREQ_NULLABLE(parser->name, parser->entry.hostname))
            found = true;
    }
    DEBUG("Found %d", found);
    if (parser->entry.expiry < parser->now) {
        DEBUG("Entry expired at %llu vs now %llu",
              parser->entry.expiry, parser->now);
        found = false;
    }
    if (!parser->entry.ipaddr)
        found = false;

    if (found) {
        *parser->found = true;

        if (appendAddr(parser->name,
                       parser->addrs, parser->naddrs,
                       parser->entry.ipaddr,
                       parser->entry.expiry,
                       parser->af) < 0)
            return 0;
    }

    free(parser->entry.macaddr);
    free(parser->entry.ipaddr);
    free(parser->entry.hostname);
    parser->entry.macaddr = NULL;
    parser->entry.ipaddr = NULL;
    parser->entry.hostname = NULL;

    parser->state = FIND_LEASES_STATE_LIST;

    return 1;
}


static int
findLeasesParserStartArray(void *ctx)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse start array state=%d", parser->state);

    if (parser->state == FIND_LEASES_STATE_START) {
        parser->state = FIND_LEASES_STATE_LIST;
    } else {
        return 0;
    }

    return 1;
}


static int
findLeasesParserEndArray(void *ctx)
{
    findLeasesParser *parser = ctx;

    DEBUG("Parse end array state=%d", parser->state);

    if (parser->state == FIND_LEASES_STATE_LIST)
        parser->state = FIND_LEASES_STATE_START;
    else
        return 0;

    return 1;
}


int
findLeases(const char *file,
           const char *name,
           char **macs,
           size_t nmacs,
           int af,
           time_t now,
           leaseAddress **addrs,
           size_t *naddrs,
           bool *found)
{
    int fd = -1;
    int ret = -1;
    const yajl_callbacks parserCallbacks = {
        NULL, /* null */
        NULL, /* bool */
        findLeasesParserInteger,
        NULL, /* double */
        NULL, /* number */
        findLeasesParserString,
        findLeasesParserStartMap,
        findLeasesParserMapKey,
        findLeasesParserEndMap,
        findLeasesParserStartArray,
        findLeasesParserEndArray,
    };
    findLeasesParser parserState = {
        .name = name,
        .macs = macs,
        .nmacs = nmacs,
        .af = af,
        .now = now,
        .found = found,
        .addrs = addrs,
        .naddrs = naddrs,
    };
    yajl_handle parser;
    char line[1024];
    int rv;

    if ((fd = open(file, O_RDONLY)) < 0) {
        ERROR("Cannot open %s", file);
        goto cleanup;
    }

    parser = yajl_alloc(&parserCallbacks, NULL, &parserState);
    if (!parser) {
        ERROR("Unable to create JSON parser");
        goto cleanup;
    }

    while (1) {
        rv = read(fd, line, sizeof(line));
        if (rv < 0)
            goto cleanup;
        if (rv == 0)
            break;

        if (yajl_parse(parser, (const unsigned char *)line, rv)  !=
            yajl_status_ok) {
            ERROR("Parse failed %s",
                  yajl_get_error(parser, 1,
                                 (const unsigned char*)line, rv));
            goto cleanup;
        }
    }

    if (yajl_complete_parse(parser) != yajl_status_ok) {
        ERROR("Parse failed %s",
              yajl_get_error(parser, 1, NULL, 0));
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (ret != 0) {
        free(*addrs);
        *addrs = NULL;
        *naddrs = 0;
    }
    free(parserState.entry.ipaddr);
    free(parserState.entry.macaddr);
    free(parserState.entry.hostname);
    free(parserState.key);
    if (fd != -1)
        close(fd);
    return ret;
}
