/*
 * Temporary I2C bus scanner (diagnostic) for ZMK.
 *
 * Enable in your .conf:
 *     CONFIG_I2C=y
 *     CONFIG_ZMK_USB_LOGGING=y
 *     CONFIG_ZMK_I2C_SCANNER=y
 *
 * Flash the keyboard half that has the device attached, plug it into USB,
 * open a serial monitor (e.g. `screen /dev/tty.usbmodemXXXX 115200`) and read
 * which I2C addresses ACK. Expect the OLED at 0x3c and, if the trackball is on
 * this bus, the PIM447 at 0x0a. Remove these options (and this file) afterward.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_scanner, LOG_LEVEL_INF);

#define I2C_SCAN_NODE DT_NODELABEL(i2c0)

#if !DT_NODE_HAS_STATUS(I2C_SCAN_NODE, okay)
#error "i2c0 is not enabled on this build; the I2C scanner needs an active i2c0 bus."
#endif

static void i2c_scan_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *const i2c = DEVICE_DT_GET(I2C_SCAN_NODE);

	/* Give USB CDC logging time to enumerate so the first scan isn't missed. */
	k_sleep(K_SECONDS(3));

	while (1) {
		if (!device_is_ready(i2c)) {
			LOG_ERR("I2C bus %s not ready", i2c->name);
			k_sleep(K_SECONDS(5));
			continue;
		}

		LOG_INF("===== I2C scan on %s =====", i2c->name);
		int found = 0;
		for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
			uint8_t dummy;
			if (i2c_read(i2c, &dummy, 1, addr) == 0) {
				LOG_INF("  device ACK at 0x%02x", addr);
				found++;
			}
		}
		LOG_INF("===== scan complete: %d device(s) found =====", found);

		k_sleep(K_SECONDS(5));
	}
}

K_THREAD_DEFINE(i2c_scan_tid, 1536, i2c_scan_thread,
		NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
