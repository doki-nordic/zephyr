#ifndef _SHMEM_NRF53_H_
#define _SHMEM_NRF53_H_

int shmem_init();
int shmem_tx_send(const uint8_t* data, uint16_t size, uint16_t oob_data);
int shmem_rx_wait(uint16_t *oob_data);
int shmem_rx_recv(uint8_t* data, uint16_t size, uint16_t *oob_data);

#endif /* _SHMEM_NRF53_H_ */
