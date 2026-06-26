/*
 * Pimoroni PIM447 trackball — polled I2C input driver for ZMK.
 *
 * Binds to the t0bybr module's "zmk,pimoroni-pim447" devicetree binding (which
 * is reliably discovered, unlike a config-repo binding), but reads on a timer
 * instead of using the interrupt line. The node must carry an int-gpios (the
 * binding requires it) but this driver never touches that pin.
 *
 * Verbose logging: init state, ~2 s heartbeat, and every detected movement.
 */
#define DT_DRV_COMPAT zmk_pimoroni_pim447

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pim447_poll, LOG_LEVEL_INF);

#define PIM447_REG_LEFT 0x04
#define PIM447_REG_RIGHT 0x05
#define PIM447_REG_UP 0x06
#define PIM447_REG_DOWN 0x07
#define PIM447_REG_SWITCH 0x08
#define PIM447_MSK_SWITCH_PRESSED 0x80

/* --- Movement feel: change a value, then rebuild + flash the right half -----
 * Output per axis = raw * (ACCEL_BASE + speed * ACCEL_GAIN), where speed is
 * |rx| + |ry| this poll. Slow rolls stay precise (~ACCEL_BASE x); fast rolls
 * accelerate so a flick covers distance. ACCEL_GAIN 0 = flat ACCEL_BASE scale.
 *   ACCEL_BASE   baseline multiplier for slow movement (raise = faster
 * overall). ACCEL_GAIN   acceleration strength (raise = snappier fast moves).
 *   SWAP_XY      1 = swap X/Y; fixes a 90-degree mounting rotation.
 *   INVERT_X/_Y  1 = reverse that axis. */
#define PIM447_POLL_MS 15
#define PIM447_ACCEL_BASE 2
#define PIM447_ACCEL_GAIN 1
#define PIM447_SWAP_XY 1
#define PIM447_INVERT_X 0
#define PIM447_INVERT_Y 1

struct pim447_poll_config {
  struct i2c_dt_spec i2c;
};

struct pim447_poll_data {
  const struct device *dev;
  struct k_work_delayable work;
  bool sw_last;
  uint32_t polls;
};

static void pim447_poll_work(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct pim447_poll_data *data =
      CONTAINER_OF(dwork, struct pim447_poll_data, work);
  const struct device *dev = data->dev;
  const struct pim447_poll_config *cfg = dev->config;

  data->polls++;

  /* One burst read covers LEFT..SWITCH (0x04-0x08 are contiguous), replacing
   * the old 4-byte read plus a separate switch read. */
  uint8_t buf[5] = {0};
  int rc = i2c_burst_read_dt(&cfg->i2c, PIM447_REG_LEFT, buf, sizeof(buf));

  if (rc == 0) {
    /* Movement accumulators do NOT auto-clear on read; zero only the axes that
     * actually moved. Idle polls thus issue a single I2C read (no writes),
     * which sharply cuts bus traffic and the address/data NACKs seen at 66 Hz.
     */
    if (buf[0]) {
      i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_LEFT, 0);
    }
    if (buf[1]) {
      i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_RIGHT, 0);
    }
    if (buf[2]) {
      i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_UP, 0);
    }
    if (buf[3]) {
      i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_DOWN, 0);
    }

    /* Raw ball deltas, then apply mounting orientation + speed scaling. */
    int rx = (int)buf[1] - (int)buf[0]; /* RIGHT - LEFT */
    int ry = (int)buf[3] - (int)buf[2]; /* DOWN  - UP   */
#if PIM447_SWAP_XY
    int tmp = rx;
    rx = ry;
    ry = tmp;
#endif
#if PIM447_INVERT_X
    rx = -rx;
#endif
#if PIM447_INVERT_Y
    ry = -ry;
#endif
    /* Speed-based acceleration: bigger multiplier for faster rolls, so slow
     * movement stays precise and fast flicks cover ground (less janky). */
    int speed = (rx < 0 ? -rx : rx) + (ry < 0 ? -ry : ry);
    int accel = PIM447_ACCEL_BASE + speed * PIM447_ACCEL_GAIN;
    int dx = rx * accel;
    int dy = ry * accel;

    if (dx != 0 || dy != 0) {
      LOG_INF("move: L%u R%u U%u D%u -> dx=%d dy=%d", buf[0], buf[1], buf[2],
              buf[3], dx, dy);
      if (dx != 0) {
        input_report_rel(dev, INPUT_REL_X, dx, (dy == 0), K_NO_WAIT);
      }
      if (dy != 0) {
        input_report_rel(dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
      }
    }

    /* Switch state arrived in the same burst (buf[4] = REG_SWITCH = 0x08). */
    bool pressed = (buf[4] & PIM447_MSK_SWITCH_PRESSED) != 0;
    if (pressed != data->sw_last) {
      LOG_INF("button %s", pressed ? "DOWN" : "up");
      input_report_key(dev, INPUT_BTN_0, pressed ? 1 : 0, true, K_NO_WAIT);
      data->sw_last = pressed;
    }
  }

  if (data->polls % 130 == 0) {
    LOG_INF("alive: polls=%u last_i2c_rc=%d", data->polls, rc);
  }

  k_work_reschedule(dwork, K_MSEC(PIM447_POLL_MS));
}

static int pim447_poll_init(const struct device *dev) {
  struct pim447_poll_data *data = dev->data;
  const struct pim447_poll_config *cfg = dev->config;

  data->dev = dev;

  bool ready = i2c_is_ready_dt(&cfg->i2c);
  LOG_INF("init: node matched, i2c_ready=%d, central=%d", ready,
          IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL));

  if (!ready) {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  k_work_init_delayable(&data->work, pim447_poll_work);

  /* The trackball node lives only in the peripheral overlay, so this driver
   * instantiates only on the half that physically has the ball. Poll wherever
   * it runs; its events are forwarded to the central via zmk,input-split. */
  k_work_schedule(&data->work, K_MSEC(500));
  LOG_INF("polling scheduled");
  return 0;
}

#define PIM447_POLL_INST(n)                                                    \
  static struct pim447_poll_data pim447_poll_data_##n;                         \
  static const struct pim447_poll_config pim447_poll_cfg_##n = {               \
      .i2c = I2C_DT_SPEC_INST_GET(n),                                          \
  };                                                                           \
  DEVICE_DT_INST_DEFINE(n, pim447_poll_init, NULL, &pim447_poll_data_##n,      \
                        &pim447_poll_cfg_##n, POST_KERNEL,                     \
                        CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PIM447_POLL_INST)
