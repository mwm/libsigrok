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

#define DEFAULT_NUM_LOGIC_CHANNELS	8
#define DEFAULT_NUM_ANALOG_CHANNELS	4

#define DEFAULT_ANALOG_AMPLITUDE	10

static const char *logic_pattern_str[] = {
	"sigrok",
	"random",
	"incremental",
	"all-low",
	"all-high",
};

static const uint32_t drvopts[] = {
	SR_CONF_DEMO_DEV,
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
	SR_CONF_NUM_ANALOG_CHANNELS,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AVERAGING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_logic[] = {
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog_group[] = {
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog_channel[] = {
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_GHZ(1),
	SR_HZ(1),
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg, *acg;
	struct sr_config *src;
	struct analog_gen *ag;
	GSList *l;
	int num_logic_channels, num_analog_channels, pattern, i;
	char channel_name[16];

	num_logic_channels = DEFAULT_NUM_LOGIC_CHANNELS;
	num_analog_channels = DEFAULT_NUM_ANALOG_CHANNELS;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_NUM_LOGIC_CHANNELS:
			num_logic_channels = g_variant_get_int32(src->data);
			break;
		case SR_CONF_NUM_ANALOG_CHANNELS:
			num_analog_channels = g_variant_get_int32(src->data);
			break;
		}
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup("Demo device");

	devc = g_malloc0(sizeof(struct dev_context));
	devc->cur_samplerate = SR_KHZ(200);
	devc->num_logic_channels = num_logic_channels;
	devc->logic_unitsize = (devc->num_logic_channels + 7) / 8;
	devc->logic_pattern = PATTERN_SIGROK;
	devc->num_analog_channels = num_analog_channels;

	if (num_logic_channels > 0) {
		/* Logic channels, all in one channel group. */
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");
		for (i = 0; i < num_logic_channels; i++) {
			sprintf(channel_name, "D%d", i);
			ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);
	}

	/* Analog channels, channel groups and pattern generators. */
	if (num_analog_channels > 0) {
		pattern = 0;
		/* An "Analog" channel group with all analog channels in it. */
		acg = g_malloc0(sizeof(struct sr_channel_group));
		acg->name = g_strdup("Analog");
		sdi->channel_groups = g_slist_append(sdi->channel_groups, acg);

		devc->ch_ag = g_hash_table_new(g_direct_hash, g_direct_equal);
		for (i = 0; i < num_analog_channels; i++) {
			snprintf(channel_name, 16, "A%d", i);
			ch = sr_channel_new(sdi, i + num_logic_channels, SR_CHANNEL_ANALOG,
					TRUE, channel_name);
			acg->channels = g_slist_append(acg->channels, ch);

			/* Every analog channel gets its own channel group as well. */
			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup(channel_name);
			cg->channels = g_slist_append(NULL, ch);
			sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

			/* Every channel gets a generator struct. */
			ag = g_malloc(sizeof(struct analog_gen));
			ag->amplitude = DEFAULT_ANALOG_AMPLITUDE;
			sr_analog_init(&ag->packet, &ag->encoding, &ag->meaning, &ag->spec, 2);
			ag->packet.meaning->channels = cg->channels;
			ag->packet.meaning->mq = 0;
			ag->packet.meaning->mqflags = 0;
			ag->packet.meaning->unit = SR_UNIT_VOLT;
			ag->packet.data = ag->pattern_data;
			ag->pattern = pattern;
			ag->avg_val = 0.0f;
			ag->num_avgs = 0;
			g_hash_table_insert(devc->ch_ag, ch, ag);

			if (++pattern == ARRAY_SIZE(analog_pattern_str))
				pattern = 0;
		}
	}

	sdi->priv = devc;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_open(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;
	GHashTableIter iter;
	void *value;

	devc = priv;

	/* Analog generators. */
	g_hash_table_iter_init(&iter, devc->ch_ag);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		g_free(value);
	g_hash_table_unref(devc->ch_ag);
	g_free(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, clear_helper);
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct analog_gen *ag;
	int pattern;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_AVERAGING:
		*data = g_variant_new_boolean(devc->avg);
		break;
	case SR_CONF_AVG_SAMPLES:
		*data = g_variant_new_uint64(devc->avg_samples);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		/* Any channel in the group will do. */
		ch = cg->channels->data;
		if (ch->type == SR_CHANNEL_LOGIC) {
			pattern = devc->logic_pattern;
			*data = g_variant_new_string(logic_pattern_str[pattern]);
		} else if (ch->type == SR_CHANNEL_ANALOG) {
			ag = g_hash_table_lookup(devc->ch_ag, ch);
			pattern = ag->pattern;
			*data = g_variant_new_string(analog_pattern_str[pattern]);
		} else
			return SR_ERR_BUG;
		break;
	case SR_CONF_AMPLITUDE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		/* Any channel in the group will do. */
		ch = cg->channels->data;
		if (ch->type != SR_CHANNEL_ANALOG)
			return SR_ERR_ARG;
		ag = g_hash_table_lookup(devc->ch_ag, ch);
		*data = g_variant_new_double(ag->amplitude);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct analog_gen *ag;
	struct sr_channel *ch;
	GSList *l;
	int logic_pattern, analog_pattern, ret;
	unsigned int i;
	const char *stropt;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_msec = 0;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		devc->limit_samples = 0;
		break;
	case SR_CONF_AVERAGING:
		devc->avg = g_variant_get_boolean(data);
		sr_dbg("%s averaging", devc->avg ? "Enabling" : "Disabling");
		break;
	case SR_CONF_AVG_SAMPLES:
		devc->avg_samples = g_variant_get_uint64(data);
		sr_dbg("Setting averaging rate to %" PRIu64, devc->avg_samples);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		stropt = g_variant_get_string(data, NULL);
		logic_pattern = analog_pattern = -1;
		for (i = 0; i < ARRAY_SIZE(logic_pattern_str); i++) {
			if (!strcmp(stropt, logic_pattern_str[i])) {
				logic_pattern = i;
				break;
			}
		}
		for (i = 0; i < ARRAY_SIZE(analog_pattern_str); i++) {
			if (!strcmp(stropt, analog_pattern_str[i])) {
				analog_pattern = i;
				break;
			}
		}
		if (logic_pattern == -1 && analog_pattern == -1)
			return SR_ERR_ARG;
		for (l = cg->channels; l; l = l->next) {
			ch = l->data;
			if (ch->type == SR_CHANNEL_LOGIC) {
				if (logic_pattern == -1)
					return SR_ERR_ARG;
				sr_dbg("Setting logic pattern to %s",
						logic_pattern_str[logic_pattern]);
				devc->logic_pattern = logic_pattern;
				/* Might as well do this now, these are static. */
				if (logic_pattern == PATTERN_ALL_LOW)
					memset(devc->logic_data, 0x00, LOGIC_BUFSIZE);
				else if (logic_pattern == PATTERN_ALL_HIGH)
					memset(devc->logic_data, 0xff, LOGIC_BUFSIZE);
			} else if (ch->type == SR_CHANNEL_ANALOG) {
				if (analog_pattern == -1)
					return SR_ERR_ARG;
				sr_dbg("Setting analog pattern for channel %s to %s",
						ch->name, analog_pattern_str[analog_pattern]);
				ag = g_hash_table_lookup(devc->ch_ag, ch);
				ag->pattern = analog_pattern;
			} else
				return SR_ERR_BUG;
		}
		break;
	case SR_CONF_AMPLITUDE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		for (l = cg->channels; l; l = l->next) {
			ch = l->data;
			if (ch->type != SR_CHANNEL_ANALOG)
				return SR_ERR_ARG;
			ag = g_hash_table_lookup(devc->ch_ag, ch);
			ag->amplitude = g_variant_get_double(data);
		}
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	GVariant *gvar;
	GVariantBuilder gvb;

	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;

	if (!cg) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			break;
		case SR_CONF_SAMPLERATE:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
			gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
					ARRAY_SIZE(samplerates), sizeof(uint64_t));
			g_variant_builder_add(&gvb, "{sv}", "samplerate-steps", gvar);
			*data = g_variant_builder_end(&gvb);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			if (ch->type == SR_CHANNEL_LOGIC)
				*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
						devopts_cg_logic, ARRAY_SIZE(devopts_cg_logic),
						sizeof(uint32_t));
			else if (ch->type == SR_CHANNEL_ANALOG) {
				if (strcmp(cg->name, "Analog") == 0)
					*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
							devopts_cg_analog_group, ARRAY_SIZE(devopts_cg_analog_group),
							sizeof(uint32_t));
				else
					*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
							devopts_cg_analog_channel, ARRAY_SIZE(devopts_cg_analog_channel),
							sizeof(uint32_t));
			}
			else
				return SR_ERR_BUG;
			break;
		case SR_CONF_PATTERN_MODE:
			/* The analog group (with all 4 channels) shall not have a pattern property. */
			if (strcmp(cg->name, "Analog") == 0)
				return SR_ERR_NA;

			if (ch->type == SR_CHANNEL_LOGIC)
				*data = g_variant_new_strv(logic_pattern_str,
						ARRAY_SIZE(logic_pattern_str));
			else if (ch->type == SR_CHANNEL_ANALOG)
				*data = g_variant_new_strv(analog_pattern_str,
						ARRAY_SIZE(analog_pattern_str));
			else
				return SR_ERR_BUG;
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GHashTableIter iter;
	void *value;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->sent_samples = 0;

	g_hash_table_iter_init(&iter, devc->ch_ag);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		demo_generate_analog_pattern(value, devc->cur_samplerate);

	sr_session_source_add(sdi->session, -1, 0, 100,
			demo_prepare_data, (struct sr_dev_inst *)sdi);

	std_session_send_df_header(sdi);

	/* We use this timestamp to decide how many more samples to send. */
	devc->start_us = g_get_monotonic_time();
	devc->spent_us = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_dbg("Stopping acquisition.");
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver demo_driver_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(demo_driver_info);
