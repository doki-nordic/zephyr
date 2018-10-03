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

#define SYS_LOG_DOMAIN "slip"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_SLIP_LEVEL
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
#include <rtt/SEGGER_RTT.h>

#define CONFIG_SLIP_TAP 1
#define CONFIG_SLIP_MAC_ADDR "00:00:12:34:56:78"
#define CONFIG_SLIP_DRV_NAME "net_rtt"

#define CONFIG_NET_RTT_CHANNEL 2
#define CONFIG_NET_RTT_UP_BUFFER_SIZE 6144
#define CONFIG_NET_RTT_DOWN_BUFFER_SIZE 6144

BUILD_ASSERT_MSG(CONFIG_NET_RTT_CHANNEL < SEGGER_RTT_MAX_NUM_UP_BUFFERS,
	"RTT channel number used in RTT network driver "
	"must be lower than SEGGER_RTT_MAX_NUM_UP_BUFFERS");

#define CHANNEL_NAME "NET_TAP"


#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335

enum slip_state {
	STATE_GARBAGE,
	STATE_OK,
	STATE_ESC,
};

struct slip_context {
	bool init_done;
	bool first;		/* SLIP received it's byte or not after
				 * driver initialization or SLIP_END byte.
				 */
	u8_t buf[1];		/* SLIP data is read into this buf */
	struct net_pkt *rx;	/* and then placed into this net_pkt */
	struct net_buf *last;	/* Pointer to last fragment in the list */
	u8_t *ptr;		/* Where in net_pkt to add data */
	struct net_if *iface;
	u8_t state;

	u8_t mac_addr[6];
	struct net_linkaddr ll_addr;

#if defined(CONFIG_SLIP_STATISTICS)
#define SLIP_STATS(statement)
#else
	u16_t garbage;
#define SLIP_STATS(statement) statement
#endif
	u8_t rtt_up_buffer[CONFIG_NET_RTT_UP_BUFFER_SIZE];
	u8_t rtt_down_buffer[CONFIG_NET_RTT_DOWN_BUFFER_SIZE];
};

#if defined(CONFIG_NET_LLDP)
static const struct net_lldpdu lldpdu = {
	.chassis_id = {
		.type_length = htons((LLDP_TLV_CHASSIS_ID << 9) |
			NET_LLDP_CHASSIS_ID_TLV_LEN),
		.subtype = CONFIG_NET_LLDP_CHASSIS_ID_SUBTYPE,
		.value = NET_LLDP_CHASSIS_ID_VALUE
	},
	.port_id = {
		.type_length = htons((LLDP_TLV_PORT_ID << 9) |
			NET_LLDP_PORT_ID_TLV_LEN),
		.subtype = CONFIG_NET_LLDP_PORT_ID_SUBTYPE,
		.value = NET_LLDP_PORT_ID_VALUE
	},
	.ttl = {
		.type_length = htons((LLDP_TLV_TTL << 9) |
			NET_LLDP_TTL_TLV_LEN),
		.ttl = htons(NET_LLDP_TTL)
	},
#if defined(CONFIG_NET_LLDP_END_LLDPDU_TLV_ENABLED)
	.end_lldpdu_tlv = NET_LLDP_END_LLDPDU_VALUE
#endif /* CONFIG_NET_LLDP_END_LLDPDU_TLV_ENABLED */
};

#define lldpdu_ptr (&lldpdu)
#else
#define lldpdu_ptr NULL
#endif /* CONFIG_NET_LLDP */


static struct slip_context slip_context_data;

#if SYS_LOG_LEVEL >= SYS_LOG_LEVEL_DEBUG
#if defined(CONFIG_SYS_LOG_SHOW_COLOR)
#define COLOR_OFF     "\x1B[0m"
#define COLOR_YELLOW  "\x1B[0;33m"
#else
#define COLOR_OFF     ""
#define COLOR_YELLOW  ""
#endif

static void hexdump(const char *str, const u8_t *packet,
		    size_t length, size_t ll_reserve)
{
	int n = 0;

	if (!length) {
		SYS_LOG_DBG("%s zero-length packet", str);
		return;
	}

	while (length--) {
		if (n % 16 == 0) {
			printf("%s %08X ", str, n);
		}

#if defined(CONFIG_SYS_LOG_SHOW_COLOR)
		if (n < ll_reserve) {
			printf(COLOR_YELLOW);
		} else {
			printf(COLOR_OFF);
		}
#endif
		printf("%02X ", *packet++);

#if defined(CONFIG_SYS_LOG_SHOW_COLOR)
		if (n < ll_reserve) {
			printf(COLOR_OFF);
		}
#endif
		n++;
		if (n % 8 == 0) {
			if (n % 16 == 0) {
				printf("\n");
			} else {
				printf(" ");
			}
		}
	}

	if (n % 16) {
		printf("\n");
	}
}
#else
#define hexdump(slip, str, packet, length, ll_reserve)
#endif

static inline void slip_writeb(unsigned char c)
{
	u8_t buf[1] = { c };

	uart_pipe_send(&buf[0], 1);
}

/**
 *  @brief Write byte to SLIP, escape if it is END or ESC character
 *
 *  @param c  a byte to write
 */
static void slip_writeb_esc(unsigned char c)
{
	switch (c) {
	case SLIP_END:
		/* If it's the same code as an END character,
		 * we send a special two character code so as
		 * not to make the receiver think we sent
		 * an END.
		 */
		slip_writeb(SLIP_ESC);
		slip_writeb(SLIP_ESC_END);
		break;
	case SLIP_ESC:
		/* If it's the same code as an ESC character,
		 * we send a special two character code so as
		 * not to make the receiver think we sent
		 * an ESC.
		 */
		slip_writeb(SLIP_ESC);
		slip_writeb(SLIP_ESC_ESC);
		break;
	default:
		slip_writeb(c);
	}
}

static u16_t crc16;

static void send_begin()
{
	u8_t data = SLIP_END;
	printk("BEGIN\n");
	SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, &data, sizeof(data));
	crc16 = 0xFFFF;
}

static void send_fragment(u8_t *ptr, int len)
{
	printk("FRAG %d\n", len);
	u8_t *end = ptr + len;
	u8_t *plain_begin = ptr;

	crc16 = crc16_ccitt(crc16, ptr, len);

	while (ptr < end)
	{
		if (*ptr == SLIP_END || *ptr == SLIP_ESC)
		{
			if (ptr > plain_begin)
			{
				SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, plain_begin, ptr - plain_begin);
			}

			if (*ptr == SLIP_END)
			{
				static const u8_t data[2] = { SLIP_ESC, SLIP_ESC_END };
				SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, data, sizeof(data));
			}
			else if (*ptr == SLIP_ESC)
			{
				static const u8_t data[2] = { SLIP_ESC, SLIP_ESC_ESC };
				SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, data, sizeof(data));
			}
			plain_begin = ptr + 1;
		}
		ptr++;
	}

	if (ptr > plain_begin)
	{
		SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, plain_begin, ptr - plain_begin);
	}
}

static void send_end()
{
	u8_t crc_buffer[2] = { crc16 >> 8, crc16 & 0xFF };
	u8_t data = SLIP_END;
	send_fragment(crc_buffer, sizeof(crc_buffer));
	printk("END\n");
	SEGGER_RTT_Write(CONFIG_NET_RTT_CHANNEL, &data, sizeof(data));
}

static int slip_send(struct net_if *iface, struct net_pkt *pkt)
{
	struct device *dev = net_if_get_device(iface);
	struct slip_context *context = dev->driver_data;
	struct net_buf *frag;

	if (!pkt->frags) {
		return -ENODATA;
	}

	send_begin();

	send_fragment(net_pkt_ll(pkt), net_pkt_ll_reserve(pkt));

	for (frag = pkt->frags; frag; frag = frag->frags)
	{
		send_fragment(frag->data, frag->len);
	};

	send_end();

	net_pkt_unref(pkt);
	
	return 0;
}

#if 0

static struct net_pkt *slip_poll_handler(struct slip_context *slip)
{
	if (slip->last && slip->last->len) {
		return slip->rx;
	}

	return NULL;
}

static inline struct net_if *get_iface(struct slip_context *context,
				       u16_t vlan_tag)
{
#if defined(CONFIG_NET_VLAN)
	struct net_if *iface;

	iface = net_eth_get_vlan_iface(context->iface, vlan_tag);
	if (!iface) {
		return context->iface;
	}

	return iface;
#else
	ARG_UNUSED(vlan_tag);

	return context->iface;
#endif
}

static void process_msg(struct slip_context *slip)
{
	u16_t vlan_tag = NET_VLAN_TAG_UNSPEC;
	struct net_pkt *pkt;

	pkt = slip_poll_handler(slip);
	if (!pkt || !pkt->frags) {
		return;
	}

#if defined(CONFIG_NET_VLAN)
	{
		struct net_eth_hdr *hdr = NET_ETH_HDR(pkt);

		if (ntohs(hdr->type) == NET_ETH_PTYPE_VLAN) {
			struct net_eth_vlan_hdr *hdr_vlan =
				(struct net_eth_vlan_hdr *)NET_ETH_HDR(pkt);

			net_pkt_set_vlan_tci(pkt, ntohs(hdr_vlan->vlan.tci));
			vlan_tag = net_pkt_vlan_tag(pkt);
		}
	}
#endif

	if (net_recv_data(get_iface(slip, vlan_tag), pkt) < 0) {
		net_pkt_unref(pkt);
	}

	slip->rx = NULL;
	slip->last = NULL;
}

static inline int slip_input_byte(struct slip_context *slip,
				  unsigned char c)
{
	switch (slip->state) {
	case STATE_GARBAGE:
		if (c == SLIP_END) {
			slip->state = STATE_OK;
		}

		return 0;
	case STATE_ESC:
		if (c == SLIP_ESC_END) {
			c = SLIP_END;
		} else if (c == SLIP_ESC_ESC) {
			c = SLIP_ESC;
		} else {
			slip->state = STATE_GARBAGE;
			SLIP_STATS(slip->garbage++);
			return 0;
		}

		slip->state = STATE_OK;

		break;
	case STATE_OK:
		if (c == SLIP_ESC) {
			slip->state = STATE_ESC;
			return 0;
		}

		if (c == SLIP_END) {
			slip->state = STATE_OK;
			slip->first = false;

			if (slip->rx) {
				return 1;
			}

			return 0;
		}

		if (slip->first && !slip->rx) {
			/* Must have missed buffer allocation on first byte. */
			return 0;
		}

		if (!slip->first) {
			slip->first = true;

			slip->rx = net_pkt_get_reserve_rx(0, K_NO_WAIT);
			if (!slip->rx) {
				SYS_LOG_ERR("[%p] cannot allocate pkt",
					    slip);
				return 0;
			}

			slip->last = net_pkt_get_frag(slip->rx, K_NO_WAIT);
			if (!slip->last) {
				SYS_LOG_ERR("[%p] cannot allocate 1st data frag",
					    slip);
				net_pkt_unref(slip->rx);
				slip->rx = NULL;
				return 0;
			}

			net_pkt_frag_add(slip->rx, slip->last);
			slip->ptr = net_pkt_ip_data(slip->rx);
		}

		break;
	}

	/* It is possible that slip->last is not set during the startup
	 * of the device. If this happens do not continue and overwrite
	 * some random memory.
	 */
	if (!slip->last) {
		return 0;
	}

	if (!net_buf_tailroom(slip->last)) {
		/* We need to allocate a new fragment */
		struct net_buf *frag;

		frag = net_pkt_get_reserve_rx_data(0, K_NO_WAIT);
		if (!frag) {
			SYS_LOG_ERR("[%p] cannot allocate next data frag",
				    slip);
			net_pkt_unref(slip->rx);
			slip->rx = NULL;
			slip->last = NULL;

			return 0;
		}

		net_buf_frag_insert(slip->last, frag);
		slip->last = frag;
		slip->ptr = slip->last->data;
	}

	/* The net_buf_add_u8() cannot add data to ll header so we need
	 * a way to do it.
	 */
	if (slip->ptr < slip->last->data) {
		*slip->ptr = c;
	} else {
		slip->ptr = net_buf_add_u8(slip->last, c);
	}

	slip->ptr++;

	return 0;
}

static u8_t *recv_cb(u8_t *buf, size_t *off)
{
	struct slip_context *slip =
		CONTAINER_OF(buf, struct slip_context, buf);
	size_t i;

	if (!slip->init_done) {
		*off = 0;
		return buf;
	}

	for (i = 0; i < *off; i++) {
		if (slip_input_byte(slip, buf[i])) {
#if SYS_LOG_LEVEL >= SYS_LOG_LEVEL_DEBUG
			struct net_buf *frag = slip->rx->frags;
			int bytes = net_buf_frags_len(frag);
			int count = 0;

			while (bytes && frag) {
				char msg[8 + 1];

				snprintf(msg, sizeof(msg), ">slip %2d", count);

				hexdump(msg, frag->data, frag->len, 0);

				frag = frag->frags;
				count++;
			}

			SYS_LOG_DBG("[%p] received data %d bytes", slip, bytes);
#endif
			process_msg(slip);
			break;
		}
	}

	*off = 0;

	return buf;
}
#endif

static int slip_init(struct device *dev)
{
	struct slip_context *slip = dev->driver_data;

	SEGGER_RTT_ConfigUpBuffer(CONFIG_NET_RTT_CHANNEL, CHANNEL_NAME, slip->rtt_up_buffer, sizeof(slip->rtt_up_buffer), SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
	SEGGER_RTT_ConfigDownBuffer(CONFIG_NET_RTT_CHANNEL, CHANNEL_NAME, slip->rtt_down_buffer, sizeof(slip->rtt_down_buffer), SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

	SYS_LOG_DBG("[%p] dev %p", slip, dev);

	return 0;
}

static u8_t buffer[1536];
static u32_t used_size = 0;

void my_timer_handler(struct k_timer *dummy);

void output_packet(struct slip_context *context, u8_t* data, int len)
{
	struct net_pkt *pkt;
	struct net_buf *pkt_buf = NULL;
	struct net_buf *last_buf = NULL;
	if (len <= 2)
	{
		// TODO: Log invalid packet
		return;
	}
	u16_t crc16 = crc16_ccitt(0xFFFF, data, len - 2);
	if (data[len - 2] != (crc16 >> 8) || data[len - 1] != (crc16 & 0xFF))
	{
		// TODO: Log invalid CRC
		return;
	}

	len -= 2;

	pkt = net_pkt_get_reserve_rx(0, 1000); // TODO: Some timeout
	if (!pkt) {
		SYS_LOG_ERR("Could not allocate rx buffer");
		return;
	}

	while (len > 0)
	{
		/* Reserve a data frag to receive the frame */
		pkt_buf = net_pkt_get_frag(pkt, 1000); // TODO: Some timeout
		if (!pkt_buf) {
			SYS_LOG_ERR("Could not allocate data buffer");
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

void slip_decode_new_data(struct slip_context *context, int new_data_size)
{
	u8_t *src = &buffer[used_size];
	u8_t *dst = &buffer[used_size];
	u8_t *end = src + new_data_size;
	u8_t *packet_start = buffer;
	u8_t last_byte = used_size > 0 ? dst[-1] : 0;

	while (src < end)
	{
		u8_t byte = *src++;
		*dst++ = byte;
		if (byte == SLIP_END)
		{
			output_packet(context, packet_start, dst - packet_start - 1);
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

	used_size = dst - packet_start;

	if (used_size > 0 && packet_start != buffer)
	{
		memmove(buffer, packet_start, used_size);
	}
}

K_TIMER_DEFINE(my_timer, my_timer_handler, NULL);

static void my_work_handler(struct k_work *work)
{
	int total = 0;
	int num;
	do {
		num = SEGGER_RTT_Read(CONFIG_NET_RTT_CHANNEL, &buffer[used_size], sizeof(buffer) - used_size);
		if (num > 0)
		{
			slip_decode_new_data(&slip_context_data, num);
			printk("READ: %d\n", num);
			total += num;
		}
	} while (num > 0);

	k_timer_start(&my_timer, total == 0 ? K_MSEC(50) : K_MSEC(5), K_MSEC(5000));
}

K_WORK_DEFINE(my_work, my_work_handler);

void my_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&my_work);
}


static void slip_iface_init(struct net_if *iface)
{
	struct slip_context *slip = net_if_get_device(iface)->driver_data;

	ethernet_init(iface);

	net_eth_set_lldpdu(iface, lldpdu_ptr);

	if (slip->init_done) {
		return;
	}

	slip->init_done = true;
	slip->iface = iface;

	if (CONFIG_SLIP_MAC_ADDR[0] != 0) {
		if (net_bytes_from_str(slip->mac_addr, sizeof(slip->mac_addr),
				       CONFIG_SLIP_MAC_ADDR) < 0) {
			goto use_random_mac;
		}
	} else {
use_random_mac:
		/* 00-00-5E-00-53-xx Documentation RFC 7042 */
		slip->mac_addr[0] = 0x00;
		slip->mac_addr[1] = 0x00;
		slip->mac_addr[2] = 0x5E;
		slip->mac_addr[3] = 0x00;
		slip->mac_addr[4] = 0x53;
		slip->mac_addr[5] = sys_rand32_get();
	}
	net_if_set_link_addr(iface, slip->mac_addr, sizeof(slip->mac_addr),
			     NET_LINK_ETHERNET);

	k_timer_start(&my_timer, K_MSEC(50), K_MSEC(5000));

}

static enum ethernet_hw_caps eth_capabilities(struct device *dev)
{
	ARG_UNUSED(dev);

	return ETHERNET_HW_VLAN
#if defined(CONFIG_NET_LLDP)
		| ETHERNET_LLDP
#endif
		;
}

static const struct ethernet_api slip_if_api = {
	.iface_api.init = slip_iface_init,
	.iface_api.send = slip_send,

	.get_capabilities = eth_capabilities,
};

#define _SLIP_MTU 1500

ETH_NET_DEVICE_INIT(slip, CONFIG_SLIP_DRV_NAME, slip_init, &slip_context_data,
		    NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &slip_if_api,
		    _SLIP_MTU);
