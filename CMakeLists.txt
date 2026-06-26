# Temporary diagnostics — each source compiles only when its option is enabled.
# Normal builds (both options off) get an empty library and are unaffected.
if(CONFIG_ZMK_I2C_SCANNER OR CONFIG_ZMK_GPIO_FINDER)
  zephyr_library()
  zephyr_library_sources_ifdef(CONFIG_ZMK_I2C_SCANNER src/i2c_scanner.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_GPIO_FINDER  src/gpio_finder.c)
endif()
