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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define ANALOG_SAMPLES_PER_PERIOD 20

static const uint8_t pattern_sigrok[] = {
	0x4c, 0x92, 0x92, 0x92, 0x64, 0x00, 0x00, 0x00,
	0x82, 0xfe, 0xfe, 0x82, 0x00, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x92, 0x74, 0x00, 0x00, 0x00,
	0xfe, 0x12, 0x12, 0x32, 0xcc, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x82, 0x7c, 0x00, 0x00, 0x00,
	0xfe, 0x10, 0x28, 0x44, 0x82, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xbe, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

SR_PRIV void demo_generate_analog_pattern(struct analog_gen *ag, uint64_t sample_rate)
{
	double t, frequency;
	float value;
	unsigned int num_samples, i;
	int last_end;

	sr_dbg("Generating %s pattern.", analog_pattern_str[ag->pattern]);

	num_samples = ANALOG_BUFSIZE / sizeof(float);

	switch (ag->pattern) {
	case PATTERN_SQUARE:
		value = ag->amplitude;
		last_end = 0;
		for (i = 0; i < num_samples; i++) {
			if (i % 5 == 0)
				value = -value;
			if (i % 10 == 0)
				last_end = i;
			ag->pattern_data[i] = value;
		}
		ag->num_samples = last_end;
		break;
	case PATTERN_SINE:
		frequency = (double) sample_rate / ANALOG_SAMPLES_PER_PERIOD;

		/* Make sure the number of samples we put out is an integer
		 * multiple of our period size */
		/* FIXME we actually need only one period. A ringbuffer would be
		 * useful here. */
		while (num_samples % ANALOG_SAMPLES_PER_PERIOD != 0)
			num_samples--;

		for (i = 0; i < num_samples; i++) {
			t = (double) i / (double) sample_rate;
			ag->pattern_data[i] = ag->amplitude *
						sin(2 * G_PI * frequency * t);
		}

		ag->num_samples = num_samples;
		break;
	case PATTERN_TRIANGLE:
		frequency = (double) sample_rate / ANALOG_SAMPLES_PER_PERIOD;

		while (num_samples % ANALOG_SAMPLES_PER_PERIOD != 0)
			num_samples--;

		for (i = 0; i < num_samples; i++) {
			t = (double) i / (double) sample_rate;
			ag->pattern_data[i] = (2 * ag->amplitude / G_PI) *
						asin(sin(2 * G_PI * frequency * t));
		}

		ag->num_samples = num_samples;
		break;
	case PATTERN_SAWTOOTH:
		frequency = (double) sample_rate / ANALOG_SAMPLES_PER_PERIOD;

		while (num_samples % ANALOG_SAMPLES_PER_PERIOD != 0)
			num_samples--;

		for (i = 0; i < num_samples; i++) {
			t = (double) i / (double) sample_rate;
			ag->pattern_data[i] = 2 * ag->amplitude *
						((t * frequency) - floor(0.5f + t * frequency));
		}

		ag->num_samples = num_samples;
		break;
	}
}

static void logic_generator(struct sr_dev_inst *sdi, uint64_t size)
{
	struct dev_context *devc;
	uint64_t i, j;
	uint8_t pat;

	devc = sdi->priv;

	switch (devc->logic_pattern) {
	case PATTERN_SIGROK:
		memset(devc->logic_data, 0x00, size);
		for (i = 0; i < size; i += devc->logic_unitsize) {
			for (j = 0; j < devc->logic_unitsize; j++) {
				pat = pattern_sigrok[(devc->step + j) % sizeof(pattern_sigrok)] >> 1;
				devc->logic_data[i + j] = ~pat;
			}
			devc->step++;
		}
		break;
	case PATTERN_RANDOM:
		for (i = 0; i < size; i++)
			devc->logic_data[i] = (uint8_t)(rand() & 0xff);
		break;
	case PATTERN_INC:
		for (i = 0; i < size; i++) {
			for (j = 0; j < devc->logic_unitsize; j++) {
				devc->logic_data[i + j] = devc->step;
			}
			devc->step++;
		}
		break;
	case PATTERN_ALL_LOW:
	case PATTERN_ALL_HIGH:
		/* These were set when the pattern mode was selected. */
		break;
	default:
		sr_err("Unknown pattern: %d.", devc->logic_pattern);
		break;
	}
}

static void send_analog_packet(struct analog_gen *ag,
		struct sr_dev_inst *sdi, uint64_t *analog_sent,
		uint64_t analog_pos, uint64_t analog_todo)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;
	uint64_t sending_now, to_avg;
	int ag_pattern_pos;
	unsigned int i;

	devc = sdi->priv;
	packet.type = SR_DF_ANALOG;
	packet.payload = &ag->packet;

	if (!devc->avg) {
		ag_pattern_pos = analog_pos % ag->num_samples;
		sending_now = MIN(analog_todo, ag->num_samples-ag_pattern_pos);
		ag->packet.data = ag->pattern_data + ag_pattern_pos;
		ag->packet.num_samples = sending_now;
		sr_session_send(sdi, &packet);

		/* Whichever channel group gets there first. */
		*analog_sent = MAX(*analog_sent, sending_now);
	} else {
		ag_pattern_pos = analog_pos % ag->num_samples;
		to_avg = MIN(analog_todo, ag->num_samples-ag_pattern_pos);

		for (i = 0; i < to_avg; i++) {
			ag->avg_val = (ag->avg_val +
					*(ag->pattern_data +
					  ag_pattern_pos + i)) / 2;
			ag->num_avgs++;
			/* Time to send averaged data? */
			if (devc->avg_samples > 0 &&
			    ag->num_avgs >= devc->avg_samples)
				goto do_send;
		}

		if (devc->avg_samples == 0) {
			/* We're averaging all the samples, so wait with
			 * sending until the very end.
			 */
			*analog_sent = ag->num_avgs;
			return;
		}

do_send:
		ag->packet.data = &ag->avg_val;
		ag->packet.num_samples = 1;

		sr_session_send(sdi, &packet);
		*analog_sent = ag->num_avgs;

		ag->num_avgs = 0;
		ag->avg_val = 0.0f;
	}
}

/* Callback handling data */
SR_PRIV int demo_prepare_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct analog_gen *ag;
	GHashTableIter iter;
	void *value;
	uint64_t samples_todo, logic_done, analog_done, analog_sent, sending_now;
	int64_t elapsed_us, limit_us, todo_us;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	/* Just in case. */
	if (devc->cur_samplerate <= 0
			|| (devc->num_logic_channels <= 0
			&& devc->num_analog_channels <= 0)) {
		sdi->driver->dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	/* What time span should we send samples for? */
	elapsed_us = g_get_monotonic_time() - devc->start_us;
	limit_us = 1000 * devc->limit_msec;
	if (limit_us > 0 && limit_us < elapsed_us)
		todo_us = MAX(0, limit_us - devc->spent_us);
	else
		todo_us = MAX(0, elapsed_us - devc->spent_us);

	/* How many samples are outstanding since the last round? */
	samples_todo = (todo_us * devc->cur_samplerate + G_USEC_PER_SEC - 1)
			/ G_USEC_PER_SEC;
	if (devc->limit_samples > 0) {
		if (devc->limit_samples < devc->sent_samples)
			samples_todo = 0;
		else if (devc->limit_samples - devc->sent_samples < samples_todo)
			samples_todo = devc->limit_samples - devc->sent_samples;
	}
	/* Calculate the actual time covered by this run back from the sample
	 * count, rounded towards zero. This avoids getting stuck on a too-low
	 * time delta with no samples being sent due to round-off.
	 */
	todo_us = samples_todo * G_USEC_PER_SEC / devc->cur_samplerate;

	logic_done  = devc->num_logic_channels  > 0 ? 0 : samples_todo;
	analog_done = devc->num_analog_channels > 0 ? 0 : samples_todo;

	while (logic_done < samples_todo || analog_done < samples_todo) {
		/* Logic */
		if (logic_done < samples_todo) {
			sending_now = MIN(samples_todo - logic_done,
					LOGIC_BUFSIZE / devc->logic_unitsize);
			logic_generator(sdi, sending_now * devc->logic_unitsize);
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = sending_now * devc->logic_unitsize;
			logic.unitsize = devc->logic_unitsize;
			logic.data = devc->logic_data;
			sr_session_send(sdi, &packet);
			logic_done += sending_now;
		}

		/* Analog, one channel at a time */
		if (analog_done < samples_todo) {
			analog_sent = 0;

			g_hash_table_iter_init(&iter, devc->ch_ag);
			while (g_hash_table_iter_next(&iter, NULL, &value)) {
				send_analog_packet(value, sdi, &analog_sent,
						devc->sent_samples + analog_done,
						samples_todo - analog_done);
			}
			analog_done += analog_sent;
		}
	}
	/* At this point, both logic_done and analog_done should be
	 * exactly equal to samples_todo, or else.
	 */
	if (logic_done != samples_todo || analog_done != samples_todo) {
		sr_err("BUG: Sample count mismatch.");
		return G_SOURCE_REMOVE;
	}
	devc->sent_samples += samples_todo;
	devc->spent_us += todo_us;

	if ((devc->limit_samples > 0 && devc->sent_samples >= devc->limit_samples)
			|| (limit_us > 0 && devc->spent_us >= limit_us)) {

		/* If we're averaging everything - now is the time to send data */
		if (devc->avg_samples == 0) {
			g_hash_table_iter_init(&iter, devc->ch_ag);
			while (g_hash_table_iter_next(&iter, NULL, &value)) {
				ag = value;
				packet.type = SR_DF_ANALOG;
				packet.payload = &ag->packet;
				ag->packet.data = &ag->avg_val;
				ag->packet.num_samples = 1;
				sr_session_send(sdi, &packet);
			}
		}
		sr_dbg("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi);
	}

	return G_SOURCE_CONTINUE;
}
