
#include "shmem_nrf53.h"

#include <kernel.h>

struct shmem_fifo_tx {
	volatile uint32_t *rx_index;
	volatile uint32_t *tx_index;
	volatile uint32_t *rx_ack;
	uint32_t *data;
	size_t count;
	struct device *ipm_tx;
	struct device *ipm_ack;
	struct k_sem sem;
};

struct shmem_fifo_rx {
	volatile uint32_t *rx_index;
	volatile uint32_t *tx_index;
	volatile uint32_t *rx_ack;
	uint32_t *data;
	size_t count;
	struct device *ipm_ack;
};

int shmem_fifo_tx_init(struct shmem_fifo_tx *fifo, void *shmem_buffer,
		       size_t shmem_size, const char *ipm_tx,
		       const char *ipm_ack);

int shmem_fifo_tx_send(struct shmem_fifo_tx *fifo, const uint8_t* data,
		       uint16_t size, uint16_t user_data);

int shmem_fifo_rx_init(struct shmem_fifo_rx *fifo, void *shmem_buffer,
		       size_t shmem_size, const char *ipm_ack);

int shmem_fifo_rx_empty(struct shmem_fifo *fifo)
{
	uint32_t rx_index = *fifo->rx_index;
	uint32_t tx_index = *fifo->tx_index;

	return rx_index == tx_index;
}

int shmem_fifo_rx_recv(struct shmem_fifo *fifo, uint8_t* data, size_t size, uint16_t *user_data)
{
	size; // ok
	EAGAIN; // fifo empty
	EINVAL; // packet bigger than provided buffer
	EIO; // fifo corrupted
}
/*

RXI = rx
if (RX == rx) {
	take
}

==========================

old_rx
RX = rx
if (RXI == old_rx) {
	signal
}

*/



#define ITEM_SIZE (sizeof(uint32_t))
#define RX_INDEX 0
#define TX_INDEX 1
#define RX_ACK_INDEX 2
#define DATA_START 3


int shmem_fifo_tx_send(struct shmem_fifo_tx *fifo, const uint8_t* data,
		       uint16_t size, uint16_t user_data)
{
	uint32_t rx_index;
	uint32_t tx_index;
	uint32_t rx_ack;
	size_t available;
	size_t data_items = ((size_t)size + (ITEM_SIZE - 1)) / ITEM_SIZE;
	size_t total_items = 1 + data_items;
	size_t size_aligned = data_items * ITEM_SIZE;

	if (total_items > fifo->count) {
		return -ENOMEM;
	}

	// TODO: lock here if needed
	rx_index = *fifo->rx_index;
	tx_index = *fifo->tx_index;

	if (rx_index >= fifo->count || tx_index >= fifo->count) {
		return -EIO;
	}

	do {
		if (rx_index <= tx_index) {
			available = fifo->count - (tx_index - rx_index) - 1;
		} else {
			available = rx_index - tx_index - 1;
		}
		if (available < total_items) {
			*fifo->rx_ack = rx_index;
			// TODO: memory barier
			if (*fifo->rx_index == rx_index) {
				k_sem_take(fifo->tx_sem);
			}
			rx_index = *fifo->rx_index;
			tx_index = *fifo->tx_index;
		}
	} while (available < total_items);

	fifo->data[tx_index] = (uint32_t)size | ((uint32_t)user_data << 16);
	tx_index++;
	if (tx_index >= fifo->count) {
		tx_index = 0;
	}

	if (tx_index >= rx_index) {
		size_t bytes = (fifo->count - tx_index) * ITEM_SIZE;
		if (size >= bytes) {
			memcpy(&fifo->data[tx_index], data, bytes);
			data += bytes;
			size -= bytes;
			size_aligned -= bytes;
			tx_index = 0;
		}
	}

	memcpy(&fifo->data[tx_index], data, size);
	tx_index += size_aligned;

	*fifo->tx_index = tx_index;
	// TODO: unlock if needed

	return ipm_send(fifo->ipm_tx, 0, 0, NULL, 0);
}