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
                

static int jyetech_dso112a_get_stuffed(struct sr_serial_dev_inst *port)
{
     uint8_t c;
     static uint8_t stuffing = 0;

     /*
      * jyetech byte-stuffs a zero after SYNC bytes in data, which is an invalid
      * Frame ID, which always follows a proper SYNC byte. So when we see a SYNC
      * byte, we read ahead to get the stuffing byte. If it's not zero, we'll
      * return it on the next call. If it is zero, it's discarded.
      */
     if (stuffing) {
          c = stuffing;
          stuffing = 0;
          return c;
     }

     if (serial_read_blocking(port, &c, 1, TIMEOUT) != 1) {
          sr_dbg("Timeout during read.");
          return -1;
     }
     if (c == SYNC) {
          if (serial_read_blocking(port, &stuffing, 1, TIMEOUT) != 1) {
               sr_dbg("Timeout during read.");
               return -1;
          }
     }
     return c;
}

SR_PRIV uint8_t *jyetech_dso112a_read_frame(struct sr_serial_dev_inst *port)
{
        int i, c, id, lo_byte, hi_byte;
        uint16_t frame_size;
        uint8_t *frame;
        
        c = jyetech_dso112a_get_stuffed(port);
        if (c != SYNC) {
             sr_spew("Got 0x%x looking for SYNC byte.", c);
             return NULL;
        }

        id = jyetech_dso112a_get_stuffed(port);
        if (id >= 0) {
             lo_byte = jyetech_dso112a_get_stuffed(port);
             if (lo_byte >= 0) {
                  hi_byte = jyetech_dso112a_get_stuffed(port);
                  if (hi_byte >= 0) {
                       frame_size = lo_byte + 256 * hi_byte;
                       frame = g_malloc(frame_size);
                       frame[FRAME_ID] = id;
                       frame[FRAME_SIZE]     = lo_byte;
                       frame[FRAME_SIZE + 1] = hi_byte;
                       for (i = 3; i < frame_size;) {
                            c = jyetech_dso112a_get_stuffed(port);
                            if (c < 0) {
                                 g_free(frame);
                                 return NULL;
                            }
                            frame[i++] = c;
                       }
                       return frame;
                  }
             }
        }
        return NULL;
}

static int jyetech_dso112a_send_frame(
     struct sr_serial_dev_inst *serial, uint8_t *frame)
{
        int8_t sync = SYNC;

        uint16_t size = GUINT16_FROM_LE(*(uint16_t *) &frame[FRAME_SIZE]);

        if (serial_write_blocking(serial, &sync, 1, TIMEOUT) == 1
            && serial_write_blocking(serial, frame, size, TIMEOUT) == size)
                return SR_OK;
        return SR_ERR_IO;
}

SR_PRIV uint8_t *jyetech_dso112a_send_command(struct sr_serial_dev_inst *port,
                                              uint8_t ID, uint8_t extra)
{
        static uint8_t command[5] = {0, 4, 0, 0} ;

        command[0] = ID;
        command[3] = extra;
        return jyetech_dso112a_send_frame(port, command) == SR_OK
                ? jyetech_dso112a_read_frame(port)
                : NULL;
}

SR_PRIV struct dev_context *jyetech_dso112a_dev_context_new(uint8_t *frame)
{
        struct dev_context *device;
        
        if (frame[FRAME_ID] != QUERY_RESPONSE || frame[FRAME_EXTRA] != 'O') {
                sr_spew("Frame id 0x%x not a query response, or device type %c not an oscilloscope", frame[FRAME_ID], frame[FRAME_EXTRA]);
                return NULL;
        }
                
        /* This is indeed a frame describing an oscilloscope */
        device = g_malloc0(sizeof(struct dev_context));
        device->type = frame[FRAME_EXTRA];
        frame[GUINT16_FROM_LE(*(uint16_t *) &frame[FRAME_SIZE]) - 1] = 0;
        device->description = g_strdup((char *) &frame[QUERY_NAME]);
        return device;
}        

SR_PRIV void jyetech_dso112a_dev_context_free(void *p)
{
        struct dev_context *devc = p;

        if (devc->params)
                g_free(devc->params);
        g_free(devc->description);
        g_free(devc);
}


SR_PRIV int jyetech_dso112a_get_parameters(const struct sr_dev_inst *sdi)
{
        int status = SR_ERR_IO;
        uint8_t *frame;
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;

        if (!(devc = sdi->priv)) {
                return SR_ERR_ARG;
        }
        
        serial = sdi->conn;
        status = SR_ERR_IO;
        sr_spew("getting parameters");
        if ((frame = jyetech_dso112a_send_command(serial,
                                                  COMMAND_GET, PARAM_EXTRA))) {
                if (frame[FRAME_ID] != GET_RESPONSE 
                    || frame[FRAME_EXTRA] != PARM_RESP_EXTRA) {
                        g_free(frame);
                } else {
                        sr_spew("Got parameters");
                        if (devc->params) {
                                g_free(devc->params);
                        }
                        devc->params = frame;
                        status = SR_OK;
                }
        }
        return status;
}


SR_PRIV int jyetech_dso112a_set_parameters(const struct sr_dev_inst *sdi)
{
        uint8_t *frame;
        struct dev_context *devc;

        if (!(devc = sdi->priv))
                return SR_ERR_ARG;

        if (!(frame = devc->params))
                return SR_ERR_ARG;
                
        frame[FRAME_ID] = COMMAND_SET;
        frame[FRAME_EXTRA] = SET_EXTRA;
        /* Force auto-trigger mode to make sure we are getting data */
        frame[PARAM_TRIGMODE] = 0;
        return jyetech_dso112a_send_frame(sdi->conn, frame);
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

        sr_spew("Handling event");
	if (!(sdi = cb_data) || !(devc = sdi->priv) || !(serial = sdi->conn))
		return TRUE;

	if (revents == G_IO_IN) {
                sr_spew("Reading frame %ld of %ld", devc->num_frames + 1,
                        devc->limit_frames);
                frame = jyetech_dso112a_read_frame(serial);
                if (!devc->acquiring) {
                        if (frame) {
                                g_free(frame);
                        }
                        frame = jyetech_dso112a_send_command(
                                     serial, COMMAND_STOP, STOP_EXTRA); 
                } else if (!frame) {	// Hmm. We seem to see this after every packet.
                        sr_err("IO error during capture.");
                } else if (frame[FRAME_ID] != SAMPLE_FRAME) {
                        sr_err("Bad frame id 0x%x during capture.",
                               frame[FRAME_ID]);
                } else {
                        sr_spew("Got sample");
                        sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
                        encoding.unitsize = sizeof(uint8_t);
                        encoding.is_signed = FALSE;
                        encoding.is_float = FALSE;
                        value_p = jyetech_dso112a_get_vdiv(devc);
                        encoding.scale.p = (*value_p)[0];
                        encoding.scale.q = 25 * (*value_p)[1];
                        encoding.offset.p =
                                -(GINT16_FROM_LE(*(int16_t *)
                                                 &devc->params[PARAM_VPOS]) + 128)
                                * (*value_p)[0];
                        encoding.offset.q = 25 * (*value_p)[1];
                        if (frame[FRAME_EXTRA] == SINGLE_SAMPLE) {
                                analog.num_samples = 1;
                        } else if (frame[FRAME_EXTRA] == BULK_SAMPLE) {
                                analog.num_samples = GUINT16_FROM_LE(
                                        *(uint16_t *) &frame[FRAME_SIZE]) - 8;
                        } else {
                                sr_err("Got 0xC0 frame type=0x%c while looking for sample.", frame[FRAME_EXTRA]);
                                return TRUE;
                        }
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
                if (frame)
                        g_free(frame);
        }
        return TRUE;
}
