/*
 * virendian.h: aid for reading endian-specific data
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

/* The interfaces in this file are provided as macros for speed.  */

/**
 * virReadBufInt64BE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 8 bytes at BUF as a big-endian 64-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt64BE(buf) \
    (((uint64_t)(uint8_t)((buf)[0]) << 56) | \
     ((uint64_t)(uint8_t)((buf)[1]) << 48) | \
     ((uint64_t)(uint8_t)((buf)[2]) << 40) | \
     ((uint64_t)(uint8_t)((buf)[3]) << 32) | \
     ((uint64_t)(uint8_t)((buf)[4]) << 24) | \
     ((uint64_t)(uint8_t)((buf)[5]) << 16) | \
     ((uint64_t)(uint8_t)((buf)[6]) << 8) | \
     (uint64_t)(uint8_t)((buf)[7]))

/**
 * virReadBufInt64LE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 8 bytes at BUF as a little-endian 64-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt64LE(buf) \
    ((uint64_t)(uint8_t)((buf)[0]) | \
     ((uint64_t)(uint8_t)((buf)[1]) << 8) | \
     ((uint64_t)(uint8_t)((buf)[2]) << 16) | \
     ((uint64_t)(uint8_t)((buf)[3]) << 24) | \
     ((uint64_t)(uint8_t)((buf)[4]) << 32) | \
     ((uint64_t)(uint8_t)((buf)[5]) << 40) | \
     ((uint64_t)(uint8_t)((buf)[6]) << 48) | \
     ((uint64_t)(uint8_t)((buf)[7]) << 56))

/**
 * virReadBufInt32BE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 4 bytes at BUF as a big-endian 32-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt32BE(buf) \
    (((uint32_t)(uint8_t)((buf)[0]) << 24) | \
     ((uint32_t)(uint8_t)((buf)[1]) << 16) | \
     ((uint32_t)(uint8_t)((buf)[2]) << 8) | \
     (uint32_t)(uint8_t)((buf)[3]))

/**
 * virReadBufInt32LE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 4 bytes at BUF as a little-endian 32-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt32LE(buf) \
    ((uint32_t)(uint8_t)((buf)[0]) | \
     ((uint32_t)(uint8_t)((buf)[1]) << 8) | \
     ((uint32_t)(uint8_t)((buf)[2]) << 16) | \
     ((uint32_t)(uint8_t)((buf)[3]) << 24))

/**
 * virReadBufInt16BE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 2 bytes at BUF as a big-endian 16-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt16BE(buf) \
    (((uint16_t)(uint8_t)((buf)[0]) << 8) | \
     (uint16_t)(uint8_t)((buf)[1]))

/**
 * virReadBufInt16LE:
 * @buf: byte to start reading at (can be 'char*' or 'unsigned char*');
 *       evaluating buf must not have any side effects
 *
 * Read 2 bytes at BUF as a little-endian 16-bit number.  Caller is
 * responsible to avoid reading beyond array bounds.
 */
#define virReadBufInt16LE(buf) \
    ((uint16_t)(uint8_t)((buf)[0]) | \
     ((uint16_t)(uint8_t)((buf)[1]) << 8))
