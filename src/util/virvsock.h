/*
 * virvsock.h - vsock related util functions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int
virVsockSetGuestCid(int fd,
                    unsigned int guest_cid);

int
virVsockAcquireGuestCid(int fd,
                        unsigned int *guest_cid);
