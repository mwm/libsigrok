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
	SR_CONF_SERIALCOMM
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
        SR_CONF_CONTINUOUS,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET ,//| SR_CONF_LIST,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET ,//| SR_CONF_LIST,
	SR_CONF_BUFFERSIZE | SR_CONF_GET | SR_CONF_SET,// | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET ,//| SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET,// | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET,// | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,// | SR_CONF_LIST,
        SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET,// | SR_CONF_LIST,
};

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
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!serialcomm)
		serialcomm = SERIALCOMM;

        sr_info("Probing port %s.", conn);
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
                sr_serial_dev_inst_free(serial);
                return NULL;
        }

        /* Ask whatever's there what kind of jyetech device is is */
        if (!jyetech_dso112a_send_command(serial, COMMAND_QUERY, QUERY_EXTRA)) {
                serial_close(serial);
                sr_serial_dev_inst_free(serial);
                return NULL;
        }

        frame = jyetech_dso112a_read_frame(serial);
        if (!frame) {
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
        sdi = g_malloc0(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INACTIVE;
        sdi->vendor = g_strdup("JYETech");
        sdi->model = g_strdup(devc->description);
        sdi->inst_type = SR_INST_SERIAL;
        sdi->conn = serial;
        sdi->priv = devc;
        sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
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

        if (sdi->status == SR_ST_ACTIVE)
                return SR_ERR;

        serial = sdi->conn;
	sr_info("Opening device %s.", serial->port);
        status = serial_open(serial, SERIAL_RDWR);
        if (status != SR_OK)
                return status;

        sdi->status = SR_ST_ACTIVE;
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *serial = sdi->conn;

	sdi->status = SR_ST_INACTIVE;
	sr_info("Closing device %s.", serial->port);
        return serial_close(serial);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
                sr_err("Invalid config item 0x%x requested.", key);
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)data;
	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
                sr_err("Tried to set invalid config item 0x%x.", key);
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)data;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
                *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
                                                  scanopts, ARRAY_SIZE(scanopts),
                                                  sizeof(uint32_t));
                return SR_OK;
	case SR_CONF_DEVICE_OPTIONS:
                *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
                                                  devopts,
                                                  ARRAY_SIZE(devopts),
                                                  sizeof(uint32_t));
                return SR_OK;
	default:
                sr_err("Invalid config list 0x%x requested.", key);
		return SR_ERR_NA;
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
        int status;
        struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

        devc = sdi->priv;
        devc->acquiring = TRUE;
        status = serial_source_add(sdi->session, sdi->conn, G_IO_IN,
                                   50, jyetech_dso112a_receive_data, (void *) sdi);
        if (status == SR_OK) {
                /* Users can change parameters on the scope, so get them */
                status = jyetech_dso112a_get_parameters(sdi);
                if (status == SR_OK) {
                        status = std_session_send_df_header(sdi);
                        if (status == SR_OK) {
                                if (!jyetech_dso112a_send_command(sdi->conn,
                                                                  COMMAND_START,
                                                                  START_EXTRA))
                                        status = SR_ERR_IO;
                        }
                }
        }
        return status;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
        struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

        std_session_send_df_end(sdi);
        serial_source_remove(sdi->session, sdi->conn);
        if ((devc = sdi->priv))
                devc->acquiring = FALSE;
        jyetech_dso112a_send_command(sdi->conn, COMMAND_STOP, STOP_EXTRA);
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
