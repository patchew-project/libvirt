/*
 * remote_daemon_stream.h: APIs for managing client streams
 *
 * Copyright (C) 2009-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "remote_daemon.h"

daemonClientStream *
daemonCreateClientStream(virNetServerClientPtr client,
                         virStreamPtr st,
                         virNetServerProgramPtr prog,
                         virNetMessageHeaderPtr hdr,
                         bool allowSkip);

int daemonFreeClientStream(virNetServerClientPtr client,
                           daemonClientStream *stream);

int daemonAddClientStream(virNetServerClientPtr client,
                          daemonClientStream *stream,
                          bool transmit);

int
daemonRemoveClientStream(virNetServerClientPtr client,
                         daemonClientStream *stream);

void
daemonRemoveAllClientStreams(daemonClientStream *stream);
