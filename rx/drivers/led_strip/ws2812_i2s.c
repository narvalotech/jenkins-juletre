/*
 * Copyright (c) 2022 Jonathan Rico
 *
 * Adapted from the I2S driver, using the procedure in this blog post:
 * https://electronut.in/nrf52-i2s-ws2812/
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT worldsemi_ws2812_i2s

#include <zephyr/drivers/led_strip.h>

#include <string.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ws2812_i2s);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/util.h>
#include <zephyr/dt-bindings/led/led.h>

#define WS2812_I2S_PRE_DELAY_WORDS 1

struct ws2812_i2s_cfg {
    struct device const *dev;
    size_t tx_buf_bytes;
    struct k_mem_slab *mem_slab;
    uint8_t num_colors;
    const uint8_t *color_mapping;
    uint16_t reset_words;
    bool active_low;
};

/* Serialize an 8-bit clor channel value into two 16-bit I2S words (or 1 32-bit
 * word).
 */
static inline void ws2812_i2s_ser(uint32_t *word, uint8_t color,
                                  const uint8_t sym_one, const uint8_t sym_zero)
{
    *word = 0;
    for (int i=0; i<8; i++) {
        if ((1 << i) & color) {
            *word |= sym_one << (i * 4);
        } else {
            *word |= sym_zero << (i * 4);
        }
    }

    /* Swap the two I2S words due to the (audio) channel TX order. */
    *word = (*word >> 16) | (*word << 16);
}

static int ws2812_strip_update_rgb(const struct device *dev,
                                   struct led_rgb *pixels,
                                   size_t num_pixels)
{
    const struct ws2812_i2s_cfg *cfg = dev->config;
    uint8_t sym_one, sym_zero;
    uint32_t reset_word;
    uint32_t *tx_buf;
    void *mem_block;
    int ret;

    if(cfg->active_low) {
        sym_one = 0x1;
        sym_zero = 0x7;
        reset_word = 0xFFFFFFFF;
    } else {
        sym_one = 0xE;
        sym_zero = 0x8;
        reset_word = 0;
    }

    /* Acquire memory for the I2S payload. */
    ret = k_mem_slab_alloc(cfg->mem_slab, &mem_block, K_SECONDS(10));
    if (ret < 0) {
        LOG_ERR("Unable to allocate mem slab for TX (err %d)", ret);
        return -ENOMEM;
    }
    tx_buf = (uint32_t*)mem_block;

    /* Add a pre-data reset, so the first pixel isn't skipped by the strip. */
    for(uint16_t i = 0; i < WS2812_I2S_PRE_DELAY_WORDS; i++) {
        *tx_buf = reset_word;
        tx_buf++;
    }

    /*
     * Convert pixel data into I2S frames. Each frame has pixel data
     * in color mapping on-wire format (e.g. GRB, GRBW, RGB, etc).
     */
    for (uint16_t i = 0; i < num_pixels; i++) {
        for (uint16_t j = 0; j < cfg->num_colors; j++) {
            uint8_t pixel;

            switch (cfg->color_mapping[j]) {
                /* White channel is not supported by LED strip API. */
                case LED_COLOR_ID_WHITE:
                    pixel = 0;
                    break;
                case LED_COLOR_ID_RED:
                    pixel = pixels[i].r;
                    break;
                case LED_COLOR_ID_GREEN:
                    pixel = pixels[i].g;
                    break;
                case LED_COLOR_ID_BLUE:
                    pixel = pixels[i].b;
                    break;
                default:
                    return -EINVAL;
            }
            ws2812_i2s_ser(tx_buf, pixel, sym_one, sym_zero);
            tx_buf++;
        }
    }

    for(uint16_t i = 0; i < cfg->reset_words; i++) {
        *tx_buf = reset_word;
        tx_buf++;
    }

    /* Flush the buffer on the wire. */
    ret = i2s_write(cfg->dev, mem_block, cfg->tx_buf_bytes);
    if (ret < 0) {
        k_mem_slab_free(cfg->mem_slab, &mem_block);
        LOG_ERR("Failed to write data: %d", ret);
        return ret;
    }

    ret = i2s_trigger(cfg->dev, I2S_DIR_TX, I2S_TRIGGER_START);
    if (ret < 0) {
        LOG_ERR("Failed to trigger command %d on TX: %d", I2S_TRIGGER_START, ret);
        return ret;
    }

    ret = i2s_trigger(cfg->dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
    if (ret < 0) {
        LOG_ERR("Failed to trigger command %d on TX: %d", I2S_TRIGGER_DRAIN, ret);
        return ret;
    }

    /* FIXME: calculate this correctly. */
    /* capture says around ~3ms max */
    k_msleep(500);

    return ret;
}

static int ws2812_strip_update_channels(const struct device *dev,
                                        uint8_t *channels,
                                        size_t num_channels)
{
    LOG_ERR("update_channels not implemented");
    return -ENOTSUP;
}

static int ws2812_i2s_init(const struct device *dev)
{
    const struct ws2812_i2s_cfg *cfg = dev->config;
    uint8_t i;
    int ret;

    struct i2s_config config;

    /* 16-bit stereo, 100kHz LCLK */
    config.word_size = 16;
    config.channels = 2;
    config.format = I2S_FMT_DATA_FORMAT_I2S;
    config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
    config.frame_clk_freq = 100000; /* WS (or LRCK) */
    config.mem_slab = cfg->mem_slab;
    config.block_size = cfg->tx_buf_bytes;
    config.timeout = 1000;

    ret = i2s_configure(cfg->dev, I2S_DIR_TX, &config);
    if (ret < 0) {
        LOG_ERR("Failed to configure I2S device: %d\n", ret);
        return ret;
    }

    for (i = 0; i < cfg->num_colors; i++) {
        switch (cfg->color_mapping[i]) {
            case LED_COLOR_ID_WHITE:
            case LED_COLOR_ID_RED:
            case LED_COLOR_ID_GREEN:
            case LED_COLOR_ID_BLUE:
                break;
            default:
                LOG_ERR("%s: invalid channel to color mapping."
                        "Check the color-mapping DT property",
                        dev->name);
                return -EINVAL;
        }
    }

    return 0;
}

static const struct led_strip_driver_api ws2812_i2s_api = {
.update_rgb = ws2812_strip_update_rgb,
.update_channels = ws2812_strip_update_channels,
};

#define WS2812_RESET_DELAY_US(idx) DT_INST_PROP(idx, reset_delay)
/* Rounds up to the next 20us. */
#define WS2812_RESET_DELAY_WORDS(idx) \
    ((WS2812_RESET_DELAY_US(idx) + (20 - 1)) / 20)

#define WS2812_NUM_COLORS(idx) (DT_INST_PROP_LEN(idx, color_mapping))

#define WS2812_I2S_NUM_PIXELS(idx)              \
    (DT_INST_PROP(idx, chain_length))

#define WS2812_I2S_BUFSIZE(idx)                                     \
    ((WS2812_NUM_COLORS(idx) * WS2812_I2S_NUM_PIXELS(idx)           \
      + WS2812_I2S_PRE_DELAY_WORDS                                  \
      + WS2812_RESET_DELAY_WORDS(idx))                              \
     * 4)

#define WS2812_I2S_DEVICE(idx)                                          \
                                                                        \
    K_MEM_SLAB_DEFINE_STATIC(ws2812_i2s_##idx##_slab,                   \
                             WS2812_I2S_BUFSIZE(idx), 2, 4);            \
                                                                        \
    static const uint8_t ws2812_i2s_##idx##_color_mapping[] =           \
        DT_INST_PROP(idx, color_mapping);                               \
                                                                        \
    static const struct ws2812_i2s_cfg ws2812_i2s_##idx##_cfg = {       \
    .dev = DEVICE_DT_GET(DT_INST_PROP(idx, i2s_dev)),                   \
    .tx_buf_bytes = WS2812_I2S_BUFSIZE(idx),                            \
    .mem_slab = &ws2812_i2s_##idx##_slab,                               \
    .num_colors = WS2812_NUM_COLORS(idx),                               \
    .color_mapping = ws2812_i2s_##idx##_color_mapping,                  \
    .reset_words = WS2812_RESET_DELAY_WORDS(idx),                       \
    .active_low = DT_INST_PROP(idx, out_active_low),                    \
};                                                                      \
                                                                        \
    DEVICE_DT_INST_DEFINE(idx,                                          \
                          ws2812_i2s_init,                              \
                          NULL,                                         \
                          NULL,                                         \
                          &ws2812_i2s_##idx##_cfg,                      \
                          POST_KERNEL,                                  \
                          CONFIG_LED_STRIP_INIT_PRIORITY,               \
                          &ws2812_i2s_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_I2S_DEVICE)
