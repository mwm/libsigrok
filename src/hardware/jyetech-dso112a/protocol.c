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

#define SYNC 0xFE
// Let IO time out in 1 second for now.
#define TIMEOUT 1000
                
static uint8_t *read_packet(struct sr_serial_dev_inst *port) {
        uint8_t c;
        uint16_t frame_size;
        uint8_t header[3];
        uint8_t *ret;

        if (serial_read_blocking(port, &c, 1, TIMEOUT) != 1
            || c != SYNC 
            || serial_read_blocking(port, header, 3, TIMEOUT) != 3)
                return NULL;
                
        frame_size = header[1] + 256 * header[2];
        ret = g_malloc(frame_size);
        memcpy(ret, header, 3);
        if (serial_read_blocking(port, ret + 3, frame_size - 3, TIMEOUT) !=
            frame_size - 3) {
                g_free(ret);
                return NULL;
        }
        return ret;
}
                

SR_PRIV int jyetech_dso112a_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}

/**
 * @private
 *
 * Send a frame to the dso112a.
 *
 * @param[in] port to write to.
 *
 * @param[in] ID the Frame ID for this frame.
 *
 * @param[in] data pointer to the data bytes for this frame. About
 *	half the command frames are completely determined by their ID,
 *	but have an extra data byte (called "reserved" or "command
 *	byte" or some such) whose value is fixed for that Frame
 *	ID. The exception is the Frame ID 0xC0, which for commands has
 *	a data byte called "subcommand", whose value determines the
 *	command type.  All of these frame have no other data.
 */ 
SR_PRIV int jyetech_dso112a_send_command(struct sr_serial_dev_inst *port,
                                         uint8_t ID, uint8_t extra) {
        static uint8_t command[5] = {254, 0, 4, 0, 0} ;
        command[1] = ID;
        command[4] = extra;
        
        return serial_write_blocking(port, command, 5, TIMEOUT) == 5;
}

SR_PRIV int jyetech_dso112a_parse_query(struct sr_serial_dev_inst *port,
                                        struct dev_context *device) {
        uint8_t *packet;
        (void) device;
        int ret;

        packet = read_packet(port);
        if (!packet)
                return SR_ERR_IO;
        if (packet[0] != 0xE2 || packet[3] != 'O') {
                sr_spew("Packet id 0x%x not a query response, or device type %c not an oscilloscope", packet[0], packet[3]);
                ret = SR_ERR_NA;
        } else {
                /* This is indeed a packet describing an oscilloscope */
                device->type = packet[3];
                device->description = g_strdup((char *) &packet[5]);
                ret = SR_OK;
        }
        g_free(packet);
        return ret;
}

