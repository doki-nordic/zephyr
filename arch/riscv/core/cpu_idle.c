/*
 * Copyright (c) 2016 Jean-Paul Etienne <fractalclone@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/tracing/tracing.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_IDLE
void arch_cpu_idle(void)
{
	sys_trace_idle();
	*(volatile uint32_t *)(0x5F938000 + 8) = 1 << 2;
	__asm__ volatile("wfi");
	*(volatile uint32_t *)(0x5F938000 + 4) = 1 << 2;
	sys_trace_idle_exit();
	irq_unlock(MSTATUS_IEN);
}
#endif

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_ATOMIC_IDLE
void arch_cpu_atomic_idle(unsigned int key)
{
	sys_trace_idle();
	*(volatile uint32_t *)(0x5F938000 + 8) = 1 << 2;
	__asm__ volatile("wfi");
	*(volatile uint32_t *)(0x5F938000 + 4) = 1 << 2;
	sys_trace_idle_exit();
	irq_unlock(key);
}
#endif

const struct gpio_dt_spec debug2_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(debug2pin), gpios);

static int debug_port_init(void)
{
	int err = 0;
	err = gpio_pin_configure_dt(&debug2_spec, GPIO_OUTPUT_INACTIVE);
	*(volatile uint32_t *)(0x5F938000 + 4) = 1 << 2;
	return err;
}

SYS_INIT(debug_port_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
