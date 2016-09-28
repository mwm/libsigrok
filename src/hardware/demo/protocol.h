/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2015 Bartosz Golaszewski <bgolaszewski@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_DEMO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DEMO_PROTOCOL_H

#include <stdint.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "demo"

/* The size in bytes of chunks to send through the session bus. */
#define LOGIC_BUFSIZE			4096
/* Size of the analog pattern space per channel. */
#define ANALOG_BUFSIZE			4096

/* Private, per-device-instance driver context. */
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t limit_msec;
	uint64_t sent_samples;
	int64_t start_us;
	int64_t spent_us;
	uint64_t step;
	/* Logic */
	int32_t num_logic_channels;
	unsigned int logic_unitsize;
	/* There is only ever one logic channel group, so its pattern goes here. */
	uint8_t logic_pattern;
	unsigned char logic_data[LOGIC_BUFSIZE];
	/* Analog */
	int32_t num_analog_channels;
	GHashTable *ch_ag;
	gboolean avg; /* True if averaging is enabled */
	uint64_t avg_samples;
};

/* Logic patterns we can generate. */
enum {
	/**
	 * Spells "sigrok" across 8 channels using '0's (with '1's as
	 * "background") when displayed using the 'bits' output format.
	 * The pattern is repeated every 8 channels, shifted to the right
	 * in time by one bit.
	 */
	PATTERN_SIGROK,

	/** Pseudo-random values on all channels. */
	PATTERN_RANDOM,

	/**
	 * Incrementing number across 8 channels. The pattern is repeated
	 * every 8 channels, shifted to the right in time by one bit.
	 */
	PATTERN_INC,

	/** All channels have a low logic state. */
	PATTERN_ALL_LOW,

	/** All channels have a high logic state. */
	PATTERN_ALL_HIGH,
};

/* Analog patterns we can generate. */
enum {
	/**
	 * Square wave.
	 */
	PATTERN_SQUARE,
	PATTERN_SINE,
	PATTERN_TRIANGLE,
	PATTERN_SAWTOOTH,
};

static const char *analog_pattern_str[] = {
	"square",
	"sine",
	"triangle",
	"sawtooth",
};

struct analog_gen {
	int pattern;
	float amplitude;
	float pattern_data[ANALOG_BUFSIZE];
	unsigned int num_samples;
	struct sr_datafeed_analog packet;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float avg_val; /* Average value */
	unsigned num_avgs; /* Number of samples averaged */
};

SR_PRIV void demo_generate_analog_pattern(struct analog_gen *ag, uint64_t sample_rate);
SR_PRIV int demo_prepare_data(int fd, int revents, void *cb_data);

#endif
