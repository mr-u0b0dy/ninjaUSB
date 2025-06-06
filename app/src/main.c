#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>

struct usbd_context *app_usbd_init_device(usbd_msg_cb_t msg_cb);
const struct device *hid_dev;

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

enum kb_report_idx {
  KB_MOD_KEY = 0,
  KB_RESERVED,
  KB_KEY_CODE1,
  KB_KEY_CODE2,
  KB_KEY_CODE3,
  KB_KEY_CODE4,
  KB_KEY_CODE5,
  KB_KEY_CODE6,
  KB_REPORT_COUNT,
};

struct kb_event {
  uint16_t code;
  int32_t value;
};

K_MSGQ_DEFINE(kb_msgq, sizeof(struct kb_event), 2, 1);

UDC_STATIC_BUF_DEFINE(report, KB_REPORT_COUNT);
static uint32_t kb_duration;
static bool kb_ready;

static void input_cb(struct input_event *evt, void *user_data) {
  struct kb_event kb_evt;

  ARG_UNUSED(user_data);

  kb_evt.code = evt->code;
  kb_evt.value = evt->value;
  if (k_msgq_put(&kb_msgq, &kb_evt, K_NO_WAIT) != 0) {
    LOG_ERR("Failed to put new input event");
  }
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

static void kb_iface_ready(const struct device *dev, const bool ready) {
  LOG_INF("HID device %s interface is %s", dev->name,
          ready ? "ready" : "not ready");
  kb_ready = ready;
}

static int kb_get_report(const struct device *dev, const uint8_t type,
                         const uint8_t id, const uint16_t len,
                         uint8_t *const buf) {
  LOG_WRN("Get Report not implemented, Type %u ID %u", type, id);

  return 0;
}

static int kb_set_report(const struct device *dev, const uint8_t type,
                         const uint8_t id, const uint16_t len,
                         const uint8_t *const buf) {
  if (type != HID_REPORT_TYPE_OUTPUT) {
    LOG_WRN("Unsupported report type");
    return -ENOTSUP;
  }

  (void)gpio_pin_set_dt(&led, buf[0] & BIT(0));

  return 0;
}

/* Idle duration is stored but not used to calculate idle reports. */
static void kb_set_idle(const struct device *dev, const uint8_t id,
                        const uint32_t duration) {
  LOG_INF("Set Idle %u to %u", id, duration);
  kb_duration = duration;
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id) {
  LOG_INF("Get Idle %u to %u", id, kb_duration);
  return kb_duration;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto) {
  LOG_INF("Protocol changed to %s",
          proto == 0U ? "Boot Protocol" : "Report Protocol");
}

static void kb_output_report(const struct device *dev, const uint16_t len,
                             const uint8_t *const buf) {
  LOG_HEXDUMP_DBG(buf, len, "o.r.");
  kb_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0U, len, buf);
}

struct hid_device_ops kb_ops = {
    .iface_ready = kb_iface_ready,
    .get_report = kb_get_report,
    .set_report = kb_set_report,
    .set_idle = kb_set_idle,
    .get_idle = kb_get_idle,
    .set_protocol = kb_set_protocol,
    .output_report = kb_output_report,
};

/* doc device msg-cb start */
static void msg_cb(struct usbd_context *const usbd_ctx,
                   const struct usbd_msg *const msg) {
  LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

  if (msg->type == USBD_MSG_CONFIGURATION) {
    LOG_INF("\tConfiguration value %d", msg->status);
  }

  if (usbd_can_detect_vbus(usbd_ctx)) {
    if (msg->type == USBD_MSG_VBUS_READY) {
      if (usbd_enable(usbd_ctx)) {
        LOG_ERR("Failed to enable device support");
      }
    }

    if (msg->type == USBD_MSG_VBUS_REMOVED) {
      if (usbd_disable(usbd_ctx)) {
        LOG_ERR("Failed to disable device support");
      }
    }
  }
}
/* doc device msg-cb end */

/************************************************************************/
/*	BLE
 */
/************************************************************************/
#define BT_UUID_CUSTOM_SERVICE_VAL                                             \
  BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_CMD_CHAR_VAL                                                   \
  BT_UUID_128_ENCODE(0xabcdef01, 0x2345, 0x6789, 0x2345, 0x6789abcdef01)

static struct bt_uuid_128 custom_service_uuid =
    BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE_VAL);
static struct bt_uuid_128 cmd_char_uuid =
    BT_UUID_INIT_128(BT_UUID_CMD_CHAR_VAL);

static uint8_t command_buf[20];

ssize_t write_command(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      const void *buf, uint16_t len, uint16_t offset,
                      uint8_t flags) {
  if (len > sizeof(command_buf)) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }

  memcpy(command_buf, buf, len);
  printk("Received command: ");
  for (int i = 0; i < len; i++) {
    printk("%02x ", command_buf[i]);
  }
  printk("\n");

  if (command_buf[0] == 1) {
    report[KB_KEY_CODE1] = HID_KEY_NUMLOCK;
    hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report);
  }

  return len;
}

BT_GATT_SERVICE_DEFINE(
    custom_svc, BT_GATT_PRIMARY_SERVICE(&custom_service_uuid),
    BT_GATT_CHARACTERISTIC(&cmd_char_uuid.uuid, BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, write_command, NULL), );

static const struct bt_le_adv_param *adv_param =
    BT_LE_ADV_PARAM((BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY),
                    800,   /*Min Advertising Interval 500ms (800*0.625ms) */
                    801,   /*Max Advertising Interval 500.625ms (801*0.625ms)*/
                    NULL); /* Set to NULL for undirected advertising*/

static const struct bt_data ad[] = {
    BT_DATA_BYTES(
        BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)), /* Set the advertising flags */
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    /* Set the advertising packet data  */};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Resume advertising after a disconnection */
static void adv_work_handler(struct k_work *work) {
  int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    printk("Advertising failed to start (err %d)\n", err);
    return;
  }
  printk("Advertising successfully started\n");
}

static struct k_work adv_work;
static void advertising_start(void) { k_work_submit(&adv_work); }

struct bt_conn *my_conn = NULL;

void connected_cb(struct bt_conn *conn, uint8_t err) {
  if (err) {
    LOG_ERR("Connection error %d", err);
    return;
  }
  LOG_INF("Connected");
  my_conn = bt_conn_ref(conn);

  /* TODO: Turn the connection status LED on */
}

void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
  LOG_INF("Disconnected. Reason %d", reason);
  bt_conn_unref(my_conn);

  /* TODO: Turn the connection status LED off */
}

static void recycled_cb(void) {
  LOG_INF(" Connection object available from previous conn. Disconnect is "
          "complete!\n");
  advertising_start();
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
    .recycled = recycled_cb,
};

int main(void) {
  struct usbd_context *app_usbd;
  int ret;

  if (!gpio_is_ready_dt(&led)) {
    LOG_ERR("LED device %s is not ready", led.port->name);
    return -EIO;
  }

  ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
  if (ret != 0) {
    LOG_ERR("Failed to configure the LED pin, %d", ret);
    return -EIO;
  }

  hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
  if (!device_is_ready(hid_dev)) {
    LOG_ERR("HID Device is not ready");
    return -EIO;
  }

  ret = hid_device_register(hid_dev, hid_report_desc, sizeof(hid_report_desc),
                            &kb_ops);
  if (ret != 0) {
    LOG_ERR("Failed to register HID Device, %d", ret);
    return ret;
  }

  app_usbd = app_usbd_init_device(msg_cb);
  if (app_usbd == NULL) {
    LOG_ERR("Failed to initialize USB device");
    return -ENODEV;
  }

  if (!usbd_can_detect_vbus(app_usbd)) {
    /* doc device enable start */
    ret = usbd_enable(app_usbd);
    if (ret) {
      LOG_ERR("Failed to enable device support");
      return ret;
    }
    /* doc device enable end */
  }

  LOG_INF("HID keyboard is initialized");

#if 0
  /* Configure the random static address */
  bt_addr_le_t addr;
  ret = bt_addr_le_from_str("FF:EE:DD:CC:BB:AA", "random", &addr);
  if (ret) {
    printk("Invalid BT address (err %d)\n", ret);
  }

  ret = bt_id_create(&addr, NULL);
  if (ret < 0) {
    printk("Creating new ID failed (err %d)\n", ret);
  }
#endif

  ret = bt_enable(NULL);
  if (ret) {
    LOG_ERR("Bluetooth init failed (err %d)\n", ret);
    return -1;
  }

  /* Start connectable advertising */
  k_work_init(&adv_work, adv_work_handler);
  advertising_start();

  LOG_INF("Bluetooth is initialized");

  while (true) {
    struct kb_event kb_evt;

    k_msgq_get(&kb_msgq, &kb_evt, K_FOREVER);

    switch (kb_evt.code) {
    case INPUT_KEY_0:
      if (kb_evt.value) {
        report[KB_KEY_CODE1] = HID_KEY_NUMLOCK;
      } else {
        report[KB_KEY_CODE1] = 0;
      }

      break;
    /*case INPUT_KEY_1:*/
    /*	if (kb_evt.value) {*/
    /*		report[KB_KEY_CODE2] = HID_KEY_CAPSLOCK;*/
    /*	} else {*/
    /*		report[KB_KEY_CODE2] = 0;*/
    /*	}*/
    /**/
    /*	break;*/
    /*case INPUT_KEY_2:*/
    /*	if (kb_evt.value) {*/
    /*		report[KB_KEY_CODE3] = HID_KEY_SCROLLLOCK;*/
    /*	} else {*/
    /*		report[KB_KEY_CODE3] = 0;*/
    /*	}*/
    /**/
    /*	break;*/
    /*case INPUT_KEY_3:*/
    /*	if (kb_evt.value) {*/
    /*		report[KB_MOD_KEY] = HID_KBD_MODIFIER_RIGHT_ALT;*/
    /*		report[KB_KEY_CODE4] = HID_KEY_1;*/
    /*		report[KB_KEY_CODE5] = HID_KEY_2;*/
    /*		report[KB_KEY_CODE6] = HID_KEY_3;*/
    /*	} else {*/
    /*		report[KB_MOD_KEY] = HID_KBD_MODIFIER_NONE;*/
    /*		report[KB_KEY_CODE4] = 0;*/
    /*		report[KB_KEY_CODE5] = 0;*/
    /*		report[KB_KEY_CODE6] = 0;*/
    /*	}*/
    /**/
    /*	break;*/
    default:
      LOG_INF("Unrecognized input code %u value %d", kb_evt.code, kb_evt.value);
      continue;
    }

    if (!kb_ready) {
      LOG_INF("USB HID device is not ready");
      continue;
    }

    ret = hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report);
    if (ret) {
      LOG_ERR("HID submit report error, %d", ret);
    }
  }

  return 0;
}
