#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/ring_buffer.h>
#include "serial.h"

#define NUM_LEDS 68
#define DATA_LEN (NUM_LEDS * 4)

static uint8_t ad_data[DATA_LEN];

/* Size of AD data format length field in octets */
#define BT_AD_DATA_FORMAT_LEN_SIZE 1U

/* Size of AD data format type field in octets */
#define BT_AD_DATA_FORMAT_TYPE_SIZE 1U

/* Device name length, size minus one null character */
#define BT_DEVICE_NAME_LEN (sizeof(CONFIG_BT_DEVICE_NAME) - 1U)

/* Device name length in AD data format, 2 bytes for length and type overhead */
#define BT_DEVICE_NAME_AD_DATA_LEN (BT_AD_DATA_FORMAT_LEN_SIZE + \
				    BT_AD_DATA_FORMAT_TYPE_SIZE + \
				    BT_DEVICE_NAME_LEN)
#define MAN_DATA_LEN (255 - BT_DEVICE_NAME_AD_DATA_LEN)

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_MANUFACTURER_DATA, ad_data, MAN_DATA_LEN),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, ad_data + MAN_DATA_LEN, DATA_LEN - MAN_DATA_LEN),
};

RING_BUF_DECLARE(uart_ringbuf, 512);
static uint8_t led_data[DATA_LEN];

static struct rx_uart_header uart_header;
static struct rx_uart config = {
    .uart = DEVICE_DT_GET(DT_NODELABEL(uart0)),
    .header = &uart_header,
	.ringbuf = &uart_ringbuf,
    .packet = led_data,
    .packet_max_len = DATA_LEN,
};

#define VALIDATE(x) do {						\
		int err = x;							\
		__ASSERT(err == 0, "err %d", err);		\
	} while (0)


extern struct k_sem serial_data;

struct bt_le_ext_adv *adv;

void main(void)
{
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U, /* Supply unique SID when creating advertising set */
		.secondary_max_skip = 0U,
		.options = (BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_USE_IDENTITY),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};

	printk("Starting Periodic Advertising Demo\n");

	VALIDATE(serial_init(&config));

	/* Initialize the Bluetooth Subsystem */
	VALIDATE(bt_enable(NULL));

	/* Create a non-connectable non-scannable advertising set */
	VALIDATE(bt_le_ext_adv_create(&adv_param, NULL, &adv));

	while (true) {
		/* Wait until we have received fresh data over serial */
		k_sem_take(&serial_data, K_FOREVER);
		printk("Refreshing Advertising Data...");

		/* Stop adv (to be able to update long data) */
		VALIDATE(bt_le_ext_adv_stop(adv));

		/* Update the data */
		memcpy(ad_data, led_data, sizeof(ad_data));

		/* Set extended advertising data */
		VALIDATE(bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0));

		/* Start extended advertising set */
		VALIDATE(bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT));

		printk("done.\n");
	}
}
