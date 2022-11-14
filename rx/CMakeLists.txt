cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

add_subdirectory_ifdef(CONFIG_LED_STRIP drivers/led_strip)

project(periodic_sync)

target_sources(app PRIVATE
  src/main.c
  src/led.c
  )
