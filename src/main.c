#include <zephyr.h>
#include <device.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>
#include <bluetooth/services/hids.h>

static struct bt_hids hids;

void send_key(uint8_t key_code) {
    uint8_t report[] = {0x01, key_code, 0};  // HID report format
    bt_hids_send(&hids, report, sizeof(report));
}

void main(void) {
    int err;

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    // Initialize HID
    struct bt_hids_init_param hids_init = {
        .protocol_mode = BT_HIDS_PROTOCOL_MODE_REPORT,
        .in_report = {
            .data = NULL,
            .len = 0,
        },
        .out_report = {
            .data = NULL,
            .len = 0,
        },
    };

    err = bt_hids_init(&hids, &hids_init);
    if (err) {
        printk("HID init failed (err %d)\n", err);
        return;
    }

    // Example: Send 'A' key press (key code 0x04)
    send_key(0x04);
}
