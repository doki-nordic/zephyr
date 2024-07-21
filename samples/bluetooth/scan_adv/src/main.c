/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)
#define LED3_NODE DT_ALIAS(led3)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(LED3_NODE, gpios);

static const char expected_name[] = "Gerard";
static const uint64_t expected_max_period_ms = 900;
static const uint64_t max_inactive = 5 * 60 * 1000;

static char name[] = "MowerOk";

static uint64_t last_time = 0;
static volatile uint64_t valid_time = 0;

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, name, sizeof(name) - 1),
};

static void valid_adv() {
	uint64_t now = k_uptime_get();
	uint64_t period = now - last_time;
	last_time = now;
	if (period <= expected_max_period_ms) {
		valid_time = now;
		//printk("Valid after: %d ms\n", (int)period);
	} else {
		printk("Invalid after: %d ms\n", (int)period);
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	static char strbuf[64];
	/*ok = !ok;
	for (uint16_t i = 0; i < buf->len; i++) {
		//printk("%02X", buf->data[i]);
	}
	bt_addr_le_to_str(addr, addrbuf, sizeof(addrbuf));
	printk("  %s %d\n", addrbuf, adv_type);*/
	uint16_t index = 0;
	while (index + 2 < buf->len) {
		uint8_t len = buf->data[index++];
		uint8_t type = buf->data[index++];
		//printk("%d %d\n", len, type);
		if (len < 1 || index + len - 1 > buf->len) break;
		len--;
		if (type == 0x08 && len < sizeof(strbuf) - 1) {
			memcpy(strbuf, buf->data + index, len);
			strbuf[len] = 0;
			if (strcmp(strbuf, expected_name) == 0) {
				valid_adv();
			}
		}
		index += len;
	}
}

int main(void)
{
	struct bt_le_scan_param scan_param = {
		//.type       = BT_HCI_LE_SCAN_PASSIVE,
		.type       = BT_HCI_LE_SCAN_ACTIVE,
		.options    = BT_LE_SCAN_OPT_NONE,
		.interval   = 0x0010,
		.window     = 0x0010,
	};
	int err;
	uint64_t now;
	uint64_t inactive_time;
	bool ok = true;
	bool oldOk = true;

	gpio_is_ready_dt(&led0);
	gpio_is_ready_dt(&led1);
	gpio_is_ready_dt(&led2);
	gpio_is_ready_dt(&led3);

	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led3, GPIO_OUTPUT_ACTIVE);

	printk("Starting Scanner/Advertiser Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		printk("Starting scanning failed (err %d)\n", err);
		return 0;
	}

	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	int state = 0;

	do {
		if (ok) {
			state = 0;
		} else {
			state = !state;
		}
		gpio_pin_set_dt(&led0, state);
		gpio_pin_set_dt(&led1, state);
		gpio_pin_set_dt(&led2, state);
		gpio_pin_set_dt(&led3, state);
		k_sleep(ok ? K_MSEC(1000) : state ? K_MSEC(100) : K_MSEC(200));
		now = k_uptime_get();
		inactive_time = now - valid_time;

		ok = inactive_time <= max_inactive;

		printk("Inactive time: %d ms %s\n", (int)inactive_time, ok ? "OK" : "Failed");

		if (ok != oldOk) {
			oldOk = ok;
			if (ok) {
				name[5] = 'O';
				name[6] = 'k';
			} else {
				name[5] = 'E';
				name[6] = 'r';
			}
			err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
			if (err) {
				printk("Advertising failed to update (err %d)\n", err);
				return 0;
			}
		}

	} while (1);
	return 0;
}
