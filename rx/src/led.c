#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>

#include "led.h"
#include "i2s_led.h"

static struct led_data *data;
static uint16_t data_length;
static bool idle;

#define LED_STACK_SIZE 2000
#define LED_PRIORITY K_PRIO_PREEMPT(0)
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_NUM_PIXELS DT_PROP(DT_ALIAS(led_strip), chain_length)

struct led_rgb pixels[STRIP_NUM_PIXELS];

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);

K_SEM_DEFINE(data_updated, 0, 1);

void led_entry_point(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        if (idle) {
            /* TODO: show idle animation */
            for (int i=0; i<STRIP_NUM_PIXELS; i++) {
                pixels[i].r = 0;
                pixels[i].g = 0;
                pixels[i].b = 0;
            }
        } else {
            k_sem_take(&data_updated, K_FOREVER);
            /* read out array and set led data colors */
            /* TODO: respect effect flag */
            for (int i=0; i<STRIP_NUM_PIXELS; i++) {
                pixels[i].r = data[i].r;
                pixels[i].g = data[i].g;
                pixels[i].b = data[i].b;
            }
        }

        int rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
        if (rc) {
            printk("couldn't update strip: %d", rc);
        }
    }
}

K_THREAD_STACK_DEFINE(led_stack_area, LED_STACK_SIZE);
struct k_thread led_thread_data;
static k_tid_t led_tid;

void led_register_data(struct led_data *array, uint16_t len)
{
    if (!array) return;

    data = array;
    data_length = len;
}

void led_thread_start(void)
{
    led_tid = k_thread_create(&led_thread_data, led_stack_area,
                              K_THREAD_STACK_SIZEOF(led_stack_area),
                              led_entry_point,
                              NULL, NULL, NULL,
                              LED_PRIORITY, 0, K_NO_WAIT);
    k_thread_start(led_tid);

    printk("started thread");
}

void led_idle_animation(bool use_idle)
{
    k_sem_give(&data_updated);
    idle = use_idle;
}
