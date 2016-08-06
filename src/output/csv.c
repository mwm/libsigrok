/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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
 *
 * Options and their values:
 *
 * Xsigrok:  Set the formatting options to the proper value for sigrok's
 *          csv input routines.
 *
 * Xgnuplot: Write out a gnuplot intrerpreter script (.gpi file) to plot
 *          the datafile using the parameters given.
 *
 * value:   The string to use to separate values in a record. Defaults to ','.
 *
 * record:  The string to use to separate records. Default is newline. gnuplot
 *          files must use newline.
 *
 * frame:   The string to use when a frame ends. The default is a blank line.
 *          This may confuse some csv parsers, but it makes gnuplot happy.
 *
 * comment: The string that starts a comment line. Defaults to ';'.
 *
 * header:  Print header comment with capture metadata.
 *
 * label:   Add a line of channel labels as the first line of output.
 *
 * time:    Whether or not the first column should include the time the sample
 *          was taken. Defaults to TRUE.
 *
 * dedup:   Don't output duplicate rows. Defaults to TRUE. If time is off, then
 *          this is forced to be off.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/csv"

struct context {
        /* Options */
        const char *gnuplot;
	const char *value;
        const char *record;
        const char *frame;
        const char *comment;
        gboolean header;
        gboolean label;
        gboolean time;
        gboolean dedup;

        /* Plot data */
	unsigned num_analog_channels;
        unsigned num_logic_channels;
	struct sr_channel **channels;

        /* Metadata */
	uint64_t period;
        uint64_t sample_time;
        uint8_t *previous_sample;
        float *analog_samples;
        uint8_t *logic_samples;
        uint32_t channels_seen, num_samples;
        const char *xlabel;    // Don't free: will point to a static string.
        const char *title;     // Don't free: will point into the driver structure.
};

/*
 * TODO:
 *  - Option to print comma-separated bits, or whole bytes/words (for 8/16
 *    channel LAs) as ASCII/hex etc. etc.
 *  - Trigger support.
 */

static int init(struct sr_output *o, GHashTable *options)
{
        unsigned i, analog_channels, logic_channels;
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;

	(void)options;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	ctx = g_malloc0(sizeof(struct context));
	o->priv = ctx;

        /* Options */
        ctx->gnuplot = g_strdup(
                g_variant_get_string(
                        g_hash_table_lookup(options, "gnuplot"), NULL));
	ctx->value = g_strdup(
                g_variant_get_string(g_hash_table_lookup(options, "value"), NULL));
	ctx->record = g_strdup(
                g_variant_get_string(
                        g_hash_table_lookup(options, "record"), NULL));
	ctx->frame = g_strdup(
                g_variant_get_string(g_hash_table_lookup(options, "frame"), NULL));
	ctx->comment = g_strdup(
                g_variant_get_string(
                        g_hash_table_lookup(options, "comment"), NULL));
	ctx->header = g_variant_get_boolean(
                g_hash_table_lookup(options, "header"));
	ctx->time = g_variant_get_boolean(g_hash_table_lookup(options, "time"));
	ctx->label = g_variant_get_boolean(g_hash_table_lookup(options, "label"));
	ctx->dedup = g_variant_get_boolean(
             g_hash_table_lookup(options, "dedup"));
        ctx->dedup &= ctx->time;

        if (*ctx->gnuplot && !strcmp(ctx->record, "\n")) {
                sr_warn("gnuplot record separator must be newline");
                g_free((gpointer) ctx->record);
                ctx->record = g_strdup("\n");
        }

        sr_dbg("Gnuplot = '%s', value = '%s', record = '%s', frame = '%s'",
               ctx->gnuplot, ctx->value, ctx->record, ctx->frame);
        sr_dbg("comment = '%s', header = %d, label = %d, time = %d, dedup = %d",
               ctx->comment, ctx->header, ctx->label, ctx->time, ctx->dedup);

        analog_channels = logic_channels = 0;
	/* Get the number of channels, and the unitsize. */
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->enabled) {
			if (ch->type == SR_CHANNEL_LOGIC)
				logic_channels += 1;
			if (ch->type == SR_CHANNEL_ANALOG)
				analog_channels += 1;
		}
	}
        if (analog_channels) {
                sr_info("Outputting %d analog values", analog_channels);
                ctx->num_analog_channels = analog_channels;
        }
        if (logic_channels) {
                sr_info("Outputting %d logic values", logic_channels);
                ctx->num_logic_channels = logic_channels;
        }
	ctx->channels = g_malloc(
                sizeof(struct sr_channel *)
                * (ctx->num_analog_channels + ctx->num_logic_channels));


	/* Once more to map the enabled channels. */
	for (i = 0, l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->enabled)
                        ctx->channels[i++] = ch;
	}

	return SR_OK;
}

static const char *xlabels[] = {
        "samples", "milliseconds", "microseconds", "nanoseconds", "picoseconds",
        "femtoseconds", "attoseconds"
} ;

static GString *gen_header(
        const struct sr_output *o, const struct sr_datafeed_header *hdr)
{
	struct context *ctx;
	struct sr_channel *ch;
	GVariant *gvar;
	GString *header;
	GSList *l;
	unsigned num_channels, i;
        uint64_t samplerate = 0, sr;
	char *samplerate_s;

	ctx = o->priv;
	header = g_string_sized_new(512);

        if (ctx->period == 0) {
                if (sr_config_get(o->sdi->driver, o->sdi, NULL,
                                  SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
                        samplerate = g_variant_get_uint64(gvar);
                        g_variant_unref(gvar);
                }

                i = 0;
                sr = 1;
                while (sr < samplerate) {
                        i += 1;
                        sr *= 1000;
                }
                if (samplerate)
                        ctx->period = sr / samplerate;
                if (i < ARRAY_SIZE(xlabels)) {
                        ctx->xlabel = xlabels[i];
                }
                sr_info("Set sample period to %" PRIu64 " %s",
                       ctx->period, ctx->xlabel);
        }
        ctx->title = o->sdi->driver->longname;

	/* Some metadata */
        if (ctx->header) {
                g_string_append_printf(
                        header, "%s CSV generated by %s %s\n%s from %s on %s",
                        ctx->comment, PACKAGE_NAME, SR_PACKAGE_VERSION_STRING,
                        ctx->comment, ctx->title, ctime(&hdr->starttime.tv_sec));

                /* Columns / channels */
                num_channels = g_slist_length(o->sdi->channels);
                g_string_append_printf(
                        header, "%s Channels (%d/%d):", ctx->comment,
                        ctx->num_analog_channels + ctx->num_logic_channels,
                        num_channels);
                for (i = 0, l = o->sdi->channels; l; l = l->next, i++) {
                        ch = l->data;
                        if (ch->enabled)
                                g_string_append_printf(header, " %s,", ch->name);
                }
                if (o->sdi->channels)
                        /* Drop last separator. */
                        g_string_truncate(header, header->len - 1);
                g_string_append_printf(header, "\n");
                if (samplerate != 0) {
                        samplerate_s = sr_samplerate_string(samplerate);
                        g_string_append_printf(
                                header, "%s Samplerate: %s\n", ctx->comment,
                                samplerate_s);
                        g_free(samplerate_s);
                }
        }

	return header;
}

/*
 * Analog devices can have samples of different types. Since each
 * packet has only one meaning, it is restricted to having at most one
 * type of data. So they can send multiple packets for a single sample.
 * To further complicate things, they can send multiple samples in a
 * single packet.
 *
 * So we need to pull any channels of interest out of a packet and save
 * them until we have complete samples to output. Some devices make this
 * simple by sending DF_FRAME_BEGIN/DF_FRAME_END packets, the latter of which
 * signals the end of a set of samples, so we can dump things there.
 *
 * At least one driver (the demo driver) sends packets that contain parts of
 * multiple samples without wrapping them in DF_FRAME. Possibly this driver
 * is buggy, but it's also the standard for testing, so it has to be supported
 * as is.
 *
 * Many assumptions about the "shape" of the data here:
 *
 * All of the data for a channel is assumed to be in one frame;
 * otherwise the data in the second packet will overwrite the data in
 * the first packet.
 */
static void process_analog(struct context *ctx,
                          const struct sr_datafeed_analog *analog)
{
        int ret;
        unsigned i, j, c, num_channels;
        struct sr_analog_meaning *meaning;
        GSList *l;
        float *fdata = NULL;

        if (!ctx->analog_samples) {
                ctx->analog_samples = g_malloc(analog->num_samples * sizeof(float)
                                               * ctx->num_analog_channels);
                if (!ctx->num_samples)
                        ctx->num_samples = analog->num_samples;
        }
        if (ctx->num_samples != analog->num_samples) {
                sr_warn("Expecting %u analog samples, got %u", ctx->num_samples,
                        analog->num_samples);
        }

        meaning = analog->meaning;
        num_channels = g_slist_length(meaning->channels);
        sr_dbg("Processing packet of %u analog channels", num_channels);
        fdata = g_malloc(analog->num_samples * num_channels);
        if ((ret = sr_analog_to_float(analog, fdata)) != SR_OK) {
                sr_warn("Problems converting data to floating point values.");
        }

        for (i=0; i < ctx->num_analog_channels + ctx->num_logic_channels; i += 1){
                sr_dbg("Looking for channel %s", ctx->channels[i]->name);
                if (ctx->channels[i]->type == SR_CHANNEL_ANALOG) {
                        for (l=meaning->channels, c=0; l; l=l->next, c += 1) {
                                struct sr_channel *ch = l->data;
                                sr_dbg("Checking %s", ch->name);
                                if (ctx->channels[i] == l->data) {
                                        ctx->channels_seen += 1;
                                        sr_dbg("Seen %u of %u channels in analog",
                                               ctx->channels_seen,
                                               ctx->num_analog_channels +
                                               ctx->num_logic_channels);
                                        for (j=0; j < analog->num_samples; j += 1){
                                                ctx->analog_samples[j * ctx->num_analog_channels + i] = fdata[j * num_channels + c];
                                        }
                                        break;
                                }
                        }
                }
        }
        g_free(fdata);
}

/*
 * We treat logic packets the same as analog packets, though it's not
 * strictly required. This allows us to process mixed signals properly.
 */
static void process_logic(
        struct context *ctx, const struct sr_datafeed_logic *logic)
{
        unsigned i, j, ch, num_samples;
	int idx;
        uint8_t *sample;

        num_samples = logic->length / logic->unitsize;
        if (!ctx->logic_samples) {
                ctx->logic_samples = g_malloc(
                        num_samples * ctx->num_logic_channels);
                if (!ctx->num_samples)
                        ctx->num_samples = num_samples;
        } 
        if (ctx->num_samples != num_samples) {
                sr_warn("Expecting %u samples, got %u", ctx->num_samples,
                        num_samples);
        }

        for (j = ch = 0; ch < ctx->num_logic_channels; j += 1) {
                if (ctx->channels[j]->type == SR_CHANNEL_LOGIC) {
                        ctx->channels_seen += 1;
                        sr_dbg("Seen %u of %u channels in logic",
                               ctx->channels_seen, ctx->num_analog_channels +
                               ctx->num_logic_channels);
                        for (i = 0; i <= logic->length - logic->unitsize;
                             i += logic->unitsize) {
                                sample = logic->data + i;
                                idx = ctx->channels[ch]->index;
                                ctx->logic_samples[
                                        i * ctx->num_logic_channels + ch] =
                                        sample[ idx / 8] & (1 << (idx % 8));
                        }
                        ch += 1;
                }
        }
}

static void dump_saved_values(struct context *ctx, GString **out)
{
        unsigned i, j, analog_size, num_channels;
        float *analog_sample;
        uint8_t *logic_sample;

        /* If we haven't seen samples we're expecting, skip them */
        if ((ctx->num_analog_channels && !ctx->analog_samples) || 
            (ctx->num_logic_channels && !ctx->logic_samples)) {
                sr_warn("Discarding partial packet");
        } else {
                sr_info("Dumping %u samples", ctx->num_samples);

                *out = g_string_sized_new(512);
                num_channels = ctx->num_logic_channels + ctx->num_analog_channels;

                if (ctx->label) {
                        if (ctx->time) {
                                g_string_append_printf(*out, "Time%s", ctx->value);
                        }
                        for (i = 0; i < num_channels; i += 1) {
                                g_string_append_printf(
                                        *out, "%s%s", ctx->channels[i]->name,
                                        ctx->value);
                        }
                        /* Drop last separator. */
                        g_string_truncate(*out, (*out)->len - strlen(ctx->value));
                        g_string_append(*out, ctx->record);

                        ctx->label = FALSE;
                }

                analog_size = ctx->num_analog_channels * sizeof(float);
                if (ctx->dedup && !ctx->previous_sample) {
                        ctx->previous_sample = g_malloc0(
                                analog_size + ctx->num_logic_channels);
                }

                for (i = 0; i < ctx->num_samples; i += 1) {
                        ctx->sample_time += ctx->period;
                        analog_sample = 
                                &ctx->analog_samples[i * ctx->num_analog_channels];
                        logic_sample =
                                &ctx->logic_samples[i * ctx->num_logic_channels];

                        if (ctx->dedup) {
                                if (i > 0 && i < ctx->num_samples - 1 &&
                                    !memcmp(logic_sample, ctx->previous_sample, 
                                            ctx->num_logic_channels) &&
                                    !memcmp(analog_sample, 
                                            ctx->previous_sample +
                                            ctx->num_logic_channels,
                                            analog_size))
                                        continue;
                                memcpy(ctx->previous_sample, logic_sample,
                                       ctx->num_logic_channels);
                                memcpy(ctx->previous_sample
                                       + ctx->num_logic_channels,
                                       analog_sample, analog_size);
                        }

                        if (ctx->time) {
                                g_string_append_printf(
                                        *out ,"%lu%s", ctx->sample_time, 
                                        ctx->value);
                        }
                        for (j = 0; j < num_channels; j += 1) {
                                if (ctx->channels[j]->type == SR_CHANNEL_ANALOG)
                                        g_string_append_printf(
                                                *out, "%f%s",
                                                ctx->analog_samples[
                                                        i * ctx->num_analog_channels + j],
                                                ctx->value);
                                else if (ctx->channels[j]->type == 
                                         SR_CHANNEL_LOGIC) {
                                        g_string_append_printf(
                                                *out, "%c%s",
                                                ctx->logic_samples[
                                                        i * ctx->num_logic_channels + j]
                                                ? '1' : '0',
                                                ctx->value);
                                } else {
                                        sr_warn("Unknown channel type in data");
                                }
                        }
                        g_string_truncate(*out, (*out)->len - strlen(ctx->value));
                        g_string_append(*out, ctx->record);
                }
        }

        /* Discard all of the working space */
        g_free(ctx->previous_sample);
        g_free(ctx->analog_samples);
        g_free(ctx->logic_samples);
        ctx->channels_seen = 0;
        ctx->num_samples = 0;
        ctx->previous_sample = NULL;
        ctx->analog_samples = NULL;
        ctx->logic_samples = NULL;
}

static int receive(
        const struct sr_output *o, const struct sr_datafeed_packet *packet,
        GString **out)
{
	struct context *ctx;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->priv))
		return SR_ERR_ARG;

        sr_dbg("Got packet of type %d", packet->type);
	switch (packet->type) {
        case SR_DF_HEADER:
                *out = gen_header(o, packet->payload);
                break;
	case SR_DF_LOGIC:
                process_logic(ctx, packet->payload);
		break;
	case SR_DF_ANALOG:
                process_analog(ctx, packet->payload);
		break;
	case SR_DF_FRAME_BEGIN:
                *out = g_string_new(ctx->frame);
                /* And then fall through to */
        case SR_DF_END:
                /* Got to end of frame/session with part of the data */
                if (ctx->channels_seen)
                        ctx->channels_seen = ctx->num_analog_channels +
                                ctx->num_logic_channels;
                break;
	}

        /* If we've got them all, dump the values */
        if (ctx->channels_seen >=
            ctx->num_analog_channels + ctx->num_logic_channels) {
                dump_saved_values(ctx, out);
        }
	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	if (o->priv) {
		ctx = o->priv;
                g_free((gpointer) ctx->value);
                g_free((gpointer) ctx->record);
                g_free((gpointer) ctx->frame);
                g_free((gpointer) ctx->comment);
                g_free((gpointer) ctx->gnuplot);
                g_free(ctx->previous_sample);
		g_free(ctx->channels);
		g_free(o->priv);
		o->priv = NULL;
	}

	return SR_OK;
}

static struct sr_option options[] = {
     {"sigrok", "sigrok", "Set options properly for sigrok csv input", NULL, NULL},
     {"gnuplot", "gnuplot", "gnuplot script file name", NULL, NULL},
     {"value", "Value separator", "String to print between values", NULL, NULL},
     {"record", "Record separator", "String to print between records", NULL, NULL},
     {"frame", "Frame seperator", "String to print between frames", NULL, NULL},
     {"comment", "Comment start string",
      "String used at start of comment lines", NULL, NULL},
     {"header", "Output header", "Output header comment with capture metdata",
      NULL, NULL},
     {"label", "Label values", "Output labels for each value", NULL, NULL},
     {"time", "Time column", "Output sample time as column 1", NULL, NULL},
     {"dedup", "Dedup rows", "Set to false to output duplicate rows", NULL, NULL},
     ALL_ZERO
};

static const struct sr_option *get_options(void)
{
        if (!options[0].def) {
                options[0].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
                options[1].def = g_variant_ref_sink(g_variant_new_string(""));
                options[2].def = g_variant_ref_sink(g_variant_new_string(","));
                options[3].def = g_variant_ref_sink(g_variant_new_string("\n"));
                options[4].def = g_variant_ref_sink(g_variant_new_string("\n"));
                options[5].def = g_variant_ref_sink(g_variant_new_string(";"));
                options[6].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
                options[7].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
                options[8].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
                options[9].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
        }
        return options;
}

SR_PRIV struct sr_output_module output_csv = {
	.id = "csv",
	.name = "CSV",
	.desc = "Comma-separated values",
	.exts = (const char*[]){"csv", NULL},
	.flags = 0,
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
