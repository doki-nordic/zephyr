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

#define LOG_LEVEL LOG_LEVEL_DEBUG // TODO: make it configurable
#define LOG_MODULE_NAME hci_shmem_nrf53
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "Module requires definition of shared memory for rpmsg"
#endif

#define SHM_NODE            DT_CHOSEN(zephyr_ipc_shm)
#define SHM_BASE_ADDRESS    DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE            ((DT_REG_SIZE(SHM_NODE) / 4) & ~7) // TODO: remove /4

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
static void rx_work_handler(struct k_work *work);
static K_WORK_DEFINE(rx_work, rx_work_handler);

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

int shmem_tx_send(const uint8_t* data, uint16_t size, uint16_t user_data)
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
	tx_data[write_index] = (uint32_t)size | ((uint32_t)user_data << 16);
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

int shmem_fifo_rx_empty()
{
	return *rx_read_index == *rx_write_index;
}

int shmem_fifo_rx_recv(uint8_t* data, uint16_t size, uint16_t *user_data)
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
	if (size < msg_size) {
		*user_data = msg_size;
		return -EINVAL;
	}
	result = msg_size;
	*user_data = header >> 16;
	read_index++;
	if (read_index >= rx_count) {
		read_index = 0;
		//LOG_WRN("============================ READ CYCLE");
	}
	msg_items = (msg_size + (ITEM_SIZE - 1)) / ITEM_SIZE;

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

static void tx_ack_callback(struct device *dev, void *context,
			    uint32_t id, volatile void *data)
{
	LOG_DBG("Waiting is done for TX data consumed.");
	k_sem_give(&tx_sem);
}

static void rx_recv_callback(struct device *dev, void *context,
			     uint32_t id, volatile void *data)
{
	LOG_DBG("Received data.");
	k_work_submit(&rx_work);
}

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
uint32_t rx_rand = 0x67491643;
uint32_t tx_rand = 0x23786234;
#else
uint32_t rx_rand = 0x23786234;
uint32_t tx_rand = 0x67491643;
#endif

uint32_t myrand(uint32_t *x)
{
	*x = (*x) * 1664525u + 1013904223u;
	//printk("%08X\n", *x);
	return *x >> 16;
}

void failed()
{
	while (1) {
		k_sleep(K_MSEC(5000));
	}
}

static void rx_work_handler(struct k_work *work)
{
	int i, k;
	char line[80];
	char *ptr = line;
	uint8_t buf[0x50];
	uint16_t user_data;
	int result;

	static uint32_t total = 0;
	static uint32_t next = 1;

	while (!shmem_fifo_rx_empty()) {
		result = shmem_fifo_rx_recv(buf, sizeof(buf), &user_data);
		if (result < 0) {
			LOG_ERR("Error receiving %d, user_data=%d", result, user_data);
			continue;
		}
		//LOG_INF("Received %d bytes, user_data=%d", result, user_data);

		if (result != (myrand(&rx_rand) & 0x3F)) {
			LOG_ERR("Invalid length");
			failed();
		}
		for (i = 0; i < result; i++) {
			if (buf[i] != (myrand(&rx_rand) & 0xFF)) {
				LOG_ERR("Invalid data");
				failed();
			}
		}
		if (user_data != (myrand(&rx_rand) & 0xFFFF)) {
			LOG_ERR("Invalid data");
			failed();
		}
		total += result;
		if (total >= next) {
			next = total + 1024000;
			LOG_INF("Recv total %d (%dMB)", total, total / (1024 * 1024));
		}
#		if !defined(CONFIG_SOC_NRF5340_CPUAPP)
		if ((user_data & 0xFFF) == 0) {
			LOG_WRN("Forcing sleep");
			k_sleep(K_MSEC(1000));
		}
#		endif
		/*
		k = 0;
		while (k < result) {
			ptr = line;
			for (i = 0; i < 16 && k < result; i++, k++) {
				ptr += sprintf(ptr, " %02X", buf[k]);
			}
			//LOG_INF("DATA:%s", log_strdup(line));
		}*/
	}
}

static int shmem_init()
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

	ipm_register_callback(tx_ipm_ack, tx_ack_callback, NULL);
	ipm_register_callback(rx_ipm_recv, rx_recv_callback, NULL);

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


int main()
{

	int result;

	printk("\n\n\n========================================\n");
	printk("    Shared memory FIFO test started\n");
	printk("========================================\n");

	result = shmem_init();

	if (result != 0) {
		printk("Init error: %d\n", result);
		goto end_of_main;
	}

	uint8_t buf[0x40];

	uint32_t total = 0;
	uint32_t next = total + 1;

	while (1) {
		int len = myrand(&tx_rand) & 0x3F;
		int i;
		uint16_t user_data;
		for (i = 0; i < len; i++) {
			buf[i] = myrand(&tx_rand);
		}
		user_data = myrand(&tx_rand);
		//LOG_INF("Sending  %d bytes, user_data=%d", len, user_data);
		int result = shmem_tx_send(buf, len, user_data);
		if (result < 0) {
			LOG_ERR("Error sending %d", result);
			failed();
		} else {
			LOG_DBG("Send %d", len);			
		}
		if ((total & 127) == 0)
			k_sleep(K_MSEC(1));
		total += len;
		if (total >= next) {
			next = total + 1024000;
			LOG_INF("Send total %d (%dMB)", total, total / (1024 * 1024));
		}
	}

end_of_main:

	while (1) {
		k_sleep(K_MSEC(5000));
	}
	return 0;
}
