
// Converter program: https://github.com/doki-nordic/rtt_lite_trace/tree/simple-timing

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/ipc/icmsg.h>
#include <zephyr/ipc/ipc_service_backend.h>
#include <zephyr/cache.h>

#include <SEGGER_RTT.h>

#include "ltrace.h"


#define REMOTE_SEGGER_RTT (*(SEGGER_RTT_CB*)((uintptr_t)(&_SEGGER_RTT) ^ 0x01000000))

#define REMOTE_RTT_BUFFER_READ_INDEX (*(volatile unsigned int*) \
		(&REMOTE_SEGGER_RTT.aUp[TRACE_RTT_CHANNEL].RdOff))

#define RTT_BUFFER_READ_INDEX (*(volatile unsigned int*) \
		(&_SEGGER_RTT.aUp[TRACE_RTT_CHANNEL].RdOff))

BUILD_ASSERT(CONFIG_SEGGER_RTT_MAX_NUM_UP_BUFFERS >= 2, "More RTT buffers required.");

uint32_t ltrace_rtt_buffer[RTT_BUFFER_WORDS];

void initialize_trace(void)
{
	static bool initialized; /* zero-initialized after reset */

	if (!initialized) {
		SYST_CSR = (1 << 2) | (1 << 0);
		SYST_RVR = 0x00FFFFFF;
		SYST_CSR = (1 << 2) | (1 << 0);

		SEGGER_RTT_BUFFER_UP *up;

		/*
		 * Directly initialize RTT up channel to avoid unexpected traces
		 * before initialization.
		 */
		up = &_SEGGER_RTT.aUp[TRACE_RTT_CHANNEL];
		up->sName = IS_ENABLED(CONFIG_SOC_NRF5340_CPUAPP) ? "LiteTrace-APP" : "LiteTrace-NET";
		up->pBuffer = (char *)(&ltrace_rtt_buffer[0]);
		up->SizeOfBuffer = sizeof(ltrace_rtt_buffer);
		up->RdOff = 0u;
		up->WrOff = 0u;
		up->Flags = SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL;

		initialized = true;
	}

	TRACE_INIT();
	printk("Trace initialized, waiting for RTT connection at 0x%08X ...\n", (int)&_SEGGER_RTT);
#ifdef CONFIG_SOC_NRF5340_CPUAPP
	while (RTT_BUFFER_READ_INDEX == 0) {
		__sync_synchronize();
	}
	if (RTT_BUFFER_READ_INDEX == 0xFFFFFFFF) {
		printk("RTT Connection skipped - active on remote\n");
	} else {
		printk("RTT Connection active\n");
	}
#else
	while (true) {
		if (RTT_BUFFER_READ_INDEX != 0) {
			REMOTE_RTT_BUFFER_READ_INDEX = 0xFFFFFFFF;
			printk("RTT Connection active\n");
		} else if (REMOTE_RTT_BUFFER_READ_INDEX != 0) {
			printk("RTT Connection skipped - active on remote\n");
		} else {
			continue;
		}
		break;
	}
#endif
}
