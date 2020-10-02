/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <arch/cpu.h>
#include <sys/byteorder.h>
#include <logging/log.h>
#include <sys/util.h>

#include <net/buf.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/buf.h>
#include <bluetooth/hci_raw.h>

#include <shmem_nrf53.h>

#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_MODULE_NAME hci_rpmsg
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DRV_SHMEM_CMD 0x0001
#define DRV_SHMEM_ACL 0x0002
#define DRV_SHMEM_SCO 0x0003
#define DRV_SHMEM_EVT 0x0004
#define DRV_SHMEM_EVT_DISCARDABLE 0x0005

static bool receive_buf() // TODO: check if __attribute__ ((noinline)) and bt_recv(buf) moved outside will reduce stack usage
{
	int err;
	int length;
	uint16_t pkt_indicator;
	struct net_buf *buf;

	length = shmem_rx_wait(&pkt_indicator);
	LOG_WRN("FIFO ->");
	if (length < 0) {
		LOG_ERR("RX error %d", length);
		return false;
	}

	switch (pkt_indicator & 0xFF) {

	case DRV_SHMEM_CMD:
		LOG_ERR("app -> net CMD %d", length);
		buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
		break;

	case DRV_SHMEM_ACL:
		LOG_ERR("app -> net ACL %d", length);
		buf = bt_buf_get_tx(BT_BUF_ACL_OUT, K_FOREVER, NULL, 0);
		break;

	default:
		LOG_ERR("Unknown HCI type %u", pkt_indicator);
		goto skip_input;
	}

	if (buf == NULL) {
		LOG_ERR("Buffer allocation failed!");
		goto skip_input;
	}

	if (length > net_buf_tailroom(buf)) {
		LOG_ERR("Buffer too small, required %d, current %d!", length, net_buf_tailroom(buf));
		goto unref_buf_and_skip;
	}

	net_buf_add(buf, length);

	err = shmem_rx_recv(buf->data, buf->len, NULL);

	if (err < 0) {
		LOG_ERR("RX error %d!", err);
		net_buf_unref(buf);
		return false;
	}

	err = bt_send(buf);
	static int sum = 0;
	sum += buf->len;
	LOG_WRN("-> CTRL %d", sum);
	if (err) {
		LOG_ERR("Unable to send %d", err);
		net_buf_unref(buf);
	}

	return true;

unref_buf_and_skip:
	net_buf_unref(buf);
skip_input:
	shmem_rx_recv(NULL, 0, NULL);
	return true;
}

static bool is_hci_event_discardable(const uint8_t *evt_data)
{
	uint8_t evt_type = evt_data[0];

	switch (evt_type) {
#if defined(CONFIG_BT_BREDR)
	case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
	case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
		return true;
#endif
	case BT_HCI_EVT_LE_META_EVENT: {
		uint8_t subevt_type = evt_data[sizeof(struct bt_hci_evt_hdr)];

		switch (subevt_type) {
		case BT_HCI_EVT_LE_ADVERTISING_REPORT:
			return true;
		default:
			return false;
		}
	}
	default:
		return false;
	}
}

static int hci_rpmsg_send(struct net_buf *buf)
{
	struct bt_hci_evt_hdr *hdr;
	uint16_t pkt_indicator;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf),
		buf->len);

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_IN:
		LOG_ERR("app <- net ACL %d", buf->len);
		pkt_indicator = DRV_SHMEM_ACL;
		break;
	case BT_BUF_EVT:
		LOG_ERR("app <- net EVT %d", buf->len);
		hdr = (struct bt_hci_evt_hdr *)buf->data;
		if (is_hci_event_discardable(buf->data)) {
			pkt_indicator = DRV_SHMEM_EVT_DISCARDABLE;
		} else {
			pkt_indicator = DRV_SHMEM_EVT;
		}
		pkt_indicator |= (uint16_t)hdr->evt << 8;
		break;
	default:
		LOG_ERR("Unknown type %u", bt_buf_get_type(buf));
		return -EINVAL;
	}

	int r = shmem_tx_send(buf->data, buf->len, pkt_indicator);
	static int sum = 0;
	sum += buf->len;
	LOG_WRN("-> FIFO %d", sum);
	return r;
}

#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER)
void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	LOG_ERR("Controller assert in: %s at %d", file, line);
}
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER */

static K_THREAD_STACK_DEFINE(rx_thread_stack, 2048); // TODO: configurable
static struct k_thread rx_thread_data;

static void rx_thread(void *p1, void *p2, void *p3)
{
	bool ok;

	do {
		ok = receive_buf();
	} while (ok);

	LOG_ERR("FATAL SHMEM FIFO ERROR. HCI transfer stopped.");
}

void main(void)
{
	int err;

	/* incoming events and data from the controller */
	static K_FIFO_DEFINE(rx_queue);

	err = shmem_init();
	if (err < 0) {
		LOG_ERR("SHMEM init error. Controller will not work.");
		return;
	}

	LOG_INF("Start");

	/* Enable the raw interface, this will in turn open the HCI driver */
	bt_enable_raw(&rx_queue);

	/* Create receive thread */
	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack), rx_thread,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT); // TODO: priority configurable
	k_thread_name_set(&rx_thread_data, "HCI shmem RX");

	while (1) {
		struct net_buf *buf;

		buf = net_buf_get(&rx_queue, K_FOREVER);
		LOG_WRN("CTRL ->");
		err = hci_rpmsg_send(buf);
		net_buf_unref(buf);
		if (err) {
			LOG_ERR("Failed to send (err %d)", err);
		}
	}
}
