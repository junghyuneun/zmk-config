/*
 * Pimoroni PIM447 trackball — polled I2C input driver for ZMK.
 *
 * Reads the movement registers over I2C on a timer (no interrupt line needed)
 * and reports relative X/Y + a button via the Zephyr input subsystem. A
 * zmk,input-listener then turns those into HID mouse reports. The polling loop
 * only runs on the CENTRAL half (the one connected to the host).
 */
#define DT_DRV_COMPAT zmk_pim447_poll

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

struct pim447_poll_config {
  struct i2c_dt_spec i2c;
  uint16_t poll_ms;
  uint8_t scale;
};

struct pim447_poll_data {
  const struct device *dev;
  struct k_work_delayable work;
  bool sw_last;
};

static void pim447_poll_work(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct pim447_poll_data *data =
      CONTAINER_OF(dwork, struct pim447_poll_data, work);
  const struct device *dev = data->dev;
  const struct pim447_poll_config *cfg = dev->config;

  uint8_t buf[4] = {0};

  if (i2c_burst_read_dt(&cfg->i2c, PIM447_REG_LEFT, buf, sizeof(buf)) == 0) {
    /* The L/R/U/D counters accumulate; clear them after each read. */
    i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_LEFT, 0);
    i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_RIGHT, 0);
    i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_UP, 0);
    i2c_reg_write_byte_dt(&cfg->i2c, PIM447_REG_DOWN, 0);

    int dx = ((int)buf[1] - (int)buf[0]) * cfg->scale; /* RIGHT - LEFT */
    int dy = ((int)buf[3] - (int)buf[2]) * cfg->scale; /* DOWN  - UP   */

    if (dx != 0) {
      input_report_rel(dev, INPUT_REL_X, dx, (dy == 0), K_NO_WAIT);
    }
    if (dy != 0) {
      input_report_rel(dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
    }
  }

  uint8_t sw = 0;
  if (i2c_reg_read_byte_dt(&cfg->i2c, PIM447_REG_SWITCH, &sw) == 0) {
    bool pressed = (sw & PIM447_MSK_SWITCH_PRESSED) != 0;
    if (pressed != data->sw_last) {
      input_report_key(dev, INPUT_BTN_0, pressed ? 1 : 0, true, K_NO_WAIT);
      data->sw_last = pressed;
    }
  }

  k_work_reschedule(dwork, K_MSEC(cfg->poll_ms));
}

static int pim447_poll_init(const struct device *dev) {
  struct pim447_poll_data *data = dev->data;
  const struct pim447_poll_config *cfg = dev->config;

  data->dev = dev;

  if (!i2c_is_ready_dt(&cfg->i2c)) {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  k_work_init_delayable(&data->work, pim447_poll_work);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  k_work_schedule(&data->work, K_MSEC(500));
  LOG_INF("PIM447 polling started @ %u ms", cfg->poll_ms);
#else
  LOG_INF("PIM447 present; polling disabled on peripheral half");
#endif
  return 0;
}

#define PIM447_POLL_INST(n)                                                    \
  static struct pim447_poll_data pim447_poll_data_##n;                         \
  static const struct pim447_poll_config pim447_poll_cfg_##n = {               \
      .i2c = I2C_DT_SPEC_INST_GET(n),                                          \
      .poll_ms = DT_INST_PROP_OR(n, poll_interval_ms, 15),                     \
      .scale = DT_INST_PROP_OR(n, scale, 1),                                   \
  };                                                                           \
  DEVICE_DT_INST_DEFINE(n, pim447_poll_init, NULL, &pim447_poll_data_##n,      \
                        &pim447_poll_cfg_##n, POST_KERNEL,                     \
                        CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PIM447_POLL_INST)
