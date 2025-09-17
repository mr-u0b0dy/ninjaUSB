## ninjaUSB – AI Coding Assistant Instructions

Goal: Firmware for an nRF52840 dongle acting as a BLE peripheral that receives commands/keystrokes and emits USB HID keyboard reports (future BadUSB extensions). Keep changes minimal, Zephyr‑aligned, and reproducible.

### Architecture & Flow
1. Entry: `app/src/main.c` – initializes GPIO LED, USB HID device, Bluetooth stack, advertising, event loop.
2. USB: `app/src/usb_init.c` builds descriptors & config via Zephyr NEXT USB stack (`CONFIG_USB_DEVICE_STACK_NEXT`, HID enabled). Placeholders (VID/PID, manufacturer) still marked TODO.
3. HID Device Node: `app/app.overlay` defines `hid_dev_0` (keyboard protocol, 64‑byte IN report, 1 ms poll). Retrieved with `DEVICE_DT_GET_ONE(zephyr_hid_device)`.
4. Reports: Static buffer `report[]` sized by `KB_REPORT_COUNT` indexes (mod key + 6 keycodes). Submission via `hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report)` after readiness flag set in `.iface_ready` callback.
5. Input Path: Zephyr input subsystem callback (`input_cb`) enqueues events into `kb_msgq`; main loop dequeues and mutates `report` based on `INPUT_KEY_*` codes.
6. BLE: Custom 128‑bit service + writable characteristic (`write_command`) in `main.c`; writes copied to `command_buf`. Example: first byte == 1 triggers NumLock key press.
7. Event Loop: Blocks on `k_msgq_get`, updates report, checks `kb_ready`, then sends HID report.

### Build / Flash Workflow
Build (from repo root inside Zephyr workspace):
```
west build -b nrf52840dongle/nrf52840 app
```
Artifacts: `build/zephyr/zephyr.hex`, signed package generated manually.
Flash via serial DFU (adjust `/dev/ttyACM0` as needed):
```
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
  --application build/zephyr/zephyr.hex \
  --application-version 1 build/firmware.zip
nrfutil dfu usb-serial -pkg build/firmware.zip -p /dev/ttyACM0
```
If USB VBUS detection unsupported, device enabling is forced (`!usbd_can_detect_vbus`).

### Conventions & Patterns
- CMake: `app/CMakeLists.txt` glob‑adds `src/*.c`; keep new sources in `app/src/` or include headers via `app/inc/` + `zephyr_include_directories(inc)`.
- Configuration split: Feature toggles & stack settings in `app/prj.conf`; only add Zephyr Kconfig symbols actually used.
- Versioning: `app/VERSION` (Zephyr format) – bump when changing externally observable behavior.
- Logging: Use `LOG_MODULE_REGISTER(<module>, LOG_LEVEL_*)`; prefer existing modules (`main`, `usbd_app_config`); avoid printk except for very early BLE messages (some still present).
- HID report editing: Mutate indices defined by `enum kb_report_idx`; always clear released keys (set to 0) to avoid stuck modifiers.
- BLE characteristic writes: Validate length (`len <= sizeof(command_buf)`), return proper ATT error on overflow. Extend action dispatch using first command byte (add `switch` instead of chained `if`).
- USB descriptors: Update constants in `usb_init.c` (`USB_DEVICE_MANUFACTURER`, VID/PID) before distributing hardware; ensure uniqueness.
- Avoid blocking outside of main loop; use Zephyr work (`k_work`) for deferred operations like restarting advertising (`adv_work`).

### Extending Functionality (Examples)
- Add a new command (e.g., send string): extend `write_command` with command byte map; enqueue synthesized `kb_event`s instead of directly editing `report` for uniformity.
- Add more input sources: register additional `INPUT_CALLBACK_DEFINE` handlers; ensure they produce `INPUT_KEY_*` codes the switch handles.
- Additional keys: Expand switch in main loop; maintain mutual exclusivity if needed (clear prior codes).

### Gotchas
- Do not exceed `KB_REPORT_COUNT`; HID keyboard standard supports 6 simultaneous keys + modifiers.
- `hid_device_submit_report` requires device readiness (`kb_ready` flag set in `.iface_ready`). Submissions earlier silently fail or log errors.
- VID/PID placeholders must not ship; Nordic examples’ values are not production legal.
- Message queue depth is 2 (`K_MSGQ_DEFINE(kb_msgq, ..., 2, ...)`); burst inputs beyond this are dropped; enlarge if adding rapid input sources.

### Safe Change Checklist
1. Build succeeds (`west build ...`).
2. USB enumeration intact (keyboard recognized) – changing descriptors can break host detection.
3. BLE advertising still starts after disconnect (`recycled_cb` triggers `advertising_start`).
4. No stuck keys (press/release path clears report indices).

### Areas Lacking Automation
No unit / integration tests present; validation is manual (USB enumeration + BLE write -> HID action). Keep additions deterministic and log‑rich.

Feedback Wanted: Clarify more on BLE command protocol structure? Add guidance for future BadUSB scripting layer? Indicate and I’ll refine.
