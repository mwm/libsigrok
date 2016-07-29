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
#include <string.h>
#include "protocol.h"

// Let IO time out in 1 second for now.
#define TIMEOUT 1000
                

static int get_stuffed(const struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *port;
        struct dev_context *devc;
        uint8_t c;

        if (!(port = sdi->conn) || !(devc = sdi->priv)) {
                sr_info("Called get stuffed with null conn or devc");
                return -1;
        }

        /*
         * jyetech byte-stuffs a zero after SYNC bytes in data, which is an invalid
         * Frame ID, which always follows a proper SYNC byte. So when we see a SYNC
         * byte, we read ahead to get the stuffing byte. If it's not zero, we'll
         * return it on the next call. If it is zero, it's discarded.
         */
        if (devc->buffer) {
                c = devc->buffer;
                devc->buffer = 0;
                sr_spew("Read byte 0x%x", c);
                return c;
        }

        if (serial_read_blocking(port, &c, 1, TIMEOUT) != 1) {
                sr_info("Timeout during read.");
                return -1;
        }
        if (c == SYNC) {
                if (serial_read_blocking(port, &devc->buffer, 1, TIMEOUT) != 1) {
                        sr_info("Timeout during read.");
                        return -1;
                }
                if (!devc->buffer) {
                     sr_dbg("Discarded stuffed 0 byte.");
                }
        }
        sr_spew("Read byte 0x%x", c);
        return c;
}

static uint8_t *read_frame(const struct sr_dev_inst *sdi)
{
        uint8_t c;
        int i, id, lo_byte, hi_byte;
        int frame_size;
        struct sr_serial_dev_inst *port;
        uint8_t *frame = NULL;
        
        
        if (!(port = sdi->conn)) {
                sr_err("read_frame called without a serial port.");
                return NULL;
        }

        do {
                sr_spew("Starting read for sync.");
                if (serial_read_blocking(port, &c, 1, TIMEOUT) < 1) {
                        sr_err("Timeout looking for frame header.");
                        return NULL;
                }
                sr_spew("Got 0x%x looking for SYNC byte.", c);
        } while (c != SYNC) ;
        if ((id = get_stuffed(sdi)) >= 0) {
                lo_byte = get_stuffed(sdi);
                if (lo_byte >= 0) {
                        hi_byte = get_stuffed(sdi);
                        if (hi_byte >= 0) {
                                frame_size = lo_byte + 256 * hi_byte;
                                frame = g_malloc(frame_size);
                                frame[FRAME_ID] = id;
                                frame[FRAME_SIZE]     = lo_byte;
                                frame[FRAME_SIZE + 1] = hi_byte;
                                for (i = 3; i < frame_size;) {
                                        c = get_stuffed(sdi);
                                        if (c < 0) {
                                                sr_info("Timed out reading capture data.");
                                                g_free(frame);
                                                return NULL;
                                        }
                                        frame[i++] = c;
                                }
                        }
                }
        }
        return frame;
}

static uint8_t *raw_read_frame(struct sr_serial_dev_inst *port) {
        int frame_size;
        uint8_t c, bytes[3];
        uint8_t *frame = NULL;
        
        do {
                if (serial_read_blocking(port, &c, 1, TIMEOUT) != 1) {
                        sr_info("Timeout looking for frame header.");
                        return NULL;
                }
                sr_spew("Got 0x%x looking for SYNC byte.", c);
        } while (c != SYNC) ;

        if (serial_read_blocking(
                    port, &bytes, sizeof(bytes), TIMEOUT) != sizeof(bytes)) {
                sr_info("Timeout frame header read.");
        } else {
                frame_size = GUINT16_FROM_LE(*(uint16_t *) &bytes[1]);
                frame = g_malloc(frame_size);
                memcpy(frame, bytes, sizeof(bytes));
                frame_size -= sizeof(bytes);
                if (serial_read_blocking(
                            port, &frame[FRAME_DATA], frame_size, TIMEOUT)
                    != frame_size) {
                        sr_info("Timeout during frame read.");
                        g_free(frame);
                        frame = NULL;
                }
        }
        return frame;
}


static int send_frame(
        struct sr_serial_dev_inst *port, uint8_t *frame)
{
        int8_t sync = SYNC;
        uint16_t size = GUINT16_FROM_LE(*(uint16_t *) &frame[FRAME_SIZE]);

        if (serial_write_blocking(port, &sync, 1, TIMEOUT) == 1
            && serial_write_blocking(port, frame, size, TIMEOUT) == size)
                return SR_OK;
        sr_info("Timeout during write.");
        return SR_ERR_IO;
}

static uint8_t *send_command(
        const void *dev, uint8_t id, uint8_t extra, gboolean raw)
{
        static uint8_t command[5] = {0, 4, 0, 0} ;
        const struct sr_dev_inst *sdi;
        struct sr_serial_dev_inst *port;

        command[0] = id;
        command[3] = extra;
        
        if (raw) {
                port = (struct sr_serial_dev_inst *) dev;
        } else {
                sdi = dev;
                if (!(port = sdi->conn)) {
                        sr_info("Called send_command with invalid port");
                        return NULL;
                }
        }

        return send_frame(port, command) == SR_OK 
                ? (raw ? raw_read_frame(port) : read_frame(sdi))
                : NULL;
}

SR_PRIV uint8_t *jyetech_dso112a_send_command(
        const struct sr_dev_inst *sdi, uint8_t id, uint8_t extra)
{
        return send_command(sdi, id, extra, FALSE);
}

/*
 * This version doesn't use the devc-buffered IO. It should only be used
 * when the command and return frame are known to not contain a SYNC
 * byte. Generally, only for use by the scan routine.
 */
SR_PRIV uint8_t *jyetech_dso112a_raw_send_command(
        struct sr_serial_dev_inst *port, uint8_t id, uint8_t extra) {

        return send_command(port, id, extra, TRUE);
}
        

SR_PRIV struct dev_context *jyetech_dso112a_dev_context_new(uint8_t *frame)
{
        uint16_t frame_size;
        struct dev_context *device;
        
        if (frame[FRAME_ID] != QUERY_RESPONSE || frame[FRAME_DATA] != 'O') {
                sr_dbg("Frame id 0x%x not a query response, or device type %c not an oscilloscope", frame[FRAME_ID], frame[FRAME_DATA]);
                return NULL;
        }
                
        frame_size = GUINT16_FROM_LE(*(uint16_t *) &frame[FRAME_SIZE]);
        if (frame_size < QUERY_LENGTH) {
                sr_err("You need to update your DOS 112A firmware. Length: 0x%x",
                       frame_size);
                return NULL;
        }

        /* This is indeed a frame describing an oscilloscope */
        device = g_malloc0(sizeof(struct dev_context));
        device->type = frame[FRAME_DATA];
        frame[frame_size - 1] = 0;
        device->description = g_strdup((char *) &frame[QUERY_NAME]);
        return device;
}        

SR_PRIV void jyetech_dso112a_dev_context_free(void *p)
{
        struct dev_context *devc = p;

        g_free(devc->params);
        g_free(devc->description);
        g_free(devc);
}


SR_PRIV int jyetech_dso112a_get_parameters(const struct sr_dev_inst *sdi)
{
        int status = SR_ERR_IO;
        uint8_t *frame;
        struct dev_context *devc;

        if (!(devc = sdi->priv)) {
                sr_info("Called get_parameters with invalid devc");
                return SR_ERR_ARG;
        }
        
        status = SR_ERR_IO;
        sr_dbg("getting parameters");
        if ((frame = jyetech_dso112a_send_command(
                     sdi, COMMAND_GET, PARAM_EXTRA))) {
                if (frame[FRAME_ID] != GET_RESPONSE 
                    || frame[FRAME_DATA] != PARM_RESP_EXTRA) {
                        sr_info("Got something other than parameters");
                        g_free(frame);
                } else {
                        sr_spew("Got parameters");
                        g_free(devc->params);
                        /* We right this back with changes, so set that up */
                        frame[FRAME_ID] = COMMAND_SET;
                        frame[FRAME_DATA] = SET_EXTRA;
                        frame[PARAM_TRIGMODE] = 0;
                        devc->params = frame;
                        status = SR_OK;
                }
        }
        return status;
}


SR_PRIV int jyetech_dso112a_set_parameters(const struct sr_dev_inst *sdi)
{
        int status;
        uint8_t *frame;
        struct dev_context *devc;
        struct sr_serial_dev_inst *port;

        if (!(devc = sdi->priv) || !(frame = devc->params) || !(port = sdi->conn))
        {
                sr_info("Called set_parameters with invalid devc, params or port");
                return SR_ERR_ARG;
        }
                
        if ((status = send_frame(port, frame)) == SR_OK) {
                if ((frame = read_frame(sdi))) {
                        g_free(frame);
                } else {
                        status = SR_ERR_IO;
                }
        }
        return status ;
}
                        

SR_PRIV int jyetech_dso112a_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
        struct sr_datafeed_packet packet;
        struct sr_datafeed_analog analog;
        struct sr_analog_encoding encoding;
        struct sr_analog_meaning meaning;
        struct sr_analog_spec spec;
        
        const uint64_t (*value_p)[2];
        uint8_t *frame;

	(void)fd;

        sr_dbg("Handling event");
	if (!(sdi = cb_data) || !(devc = sdi->priv) || !(serial = sdi->conn))
		return TRUE;

	if (revents == G_IO_IN) {
                sr_dbg("Reading frame %ld of %ld", devc->num_frames + 1,
                        devc->limit_frames);
                frame = read_frame(sdi);
                if (!devc->acquiring) {
                        sr_warn("Event handler called while not capturing data.");
                        g_free(frame);
                        frame = jyetech_dso112a_send_command(
                                     sdi, COMMAND_STOP, STOP_EXTRA); 
                } else if (!frame) {
                        sr_info("Buggy IO capture error.");
                } else if (frame[FRAME_ID] != SAMPLE_FRAME) {
                        sr_info("Bad frame id 0x%x during capture.",
                               frame[FRAME_ID]);
                } else {
                        sr_analog_init(&analog, &encoding, &meaning, &spec, 5);
                        encoding.unitsize = sizeof(uint8_t);
                        encoding.is_float = FALSE;
                        value_p = jyetech_dso112a_get_vdiv(devc);
                        encoding.scale.p = (*value_p)[0];
                        encoding.scale.q = 25 * (*value_p)[1];
                        if ((*value_p)[1] == 1000) {
                                switch ((*value_p)[0]) {
                                default: case 2:
                                        spec.spec_digits = 5;
                                        break;
                                case 5: case 10: case 20:
                                        spec.spec_digits = 4;
                                        break;
                                case 50: case 100: case 200:
                                        spec.spec_digits = 3;
                                        break;
                                case 500: spec.spec_digits = 2;
                                        break;
                                }
                        } else {
                                switch ((*value_p)[0]) {
                                default: case 1: case 2:
                                        spec.spec_digits = 2;
                                        break;
                                case 5: case 10: case 20:
                                        spec.spec_digits = 1;
                                        break;
                                }
                        }
                        encoding.offset.p =
                                -(GINT16_FROM_LE(*(int16_t *)
                                                 &devc->params[PARAM_VPOS]) + 128)
                                * (*value_p)[0];
                        encoding.offset.q = 25 * (*value_p)[1];
                        if (frame[FRAME_DATA] == SINGLE_SAMPLE) {
                                analog.num_samples = 1;
                        } else if (frame[FRAME_DATA] == BULK_SAMPLE) {
                                analog.num_samples = GUINT16_FROM_LE(
                                        *(uint16_t *) &frame[FRAME_SIZE]) - 8;
                        } else {
                                sr_info("Got 0xC0 frame type=0x%c while looking for sample.", frame[FRAME_DATA]);
                                return TRUE;
                        }
                        sr_dbg("Got capture frame with %d samples",
                        analog.num_samples);
                        sr_sw_limits_update_samples_read(
                                &devc->limits, analog.num_samples);
                        
                        memcpy(devc->data, &frame[CAPTURE_DATA], 
                               analog.num_samples);
                        analog.data = &devc->data;
                        analog.meaning->channels = g_slist_copy(sdi->channels);
                        analog.meaning->mq = SR_MQ_VOLTAGE;
                        analog.meaning->unit = SR_UNIT_VOLT;
                        analog.meaning->mqflags = 0;
                        packet.type = SR_DF_ANALOG;
                        packet.payload = &analog;
                        sr_session_send(sdi, &packet);
                        g_slist_free(analog.meaning->channels);
                        if (sr_sw_limits_check(&devc->limits) ||
                            (devc->limit_frames 
                             && devc->num_frames++ >= devc->limit_frames)) {
                                sdi->driver->dev_acquisition_stop(sdi);
                        }
                }
                g_free(frame);
        }
        return TRUE;
}
