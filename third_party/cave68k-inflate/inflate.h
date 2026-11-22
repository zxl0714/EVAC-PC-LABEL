/**
 *    Name:        inflate.h
 *    Description: DEFLATE (RFC 1951) decompression, for the .zip of the player's ROM set (inflate.c)
 *    Version:     0.1.0
 *    Created:     25-10-2026
 *    Modified:    25-10-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#ifndef GAME_INFLATE_H
#define GAME_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* The raw DEFLATE stream 'in' into 'out', which takes exactly 'out_size' bytes: 1 when it does */
int inflate_raw(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

#endif
