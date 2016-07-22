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

SR_PRIV int jyetech_dso112a_send_command(struct sr_serial_dev_inst *port,
                                         uint8_t ID, uint8_t extra)
{
        static uint8_t command[5] = {254, 0, 4, 0, 0} ;
        command[1] = ID;
        command[4] = extra;
        
        return serial_write_blocking(port, command, 5, TIMEOUT) == 5;
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
        frame[GET_UNSIGNED(frame, FRAME_SIZE) - 1] = 0;
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
        int status;
        uint8_t *frame;
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;

        if (!(devc = sdi->priv)) {
                return SR_ERR_ARG;
        }
        
        serial = sdi->conn;
        status = SR_ERR_IO;
        sr_spew("getting parameters");
        if (jyetech_dso112a_send_command(serial, COMMAND_GET, PARAM_EXTRA)) {
                frame = jyetech_dso112a_read_frame(serial);
                if (frame) {
                        if (frame[FRAME_ID] == GET_RESPONSE 
                            && frame[FRAME_EXTRA] == PARM_RESP_EXTRA) {
                                sr_spew("Got parameters");
                                status = SR_OK;
                                if (devc->params) {
                                        g_free(devc->params);
                                }
                                devc->params = frame;
                        } else {
                                status = SR_ERR;
                                g_free(frame);
                        }
                }
        }
        return status;
}


/* static int jyetech_dso112a_handle_sample( */
/*         uint8_t *frame, struct sr_dev_inst *sdi, struct dev_context *devc) */
/* { */

/*         int status; */
/*         struct sr_datafeed_packet packet; */
/*         struct sr_datafeed_analog analog; */
/*         struct sr_analog_encoding encoding; */
/*         struct sr_analog_meaning meaning; */
/*         struct sr_analog_spec spec; */

/*         sr_spew("Got sample"); */
/*         sr_analog_init(&analog, &encoding, &meaning, &spec, 0); */
/*         encoding.unitsize = sizeof(uint8_t); */
/*         encoding.is_signed = FALSE; */
/*         encoding.is_float = FALSE; */
/*         /\* WARNING!!! These values assume 1volt/div setting on scope!!! *\/ */
/*         encoding.scale.p = 1; */
/*         encoding.scale.q = 25; */
/*         encoding.offset.p = -(GET_SIGNED(devc->params, PARAM_VPOS) + 128); */
/*         encoding.offset.q = 25; */
/*         if (frame[FRAME_EXTRA] == SINGLE_SAMPLE) { */
/*                 analog.num_samples = 1; */
/*         } else if (frame[FRAME_EXTRA] == BULK_SAMPLE) { */
/*                 analog.num_samples = GET_UNSIGNED(frame, FRAME_SIZE) - 8; */
/*         } else { */
/*                 sr_dbg("Got 0xC0 frame type=0x%c while looking for sample.", */
/*                        frame[FRAME_EXTRA]); */
/*                 return SR_ERR; */
/*         } */
/*         memcpy(devc->data, &frame[CAPTURE_DATA], analog.num_samples); */
/*         analog.data = &devc->data; */
/*         analog.meaning->channels = g_slist_copy(sdi->channels); */
/*         analog.meaning->mq = SR_MQ_VOLTAGE; */
/*         analog.meaning->unit = SR_UNIT_VOLT; */
/*         analog.meaning->mqflags = 0; */
/*         packet.type = SR_DF_ANALOG; */
/*         packet.payload = &analog; */
/*         status = sr_session_send(sdi, &packet); */
/*         g_slist_free(analog.meaning->channels); */
/*         return status; */
/* } */

static int jyetech_dso112a_send_frame(
     struct sr_serial_dev_inst *serial, uint8_t *frame)
{
        int8_t sync = SYNC;

        uint16_t size = GET_UNSIGNED(frame, FRAME_SIZE);

        if (serial_write_blocking(serial, &sync, 1, TIMEOUT) == 1
            && serial_write_blocking(serial, frame, size, TIMEOUT) == size)
                return SR_OK;
        return SR_ERR_IO;
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
        
        uint8_t *frame;

	(void)fd;

        sr_spew("Handling event");
	if (!(sdi = cb_data) || !(devc = sdi->priv) || !(serial = sdi->conn))
		return TRUE;

	if (revents == G_IO_IN) {
                sr_spew("Reading frame");
                frame = jyetech_dso112a_read_frame(serial);
                if (!devc->acquiring) {
                        jyetech_dso112a_send_command(
                                serial, COMMAND_STOP, STOP_EXTRA); 
                } else if (frame && frame[FRAME_ID] == SAMPLE_FRAME) {
                        sr_spew("Got sample");
                        sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
                        encoding.unitsize = sizeof(uint8_t);
                        encoding.is_signed = FALSE;
                        encoding.is_float = FALSE;
                        /* WARNING!!! These values assume 1volt/div setting on scope!!! */
                        encoding.scale.p = 1;
                        encoding.scale.q = 25;
                        encoding.offset.p =
                             -(GET_SIGNED(devc->params, PARAM_VPOS) + 128);
                        encoding.offset.q = 25;
                        if (frame[FRAME_EXTRA] == SINGLE_SAMPLE) {
                                analog.num_samples = 1;
                        } else if (frame[FRAME_EXTRA] == BULK_SAMPLE) {
                                analog.num_samples = 
                                     GET_UNSIGNED(frame, FRAME_SIZE) - 8;
                        } else {
                                sr_err("Got 0xC0 frame type=0x%c while looking for sample.", frame[FRAME_EXTRA]);
                                return SR_ERR;
                        }
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
                }
                if (frame)
                        g_free(frame);
        }
        return TRUE;
}
