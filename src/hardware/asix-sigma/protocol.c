/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
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

/*
 * ASIX SIGMA/SIGMA2 logic analyzer driver
 */

#include <config.h>
#include "protocol.h"

#define USB_VENDOR			0xa600
#define USB_PRODUCT			0xa000
#define USB_DESCRIPTION			"ASIX SIGMA"
#define USB_VENDOR_NAME			"ASIX"
#define USB_MODEL_NAME			"SIGMA"

/*
 * The ASIX Sigma supports arbitrary integer frequency divider in
 * the 50MHz mode. The divider is in range 1...256 , allowing for
 * very precise sampling rate selection. This driver supports only
 * a subset of the sampling rates.
 */
SR_PRIV const uint64_t samplerates[] = {
	SR_KHZ(200),	/* div=250 */
	SR_KHZ(250),	/* div=200 */
	SR_KHZ(500),	/* div=100 */
	SR_MHZ(1),	/* div=50  */
	SR_MHZ(5),	/* div=10  */
	SR_MHZ(10),	/* div=5   */
	SR_MHZ(25),	/* div=2   */
	SR_MHZ(50),	/* div=1   */
	SR_MHZ(100),	/* Special FW needed */
	SR_MHZ(200),	/* Special FW needed */
};

SR_PRIV const int SAMPLERATES_COUNT = ARRAY_SIZE(samplerates);

static const char sigma_firmware_files[][24] = {
	/* 50 MHz, supports 8 bit fractions */
	"asix-sigma-50.fw",
	/* 100 MHz */
	"asix-sigma-100.fw",
	/* 200 MHz */
	"asix-sigma-200.fw",
	/* Synchronous clock from pin */
	"asix-sigma-50sync.fw",
	/* Frequency counter */
	"asix-sigma-phasor.fw",
};

static int sigma_read(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_read_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_read_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_write_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_write_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	} else if ((size_t) ret != size) {
		sr_err("ftdi_write_data did not complete write.");
	}

	return ret;
}

/*
 * NOTE: We chose the buffer size to be large enough to hold any write to the
 * device. We still print a message just in case.
 */
SR_PRIV int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
				 struct dev_context *devc)
{
	size_t i;
	uint8_t buf[80];
	int idx = 0;

	if ((len + 2) > sizeof(buf)) {
		sr_err("Attempted to write %zu bytes, but buffer is too small.",
		       len + 2);
		return SR_ERR_BUG;
	}

	buf[idx++] = REG_ADDR_LOW | (reg & 0xf);
	buf[idx++] = REG_ADDR_HIGH | (reg >> 4);

	for (i = 0; i < len; i++) {
		buf[idx++] = REG_DATA_LOW | (data[i] & 0xf);
		buf[idx++] = REG_DATA_HIGH_WRITE | (data[i] >> 4);
	}

	return sigma_write(buf, idx, devc);
}

SR_PRIV int sigma_set_register(uint8_t reg, uint8_t value, struct dev_context *devc)
{
	return sigma_write_register(reg, &value, 1, devc);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
			       struct dev_context *devc)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), devc);

	return sigma_read(data, len, devc);
}

static uint8_t sigma_get_register(uint8_t reg, struct dev_context *devc)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, devc)) {
		sr_err("sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
			  struct dev_context *devc)
{
	uint8_t buf[] = {
		REG_ADDR_LOW | READ_TRIGGER_POS_LOW,

		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
	};
	uint8_t result[6];

	sigma_write(buf, sizeof(buf), devc);

	sigma_read(result, sizeof(result), devc);

	*triggerpos = result[0] | (result[1] << 8) | (result[2] << 16);
	*stoppos = result[3] | (result[4] << 8) | (result[5] << 16);

	/* Not really sure why this must be done, but according to spec. */
	if ((--*stoppos & 0x1ff) == 0x1ff)
		*stoppos -= 64;

	if ((*--triggerpos & 0x1ff) == 0x1ff)
		*triggerpos -= 64;

	return 1;
}

static int sigma_read_dram(uint16_t startchunk, size_t numchunks,
			   uint8_t *data, struct dev_context *devc)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2, devc);

	/* Read the DRAM. */
	buf[idx++] = REG_DRAM_BLOCK;
	buf[idx++] = REG_DRAM_WAIT_ACK;

	for (i = 0; i < numchunks; i++) {
		/* Alternate bit to copy from DRAM to cache. */
		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_BLOCK | (((i + 1) % 2) << 4);

		buf[idx++] = REG_DRAM_BLOCK_DATA | ((i % 2) << 4);

		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_WAIT_ACK;
	}

	sigma_write(buf, idx, devc);

	return sigma_read(data, numchunks * CHUNK_SIZE, devc);
}

/* Upload trigger look-up tables to Sigma. */
SR_PRIV int sigma_write_trigger_lut(struct triggerlut *lut, struct dev_context *devc)
{
	int i;
	uint8_t tmp[2];
	uint16_t bit;

	/* Transpose the table and send to Sigma. */
	for (i = 0; i < 16; i++) {
		bit = 1 << i;

		tmp[0] = tmp[1] = 0;

		if (lut->m2d[0] & bit)
			tmp[0] |= 0x01;
		if (lut->m2d[1] & bit)
			tmp[0] |= 0x02;
		if (lut->m2d[2] & bit)
			tmp[0] |= 0x04;
		if (lut->m2d[3] & bit)
			tmp[0] |= 0x08;

		if (lut->m3 & bit)
			tmp[0] |= 0x10;
		if (lut->m3s & bit)
			tmp[0] |= 0x20;
		if (lut->m4 & bit)
			tmp[0] |= 0x40;

		if (lut->m0d[0] & bit)
			tmp[1] |= 0x01;
		if (lut->m0d[1] & bit)
			tmp[1] |= 0x02;
		if (lut->m0d[2] & bit)
			tmp[1] |= 0x04;
		if (lut->m0d[3] & bit)
			tmp[1] |= 0x08;

		if (lut->m1d[0] & bit)
			tmp[1] |= 0x10;
		if (lut->m1d[1] & bit)
			tmp[1] |= 0x20;
		if (lut->m1d[2] & bit)
			tmp[1] |= 0x40;
		if (lut->m1d[3] & bit)
			tmp[1] |= 0x80;

		sigma_write_register(WRITE_TRIGGER_SELECT0, tmp, sizeof(tmp),
				     devc);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, devc);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), devc);

	return SR_OK;
}

SR_PRIV void sigma_clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_deinit(&devc->ftdic);
}

/*
 * Configure the FPGA for bitbang mode.
 * This sequence is documented in section 2. of the ASIX Sigma programming
 * manual. This sequence is necessary to configure the FPGA in the Sigma
 * into Bitbang mode, in which it can be programmed with the firmware.
 */
static int sigma_fpga_init_bitbang(struct dev_context *devc)
{
	uint8_t suicide[] = {
		0x84, 0x84, 0x88, 0x84, 0x88, 0x84, 0x88, 0x84,
	};
	uint8_t init_array[] = {
		0x01, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01,
	};
	int i, ret, timeout = (10 * 1000);
	uint8_t data;

	/* Section 2. part 1), do the FPGA suicide. */
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);

	/* Section 2. part 2), do pulse on D1. */
	sigma_write(init_array, sizeof(init_array), devc);
	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Wait until the FPGA asserts D6/INIT_B. */
	for (i = 0; i < timeout; i++) {
		ret = sigma_read(&data, 1, devc);
		if (ret < 0)
			return ret;
		/* Test if pin D6 got asserted. */
		if (data & (1 << 5))
			return 0;
		/* The D6 was not asserted yet, wait a bit. */
		g_usleep(10 * 1000);
	}

	return SR_ERR_TIMEOUT;
}

/*
 * Configure the FPGA for logic-analyzer mode.
 */
static int sigma_fpga_init_la(struct dev_context *devc)
{
	/* Initialize the logic analyzer mode. */
	uint8_t logic_mode_start[] = {
		REG_ADDR_LOW  | (READ_ID & 0xf),
		REG_ADDR_HIGH | (READ_ID >> 8),
		REG_READ_ADDR,	/* Read ID register. */

		REG_ADDR_LOW | (WRITE_TEST & 0xf),
		REG_DATA_LOW | 0x5,
		REG_DATA_HIGH_WRITE | 0x5,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_DATA_LOW | 0xa,
		REG_DATA_HIGH_WRITE | 0xa,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_ADDR_LOW | (WRITE_MODE & 0xf),
		REG_DATA_LOW | 0x0,
		REG_DATA_HIGH_WRITE | 0x8,
	};

	uint8_t result[3];
	int ret;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), devc);

	/* Expect a 3 byte reply since we issued three READ requests. */
	ret = sigma_read(result, 3, devc);
	if (ret != 3)
		goto err;

	if (result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa)
		goto err;

	return SR_OK;
err:
	sr_err("Configuration failed. Invalid reply received.");
	return SR_ERR;
}

/*
 * Read the firmware from a file and transform it into a series of bitbang
 * pulses used to program the FPGA. Note that the *bb_cmd must be free()'d
 * by the caller of this function.
 */
static int sigma_fw_2_bitbang(struct sr_context *ctx, const char *name,
			      uint8_t **bb_cmd, gsize *bb_cmd_size)
{
	size_t i, file_size, bb_size;
	char *firmware;
	uint8_t *bb_stream, *bbs;
	uint32_t imm;
	int bit, v;
	int ret = SR_OK;

	firmware = sr_resource_load(ctx, SR_RESOURCE_FIRMWARE,
			name, &file_size, 256 * 1024);
	if (!firmware)
		return SR_ERR;

	/* Weird magic transformation below, I have no idea what it does. */
	imm = 0x3f6df2ab;
	for (i = 0; i < file_size; i++) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		firmware[i] ^= imm & 0xff;
	}

	/*
	 * Now that the firmware is "transformed", we will transcribe the
	 * firmware blob into a sequence of toggles of the Dx wires. This
	 * sequence will be fed directly into the Sigma, which must be in
	 * the FPGA bitbang programming mode.
	 */

	/* Each bit of firmware is transcribed as two toggles of Dx wires. */
	bb_size = file_size * 8 * 2;
	bb_stream = (uint8_t *)g_try_malloc(bb_size);
	if (!bb_stream) {
		sr_err("%s: Failed to allocate bitbang stream", __func__);
		ret = SR_ERR_MALLOC;
		goto exit;
	}

	bbs = bb_stream;
	for (i = 0; i < file_size; i++) {
		for (bit = 7; bit >= 0; bit--) {
			v = (firmware[i] & (1 << bit)) ? 0x40 : 0x00;
			*bbs++ = v | 0x01;
			*bbs++ = v;
		}
	}

	/* The transformation completed successfully, return the result. */
	*bb_cmd = bb_stream;
	*bb_cmd_size = bb_size;

exit:
	g_free(firmware);
	return ret;
}

static int upload_firmware(struct sr_context *ctx,
		int firmware_idx, struct dev_context *devc)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	const char *firmware = sigma_firmware_files[firmware_idx];
	struct ftdi_context *ftdic = &devc->ftdic;

	/* Make sure it's an ASIX SIGMA. */
	ret = ftdi_usb_open_desc(ftdic, USB_VENDOR, USB_PRODUCT,
				 USB_DESCRIPTION, NULL);
	if (ret < 0) {
		sr_err("ftdi_usb_open failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	ret = ftdi_set_bitmode(ftdic, 0xdf, BITMODE_BITBANG);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	ret = ftdi_set_baudrate(ftdic, 750 * 1000);
	if (ret < 0) {
		sr_err("ftdi_set_baudrate failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	/* Initialize the FPGA for firmware upload. */
	ret = sigma_fpga_init_bitbang(devc);
	if (ret)
		return ret;

	/* Prepare firmware. */
	ret = sigma_fw_2_bitbang(ctx, firmware, &buf, &buf_size);
	if (ret != SR_OK) {
		sr_err("An error occurred while reading the firmware: %s",
		       firmware);
		return ret;
	}

	/* Upload firmware. */
	sr_info("Uploading firmware file '%s'.", firmware);
	sigma_write(buf, buf_size, devc);

	g_free(buf);

	ret = ftdi_set_bitmode(ftdic, 0x00, BITMODE_RESET);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(ftdic);

	/* Discard garbage. */
	while (sigma_read(&pins, 1, devc) == 1)
		;

	/* Initialize the FPGA for logic-analyzer mode. */
	ret = sigma_fpga_init_la(devc);
	if (ret != SR_OK)
		return ret;

	devc->cur_firmware = firmware_idx;

	sr_info("Firmware uploaded.");

	return SR_OK;
}

SR_PRIV int sigma_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	unsigned int i;
	int ret;

	devc = sdi->priv;
	drvc = sdi->driver->context;
	ret = SR_OK;

	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		if (samplerates[i] == samplerate)
			break;
	}
	if (samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (samplerate <= SR_MHZ(50)) {
		ret = upload_firmware(drvc->sr_ctx, 0, devc);
		devc->num_channels = 16;
	} else if (samplerate == SR_MHZ(100)) {
		ret = upload_firmware(drvc->sr_ctx, 1, devc);
		devc->num_channels = 8;
	} else if (samplerate == SR_MHZ(200)) {
		ret = upload_firmware(drvc->sr_ctx, 2, devc);
		devc->num_channels = 4;
	}

	if (ret == SR_OK) {
		devc->cur_samplerate = samplerate;
		devc->period_ps = 1000000000000ULL / samplerate;
		devc->samples_per_event = 16 / devc->num_channels;
		devc->state.state = SIGMA_IDLE;
	}

	return ret;
}

/*
 * In 100 and 200 MHz mode, only a single pin rising/falling can be
 * set as trigger. In other modes, two rising/falling triggers can be set,
 * in addition to value/mask trigger for any number of channels.
 *
 * The Sigma supports complex triggers using boolean expressions, but this
 * has not been implemented yet.
 */
SR_PRIV int sigma_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	int channelbit, trigger_set;

	devc = sdi->priv;
	memset(&devc->trigger, 0, sizeof(struct sigma_trigger));
	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	trigger_set = 0;
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channelbit = 1 << (match->channel->index);
			if (devc->cur_samplerate >= SR_MHZ(100)) {
				/* Fast trigger support. */
				if (trigger_set) {
					sr_err("Only a single pin trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}
				if (match->match == SR_TRIGGER_FALLING)
					devc->trigger.fallingmask |= channelbit;
				else if (match->match == SR_TRIGGER_RISING)
					devc->trigger.risingmask |= channelbit;
				else {
					sr_err("Only rising/falling trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}

				trigger_set++;
			} else {
				/* Simple trigger support (event). */
				if (match->match == SR_TRIGGER_ONE) {
					devc->trigger.simplevalue |= channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_ZERO) {
					devc->trigger.simplevalue &= ~channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_FALLING) {
					devc->trigger.fallingmask |= channelbit;
					trigger_set++;
				}
				else if (match->match == SR_TRIGGER_RISING) {
					devc->trigger.risingmask |= channelbit;
					trigger_set++;
				}

				/*
				 * Actually, Sigma supports 2 rising/falling triggers,
				 * but they are ORed and the current trigger syntax
				 * does not permit ORed triggers.
				 */
				if (trigger_set > 1) {
					sr_err("Only 1 rising/falling trigger "
						   "is supported.");
					return SR_ERR;
				}
			}
		}
	}

	return SR_OK;
}


/* Software trigger to determine exact trigger position. */
static int get_trigger_offset(uint8_t *samples, uint16_t last_sample,
			      struct sigma_trigger *t)
{
	int i;
	uint16_t sample = 0;

	for (i = 0; i < 8; i++) {
		if (i > 0)
			last_sample = sample;
		sample = samples[2 * i] | (samples[2 * i + 1] << 8);

		/* Simple triggers. */
		if ((sample & t->simplemask) != t->simplevalue)
			continue;

		/* Rising edge. */
		if (((last_sample & t->risingmask) != 0) ||
		    ((sample & t->risingmask) != t->risingmask))
			continue;

		/* Falling edge. */
		if ((last_sample & t->fallingmask) != t->fallingmask ||
		    (sample & t->fallingmask) != 0)
			continue;

		break;
	}

	/* If we did not match, return original trigger pos. */
	return i & 0x7;
}

/*
 * Return the timestamp of "DRAM cluster".
 */
static uint16_t sigma_dram_cluster_ts(struct sigma_dram_cluster *cluster)
{
	return (cluster->timestamp_hi << 8) | cluster->timestamp_lo;
}

static void sigma_decode_dram_cluster(struct sigma_dram_cluster *dram_cluster,
				      unsigned int events_in_cluster,
				      unsigned int triggered,
				      struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sigma_state *ss = &devc->state;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint16_t tsdiff, ts;
	uint8_t samples[2048];
	unsigned int i;

	ts = sigma_dram_cluster_ts(dram_cluster);
	tsdiff = ts - ss->lastts;
	ss->lastts = ts;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = 2;
	logic.data = samples;

	/*
	 * First of all, send Sigrok a copy of the last sample from
	 * previous cluster as many times as needed to make up for
	 * the differential characteristics of data we get from the
	 * Sigma. Sigrok needs one sample of data per period.
	 *
	 * One DRAM cluster contains a timestamp and seven samples,
	 * the units of timestamp are "devc->period_ps" , the first
	 * sample in the cluster happens at the time of the timestamp
	 * and the remaining samples happen at timestamp +1...+6 .
	 */
	for (ts = 0; ts < tsdiff - (EVENTS_PER_CLUSTER - 1); ts++) {
		i = ts % 1024;
		samples[2 * i + 0] = ss->lastsample & 0xff;
		samples[2 * i + 1] = ss->lastsample >> 8;

		/*
		 * If we have 1024 samples ready or we're at the
		 * end of submitting the padding samples, submit
		 * the packet to Sigrok.
		 */
		if ((i == 1023) || (ts == (tsdiff - EVENTS_PER_CLUSTER))) {
			logic.length = (i + 1) * logic.unitsize;
			sr_session_send(sdi, &packet);
		}
	}

	/*
	 * Parse the samples in current cluster and prepare them
	 * to be submitted to Sigrok.
	 */
	for (i = 0; i < events_in_cluster; i++) {
		samples[2 * i + 1] = dram_cluster->samples[i].sample_lo;
		samples[2 * i + 0] = dram_cluster->samples[i].sample_hi;
	}

	/* Send data up to trigger point (if triggered). */
	int trigger_offset = 0;
	if (triggered) {
		/*
		 * Trigger is not always accurate to sample because of
		 * pipeline delay. However, it always triggers before
		 * the actual event. We therefore look at the next
		 * samples to pinpoint the exact position of the trigger.
		 */
		trigger_offset = get_trigger_offset(samples,
					ss->lastsample, &devc->trigger);

		if (trigger_offset > 0) {
			packet.type = SR_DF_LOGIC;
			logic.length = trigger_offset * logic.unitsize;
			sr_session_send(sdi, &packet);
			events_in_cluster -= trigger_offset;
		}

		/* Only send trigger if explicitly enabled. */
		if (devc->use_triggers) {
			packet.type = SR_DF_TRIGGER;
			sr_session_send(sdi, &packet);
		}
	}

	if (events_in_cluster > 0) {
		packet.type = SR_DF_LOGIC;
		logic.length = events_in_cluster * logic.unitsize;
		logic.data = samples + (trigger_offset * logic.unitsize);
		sr_session_send(sdi, &packet);
	}

	ss->lastsample =
		samples[2 * (events_in_cluster - 1) + 0] |
		(samples[2 * (events_in_cluster - 1) + 1] << 8);

}

/*
 * Decode chunk of 1024 bytes, 64 clusters, 7 events per cluster.
 * Each event is 20ns apart, and can contain multiple samples.
 *
 * For 200 MHz, events contain 4 samples for each channel, spread 5 ns apart.
 * For 100 MHz, events contain 2 samples for each channel, spread 10 ns apart.
 * For 50 MHz and below, events contain one sample for each channel,
 * spread 20 ns apart.
 */
static int decode_chunk_ts(struct sigma_dram_line *dram_line,
			   uint16_t events_in_line,
			   uint32_t trigger_event,
			   struct sr_dev_inst *sdi)
{
	struct sigma_dram_cluster *dram_cluster;
	struct dev_context *devc = sdi->priv;
	unsigned int clusters_in_line =
		(events_in_line + (EVENTS_PER_CLUSTER - 1)) / EVENTS_PER_CLUSTER;
	unsigned int events_in_cluster;
	unsigned int i;
	uint32_t trigger_cluster = ~0, triggered = 0;

	/* Check if trigger is in this chunk. */
	if (trigger_event < (64 * 7)) {
		if (devc->cur_samplerate <= SR_MHZ(50)) {
			trigger_event -= MIN(EVENTS_PER_CLUSTER - 1,
					     trigger_event);
		}

		/* Find in which cluster the trigger occurred. */
		trigger_cluster = trigger_event / EVENTS_PER_CLUSTER;
	}

	/* For each full DRAM cluster. */
	for (i = 0; i < clusters_in_line; i++) {
		dram_cluster = &dram_line->cluster[i];

		/* The last cluster might not be full. */
		if ((i == clusters_in_line - 1) &&
		    (events_in_line % EVENTS_PER_CLUSTER)) {
			events_in_cluster = events_in_line % EVENTS_PER_CLUSTER;
		} else {
			events_in_cluster = EVENTS_PER_CLUSTER;
		}

		triggered = (i == trigger_cluster);
		sigma_decode_dram_cluster(dram_cluster, events_in_cluster,
					  triggered, sdi);
	}

	return SR_OK;
}

static int download_capture(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const uint32_t chunks_per_read = 32;
	struct sigma_dram_line *dram_line;
	int bufsz;
	uint32_t stoppos, triggerpos;
	uint8_t modestatus;

	uint32_t i;
	uint32_t dl_lines_total, dl_lines_curr, dl_lines_done;
	uint32_t dl_events_in_line = 64 * 7;
	uint32_t trg_line = ~0, trg_event = ~0;

	dram_line = g_try_malloc0(chunks_per_read * sizeof(*dram_line));
	if (!dram_line)
		return FALSE;

	sr_info("Downloading sample data.");

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11, devc);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02, devc);

	/* Get the current position. */
	sigma_read_pos(&stoppos, &triggerpos, devc);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, devc);
	if (modestatus & 0x20) {
		trg_line = triggerpos >> 9;
		trg_event = triggerpos & 0x1ff;
	}

	/*
	 * Determine how many 1024b "DRAM lines" do we need to read from the
	 * Sigma so we have a complete set of samples. Note that the last
	 * line can be only partial, containing less than 64 clusters.
	 */
	dl_lines_total = (stoppos >> 9) + 1;

	dl_lines_done = 0;

	while (dl_lines_total > dl_lines_done) {
		/* We can download only up-to 32 DRAM lines in one go! */
		dl_lines_curr = MIN(chunks_per_read, dl_lines_total);

		bufsz = sigma_read_dram(dl_lines_done, dl_lines_curr,
					(uint8_t *)dram_line, devc);
		/* TODO: Check bufsz. For now, just avoid compiler warnings. */
		(void)bufsz;

		/* This is the first DRAM line, so find the initial timestamp. */
		if (dl_lines_done == 0) {
			devc->state.lastts =
				sigma_dram_cluster_ts(&dram_line[0].cluster[0]);
			devc->state.lastsample = 0;
		}

		for (i = 0; i < dl_lines_curr; i++) {
			uint32_t trigger_event = ~0;
			/* The last "DRAM line" can be only partially full. */
			if (dl_lines_done + i == dl_lines_total - 1)
				dl_events_in_line = stoppos & 0x1ff;

			/* Test if the trigger happened on this line. */
			if (dl_lines_done + i == trg_line)
				trigger_event = trg_event;

			decode_chunk_ts(dram_line + i, dl_events_in_line,
					trigger_event, sdi);
		}

		dl_lines_done += dl_lines_curr;
	}

	std_session_send_df_end(sdi);

	sdi->driver->dev_acquisition_stop(sdi);

	g_free(dram_line);

	return TRUE;
}

/*
 * Handle the Sigma when in CAPTURE mode. This function checks:
 * - Sampling time ended
 * - DRAM capacity overflow
 * This function triggers download of the samples from Sigma
 * in case either of the above conditions is true.
 */
static int sigma_capture_mode(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	uint64_t running_msec;
	struct timeval tv;

	uint32_t stoppos, triggerpos;

	/* Check if the selected sampling duration passed. */
	gettimeofday(&tv, 0);
	running_msec = (tv.tv_sec - devc->start_tv.tv_sec) * 1000 +
		       (tv.tv_usec - devc->start_tv.tv_usec) / 1000;
	if (running_msec >= devc->limit_msec)
		return download_capture(sdi);

	/* Get the position in DRAM to which the FPGA is writing now. */
	sigma_read_pos(&stoppos, &triggerpos, devc);
	/* Test if DRAM is full and if so, download the data. */
	if ((stoppos >> 9) == 32767)
		return download_capture(sdi);

	return TRUE;
}

SR_PRIV int sigma_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if (devc->state.state == SIGMA_IDLE)
		return TRUE;

	if (devc->state.state == SIGMA_CAPTURE)
		return sigma_capture_mode(sdi);

	return TRUE;
}

/* Build a LUT entry used by the trigger functions. */
static void build_lut_entry(uint16_t value, uint16_t mask, uint16_t *entry)
{
	int i, j, k, bit;

	/* For each quad channel. */
	for (i = 0; i < 4; i++) {
		entry[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; j++)

			/* For each channel in quad. */
			for (k = 0; k < 4; k++) {
				bit = 1 << (i * 4 + k);

				/* Set bit in entry */
				if ((mask & bit) && ((!(value & bit)) !=
							(!(j & (1 << k)))))
					entry[i] &= ~(1 << j);
			}
	}
}

/* Add a logical function to LUT mask. */
static void add_trigger_function(enum triggerop oper, enum triggerfunc func,
				 int index, int neg, uint16_t *mask)
{
	int i, j;
	int x[2][2], tmp, a, b, aset, bset, rset;

	memset(x, 0, 4 * sizeof(int));

	/* Trigger detect condition. */
	switch (oper) {
	case OP_LEVEL:
		x[0][1] = 1;
		x[1][1] = 1;
		break;
	case OP_NOT:
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_RISE:
		x[0][1] = 1;
		break;
	case OP_FALL:
		x[1][0] = 1;
		break;
	case OP_RISEFALL:
		x[0][1] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTRISE:
		x[1][1] = 1;
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		x[0][1] = 1;
		break;
	case OP_NOTRISEFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		break;
	}

	/* Transpose if neg is set. */
	if (neg) {
		for (i = 0; i < 2; i++) {
			for (j = 0; j < 2; j++) {
				tmp = x[i][j];
				x[i][j] = x[1 - i][1 - j];
				x[1 - i][1 - j] = tmp;
			}
		}
	}

	/* Update mask with function. */
	for (i = 0; i < 16; i++) {
		a = (i >> (2 * index + 0)) & 1;
		b = (i >> (2 * index + 1)) & 1;

		aset = (*mask >> i) & 1;
		bset = x[b][a];

		rset = 0;
		if (func == FUNC_AND || func == FUNC_NAND)
			rset = aset & bset;
		else if (func == FUNC_OR || func == FUNC_NOR)
			rset = aset | bset;
		else if (func == FUNC_XOR || func == FUNC_NXOR)
			rset = aset ^ bset;

		if (func == FUNC_NAND || func == FUNC_NOR || func == FUNC_NXOR)
			rset = !rset;

		*mask &= ~(1 << i);

		if (rset)
			*mask |= 1 << i;
	}
}

/*
 * Build trigger LUTs used by 50 MHz and lower sample rates for supporting
 * simple pin change and state triggers. Only two transitions (rise/fall) can be
 * set at any time, but a full mask and value can be set (0/1).
 */
SR_PRIV int sigma_build_basic_trigger(struct triggerlut *lut, struct dev_context *devc)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Constant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(devc->trigger.simplevalue, devc->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; i++) {
		if (devc->trigger.risingmask & (1 << i) ||
		    devc->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}
