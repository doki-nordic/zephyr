#ifndef _LTRACE_H_
#define _LTRACE_H_

#include <zephyr/kernel.h>
#include <SEGGER_RTT.h>

enum {
#define TRACE_MARK(id, name) _TRACE_##name = id,
#define TRACE_CALL(id, name) _TRACE_##name = id,
#include "ltrace_ids.h"
};

#define TRACE_RTT_CHANNEL 1
#define RTT_BUFFER_BYTES (8 * 1024)

#define RTT_BUFFER_INDEX (*(volatile unsigned int*) \
		(&_SEGGER_RTT.aUp[TRACE_RTT_CHANNEL].WrOff))
#define RTT_BUFFER_U8(byte_index) (((volatile uint8_t*) \
		(&ltrace_rtt_buffer))[byte_index])
#define RTT_BUFFER_U32(byte_index) (*(volatile uint32_t*) \
		(&RTT_BUFFER_U8(byte_index)))
#define RTT_BUFFER_WORDS (RTT_BUFFER_BYTES / sizeof(uint32_t))

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)
#define SYST_CALIB (*(volatile uint32_t*)0xE000E01C)

extern uint32_t ltrace_rtt_buffer[RTT_BUFFER_WORDS];

void initialize_trace(void);

static inline void _trace_push(uint32_t id)
{
	uint32_t value = id << 24;
	int key = irq_lock();
	value |= SYST_CVR;
	uint32_t index = RTT_BUFFER_INDEX;
	if (index == RTT_BUFFER_BYTES - 4) {
		irq_unlock(key);
		return;
	}
	RTT_BUFFER_U32(index) = value;
	index = index + 4;
	//if (index == RTT_BUFFER_BYTES) index = 0;
	__sync_synchronize();
	RTT_BUFFER_INDEX = index;
	irq_unlock(key);
}

#undef TRACE_MARK
#undef TRACE_CALL
#define TRACE_MARK(id, name) static inline void TRACE_##name() { _trace_push(_TRACE_##name); }
#define TRACE_CALL(id, name) static inline void TRACE_##name() { _trace_push(_TRACE_##name); }
#include "ltrace_ids.h"

#endif
