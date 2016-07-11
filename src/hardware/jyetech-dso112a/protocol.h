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

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
        char type;
        char *description;

	/* Acquisition settings */
        struct sr_serial_dev_inst *serial;

	/* Operational state */

	/* Temporary state across callbacks */

};

SR_PRIV int jyetech_dso112a_send_command(struct sr_serial_dev_inst *serial,
                                         uint8_t ID, uint8_t extra);
SR_PRIV int jyetech_dso112a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int jyetech_dso112a_parse_query(struct sr_serial_dev_inst *port,
                                        struct dev_context *device);

#endif
