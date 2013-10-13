/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Hashfast Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "usbutils.h"

#include "driver-hashfast.h"

////////////////////////////////////////////////////////////////////////////////
// Support for the CRC's used in header (CRC-8) and packet body (CRC-32)
////////////////////////////////////////////////////////////////////////////////

#define GP8  0x107   /* x^8 + x^2 + x + 1 */
#define DI8  0x07

static unsigned char crc8_table[256];	/* CRC-8 table */

static void hf_init_crc8(void)
{
	int i,j;
	unsigned char crc;

	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80) ? DI8 : 0);
		crc8_table[i] = crc & 0xFF;
	}
}

static unsigned char __maybe_unused hf_crc8(unsigned char *h)
{
	int i;
	unsigned char crc;

	h++;	// Preamble not included
	for (i = 1, crc = 0xff; i < 7; i++)
		crc = crc8_table[crc ^ *h++];

	return crc;
}

struct hf_cmd {
	int cmd;
	char *cmd_name;
	enum usb_cmds usb_cmd;
};

#define C_NULL C_MAX
static const struct hf_cmd hf_cmds[] = {
	{OP_NULL, "OP_NULL", C_NULL},
	{OP_ROOT, "OP_ROOT", C_NULL},
	{OP_RESET, "OP_RESET", C_HF_RESET},
	{OP_PLL_CONFIG, "OP_PLL_CONFIG", C_HF_PLL_CONFIG},
	{OP_ADDRESS, "OP_ADDRESS", C_HF_ADDRESS},
	{OP_READDRESS, "OP_READDRESS", C_NULL},
	{OP_HIGHEST, "OP_HIGHEST", C_NULL},
	{OP_BAUD, "OP_BAUD", C_HF_BAUD},
	{OP_UNROOT, "OP_UNROOT", C_NULL},
	{OP_HASH, "OP_HASH", C_HF_HASH},
	{OP_NONCE, "OP_NONCE", C_HF_NONCE},
	{OP_ABORT, "OP_ABORT", C_HF_ABORT},
	{OP_STATUS, "OP_STATUS", C_HF_STATUS},
	{OP_GPIO, "OP_GPIO", C_NULL},
	{OP_CONFIG, "OP_CONFIG", C_HF_CONFIG},
	{OP_STATISTICS, "OP_STATISTICS", C_HF_STATISTICS},
	{OP_GROUP, "OP_GROUP", C_NULL},
	{OP_CLOCKGATE, "OP_CLOCKGATE", C_HF_CLOCKGATE}
};

/* Send an arbitrary frame, consisting of an 8 byte header and an optional
 * packet body. */

static int __maybe_unused hashfast_send_frame(struct cgpu_info *hashfast, uint8_t opcode,
			       uint8_t chip, uint8_t core, uint16_t hdata,
			       uint8_t *data, int len)
{
	int tx_length, ret, amount, id = hashfast->device_id;
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;

	p->preamble = HF_PREAMBLE;
	p->operation_code = opcode;
	p->chip_address = chip;
	p->core_address = core;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hf_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	tx_length = sizeof(struct hf_header);

	ret = usb_write(hashfast, (char *)packet, tx_length, &amount,
			hf_cmds[opcode].usb_cmd);
	if (ret < 0 || amount != tx_length) {
		applog(LOG_ERR, "HF%d: hashfast_send_frame: USB Send error, ret %d amount %d vs. tx_length %d",
		       id, ret, amount, tx_length);
		return 1;
	}
	return 0;
}

static int __maybe_unused hashfast_reset(struct cgpu_info __maybe_unused *hashfast)
{
	return 0;
}

static bool hashfast_detect_common(struct cgpu_info __maybe_unused *hashfast)
{
	return true;
}

static void hashfast_usb_initialise(struct cgpu_info *hashfast)
{
	if (hashfast->usbinfo.nodev)
		return;
	// FIXME Do necessary initialising here
}

static bool hashfast_detect_one_usb(libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *hashfast;

	hashfast = usb_alloc_cgpu(&hashfast_drv, HASHFAST_MINER_THREADS);
	if (!hashfast)
		return false;

	if (!usb_init(hashfast, dev, found)) {
		free(hashfast->device_data);
		hashfast->device_data = NULL;
		hashfast = usb_free_cgpu(hashfast);
		return false;
	}

	hashfast->usbdev->usb_type = USB_TYPE_STD;

	hashfast_usb_initialise(hashfast);

	add_cgpu(hashfast);
	return hashfast_detect_common(hashfast);
}

static void hashfast_detect(bool hotplug)
{
	/* Set up the CRC tables only once. */
	if (!hotplug)
		hf_init_crc8();
	usb_detect(&hashfast_drv, hashfast_detect_one_usb);
}

static bool hashfast_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static int64_t hashfast_scanwork(struct thr_info __maybe_unused *thr)
{
	return 0;
}

static struct api_data *hashfast_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void hashfast_init(struct cgpu_info *hashfast)
{
	usb_buffer_enable(hashfast);
}

static void hashfast_shutdown(struct thr_info __maybe_unused *thr)
{
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_hashfast,
	.dname = "Hashfast",
	.name = "HFA",
	.drv_detect = hashfast_detect,
	.thread_prepare = hashfast_prepare,
	.hash_work = &hash_driver_work,
	.scanwork = hashfast_scanwork,
	.get_api_stats = hashfast_api_stats,
	.reinit_device = hashfast_init,
	.thread_shutdown = hashfast_shutdown,
};
