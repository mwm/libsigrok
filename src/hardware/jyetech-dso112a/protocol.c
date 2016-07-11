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

#define SYNC 0xFE
// Let IO time out in 1 second for now.
#define TIMEOUT 1000

struct frame {
        uint8_t ID;
        uint16_t size;
        uint8_t data[];
};

static uint8_t *read_packet(struct sr_serial_dev_inst *port) {
        uint8_t c;
        uint16_t frame_size;
        uint8_t header[3];
        struct frame *frame = (struct frame *) header;

        if (serial_read_blocking(port, &c, 1, TIMEOUT) != 1
            || c != SYNC 
            || serial_read_blocking(port, header, 3, TIMEOUT) != 3)
                return NULL;
        frame_size = header[1] + 256 * header[2];
        sr_spew("Frame @0x%x size of %0x%x, data @0x%x computes to %0x%x\n",
                frame, frame->size, header, frame_size);
        return NULL;
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
                                         unsigned char ID, unsigned char extra) {
        static unsigned char command[5] = {254, 0, 4, 0, 0} ;
        command[1] = ID;
        command[4] = extra;
        return serial_write_blocking(port, command, 5, TIMEOUT) == 5;
}

SR_PRIV int jyetech_dso112a_parse_query(struct sr_serial_dev_inst *port,
                                        struct dev_context *device) {
        (void) device;
        read_packet(port);
        return FALSE;
}

