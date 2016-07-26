/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Mike Meyer <mwm@mired.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

SR_PRIV struct sr_dev_driver jyetech_dso112a_driver_info;

static const uint32_t scanopts[] = {
     SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
        SR_CONF_CONTINUOUS,
        SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
        SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
        SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_BUFFERSIZE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
        SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t timebases[][2] = {
	/* microseconds */
        { 1, 1000000 },		/* Timebase = 30 */
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },	/* Timebase = 7 */
};

static const uint64_t vdivs[][2] = {
        /* millivolts */
	{ 2, 1000 },	/* VSen = 15 */
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },	/* VSen = 3 */
};

static const uint64_t buffersizes[] = {512, 1024};

static const char *couplings[] = {"DC", "AC", "GND"};
        
static const char *sources[] = {"INT", "EXT"};

static const char *slopes[] = {"Neg", "Pos"};

static const double poss[] = {0.125, 0.25, 0.5, 0.75, 0.875};
        

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
        const char *conn = NULL;
        const char *serialcomm = NULL;
        GSList *l;
        struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
        struct sr_dev_inst *sdi;
        uint8_t *frame;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
                }
	}
        serialcomm = SERIALCOMM;

        /* if (!conn) */
        /*         conn = SERIALCONN; */

        sr_info("Probing port %s.", conn);
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
                sr_serial_dev_inst_free(serial);
                return NULL;
        }

        /* Ask whatever's there what kind of jyetech device is is */
        if (jyetech_dso112a_send_command(serial, COMMAND_QUERY, QUERY_EXTRA)
            != SR_OK
            || !(frame = jyetech_dso112a_read_frame(serial))) {
                serial_close(serial);
                sr_serial_dev_inst_free(serial);
                return NULL;
        }

        devc = jyetech_dso112a_dev_context_new(frame);
        g_free(frame);
        if (!devc) {
                /* Not mine, let it go */
                serial_close(serial);
                sr_serial_dev_inst_free(serial);
                return NULL;
        }
        serial_close(serial);

        /* Ours, so tell everyone about it */
        sr_info("Found device on port %s.", conn);
        sr_sw_limits_init(&devc->limits);
        sdi = g_malloc0(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INACTIVE;
        sdi->vendor = g_strdup("JYETech");
        sdi->model = g_strdup(devc->description);
        sdi->inst_type = SR_INST_SERIAL;
        sdi->conn = serial;
        sdi->priv = devc;
        sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Int");
        return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, jyetech_dso112a_dev_context_free);
}

static int dev_open(struct sr_dev_inst *sdi)
{
        int status;
        struct sr_serial_dev_inst *serial;
        struct dev_context *devc;

        if (!(devc = sdi->priv))
                return SR_ERR;

        serial = sdi->conn;
	sr_info("Opening device %s.", serial->port);
        status = serial_open(serial, SERIAL_RDWR);
        if (status == SR_OK) {
                status = jyetech_dso112a_get_parameters(sdi);
                if (status != SR_OK) {
                        serial_close(serial);
                } else {
                        sdi->status = SR_ST_ACTIVE;
                }
        }
        return status;
}

static int dev_close(struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *serial = sdi->conn;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	sr_info("Closing device %s.", serial->port);
        return serial_close(serial);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        uint32_t *long_p;
        int32_t *signed_p;
        struct dev_context *devc;
        int value;

	(void)cg;

        if (!(devc = sdi->priv))
                return SR_ERR_ARG;
	switch (key) {
        case SR_CONF_CONTINUOUS:
                *data = g_variant_new_boolean(devc->params[PARAM_TRIGMODE] != 2);
        case SR_CONF_LIMIT_FRAMES:
                *data = g_variant_new_uint64(devc->limit_frames);
                return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
        case SR_CONF_TIMEBASE:
                value = 30 - devc->params[PARAM_TIMEBASE];
                *data = g_variant_new("(tt)", timebases[value][0], 
                                      timebases[value][1]);
                return SR_OK;
        case SR_CONF_VDIV:
                value = 15 - devc->params[PARAM_VSEN];
                *data = g_variant_new("(tt)", vdivs[value][0], vdivs[value][1]);
                return SR_OK;
        case SR_CONF_BUFFERSIZE:
                long_p = (uint32_t *)&devc->params[PARAM_RECLEN];
                *data = g_variant_new_uint64(GUINT32_FROM_LE(*long_p));
                return SR_OK;
        case SR_CONF_COUPLING:
                *data = g_variant_new_string(couplings[devc->params[PARAM_CPL]]);
                return SR_OK;
        case SR_CONF_TRIGGER_SOURCE:
                *data = g_variant_new_string(
                        sources[devc->params[PARAM_TRIGSRC] == 2]);
                return SR_OK;
        case SR_CONF_TRIGGER_SLOPE:
                *data = g_variant_new_string(
                        slopes[devc->params[PARAM_TRIGSLOPE]]);
                return SR_OK;
        case SR_CONF_TRIGGER_LEVEL:
                signed_p = (int32_t *) &devc->params[PARAM_TRIGLVL];
                /* In LSB's? Probably needs fixing */
                *data = g_variant_new_double(GINT16_FROM_LE(*signed_p) * 0.04);
                return SR_OK;
        case SR_CONF_SAMPLERATE:
                // 25 divided by the timebase value.
                value = 30 - devc->params[PARAM_TIMEBASE];
                *data = g_variant_new_uint64(
                        25 * timebases[value][1] / timebases[value][0]);
                return SR_OK;
        case SR_CONF_HORIZ_TRIGGERPOS:
                *data = g_variant_new_double(poss[devc->params[PARAM_TRIGPOS]]);
                return SR_OK;
	default:
                sr_err("Invalid config item 0x%x requested.", key);
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
                      const struct sr_channel_group *cg)
{
        unsigned i;
        uint64_t p, q;
        uint32_t *long_p;
        int32_t *signed_p;
        double lvl;
        const char *string;
        struct dev_context *devc;
        (void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

        if (!(devc = sdi->priv))
                return SR_ERR_ARG;

	switch (key) {
        case SR_CONF_LIMIT_FRAMES:
                devc->limit_frames = g_variant_get_uint64(data);
                return SR_OK;
        case SR_CONF_LIMIT_SAMPLES:
        case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
        case SR_CONF_TIMEBASE:
                g_variant_get(data, "(tt)", &p, &q);
                for (i = 0; i < ARRAY_SIZE(timebases); i++) {
                        if (timebases[i][0] == p && timebases[i][1] == q) {
                                devc->params[PARAM_TIMEBASE] = 30 - i;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_VDIV:
                g_variant_get(data, "(tt)", &p, &q);
                for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
                        if (vdivs[i][0] == p && vdivs[i][1] == q) {
                                devc->params[PARAM_VSEN] = 15 - i;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_BUFFERSIZE:
                p = g_variant_get_uint64(data);
                for (i = 0; i < ARRAY_SIZE(buffersizes); i += 1) {
                        if (buffersizes[i] == p) {
                                long_p = (uint32_t *) &devc->params[PARAM_RECLEN];
                                *long_p = GUINT32_TO_LE(buffersizes[i]);
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_COUPLING:
                string = g_variant_get_string(data, NULL);
                for (i = 0; i < ARRAY_SIZE(couplings); i += 1) {
                        if (!g_strcmp0(couplings[i], string)) {
                                devc->params[PARAM_CPL] = i;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_TRIGGER_SOURCE:
                string = g_variant_get_string(data, NULL);
                for (i = 0; i < ARRAY_SIZE(sources); i += 1) {
                        if (!g_strcmp0(sources[i], string)) {
                                devc->params[PARAM_TRIGSRC] = i ? 2 : 0;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_TRIGGER_SLOPE:
                string = g_variant_get_string(data, NULL);
                for (i = 0; i < ARRAY_SIZE(slopes); i += 1) {
                        if (!g_strcmp0(slopes[i], string)) {
                                devc->params[PARAM_TRIGSLOPE] = i;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_TRIGGER_LEVEL:
                signed_p = (int32_t *) &devc->params[PARAM_TRIGLVL];
                /* This probably depends on LSB, and needs to be fixed */
                lvl = g_variant_get_double(data) / 0.04;
                *signed_p = GINT32_TO_LE((int32_t) lvl);
                return SR_OK;
        case SR_CONF_HORIZ_TRIGGERPOS:
                lvl = g_variant_get_double(data);
                for (i = 0; i < ARRAY_SIZE(poss); i += 1) {
                        if (lvl == poss[i]) {
                                devc->params[PARAM_TRIGPOS] = i;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
	default:
                sr_err("Tried to set invalid config item 0x%x.", key);
		return SR_ERR_NA;
	}
}

static GVariant *build_tuples(const uint64_t (*array)[][2], unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64((*array)[i][0]);
		rational[1] = g_variant_new_uint64((*array)[i][1]);
		g_variant_builder_add_value(
                        &gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)data;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
                *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
                                                  scanopts, ARRAY_SIZE(scanopts),
                                                  sizeof(uint32_t));
                return SR_OK;
	case SR_CONF_DEVICE_OPTIONS:
                *data = sdi
                        ? g_variant_new_fixed_array(
                                G_VARIANT_TYPE_UINT32, devopts,
                                ARRAY_SIZE(devopts), sizeof(uint32_t))
                        : g_variant_new_fixed_array(
                                G_VARIANT_TYPE_UINT32, drvopts,
                                ARRAY_SIZE(drvopts), sizeof(uint32_t));
                return SR_OK;
        case SR_CONF_TIMEBASE:
                *data = build_tuples(&timebases, ARRAY_SIZE(timebases));
                return SR_OK;
        case SR_CONF_VDIV:
                *data = build_tuples(&vdivs, ARRAY_SIZE(vdivs));
                return SR_OK;
        case SR_CONF_BUFFERSIZE:
                *data = g_variant_new_fixed_array(
                        G_VARIANT_TYPE_UINT64, buffersizes,
                        ARRAY_SIZE(buffersizes), sizeof(uint64_t));
                return SR_OK;
        case SR_CONF_COUPLING:
                *data = g_variant_new_strv(couplings, ARRAY_SIZE(couplings));
                return SR_OK;
        case SR_CONF_TRIGGER_SOURCE:
                *data = g_variant_new_strv(sources, ARRAY_SIZE(sources));
                return SR_OK;
        case SR_CONF_TRIGGER_SLOPE:
                *data = g_variant_new_strv(slopes, ARRAY_SIZE(slopes));
                return SR_OK;
        case SR_CONF_HORIZ_TRIGGERPOS:
                *data = g_variant_new_fixed_array(G_VARIANT_TYPE_DOUBLE,
                                                  poss, ARRAY_SIZE(poss),
                                                  sizeof(double));
                return SR_OK;
	default:
                sr_err("Invalid config list 0x%x requested.", key);
		return SR_ERR_NA;
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
        int status = SR_ERR;
        uint8_t *frame;
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

        if (!(devc = sdi->priv) || !(serial = sdi->conn))
                return SR_ERR_ARG;
        
        devc->num_frames = 0;
        sr_sw_limits_acquisition_start(&devc->limits);

        sr_spew("starting acquisition");
        if (jyetech_dso112a_set_parameters(sdi) != SR_OK
            || jyetech_dso112a_send_command(serial, COMMAND_START, START_EXTRA)
               != SR_OK
            || !(frame = jyetech_dso112a_read_frame(serial)))
                return SR_ERR_IO;

        if (frame[FRAME_ID] == QUERY_RESPONSE && frame[FRAME_EXTRA] == devc->type
            && !g_strcmp0((char *) &frame[QUERY_NAME], devc->description)) {
                status = serial_source_add(
                        sdi->session, sdi->conn, G_IO_IN, 50, 
                        jyetech_dso112a_receive_data, (void *) sdi);
                if (status == SR_OK) {
                        status = std_session_send_df_header(sdi);
                        if (status == SR_OK)
                                devc->acquiring = TRUE;
                }
        } else {
                sr_err("Failed to start acquisition: Frame ID: 0x%x, type %c, name %s",
                       frame[FRAME_ID], frame[FRAME_EXTRA], &frame[QUERY_NAME]);
        }

        g_free(frame);
        return status;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
        struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

        if ((devc = sdi->priv))
                devc->acquiring = FALSE;
        jyetech_dso112a_send_command(sdi->conn, COMMAND_STOP, STOP_EXTRA); 
        std_session_send_df_end(sdi);
        serial_source_remove(sdi->session, sdi->conn);
        return SR_OK;
}

SR_PRIV struct sr_dev_driver jyetech_dso112a_driver_info = {
	.name = "jyetech-dso112a",
	.longname = "JYETech DSO112A",
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

SR_REGISTER_DEV_DRIVER(jyetech_dso112a_driver_info);
