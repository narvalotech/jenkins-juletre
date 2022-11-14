#ifndef I2S_LED_H_
#define I2S_LED_H_

#include <zephyr/drivers/led_strip.h>

int strip_init(void);
int strip_update_rgb(void*, struct led_rgb*, uint16_t);

#endif // I2S_LED_H_
