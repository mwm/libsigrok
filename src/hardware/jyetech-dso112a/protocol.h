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

#ifndef LIBSIGROK_HARDWARE_JYETECH_DSO112A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_JYETECH_DSO112A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "jyetech-dso112a"

#define SERIALCOMM "115200/8n1/flow=0"
#define SERIALCONN "/dev/ttyU0"

/*
 * Serial protocol values
 *
 * About half the frames are completely determined by their ID. 
 * About half the rest have a frame ID of 0xC0, and use an extra
 * data byte (called "reserved" or "command byte" or "subcommand" or
 * some such) which actually determines the frame type.
 *
 * We won't name the command that has a Frame ID, then sub command
 * extra byte, and finally a Ctrl Byte that determines the function.
 */ 

#define SYNC		0xFE

/* Commands sent to the scope. Most just have the two bytes described above */
#define COMMAND_QUERY	0xE0
#define QUERY_EXTRA	0x00
#define QUERY_RESPONSE	0xE2

#define COMMAND_GET	0xC0
#define CONFIGURE_EXTRA	0x20
#define PARAM_EXTRA	0x21
#define GET_RESPONSE	0xC0
#define CONF_RESP_EXTRA	0x30
#define PARM_RESP_EXTRA	0x31

/* COMMAND_START gets the same response as COMMAND_QUERY */
#define COMMAND_START	0xE1
#define START_EXTRA	0xC0

/* Further commands don't have a response */
#define COMMAND_STOP	0xE9
#define STOP_EXTRA	0x00

#define COMMAND_SET	0xC0
#define SET_EXTRA	0x22

#define COMMAND_SPECIAL	0xC0
#define SPECIAL_EXTRA	0x24

/* Frames sent back from the scope */
#define QUERY_RESPONSE	0xE2
#define SAMPLE_FRAME	0xC0
#define SINGLE_SAMPLE	0x33
#define BULK_SAMPLE	0x32

/* Frame offsets */
#define FRAME_ID	0
#define FRAME_SIZE	1
#define FRAME_EXTRA	3

#define CAPTURE_DATA	4

#define QUERY_NAME	5

#define PARAM_VSEN	4
#define PARAM_CPL	5
#define PARAM_VPOS	6
#define PARAM_TIMEBASE	12
#define PARAM_TRIGMODE	16
#define PARAM_TRIGSLOPE	17
#define PARAM_TRIGLVL	18
#define PARAM_TRIGPOS	20
#define PARAM_TRIGSRC	21
#define PARAM_MEASURE	23
#define PARAM_RECLEN	24
#define PARAM_HPOS	30


/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
        char type;
        char *description;

	/* Acquisition settings */
        uint8_t *params;
        struct sr_serial_dev_inst *serial;
	struct sr_sw_limits limits;

	/* Operational state */
        gboolean acquiring;
        uint8_t data[1024];
        uint64_t limit_frames;

	/* Temporary state across callbacks */
        uint64_t num_frames;
};

SR_PRIV int jyetech_dso112a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int jyetech_dso112a_send_command(struct sr_serial_dev_inst *serial,
                                         uint8_t ID, uint8_t extra);
SR_PRIV int jyetech_dso112a_get_parameters(const struct sr_dev_inst *serial);
SR_PRIV int jyetech_dso112a_set_parameters(const struct sr_dev_inst *serial);
SR_PRIV uint8_t *jyetech_dso112a_read_frame(struct sr_serial_dev_inst *port);
SR_PRIV struct dev_context *jyetech_dso112a_dev_context_new(uint8_t *packet);
SR_PRIV void jyetech_dso112a_dev_context_free(void *p);
SR_PRIV int jyetech_dso112a_receive_data(int fd, int revents, void *cb_data);
#endif
