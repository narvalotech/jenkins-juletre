#ifndef LED_H_
#define LED_H_

#include <stdint.h>
#include <stdbool.h>


struct led_data {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t fx;
} __packed;

void led_register_data(struct led_data *array, uint16_t len);

/* Call led_register_data -before- calling this. */
void led_thread_start(void);

void led_idle_animation(bool use_idle);

#endif // LED_H_
