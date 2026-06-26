# Diagnostics + the PIM447 polling driver. Each source compiles only when its
# option is enabled, so normal builds with everything off are unaffected.
if(CONFIG_ZMK_I2C_SCANNER OR CONFIG_ZMK_GPIO_FINDER OR CONFIG_ZMK_PIM447_POLL)
  zephyr_library()
  zephyr_library_sources_ifdef(CONFIG_ZMK_I2C_SCANNER src/i2c_scanner.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_GPIO_FINDER  src/gpio_finder.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_PIM447_POLL  src/pim447_poll.c)
endif()
