#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include <zephyr/sys/util.h>

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

/* Composite HID Report Descriptor supporting Keyboard, Mouse, and Consumer Control */
static const uint8_t hid_report_desc[] = {
    /* Keyboard Report (Report ID 1) */
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0xFF,        //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0xFF,        //   Usage Maximum (0xFF)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection

    /* Mouse Report (Report ID 2) */
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x05,        //     Usage Maximum (0x05)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    /* Consumer Control Report (Report ID 3) */
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

/* Report IDs */
#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_MOUSE     2
#define REPORT_ID_CONSUMER  3

/* Report sizes */
#define KEYBOARD_REPORT_SIZE    8   /* 1 modifier + 1 reserved + 6 keys */
#define MOUSE_REPORT_SIZE       4   /* 1 buttons + 1 x + 1 y + 1 wheel */
#define CONSUMER_REPORT_SIZE    2   /* 2 bytes for consumer control */
#define MAX_REPORT_SIZE         8   /* Maximum of all report sizes */

enum kb_report_idx {
  KB_MOD_KEY = 0,
  KB_RESERVED,
  KB_KEY_CODE1,
  KB_KEY_CODE2,
  KB_KEY_CODE3,
  KB_KEY_CODE4,
  KB_KEY_CODE5,
  KB_KEY_CODE6,
  KB_REPORT_COUNT = KEYBOARD_REPORT_SIZE,
};

struct kb_event {
  uint16_t code;
  int32_t value;
};

K_MSGQ_DEFINE(kb_msgq, sizeof(struct kb_event), 2, 1);

UDC_STATIC_BUF_DEFINE(report, MAX_REPORT_SIZE);
static uint32_t kb_duration;
static bool kb_ready;
static struct bt_conn *my_conn = NULL;

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
/*	BLE HID Service
 */
/************************************************************************/
/* HID Service UUID */
#ifndef BT_UUID_HIDS_VAL
#define BT_UUID_HIDS_VAL 0x1812
#endif

/* HID Characteristics UUIDs */
#ifndef BT_UUID_HIDS_INFO_VAL
#define BT_UUID_HIDS_INFO_VAL 0x2A4A
#endif
#ifndef BT_UUID_HIDS_REPORT_MAP_VAL
#define BT_UUID_HIDS_REPORT_MAP_VAL 0x2A4B
#endif
#ifndef BT_UUID_HIDS_REPORT_VAL
#define BT_UUID_HIDS_REPORT_VAL 0x2A4D
#endif
#ifndef BT_UUID_HIDS_CTRL_POINT_VAL
#define BT_UUID_HIDS_CTRL_POINT_VAL 0x2A4C
#endif

/* HID Report Reference Descriptor UUID */
#ifndef BT_UUID_HIDS_REPORT_REF_VAL
#define BT_UUID_HIDS_REPORT_REF_VAL 0x2908
#endif

static struct bt_uuid_16 hids_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_VAL);
static struct bt_uuid_16 hids_info_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_INFO_VAL);
static struct bt_uuid_16 hids_report_map_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_REPORT_MAP_VAL);
static struct bt_uuid_16 hids_report_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_REPORT_VAL);
static struct bt_uuid_16 hids_ctrl_point_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_CTRL_POINT_VAL);
static struct bt_uuid_16 hids_report_ref_uuid = BT_UUID_INIT_16(BT_UUID_HIDS_REPORT_REF_VAL);

/* HID Information characteristic value */
static uint8_t hid_info[4] = {
    0x01, 0x11, /* HID version: 1.11 */
    0x00,       /* Country code: Not supported */
    0x03        /* Flags: Remote wake + Normally connectable */
};

/* HID Report Reference descriptors */
static uint8_t keyboard_input_report_ref[2] = {REPORT_ID_KEYBOARD, 0x01}; /* Report ID 1, Input Report */
static uint8_t mouse_input_report_ref[2] = {REPORT_ID_MOUSE, 0x01};       /* Report ID 2, Input Report */
static uint8_t consumer_input_report_ref[2] = {REPORT_ID_CONSUMER, 0x01}; /* Report ID 3, Input Report */
static uint8_t output_report_ref[2] = {REPORT_ID_KEYBOARD, 0x02};         /* Report ID 1, Output Report */

/* Composite HID Report Map supporting Keyboard, Mouse, and Consumer Control */
static const uint8_t hid_report_map[] = {
    /* Keyboard Report (Report ID 1) */
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0xFF,        //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0xFF,        //   Usage Maximum (0xFF)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection

    /* Mouse Report (Report ID 2) */
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x05,        //     Usage Maximum (0x05)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    /* Consumer Control Report (Report ID 3) */
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

static uint8_t hid_keyboard_report[KEYBOARD_REPORT_SIZE] = {0}; /* Keyboard report buffer */
static uint8_t hid_mouse_report[MOUSE_REPORT_SIZE] = {0};       /* Mouse report buffer */
static uint8_t hid_consumer_report[CONSUMER_REPORT_SIZE] = {0}; /* Consumer report buffer */
static uint8_t hid_output_report[1] = {0};                     /* LED states */

/* HID Control Point value */
static uint8_t hid_ctrl_point = 0;

/* HID Service Callbacks */

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_info, sizeof(hid_info));
}

static ssize_t read_hid_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_map, sizeof(hid_report_map));
}

static ssize_t write_hid_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    
    if (len == 0 || len > MAX_REPORT_SIZE) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    const uint8_t *report_data = (const uint8_t *)buf;
    uint8_t report_id = report_data[0];
    
    LOG_INF("Received HID input report via Bluetooth (Report ID: %d, Length: %d):", report_id, len);
    LOG_HEXDUMP_INF(report_data, len, "HID Report");
    
    /* Process based on report ID */
    switch (report_id) {
        case REPORT_ID_KEYBOARD:
            if (len != KEYBOARD_REPORT_SIZE + 1) { /* +1 for report ID */
                LOG_ERR("Invalid keyboard report length: %d (expected %d)", len, KEYBOARD_REPORT_SIZE + 1);
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            }
            memcpy(hid_keyboard_report, report_data + 1, KEYBOARD_REPORT_SIZE); /* Skip report ID */
            
            /* Forward keyboard report to USB */
            if (kb_ready) {
                memset(report, 0, MAX_REPORT_SIZE);
                memcpy(report, hid_keyboard_report, KEYBOARD_REPORT_SIZE);
                
                int ret = hid_device_submit_report(hid_dev, KEYBOARD_REPORT_SIZE, report);
                if (ret) {
                    LOG_ERR("Failed to forward keyboard report to USB: %d", ret);
                } else {
                    LOG_INF("Keyboard report forwarded to USB successfully");
                }
            } else {
                LOG_WRN("USB HID device not ready, cannot forward keyboard report");
            }
            break;
            
        case REPORT_ID_MOUSE:
            if (len != MOUSE_REPORT_SIZE + 1) { /* +1 for report ID */
                LOG_ERR("Invalid mouse report length: %d (expected %d)", len, MOUSE_REPORT_SIZE + 1);
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            }
            memcpy(hid_mouse_report, report_data + 1, MOUSE_REPORT_SIZE); /* Skip report ID */
            
            /* Forward mouse report to USB */
            if (kb_ready) {
                memset(report, 0, MAX_REPORT_SIZE);
                report[0] = REPORT_ID_MOUSE;
                memcpy(report + 1, hid_mouse_report, MOUSE_REPORT_SIZE);
                
                int ret = hid_device_submit_report(hid_dev, MOUSE_REPORT_SIZE + 1, report);
                if (ret) {
                    LOG_ERR("Failed to forward mouse report to USB: %d", ret);
                } else {
                    LOG_INF("Mouse report forwarded to USB successfully");
                }
            } else {
                LOG_WRN("USB HID device not ready, cannot forward mouse report");
            }
            break;
            
        case REPORT_ID_CONSUMER:
            if (len != CONSUMER_REPORT_SIZE + 1) { /* +1 for report ID */
                LOG_ERR("Invalid consumer report length: %d (expected %d)", len, CONSUMER_REPORT_SIZE + 1);
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            }
            memcpy(hid_consumer_report, report_data + 1, CONSUMER_REPORT_SIZE); /* Skip report ID */
            
            /* Forward consumer report to USB */
            if (kb_ready) {
                memset(report, 0, MAX_REPORT_SIZE);
                report[0] = REPORT_ID_CONSUMER;
                memcpy(report + 1, hid_consumer_report, CONSUMER_REPORT_SIZE);
                
                int ret = hid_device_submit_report(hid_dev, CONSUMER_REPORT_SIZE + 1, report);
                if (ret) {
                    LOG_ERR("Failed to forward consumer report to USB: %d", ret);
                } else {
                    LOG_INF("Consumer report forwarded to USB successfully");
                }
            } else {
                LOG_WRN("USB HID device not ready, cannot forward consumer report");
            }
            break;
            
        default:
            LOG_WRN("Unknown report ID: %d", report_id);
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    return len;
}

static ssize_t write_hid_output_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    
    if (len != sizeof(hid_output_report)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    memcpy(hid_output_report, buf, len);
    
    /* Update LED based on output report */
    gpio_pin_set_dt(&led, hid_output_report[0] & BIT(0));
    
    LOG_INF("HID Output Report received: 0x%02x", hid_output_report[0]);
    
    return len;
}

static ssize_t write_hid_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    hid_ctrl_point = *((uint8_t *)buf);
    LOG_INF("HID Control Point: %s", hid_ctrl_point ? "Suspend" : "Exit Suspend");
    
    return len;
}

static ssize_t read_input_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, keyboard_input_report_ref, sizeof(keyboard_input_report_ref));
}

static ssize_t read_mouse_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, mouse_input_report_ref, sizeof(mouse_input_report_ref));
}

static ssize_t read_consumer_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, consumer_input_report_ref, sizeof(consumer_input_report_ref));
}

static ssize_t read_output_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                     void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, output_report_ref, sizeof(output_report_ref));
}

/* HID Service Definition */
BT_GATT_SERVICE_DEFINE(hid_svc,
    BT_GATT_PRIMARY_SERVICE(&hids_uuid),
    
    /* HID Information Characteristic */
    BT_GATT_CHARACTERISTIC(&hids_info_uuid.uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          read_hid_info, NULL, NULL),
    
    /* HID Report Map Characteristic */
    BT_GATT_CHARACTERISTIC(&hids_report_map_uuid.uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          read_hid_report_map, NULL, NULL),
    
    /* HID Keyboard Input Report Characteristic - Write only for receiving reports */
    BT_GATT_CHARACTERISTIC(&hids_report_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE,
                          NULL, write_hid_input_report, NULL),
    BT_GATT_DESCRIPTOR(&hids_report_ref_uuid.uuid,
                      BT_GATT_PERM_READ,
                      read_input_report_ref, NULL, NULL),
    
    /* HID Mouse Input Report Characteristic - Write only for receiving reports */
    BT_GATT_CHARACTERISTIC(&hids_report_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE,
                          NULL, write_hid_input_report, NULL),
    BT_GATT_DESCRIPTOR(&hids_report_ref_uuid.uuid,
                      BT_GATT_PERM_READ,
                      read_mouse_report_ref, NULL, NULL),
    
    /* HID Consumer Input Report Characteristic - Write only for receiving reports */
    BT_GATT_CHARACTERISTIC(&hids_report_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE,
                          NULL, write_hid_input_report, NULL),
    BT_GATT_DESCRIPTOR(&hids_report_ref_uuid.uuid,
                      BT_GATT_PERM_READ,
                      read_consumer_report_ref, NULL, NULL),
    
    /* HID Output Report Characteristic */
    BT_GATT_CHARACTERISTIC(&hids_report_uuid.uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                          NULL, write_hid_output_report, NULL),
    BT_GATT_DESCRIPTOR(&hids_report_ref_uuid.uuid,
                      BT_GATT_PERM_READ,
                      read_output_report_ref, NULL, NULL),
    
    /* HID Control Point Characteristic */
    BT_GATT_CHARACTERISTIC(&hids_ctrl_point_uuid.uuid,
                          BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE,
                          NULL, write_hid_ctrl_point, NULL),
);

static const struct bt_le_adv_param *adv_param =
    BT_LE_ADV_PARAM((BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY),
                    800,   /*Min Advertising Interval 500ms (800*0.625ms) */
                    801,   /*Max Advertising Interval 500.625ms (801*0.625ms)*/
                    NULL); /* Set to NULL for undirected advertising*/

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0x03, 0xC0), /* Generic HID appearance */
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Resume advertising after a disconnection */
static void adv_work_handler(struct k_work *work) {
  int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    LOG_ERR("Advertising failed to start (err %d)", err);
    return;
  }
  LOG_INF("Advertising successfully started");
}

static struct k_work adv_work;

static void advertising_start(void) { k_work_submit(&adv_work); }

static void connected_cb(struct bt_conn *conn, uint8_t err) {
  if (err) {
    LOG_ERR("BLE connection error %d", err);
    return;
  }
  LOG_INF("BLE client connected - ready to receive HID reports (Keyboard/Mouse/Consumer)");
  my_conn = bt_conn_ref(conn);

  /* Clear any existing HID reports */
  memset(hid_keyboard_report, 0, sizeof(hid_keyboard_report));
  memset(hid_mouse_report, 0, sizeof(hid_mouse_report));
  memset(hid_consumer_report, 0, sizeof(hid_consumer_report));
  memset(hid_output_report, 0, sizeof(hid_output_report));
  
  /* Clear USB report buffer as well */
  memset(report, 0, MAX_REPORT_SIZE);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
  LOG_INF("BLE client disconnected. Reason %d", reason);
  
  /* Clear all reports on disconnect */
  memset(hid_keyboard_report, 0, sizeof(hid_keyboard_report));
  memset(hid_mouse_report, 0, sizeof(hid_mouse_report));
  memset(hid_consumer_report, 0, sizeof(hid_consumer_report));
  memset(hid_output_report, 0, sizeof(hid_output_report));
  memset(report, 0, MAX_REPORT_SIZE);
  
  /* Send empty reports to release any pressed keys/buttons */
  if (kb_ready) {
    /* Release keyboard keys */
    memset(report, 0, KEYBOARD_REPORT_SIZE);
    int ret = hid_device_submit_report(hid_dev, KEYBOARD_REPORT_SIZE, report);
    if (ret) {
      LOG_ERR("Failed to send keyboard release report: %d", ret);
    } else {
      LOG_INF("Released all keyboard keys on USB");
    }
    
    /* Release mouse buttons */
    memset(report, 0, MAX_REPORT_SIZE);
    report[0] = REPORT_ID_MOUSE;
    ret = hid_device_submit_report(hid_dev, MOUSE_REPORT_SIZE + 1, report);
    if (ret) {
      LOG_ERR("Failed to send mouse release report: %d", ret);
    } else {
      LOG_INF("Released all mouse buttons on USB");
    }
    
    /* Release consumer controls */
    memset(report, 0, MAX_REPORT_SIZE);
    report[0] = REPORT_ID_CONSUMER;
    ret = hid_device_submit_report(hid_dev, CONSUMER_REPORT_SIZE + 1, report);
    if (ret) {
      LOG_ERR("Failed to send consumer release report: %d", ret);
    } else {
      LOG_INF("Released all consumer controls on USB");
    }
  }
  
  if (my_conn) {
    bt_conn_unref(my_conn);
    my_conn = NULL;
  }
}

static void recycled_cb(void) {
  LOG_INF("BLE connection cleaned up, restarting advertising");
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

  ret = bt_enable(NULL);
  if (ret) {
    LOG_ERR("Bluetooth init failed (err %d)", ret);
    return -1;
  }

  LOG_INF("Bluetooth HID service initialized");

  /* Start connectable advertising */
  k_work_init(&adv_work, adv_work_handler);
  advertising_start();

  LOG_INF("Bluetooth is initialized and advertising");
  LOG_INF("Device ready: Universal BLE-to-USB HID bridge mode");
  LOG_INF("Supports: Keyboard, Mouse, and Consumer Control devices");
  LOG_INF("Waiting for BLE connections and HID reports...");

  while (true) {
    struct kb_event kb_evt;

    k_msgq_get(&kb_msgq, &kb_evt, K_FOREVER);

    /* Handle local input events (for testing purposes) */
    switch (kb_evt.code) {
    case INPUT_KEY_0:
      LOG_INF("Local input detected: Key 0 %s", kb_evt.value ? "pressed" : "released");
      
      /* Clear report first */
      memset(report, 0, MAX_REPORT_SIZE);
      
      if (kb_evt.value) {
        report[KB_KEY_CODE1] = HID_KEY_NUMLOCK;
      } else {
        report[KB_KEY_CODE1] = 0;
      }

      /* Only send via USB (this device is BLE-to-USB bridge) */
      if (kb_ready) {
        int ret = hid_device_submit_report(hid_dev, KEYBOARD_REPORT_SIZE, report);
        if (ret) {
          LOG_ERR("Local input USB HID submit error: %d", ret);
        } else {
          LOG_INF("Local input forwarded to USB");
        }
      } else {
        LOG_WRN("USB HID device not ready for local input");
      }
      break;

    default:
      LOG_DBG("Unrecognized local input code %u value %d", kb_evt.code, kb_evt.value);
      continue;
    }
  }

  return 0;
}
