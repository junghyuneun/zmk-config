/*
 * Temporary GPIO INT-finder (diagnostic) for ZMK.
 *
 * Enable: CONFIG_ZMK_GPIO_FINDER=y  (+ the zmk-usb-logging snippet on the right
 * build). Flash the RIGHT half, open a USB serial monitor, then MOVE THE
 * TRACKBALL (do NOT press keys). The pin that transitions to LOW is the PIM447
 * INT line.
 *
 * The PIM447 does NOT drive its INT pin until interrupt output is enabled over
 * I2C, so this first reads the chip id (to confirm comms) and enables INT
 * output, then watches the free nice!nano pads. Remove afterward.
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_finder, LOG_LEVEL_INF);

#define PIM447_ADDR 0x0a
#define PIM447_REG_CHIP_ID 0xFA /* 2 bytes, low then high; expect 0x11 0xBA */
#define PIM447_REG_INT 0xF9
#define PIM447_MSK_INT_OUT_EN 0x02

struct cand {
  const char *name;
  uint8_t port; /* 0 = gpio0, 1 = gpio1 */
  gpio_pin_t pin;
  int last;
  bool ok;
};

/* Free / spare nice!nano pads on a Corne (matrix + I2C + RGB excluded). */
static struct cand cands[] = {
    {"D0  (P0.08)", 0, 8, 1, false},  {"D8  (P1.04)", 1, 4, 1, false},
    {"D9  (P1.06)", 1, 6, 1, false},  {"D10 (P0.09)", 0, 9, 1, false},
    {"D16 (P0.10)", 0, 10, 1, false}, {"extra P1.01", 1, 1, 1, false},
    {"extra P1.02", 1, 2, 1, false},  {"extra P1.07", 1, 7, 1, false},
    {"extra P0.12", 0, 12, 1, false},
};

static void pim447_enable_int(void) {
  const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

  if (!device_is_ready(i2c)) {
    LOG_INF("i2c0 not ready; cannot enable INT output");
    return;
  }

  uint8_t id[2] = {0, 0};
  int rc = i2c_burst_read(i2c, PIM447_ADDR, PIM447_REG_CHIP_ID, id, sizeof(id));
  if (rc == 0) {
    LOG_INF("PIM447 @0x0a chip id = 0x%02x%02x (expect 0xba11)", id[1], id[0]);
  } else {
    LOG_INF("PIM447 chip-id read FAILED rc=%d (is it really at 0x0a?)", rc);
  }

  rc = i2c_reg_write_byte(i2c, PIM447_ADDR, PIM447_REG_INT,
                          PIM447_MSK_INT_OUT_EN);
  LOG_INF("enable INT output -> %s (rc=%d)", rc == 0 ? "OK" : "FAILED", rc);
}

static void gpio_finder_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  const struct device *g[2] = {
      DEVICE_DT_GET(DT_NODELABEL(gpio0)),
      DEVICE_DT_GET(DT_NODELABEL(gpio1)),
  };

  k_sleep(K_SECONDS(3)); /* let USB CDC logging enumerate */

  pim447_enable_int(); /* the trackball won't drive INT until this runs */

  int watched = 0;
  for (int i = 0; i < ARRAY_SIZE(cands); i++) {
    const struct device *port = g[cands[i].port];
    if (!device_is_ready(port)) {
      continue;
    }
    int err = gpio_pin_configure(port, cands[i].pin, GPIO_INPUT | GPIO_PULL_UP);
    if (err) {
      LOG_INF("skip %s (configure err %d)", cands[i].name, err);
      continue;
    }
    cands[i].ok = true;
    cands[i].last = gpio_pin_get(port, cands[i].pin);
    watched++;
  }

  LOG_INF("===== GPIO finder ready: watching %d pin(s) =====", watched);
  LOG_INF(">>> MOVE THE TRACKBALL now (do NOT press keys). Watch for a pin "
          "going LOW. <<<");

  int tick = 0;
  while (1) {
    for (int i = 0; i < ARRAY_SIZE(cands); i++) {
      if (!cands[i].ok) {
        continue;
      }
      int v = gpio_pin_get(g[cands[i].port], cands[i].pin);
      if (v != cands[i].last) {
        LOG_INF("%s : %d -> %d%s", cands[i].name, cands[i].last, v,
                v == 0 ? "   <<<<< LOW = this is your INT pin!" : "");
        cands[i].last = v;
      }
    }
    if (++tick % 100 == 0) { /* ~5 s heartbeat */
      int low = 0;
      for (int i = 0; i < ARRAY_SIZE(cands); i++) {
        if (cands[i].ok && gpio_pin_get(g[cands[i].port], cands[i].pin) == 0) {
          LOG_INF("  currently LOW: %s", cands[i].name);
          low++;
        }
      }
      if (low == 0) {
        LOG_INF("(all watched pins HIGH — move the ball)");
      }
    }
    k_sleep(K_MSEC(50));
  }
}

K_THREAD_DEFINE(gpio_finder_tid, 1536, gpio_finder_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
