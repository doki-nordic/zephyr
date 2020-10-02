/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <logging/log.h>
#include <drivers/ipm.h>
#include <init.h>
#include <sys/byteorder.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <drivers/bluetooth/hci_driver.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG // TODO: make it configurable
#define LOG_MODULE_NAME hci_shmem_nrf53
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "Module requires definition of shared memory for rpmsg"
#endif

#define SHM_NODE            DT_CHOSEN(zephyr_ipc_shm)
#define SHM_BASE_ADDRESS    DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE            (DT_REG_SIZE(SHM_NODE) & ~7)

/*

Resources organization:
	Shared memory:
		First half:  NET --> APP
		Second half: APP --> NET
	IPM:
		0: data: NET --> APP
		1: ack:  APP --> NET
		2: data: APP --> NET
		3: ack:  NET --> APP

*/

#if defined(CONFIG_SOC_NRF5340_CPUAPP)

#define SHM_RX_BASE_ADDRESS SHM_BASE_ADDRESS
#define SHM_RX_SIZE         (SHM_SIZE / 2)
#define IPM_RX_RECV         "IPM_0"
#define IPM_RX_ACK          "IPM_1"
#define SHM_TX_BASE_ADDRESS (SHM_BASE_ADDRESS + SHM_RX_SIZE)
#define SHM_TX_SIZE         (SHM_SIZE / 2)
#define IPM_TX_SEND         "IPM_2"
#define IPM_TX_ACK          "IPM_3"

#elif defined(CONFIG_SOC_NRF5340_CPUNET)

#define SHM_TX_BASE_ADDRESS SHM_BASE_ADDRESS
#define SHM_TX_SIZE         (SHM_SIZE / 2)
#define IPM_TX_SEND         "IPM_0"
#define IPM_TX_ACK          "IPM_1"
#define SHM_RX_BASE_ADDRESS (SHM_BASE_ADDRESS + SHM_TX_SIZE)
#define SHM_RX_SIZE         (SHM_SIZE / 2)
#define IPM_RX_RECV         "IPM_2"
#define IPM_RX_ACK          "IPM_3"

#else

#error Implemented only for nRF5340

#endif

#define ITEM_SIZE 4
#define NO_ACK 0xFFFFFFFF

/* RX FIFO */
static volatile uint32_t *const rx_read_index = (volatile uint32_t *)SHM_RX_BASE_ADDRESS;
static volatile const uint32_t *const rx_write_index = (volatile uint32_t *)SHM_RX_BASE_ADDRESS + 1;
static volatile const uint32_t *const rx_ack_index = (volatile uint32_t *)SHM_RX_BASE_ADDRESS + 2;
static const uint32_t *const rx_data = (uint32_t *)SHM_RX_BASE_ADDRESS + 3;
static const size_t rx_count = SHM_RX_SIZE / ITEM_SIZE - 3;
static struct device *rx_ipm_recv;
static struct device *rx_ipm_ack;
static K_SEM_DEFINE(rx_sem, 0, 1);

/* TX FIFO */
static volatile const uint32_t *const tx_read_index = (volatile uint32_t *)SHM_TX_BASE_ADDRESS;
static volatile uint32_t *const tx_write_index = (volatile uint32_t *)SHM_TX_BASE_ADDRESS + 1;
static volatile uint32_t *const tx_ack_index = (volatile uint32_t *)SHM_TX_BASE_ADDRESS + 2;
static uint32_t *const tx_data = (uint32_t *)SHM_TX_BASE_ADDRESS + 3;
static const size_t tx_count = SHM_TX_SIZE / ITEM_SIZE - 3;
static struct device *tx_ipm_send;
static struct device *tx_ipm_ack;
static K_SEM_DEFINE(tx_sem, 0, 1);

static void ipm_send_simple(struct device *dev)
{
	ipm_send(dev, 0, 0, NULL, 0);
}

int shmem_tx_send(const uint8_t* data, uint16_t size, uint16_t oob_data)
{
	uint32_t read_index;
	uint32_t write_index;
	size_t available;
	size_t data_items = ((size_t)size + (ITEM_SIZE - 1)) / ITEM_SIZE;
	size_t total_items = 1 + data_items;
	size_t tail_items;

	if (total_items > tx_count) {
		return -ENOMEM;
	}

	// TODO: lock here if needed

	read_index = *tx_read_index;
	write_index = *tx_write_index;

	if (read_index >= tx_count || write_index >= tx_count) {
		return -EIO;
	}

	/* Make sure that there is enough space in FIFO. */
	do {
		if (read_index <= write_index) {
			available = tx_count - (write_index - read_index) - 1;
		} else {
			available = read_index - write_index - 1;
		}
		/* If there is not enough, inform remote that ACK is needed. */
		if (available < total_items) {
			*tx_ack_index = read_index;
			__DMB();
			/* Skip waiting if something already was consumed. */
			if (*tx_read_index == read_index) {
				//LOG_INF("WAITING available=%d, total=%d", available, total_items);
				k_sem_take(&tx_sem, K_FOREVER);
			}
			*tx_ack_index = NO_ACK;
			read_index = *tx_read_index;
			write_index = *tx_write_index;
		}
	} while (available < total_items);

	/* Write header. */
	tx_data[write_index] = (uint32_t)size | ((uint32_t)oob_data << 16);
	write_index++;
	if (write_index >= tx_count) {
		write_index = 0;
		//LOG_WRN("============================ WRITE CYCLE");
	}

	/* Write first part of data if buffer cycle occurred. */
	if (write_index >= read_index) {
		tail_items = tx_count - write_index;
		if (data_items >= tail_items) {
			if (data_items > tail_items) {
				memcpy(&tx_data[write_index], data, tail_items * ITEM_SIZE);
				data += tail_items * ITEM_SIZE;
				size -= tail_items * ITEM_SIZE;
			} else {
				memcpy(&tx_data[write_index], data, size);
				size = 0;
			}
			data_items -= tail_items;
			write_index = 0;
			//LOG_WRN("============================ WRITE CYCLE");
		}
	}

	/* Write remaining part of data. */
	memcpy(&tx_data[write_index], data, size);
	write_index += data_items;

	/* Make sure that all data was written before updating the index. */
	__DMB();

	/* Update write index, so make data available for the remote. */
	*tx_write_index = write_index;

	// TODO: unlock if needed

	/* Make sure that index was updated in RAM before informing remote. */
	__DSB();

	/* Inform remote about a new data */
	ipm_send_simple(tx_ipm_send);

	return 0;
}

int shmem_rx_wait(uint16_t *oob_data)
{
	uint32_t header;
	uint32_t read_index;
	uint32_t write_index;

	read_index = *rx_read_index;
	write_index = *rx_write_index;

	while (read_index == write_index) {
		k_sem_take(&rx_sem, K_FOREVER);
		__DSB();
		read_index = *rx_read_index;
		write_index = *rx_write_index;
	}

	if (read_index >= rx_count) {
		return -EIO;
	}

	header = rx_data[read_index];
	*oob_data = header >> 16;

	return header & 0xFFFF;
}

int shmem_rx_recv(uint8_t* data, uint16_t size, uint16_t *oob_data)
{
	uint32_t read_index;
	uint32_t old_read_index;
	uint32_t write_index;
	uint32_t header;
	uint32_t msg_size;
	uint32_t msg_items;
	uint32_t tail_items;
	int result;

	read_index = *rx_read_index;
	old_read_index = read_index;
	write_index = *rx_write_index;

	/* Check if FIFO is valid. */
	if (read_index >= rx_count || write_index >= rx_count) {
		return -EIO;
	}

	/* Check if FIFO is empty. */
	if (read_index == write_index) {
		return -EAGAIN;
	}

	/* Read and decode header. */
	header = rx_data[read_index];
	msg_size = header & 0xFFFF;
	if (size < msg_size && data != NULL) {
		if (oob_data != NULL) {
			*oob_data = msg_size;
		}
		return -EINVAL;
	}
	msg_items = (msg_size + (ITEM_SIZE - 1)) / ITEM_SIZE;
	if (msg_items >= rx_count) {
		return -EIO;
	}
	result = msg_size;
	if (oob_data != NULL) {
		*oob_data = header >> 16;
	}
	read_index++;
	if (read_index >= rx_count) {
		read_index = 0;
		//LOG_WRN("============================ READ CYCLE");
	}

	if (data == NULL) {
		read_index += msg_items;
		if (read_index >= rx_count) {
			read_index -= rx_count;
		}
		goto update_index_and_exit;
	}

	/* Read first part of data if buffer cycle occurred. */
	if (write_index < read_index) {
		tail_items = rx_count - read_index;
		if (msg_items >= tail_items) {
			if (msg_items > tail_items) {
				memcpy(data, &rx_data[read_index], tail_items * ITEM_SIZE);
				data += tail_items * ITEM_SIZE;
				msg_size -= tail_items * ITEM_SIZE;
			} else {
				memcpy(data, &rx_data[read_index], msg_size);
				msg_size = 0;
			}
			msg_items -= tail_items;
			read_index = 0;
			//LOG_WRN("============================ READ CYCLE");
		}
	}

	/* Read remaining part of data. */
	memcpy(data, &rx_data[read_index], msg_size);
	read_index += msg_items;

update_index_and_exit:

	/* Make sure that all data was read before updating the index. */
	__DMB();

	/* Update read index, so message was consumed. */
	*rx_read_index = read_index;

	/* Make sure that index was written in RAM before informing remote. */
	__DSB();

	/* Send ACK if remote requested that. */
	if (*rx_ack_index == old_read_index) {
		ipm_send_simple(rx_ipm_ack);
	}

	return result;
}

static void sem_give_callback(struct device *dev, void *context,
			      uint32_t id, volatile void *data)
{
	LOG_DBG("Received IPM");
	k_sem_give((struct k_sem *)context);
}

int shmem_init()
{
	/* IPM setup */
	tx_ipm_send = device_get_binding(IPM_TX_SEND);
	tx_ipm_ack = device_get_binding(IPM_TX_ACK);
	rx_ipm_recv = device_get_binding(IPM_RX_RECV);
	rx_ipm_ack = device_get_binding(IPM_RX_ACK);

	if (!tx_ipm_send || !tx_ipm_ack || !rx_ipm_recv || !rx_ipm_ack) {
		LOG_ERR("Could IPM device handle");
		return -ENODEV;
	}

	ipm_register_callback(tx_ipm_ack, sem_give_callback, &tx_sem);
	ipm_register_callback(rx_ipm_recv, sem_give_callback, &rx_sem);

	/* Indexes initialization */
	*tx_write_index = 0;
	*tx_ack_index = NO_ACK;
	*rx_read_index = 0;
	__DSB();

	/* Handshake */
	LOG_INF("Handshake started");
	ipm_send_simple(rx_ipm_ack);
	k_sem_take(&tx_sem, K_FOREVER);
	ipm_send_simple(rx_ipm_ack);
	LOG_INF("Handshake done");

	return 0;
}

#if defined(CONFIG_BT_SHMEM_NRF53)

#define DRV_SHMEM_CMD 0x0001
#define DRV_SHMEM_ACL 0x0002
#define DRV_SHMEM_SCO 0x0003
#define DRV_SHMEM_EVT 0x0004
#define DRV_SHMEM_EVT_DISCARDABLE 0x0005

static K_THREAD_STACK_DEFINE(rx_thread_stack, 2048); // TODO: configurable
static struct k_thread rx_thread_data;

BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_HCI_RAW_H4), "HCI H:4 cannot be enabled!");

static bool receive_buf() // TODO: check if __attribute__ ((noinline)) and bt_recv(buf) moved outside will reduce stack usage
{
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
	case DRV_SHMEM_EVT:
		LOG_ERR("app <- net EVT %d", length);
		buf = bt_buf_get_evt(pkt_indicator >> 8, false, K_FOREVER);
		break;

	case DRV_SHMEM_EVT_DISCARDABLE:
		LOG_ERR("app <- net EVT %d", length);
		buf = bt_buf_get_evt(pkt_indicator >> 8, true, K_NO_WAIT);
		if (buf == NULL) {
			LOG_DBG("Discardable pool full, ignoring event");
			goto skip_input;
		}
		break;

	case DRV_SHMEM_ACL:
		LOG_ERR("app <- net EVT %d", length);
		buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_FOREVER);
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

	length = shmem_rx_recv(buf->data, buf->len, NULL);

	if (length < 0) {
		LOG_ERR("RX error %d!", length);
		net_buf_unref(buf);
		return false;
	}

	bt_recv(buf);
	LOG_WRN("-> HOST");

	return true;

unref_buf_and_skip:
	net_buf_unref(buf);
skip_input:
	shmem_rx_recv(NULL, 0, NULL);
	return true;
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	bool ok;

	do {
		ok = receive_buf();
	} while (ok);

	LOG_ERR("FATAL SHMEM FIFO ERROR. HCI transfer stopped.");
}

static int drv_send(struct net_buf *buf)
{
	int err;
	uint16_t pkt_indicator;
	enum bt_buf_type buf_type = bt_buf_get_type(buf);

	LOG_WRN("HOST ->");

	switch (buf_type) {
	case BT_BUF_ACL_OUT:
		LOG_ERR("app -> net ACL %d", buf->len);
		pkt_indicator = DRV_SHMEM_ACL;
		break;
	case BT_BUF_CMD:
		LOG_ERR("app -> net CMD %d", buf->len);
		pkt_indicator = DRV_SHMEM_CMD;
		break;
	default:
		LOG_ERR("Unknown type %u", buf_type);
		err = -EPROTO;
		goto done;
	}

	err = shmem_tx_send(buf->data, buf->len, pkt_indicator);

	LOG_WRN("-> FIFO");

	if (err < 0) {
		LOG_ERR("Failed to send (err %d)", err);
	}

done:
	net_buf_unref(buf);
	return err;
}

static int drv_open(void)
{
	int result = shmem_init();

	if (result < 0) {
		LOG_ERR("Init error %d", result);
		return result;
	}

	/* Create receive thread */
	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack), rx_thread,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT); // TODO: priority configurable
	k_thread_name_set(&rx_thread_data, "HCI shmem RX");

	return result;
}

static const struct bt_hci_driver drv = {
	.name		= "SHMEM",
	.open		= drv_open,
	.send		= drv_send,
	.bus		= BT_HCI_DRIVER_BUS_IPM,
};

static int drv_init(struct device *unused)
{
	ARG_UNUSED(unused);

	return bt_hci_driver_register(&drv);
}

SYS_INIT(drv_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#endif
