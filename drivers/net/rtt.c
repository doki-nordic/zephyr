/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * SLIP driver using uart_pipe. This is meant for network connectivity between
 * host and qemu. The host will need to run tunslip process.
 */

#define SYS_LOG_DOMAIN "dev/eth_rtt"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_ETHERNET_LEVEL
#include <logging/sys_log.h>
#include <stdio.h>

#include <kernel.h>

#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <misc/util.h>
#include <net/ethernet.h>
#include <net/buf.h>
#include <net/net_pkt.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/lldp.h>
#include <console/uart_pipe.h>
#include <net/ethernet.h>
#include <crc16.h>
#include <rtt/SEGGER_RTT.h>

#define CONFIG_ETH_RTT_MAC_ADDR "00:00:12:34:56:78"
#define CONFIG_ETH_RTT_DRV_NAME "net_rtt"

#define CONFIG_ETH_RTT_CHANNEL 2
#define CONFIG_ETH_RTT_UP_BUFFER_SIZE 3072
#define CONFIG_ETH_RTT_DOWN_BUFFER_SIZE 3072

BUILD_ASSERT_MSG(CONFIG_ETH_RTT_CHANNEL < SEGGER_RTT_MAX_NUM_UP_BUFFERS,
	"RTT channel number used in RTT network driver "
	"must be lower than SEGGER_RTT_MAX_NUM_UP_BUFFERS");

#define CHANNEL_NAME "NET_TAP"


#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335

#define ETH_RTT_MTU 1500
#define ETH_RTT_RX_BUFFER_SIZE (ETH_RTT_MTU + 36)

struct eth_rtt_context {
	bool init_done;
	struct net_if *iface;
	u16_t crc;
	u8_t mac_addr[6];
	u8_t rx_buffer[ETH_RTT_RX_BUFFER_SIZE];
	size_t rx_buffer_length;
	u8_t rtt_up_buffer[CONFIG_ETH_RTT_UP_BUFFER_SIZE];
	u8_t rtt_down_buffer[CONFIG_ETH_RTT_DOWN_BUFFER_SIZE];
};

static const u8_t reset_packet_data[] = {
		SLIP_END, // BEGIN OF PACKET
		0, 0, 0, 0, 0, 0, // DUMMY MAC ADDRESS
		0, 0, 0, 0, 0, 0, // DUMMY MAC ADDRESS
		254, 255,         // CUSTOM ETH TYPE
		216, 33, 105, 148, 78, 111, 203, 53, 32,    // RANDOM PAYLOAD
		137, 247, 122, 100, 72, 129, 255, 204, 173, // RANDOM PAYLOAD
		54, 81, // CRC
		SLIP_END, // END OF PACKET
		};

static struct eth_rtt_context context_data;

/*********** OUTPUT PART OF THE DRIVER (from network stack to RTT) ***********/

static void rtt_send_begin(struct eth_rtt_context *context)
{
	u8_t data = SLIP_END;
	printk("BEGIN\n");
	SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, &data, sizeof(data));
	context->crc = 0xFFFF;
}

static void rtt_send_fragment(struct eth_rtt_context *context, u8_t *ptr, int len)
{
	printk("FRAG %d\n", len);
	u8_t *end = ptr + len;
	u8_t *plain_begin = ptr;

	context->crc = crc16_ccitt(context->crc, ptr, len);

	while (ptr < end)
	{
		if (*ptr == SLIP_END || *ptr == SLIP_ESC)
		{
			if (ptr > plain_begin)
			{
				SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, plain_begin, ptr - plain_begin);
			}

			if (*ptr == SLIP_END)
			{
				static const u8_t data[2] = { SLIP_ESC, SLIP_ESC_END };
				SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, data, sizeof(data));
			}
			else if (*ptr == SLIP_ESC)
			{
				static const u8_t data[2] = { SLIP_ESC, SLIP_ESC_ESC };
				SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, data, sizeof(data));
			}
			plain_begin = ptr + 1;
		}
		ptr++;
	}

	if (ptr > plain_begin)
	{
		SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, plain_begin, ptr - plain_begin);
	}
}

static void rtt_send_end(struct eth_rtt_context *context)
{
	u8_t crc_buffer[2] = { context->crc >> 8, context->crc & 0xFF };
	u8_t data = SLIP_END;
	rtt_send_fragment(context, crc_buffer, sizeof(crc_buffer));
	SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, &data, sizeof(data));
	printk("END\n");
}

static int eth_iface_send(struct net_if *iface, struct net_pkt *pkt)
{
	struct device *dev = net_if_get_device(iface);
	struct eth_rtt_context *context = dev->driver_data;
	struct net_buf *frag;

	if (!pkt->frags) {
		return -ENODATA;
	}

	rtt_send_begin(context);

	rtt_send_fragment(context, net_pkt_ll(pkt), net_pkt_ll_reserve(pkt));

	for (frag = pkt->frags; frag; frag = frag->frags)
	{
		rtt_send_fragment(context, frag->data, frag->len);
	};

	rtt_send_end(context);

	net_pkt_unref(pkt);
	
	return 0;
}

/*********** INPUT PART OF THE DRIVER (from RTT to network stack) ***********/

static void recv_packet(struct eth_rtt_context *context, u8_t* data, int len)
{
	struct net_pkt *pkt;
	struct net_buf *pkt_buf = NULL;
	struct net_buf *last_buf = NULL;

	if (len <= 2)
	{
		if (len > 0)
		{
			printk("Invalid packet length\n");
		}
		return;
	}

	u16_t crc16 = crc16_ccitt(0xFFFF, data, len - 2);
	if (data[len - 2] != (crc16 >> 8) || data[len - 1] != (crc16 & 0xFF))
	{
		printk("Invalid packet CRC\n");
		return;
	}

	len -= 2;

	pkt = net_pkt_get_reserve_rx(0, 1000); // TODO: Some timeout
	if (!pkt) {
		SYS_LOG_ERR("Could not allocate rx context->rx_buffer");
		return;
	}

	while (len > 0)
	{
		/* Reserve a data frag to receive the frame */
		pkt_buf = net_pkt_get_frag(pkt, 1000); // TODO: Some timeout
		if (!pkt_buf) {
			SYS_LOG_ERR("Could not allocate data context->rx_buffer");
			net_pkt_unref(pkt);
			return;
		}

		if (!last_buf) {
			net_pkt_frag_insert(pkt, pkt_buf);
		} else {
			net_buf_frag_insert(last_buf, pkt_buf);
		}

		last_buf = pkt_buf;

		size_t frag_len = net_buf_tailroom(pkt_buf);

		if (len < frag_len) {
			frag_len = len;
		}

		memcpy(pkt_buf->data, data, frag_len);

		len -= frag_len;
		data += frag_len;

		net_buf_add(pkt_buf, frag_len);
	}

	net_recv_data(context->iface, pkt);
}

static void decode_new_slip_data(struct eth_rtt_context *context, int new_data_size)
{
	u8_t *src = &context->rx_buffer[context->rx_buffer_length];
	u8_t *dst = &context->rx_buffer[context->rx_buffer_length];
	u8_t *end = src + new_data_size;
	u8_t *packet_start = context->rx_buffer;
	u8_t last_byte = context->rx_buffer_length > 0 ? dst[-1] : 0;

	while (src < end)
	{
		u8_t byte = *src++;
		*dst++ = byte;
		if (byte == SLIP_END)
		{
			recv_packet(context, packet_start, dst - packet_start - 1);
			packet_start = dst;
		}
		else if (last_byte == SLIP_ESC)
		{
			if (byte == SLIP_ESC_END)
			{
				dst--;
				dst[-1] = SLIP_END;
			}
			else if (byte == SLIP_ESC_ESC)
			{
				dst--;
				dst[-1] = SLIP_ESC;
			}
		}
		last_byte = byte;
	}

	context->rx_buffer_length = dst - packet_start;

	if (context->rx_buffer_length > 0 && packet_start != context->rx_buffer)
	{
		memmove(context->rx_buffer, packet_start, context->rx_buffer_length);
	}
}

static void poll_timer_handler(struct k_timer *dummy);

K_TIMER_DEFINE(eth_rtt_poll_timer, poll_timer_handler, NULL);

static void poll_work_handler(struct k_work *work)
{
	struct eth_rtt_context *context = &context_data;
	int total = 0;
	int num;

	do {
		num = SEGGER_RTT_Read(CONFIG_ETH_RTT_CHANNEL,
				&context->rx_buffer[context->rx_buffer_length],
				sizeof(context->rx_buffer) - context->rx_buffer_length);
		if (num > 0)
		{
			decode_new_slip_data(context, num);
			printk("READ: %d\n", num);
			total += num;
		}
	} while (num > 0);

	k_timer_start(&eth_rtt_poll_timer, total == 0 ? K_MSEC(20) : K_MSEC(1), //TODO: timeouts from CONFIG_*
			K_MSEC(5000));
}

K_WORK_DEFINE(eth_rtt_poll_work, poll_work_handler);

static void poll_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&eth_rtt_poll_work);
}

/*********** COMMON PART OF THE DRIVER (from RTT to network stack) ***********/

static void eth_iface_init(struct net_if *iface)
{
	struct eth_rtt_context *context = net_if_get_device(iface)->driver_data;

	ethernet_init(iface);

	if (context->init_done) {
		return;
	}

	context->init_done = true;
	context->iface = iface;

	bool mac_addr_configured = false;

#if defined(CONFIG_ETH_RTT_MAC_ADDR)
	if (CONFIG_ETH_RTT_MAC_ADDR[0] != 0) {
		mac_addr_configured = (net_bytes_from_str(context->mac_addr,
				sizeof(context->mac_addr), CONFIG_ETH_RTT_MAC_ADDR) >= 0);
	}
#endif

	if (!mac_addr_configured)
	{
		/* 00-00-5E-00-53-xx Documentation RFC 7042 */
		context->mac_addr[0] = 0x00;
		context->mac_addr[1] = 0x00;
		context->mac_addr[2] = 0x5E;
		context->mac_addr[3] = 0x00;
		context->mac_addr[4] = 0x53;
		context->mac_addr[5] = sys_rand32_get();
	}

	net_if_set_link_addr(iface, context->mac_addr, sizeof(context->mac_addr),
			     NET_LINK_ETHERNET);

	k_timer_start(&eth_rtt_poll_timer, K_MSEC(50), K_MSEC(5000));

	SEGGER_RTT_Write(CONFIG_ETH_RTT_CHANNEL, &reset_packet_data, sizeof(reset_packet_data));
}

static enum ethernet_hw_caps eth_capabilities(struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int eth_rtt_init(struct device *dev)
{
	struct eth_rtt_context *context = dev->driver_data;

	SEGGER_RTT_ConfigUpBuffer(CONFIG_ETH_RTT_CHANNEL, CHANNEL_NAME, context->rtt_up_buffer, sizeof(context->rtt_up_buffer), SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
	SEGGER_RTT_ConfigDownBuffer(CONFIG_ETH_RTT_CHANNEL, CHANNEL_NAME, context->rtt_down_buffer, sizeof(context->rtt_down_buffer), SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

	SYS_LOG_DBG("[%p] dev %p", context, dev);

	return 0;
}

static const struct ethernet_api if_api = {
	.iface_api.init = eth_iface_init,
	.iface_api.send = eth_iface_send,
	.get_capabilities = eth_capabilities,
};

ETH_NET_DEVICE_INIT(eth_rtt, CONFIG_ETH_RTT_DRV_NAME, eth_rtt_init,
		&context_data, NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &if_api,
		ETH_RTT_MTU);
