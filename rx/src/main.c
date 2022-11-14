#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "led.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, 1);

#define NAME_LEN 30

/* TODO: get this from DT */
#define NUM_LEDS 68
static struct led_data strip[NUM_LEDS];

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#define HAS_LED     1
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#define BLINK_ONOFF K_MSEC(500)

static struct k_work_delayable blink_work;
static bool                  led_is_on;

static void blink_timeout(struct k_work *work)
{
	led_is_on = !led_is_on;
	gpio_pin_set(led.port, led.pin, (int)led_is_on);
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static bool led_data_cb(struct bt_data *data, void *user_data)
{
	switch (data->type) {
	case BT_DATA_MANUFACTURER_DATA:
		LOG_HEXDUMP_DBG(data->data, data->data_len, "ad data");
		memset((uint8_t*)strip, 0, sizeof(strip));
		memcpy((uint8_t*)strip, data->data, MIN(data->data_len, sizeof(strip)));
		return false;
	default:
		return true;
	}
}

#define PEER_NAME "santa"
static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *buf)
{
	char le_addr[BT_ADDR_LE_STR_LEN];
	char name[NAME_LEN];

	/* if (info->rssi < -60) return; */

	(void)memset(name, 0, sizeof(name));

	struct net_buf_simple_state state;

	net_buf_simple_save(buf, &state);
	bt_data_parse(buf, data_cb, name);
	net_buf_simple_restore(buf, &state);

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
	if (strncmp(PEER_NAME, name, strlen(PEER_NAME))) {
		return;
	}

	printk("[DEVICE]: %s, AD evt type %u, Tx Pwr: %i, RSSI %i %s "
	       "Interval: 0x%04x (%u ms) Len %u\n",
	       le_addr, info->adv_type, info->tx_power, info->rssi, name,
	       info->interval, info->interval * 5 / 4, buf->len);

	bt_data_parse(buf, led_data_cb, NULL);
	/* Switch to using received data */
	/* TODO: switch it back after a timeout. */
	printk("got adv\n");
	led_idle_animation(false);

	k_work_schedule(&blink_work, BLINK_ONOFF);
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

void main(void)
{
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_PASSIVE,
		.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err;

	/* Configure led strip */
	led_register_data(strip, NUM_LEDS);
	led_idle_animation(true);
	led_thread_start();

	/* Configure on-board LED */
	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	k_work_init_delayable(&blink_work, blink_timeout);

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Scan callbacks register...");
	bt_le_scan_cb_register(&scan_callbacks);
	printk("success.\n");

	printk("Start scanning...");
	err = bt_le_scan_start(&scan_param, NULL);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success.\n");

	return;
}
