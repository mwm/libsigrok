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
                
static SR_PRIV int jyetech_dso112a_get_stuffed(struct sr_serial_dev_inst *port)
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
                       frame[0] = id;
                       frame[1] = lo_byte;
                       frame[2] = hi_byte;
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

SR_PRIV int jyetech_dso112a_send_command(struct sr_serial_dev_inst *port,
                                         uint8_t ID, uint8_t extra) {
        static uint8_t command[5] = {254, 0, 4, 0, 0} ;
        command[1] = ID;
        command[4] = extra;
        
        return serial_write_blocking(port, command, 5, TIMEOUT) == 5;
}

SR_PRIV struct dev_context *jyetech_dso112a_dev_context_new(uint8_t *frame)
{
        struct dev_context *device;
        
        if (frame[0] != QUERY_RESPONSE || frame[3] != 'O') {
                sr_spew("Frame id 0x%x not a query response, or device type %c not an oscilloscope", frame[0], frame[3]);
                return NULL;
        }
                
        /* This is indeed a frame describing an oscilloscope */
        device = g_malloc0(sizeof(struct dev_context));
        device->type = frame[3];
        device->description = g_strdup((char *) &frame[5]);
        return device;
}        

SR_PRIV void jyetech_dso112a_dev_context_free(void *p)
{
        struct dev_context *devc = p;

        g_free(devc->description);
        g_free(devc);
}


SR_PRIV int jyetech_dso112a_get_parameters(const struct sr_dev_inst *sdi)
{
        int status;
        uint8_t *frame;
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;

        if (!(devc = sdi->priv)) {
                return SR_ERR_ARG;
        }
        
        serial = sdi->conn;
        status = SR_ERR_IO;
        if (jyetech_dso112a_send_command(serial, COMMAND_GET, PARAMETER_EXTRA)) {
                frame = jyetech_dso112a_read_frame(serial);
                if (frame) {
                        status = SR_OK;
                        devc->vpos = frame[6] + 256 * frame[7];
                        sr_spew("Set VPos to %d", devc->vpos);
                        g_free(frame);
                }
        }
        return status;
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
        
        uint8_t *frame;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv) || !devc->acquiring)
		return TRUE;
        
        serial = sdi->conn;
                
        sr_spew("Handling event") ;
	if (revents == G_IO_IN && devc->acquiring) {
                sr_spew("Reading frame");
                frame = jyetech_dso112a_read_frame(serial);
                if (frame && frame[0] == SAMPLE_FRAME) {
                        sr_spew("Got sample");
                        sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
                        encoding.unitsize = sizeof(uint8_t);
                        encoding.is_signed = FALSE;
                        encoding.is_float = FALSE;
                        /* WARNING!!! These values assume 1volt/div setting on scope!!! */
                        encoding.scale.p = 1;
                        encoding.scale.q = 25;
                        encoding.offset.p = -devc->vpos - 128;
                        encoding.offset.q = 25;
                        analog.meaning->channels = g_slist_copy(sdi->channels);
                        analog.meaning->mq = SR_MQ_VOLTAGE;
                        analog.meaning->unit = SR_UNIT_VOLT;
                        analog.meaning->mqflags = 0;
                        if (frame[3] == SINGLE_SAMPLE) {
                                /* TODO */
                                analog.num_samples = 1;
                        } else if (frame[3] == BULK_SAMPLE) {
                                /* TODO */
                                analog.num_samples = frame[4] + 256 * frame[5];
                        } else {
                                sr_dbg("Got 0xC0 frame type=0x%c while looking for sample.", frame[3]);
                        }
                        memcpy(devc->data, &frame[4], analog.num_samples);
                        analog.data = &devc->data;
                        packet.type = SR_DF_ANALOG;
                        packet.payload = &analog;
                        sr_session_send(sdi, &packet);
                        g_slist_free(analog.meaning->channels);
                }
                if (frame)
                        g_free(frame);
        }
        return TRUE;
}
