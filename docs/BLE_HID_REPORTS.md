# BLE HID Reports Documentation

## Overview

The ninjaUSB firmware implements a universal BLE-to-USB HID bridge that supports multiple HID device types through a composite HID descriptor. This document explains the BLE report structure and how to interact with the device.

## Supported Device Types

| Device Type | Report ID | Purpose | Report Size |
|-------------|-----------|---------|-------------|
| Keyboard | 1 | Text input, key combinations | 8 bytes |
| Mouse | 2 | Pointer control, buttons, scroll | 4 bytes |
| Consumer Control | 3 | Media keys, system controls | 2 bytes |

## BLE Service Structure

### HID Service (UUID: 0x1812)

The device implements the standard Bluetooth HID service with the following characteristics:

```text
HID Service (0x1812)
├── HID Information (0x2A4A) - Read
├── Report Map (0x2A4B) - Read
├── Keyboard Input Report (0x2A4D) - Write
├── Mouse Input Report (0x2A4D) - Write
├── Consumer Input Report (0x2A4D) - Write
├── Output Report (0x2A4D) - Read/Write
└── Control Point (0x2A4C) - Write
```

### Report Reference Descriptors

Each input report characteristic has a Report Reference descriptor (0x2908) that identifies:

- **Report ID**: Unique identifier for the report type
- **Report Type**: 0x01 for Input Report, 0x02 for Output Report

## Report Formats

### 1. Keyboard Reports (Report ID: 1)

**Size**: 9 bytes (1 byte Report ID + 8 bytes data)

```text
Byte 0: Report ID (0x01)
Byte 1: Modifier Keys Bitmap
Byte 2: Reserved (0x00)
Byte 3-8: Key Codes (6 simultaneous keys)
```

#### Modifier Keys Bitmap (Byte 1)

```text
Bit 0: Left Ctrl
Bit 1: Left Shift  
Bit 2: Left Alt
Bit 3: Left GUI (Windows/Cmd)
Bit 4: Right Ctrl
Bit 5: Right Shift
Bit 6: Right Alt
Bit 7: Right GUI (Windows/Cmd)
```

#### Example Keyboard Reports

```c
// Press 'A' key
uint8_t report[] = {0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};

// Press Ctrl+C
uint8_t report[] = {0x01, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00};

// Release all keys
uint8_t report[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
```

### 2. Mouse Reports (Report ID: 2)

**Size**: 5 bytes (1 byte Report ID + 4 bytes data)

```text
Byte 0: Report ID (0x02)
Byte 1: Button Bitmap
Byte 2: X Movement (-127 to +127)
Byte 3: Y Movement (-127 to +127)  
Byte 4: Wheel Movement (-127 to +127)
```

#### Button Bitmap (Byte 1)

```text
Bit 0: Left Button
Bit 1: Right Button
Bit 2: Middle Button
Bit 3: Back Button
Bit 4: Forward Button
Bits 5-7: Reserved (0)
```

#### Example Mouse Reports

```c
// Left click
uint8_t report[] = {0x02, 0x01, 0x00, 0x00, 0x00};

// Move right 10 pixels, down 5 pixels
uint8_t report[] = {0x02, 0x00, 0x0A, 0x05, 0x00};

// Scroll up 3 units
uint8_t report[] = {0x02, 0x00, 0x00, 0x00, 0x03};

// Right click with movement
uint8_t report[] = {0x02, 0x02, 0xFF, 0xFF, 0x00}; // Right click, move left/up
```

### 3. Consumer Control Reports (Report ID: 3)

**Size**: 3 bytes (1 byte Report ID + 2 bytes data)

```text
Byte 0: Report ID (0x03)
Byte 1: Usage Code Low Byte
Byte 2: Usage Code High Byte
```

#### Common Consumer Control Codes

```c
// Volume Controls
0x00E9: Volume Up
0x00EA: Volume Down
0x00E2: Mute

// Media Controls  
0x00CD: Play/Pause
0x00B5: Next Track
0x00B6: Previous Track
0x00B7: Stop

// System Controls
0x0082: Sleep
0x0083: Wake Up
0x0030: Power
```

#### Example Consumer Control Reports

```c
// Volume Up
uint8_t report[] = {0x03, 0xE9, 0x00};

// Play/Pause
uint8_t report[] = {0x03, 0xCD, 0x00};

// Release (stop media control)
uint8_t report[] = {0x03, 0x00, 0x00};
```

## BLE Communication Protocol

### Connection Process

1. Device advertises as "ninjaUSB HID" with HID service UUID
2. Client connects and discovers HID service characteristics
3. Client writes to appropriate input report characteristic based on device type

### Writing Reports

To send a HID report via BLE:

1. **Identify the target characteristic** based on report type:
   - Keyboard: Use keyboard input report characteristic
   - Mouse: Use mouse input report characteristic  
   - Consumer: Use consumer input report characteristic

2. **Construct the report** with proper Report ID and data format

3. **Write the complete report** (including Report ID) to the characteristic

### Report Processing Flow

```text
BLE Client → Write Report → ninjaUSB Device → Validate → Forward to USB → PC
```

1. **BLE Reception**: Device receives report via GATT write
2. **Validation**: Report ID and length are validated
3. **Processing**: Report is parsed based on Report ID
4. **USB Forwarding**: Report is immediately forwarded to USB HID interface
5. **PC Processing**: Host PC processes the HID input

## Error Handling

### Invalid Reports

The device validates all incoming reports and returns appropriate GATT errors:

- **Invalid Offset**: `BT_ATT_ERR_INVALID_OFFSET`
- **Invalid Length**: `BT_ATT_ERR_INVALID_ATTRIBUTE_LEN`
- **Unknown Report ID**: `BT_ATT_ERR_INVALID_ATTRIBUTE_LEN`

### USB Forwarding

If USB is not ready, reports are logged but not forwarded. The device will:

- Log the received report for debugging
- Store the report in internal buffers
- Skip USB forwarding with warning message

## Disconnect Behavior

When a BLE client disconnects:

1. All report buffers are cleared
2. Empty reports are sent to USB to release any pressed keys/buttons
3. Device resumes advertising for new connections

## Development Examples

### Python Example (using bleak)

```python
import asyncio
from bleak import BleakClient

# HID Service and characteristic UUIDs
HID_SERVICE_UUID = "00001812-0000-1000-8000-00805f9b34fb"
HID_REPORT_UUID = "00002a4d-0000-1000-8000-00805f9b34fb"

async def send_keyboard_report(client, keys):
    # Keyboard report: [Report ID, Modifier, Reserved, Key1-6]
    report = bytearray([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
    
    # Add key codes
    for i, key in enumerate(keys[:6]):
        report[3 + i] = key
    
    # Find keyboard characteristic (check descriptor for Report ID 1)
    await client.write_gatt_char(keyboard_char_uuid, report)

async def send_mouse_click(client):
    # Mouse left click: [Report ID, Buttons, X, Y, Wheel]
    report = bytearray([0x02, 0x01, 0x00, 0x00, 0x00])
    await client.write_gatt_char(mouse_char_uuid, report)
```

## Advanced Usage Examples

### Typing Sequences with Proper Timing

```python
async def type_string(client, text, delay_ms=20):
    """Type a string with proper key press/release timing"""
    
    # Key code mapping for common characters
    key_map = {
        'a': 0x04, 'b': 0x05, 'c': 0x06, 'd': 0x07, 'e': 0x08,
        'f': 0x09, 'g': 0x0A, 'h': 0x0B, 'i': 0x0C, 'j': 0x0D,
        'k': 0x0E, 'l': 0x0F, 'm': 0x10, 'n': 0x11, 'o': 0x12,
        'p': 0x13, 'q': 0x14, 'r': 0x15, 's': 0x16, 't': 0x17,
        'u': 0x18, 'v': 0x19, 'w': 0x1A, 'x': 0x1B, 'y': 0x1C,
        'z': 0x1D, ' ': 0x2C, '\n': 0x28
    }
    
    for char in text.lower():
        if char in key_map:
            # Press key
            modifier = 0x02 if char.isupper() else 0x00  # Shift for uppercase
            report = [0x01, modifier, 0x00, key_map[char], 0x00, 0x00, 0x00, 0x00, 0x00]
            await client.write_gatt_char(keyboard_char, bytearray(report))
            await asyncio.sleep(delay_ms / 1000.0)
            
            # Release key
            report = [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
            await client.write_gatt_char(keyboard_char, bytearray(report))
            await asyncio.sleep(delay_ms / 1000.0)

# Usage
await type_string(client, "Hello World!\n")
```

### Mouse Gesture Simulation

```python
async def draw_circle(client, radius=50, steps=36):
    """Draw a circle with mouse movement"""
    import math
    
    for i in range(steps):
        angle = 2 * math.pi * i / steps
        x = int(radius * math.cos(angle) / steps)
        y = int(radius * math.sin(angle) / steps)
        
        # Clamp to valid range
        x = max(-127, min(127, x))
        y = max(-127, min(127, y))
        
        report = [0x02, 0x00, x & 0xFF, y & 0xFF, 0x00]
        await client.write_gatt_char(mouse_char, bytearray(report))
        await asyncio.sleep(0.05)  # 50ms between moves
```

### Media Control Sequences

```python
async def media_control_sequence(client):
    """Demonstrate media control functionality"""
    controls = [
        ([0x03, 0xCD, 0x00], "Play/Pause"),
        ([0x03, 0x00, 0x00], "Release"),
        ([0x03, 0xE9, 0x00], "Volume Up"),
        ([0x03, 0x00, 0x00], "Release"),
        ([0x03, 0xB5, 0x00], "Next Track"),
        ([0x03, 0x00, 0x00], "Release")
    ]
    
    for report, description in controls:
        print(f"Sending: {description}")
        await client.write_gatt_char(consumer_char, bytearray(report))
        await asyncio.sleep(0.5)  # 500ms between controls
```

## Timing and Performance Considerations

### Report Rate Limits

- **Maximum Rate**: ~1000 reports/second per characteristic
- **Recommended Rate**: 100-200 reports/second for smooth operation
- **Minimum Delay**: 1ms between reports to avoid overwhelming the USB stack

### BLE Connection Parameters

Optimal BLE connection parameters for HID performance:

```text
Connection Interval: 7.5ms - 15ms
Slave Latency: 0-4
Supervision Timeout: 300-1000ms
```

### Battery Considerations

For battery-powered clients:

- Use connection intervals > 15ms for power saving
- Implement proper sleep modes between report transmissions
- Monitor connection supervision timeout to avoid disconnections

### USB Forwarding Latency

- **Typical Latency**: 1-5ms from BLE reception to USB transmission
- **Factors**: USB polling rate, system load, BLE connection interval
- **Optimization**: Use lowest possible BLE connection interval for gaming/real-time applications

## Firmware Configuration Options

### Kconfig Options

Key configuration options in `prj.conf`:

```ini
# BLE Configuration
CONFIG_BT_DEVICE_NAME="ninjaUSB"
CONFIG_BT_MAX_CONN=1
CONFIG_BT_L2CAP_TX_MTU=65

# HID Configuration  
CONFIG_USB_HID_REPORTS=1
CONFIG_USB_HID_BOOT_PROTOCOL=1

# Logging (disable in production)
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
```

### Memory Usage

- **RAM Usage**: ~8KB for BLE stack + HID buffers
- **Flash Usage**: ~120KB for full firmware
- **Available on nRF52840**: 256KB RAM, 1MB Flash

## Integration with ninjaUSB-util

### Expected Report Format

The ninjaUSB-util expects reports in the exact format documented here:

- Report ID must be first byte
- Data length must match specification
- All multi-byte values in little-endian format

### Error Codes

Common GATT error responses from the device:

```c
BT_ATT_ERR_SUCCESS (0x00)           // Report processed successfully
BT_ATT_ERR_INVALID_OFFSET (0x07)    // Write at non-zero offset
BT_ATT_ERR_INVALID_ATTRIBUTE_LEN (0x0D)  // Wrong report length
```

### Compatibility Testing

Test your client implementation with:

1. **Basic Reports**: Single key presses, mouse clicks
2. **Complex Reports**: Multiple simultaneous keys, mouse+scroll
3. **Edge Cases**: Maximum values, rapid succession
4. **Error Cases**: Invalid lengths, unknown Report IDs
