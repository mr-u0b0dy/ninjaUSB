# ninjaUSB Testing Guide

This document provides comprehensive information about testing the ninjaUSB BLE HID service using the provided Python test programs.

## Overview

The ninjaUSB firmware implements a universal BLE-to-USB HID bridge that supports multiple HID device types through a composite HID descriptor. This testing suite validates the BLE HID service functionality and provides reference implementations for client applications.

## Test Program

### `test/test_ninja_hid.py` - Comprehensive BLE HID Test Suite

The unified test program provides complete functionality for testing the ninjaUSB BLE HID service.

**Features:**

- ✅ Advanced argument parsing with comprehensive help system
- ✅ Device discovery and scanning with verbose output
- ✅ Automatic characteristic discovery using Report Reference descriptors
- ✅ Fallback mode for robust operation when descriptors fail
- ✅ Support for selective testing (keyboard, mouse, consumer, or all)
- ✅ Detailed logging with emoji indicators and verbose mode
- ✅ Proper error handling and connection management
- ✅ String typing simulation with configurable timing
- ✅ Comprehensive HID report testing for all device types

**Usage:**

```bash
# Show help and all available options
python test/test_ninja_hid.py --help

# Basic testing (all device types)
python test/test_ninja_hid.py

# Scan for devices only
python test/test_ninja_hid.py --scan-only

# Test specific device type
python test/test_ninja_hid.py --test keyboard

# Test with verbose output
python test/test_ninja_hid.py -v

# Test specific device name
python test/test_ninja_hid.py -d "My Device"

# Use fallback mode (send to all characteristics)
python test/test_ninja_hid.py --fallback

# Combine options for comprehensive testing
python test/test_ninja_hid.py --test all --fallback -v
```

## Installation and Setup

### Requirements

- Python 3.7+
- Linux system with Bluetooth LE support
- `bleak` library for Bluetooth LE communication
- ninjaUSB device running the universal HID firmware

### Installation

```bash
# Create virtual environment (recommended)
python -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

### Known Working Environment

- ✅ Python 3.13.3
- ✅ bleak 1.0.1
- ✅ Linux with BlueZ
- ✅ nRF52840 dongle with ninjaUSB firmware

## Test Coverage

### Keyboard Tests (Report ID: 1)

- ✅ Individual key presses (A, B, C)
- ✅ Modifier key combinations (Ctrl+C)
- ✅ String typing simulation ("Hello World!")
- ✅ Proper key press/release timing

### Mouse Tests (Report ID: 2)

- ✅ Left and right button clicks
- ✅ Mouse movement in all directions
- ✅ Scroll wheel up/down
- ✅ Combined button + movement

### Consumer Control Tests (Report ID: 3)

- ✅ Volume controls (up, down, mute)
- ✅ Media controls (play/pause, next/prev track)
- ✅ Proper press/release sequences

## Test Sequence

1. **Device Discovery**: Scans for BLE devices matching the target name
2. **Connection**: Connects to the device and discovers HID service characteristics
3. **Characteristic Discovery**: Identifies keyboard, mouse, and consumer control characteristics
4. **Report Testing**: Sends various HID reports and validates responses
5. **Cleanup**: Properly disconnects and releases resources

## Example Output

```
🥷 ninjaUSB BLE HID Tester
Target device: ninjaUSB
==================================================
🔍 Scanning for BLE devices (timeout: 10.0s)...
🎯 Target device found: NinjaUSB (DA:BB:6B:E1:DA:D4) RSSI: Unknown
🔗 Connecting to DA:BB:6B:E1:DA:D4...
✅ Connected to DA:BB:6B:E1:DA:D4
🔍 Discovering HID characteristics...
✅ Found HID service with 7 characteristics
  ⌨️  Keyboard input characteristic
  🖱️  Mouse input characteristic
  🎵 Consumer input characteristic

📝 === Testing Keyboard ===
Testing individual keys...
  Pressing 'a'
⌨️  Typing: 'Hello World!'

🖱️  === Testing Mouse ===
Testing left click...
Testing mouse movement...

🎵 === Testing Consumer Control ===
Testing Volume Up...
Testing Play/Pause...

🎉 === All tests completed! ===
```

## Report Format Validation

The test programs validate the exact report formats documented in `BLE_HID_REPORTS.md`:

```text
Keyboard: [0x01, modifier, 0x00, key1, key2, key3, key4, key5, key6]
Mouse:    [0x02, buttons, x_move, y_move, wheel]
Consumer: [0x03, usage_low, usage_high]
```

## Test Results Summary

### ✅ Working Components

1. **Device Discovery**: Successfully scans and finds ninjaUSB devices
2. **BLE Connection**: Successfully connects to the device (intermittent)
3. **Service Discovery**: Finds HID service (UUID: 0x1812)
4. **Characteristic Discovery**: Finds 4 HID report characteristics (UUID: 0x2A4D)
5. **Report Construction**: Properly constructs HID reports with correct Report IDs

### ❌ Current Issues

1. **BLE Authorization Errors**: 
   - Error: `[org.bluez.Error.NotAuthorized] Operation Not Authorized`
   - All HID characteristic write attempts fail
   - Device requires pairing/bonding for HID access

2. **Connection Instability**:
   - Connection drops immediately after first write attempt
   - Error: `Service Discovery has not been performed yet`
   - Service discovery becomes unreliable after authorization failures

3. **Report Reference Descriptor Issues**:
   - Descriptor reads fail with various errors
   - Cannot automatically identify keyboard/mouse/consumer characteristics
   - Fallback mode required for any testing

### 🔧 Current Test Status

**Last Test Results (July 6, 2025):**
- ✅ Device scanning: Working correctly
- ✅ Device discovery: Successfully finds "NinjaUSB" device
- ✅ BLE connection: Establishes initial connection
- ✅ HID service discovery: Finds service with 7 characteristics
- ❌ Authorization: All HID report writes fail with `NotAuthorized`
- ❌ Connection stability: Connection drops after first write attempt
- ❌ Report Reference reading: Fails with various errors

### 🔬 New Diagnostic Tools

**Diagnostic Mode** - Comprehensive analysis of connection issues:
```bash
# Run full diagnostics
python test/test_ninja_hid.py --diagnose -v

# Quick connection test
python test/test_ninja_hid.py --diagnose --timeout 5
```

**Pairing Helper** - Automated BLE pairing:
```bash
# Auto-pair with default device
python test/test_ninja_hid.py --pair

# Note: Uses default ninjaUSB device address
# For manual pairing with bluetoothctl, see diagnostic output
```

## Troubleshooting

### Device Not Found

```bash
# Check device is advertising
python test/test_ninja_hid.py --scan-only -v

# Try different device name
python test/test_ninja_hid.py -d "NinjaUSB"
```

**Solutions:**
- Ensure the ninjaUSB device is powered on and advertising
- Check that the device name matches (case-insensitive partial match)
- Try scanning manually with `bluetoothctl` or similar tools

### Connection Issues

```bash
# Check Bluetooth is enabled
sudo systemctl status bluetooth

# Reset Bluetooth adapter
sudo hciconfig hci0 down && sudo hciconfig hci0 up
```

**Solutions:**
- Ensure Bluetooth is enabled on your system
- Check that no other device is connected to the ninjaUSB
- Try moving closer to the device

### Permission Errors (Linux)

**Solutions:**
- Run with `sudo` or add your user to the `bluetooth` group
- Ensure the `hci0` interface is up: `sudo hciconfig hci0 up`

### Characteristic Discovery Failed

**Solutions:**
- Verify the ninjaUSB firmware is running the universal HID bridge
- Check that the HID service (UUID 0x1812) is properly advertised
- Ensure Report Reference descriptors are correctly implemented

### Authorization Errors

The test programs have encountered several BLE-related issues that suggest firmware modifications are needed:

1. **Report Reference Descriptor Issues**: ATT error 0x0e indicates the descriptor may not be properly implemented
2. **Service Discovery Instability**: Connection drops during service discovery suggest timing or security issues
3. **Authorization Requirements**: Previous tests showed pairing/bonding may be required

### Firmware Recommendations

Based on current test results, the firmware should address:

1. **Report Reference Descriptors**: Ensure descriptors return valid data (Report ID and Type)
2. **Connection Stability**: Improve BLE connection handling during service discovery
3. **Security Configuration**: Consider if pairing/bonding is required for HID characteristics
4. **ATT Error Handling**: Debug and fix ATT error 0x0e in descriptor reads

## Integration Examples

### Using the NinjaHIDTester Class

```python
from test.test_ninja_hid import NinjaHIDTester

tester = NinjaHIDTester("ninjaUSB", verbose=True, fallback_mode=True)
await tester.run_tests("keyboard")
```

### Direct Report Sending

```python
# Keyboard 'A' key press
report = bytearray([0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
await client.write_gatt_char(keyboard_char, report, response=False)

# Mouse left click
report = bytearray([0x02, 0x01, 0x00, 0x00, 0x00])
await client.write_gatt_char(mouse_char, report, response=False)

# Volume up
report = bytearray([0x03, 0xE9, 0x00])
await client.write_gatt_char(consumer_char, report, response=False)
```

### Custom Test Implementation

```python
import asyncio
from bleak import BleakClient, BleakScanner

async def custom_test():
    """Example custom test implementation"""
    
    # Find device
    devices = await BleakScanner.discover()
    target = None
    for device in devices:
        if device.name and "ninjaUSB" in device.name:
            target = device.address
            break
    
    if not target:
        print("Device not found")
        return
    
    # Connect and test
    async with BleakClient(target) as client:
        # Find HID service and characteristics
        hid_service = client.services.get_service("00001812-0000-1000-8000-00805f9b34fb")
        if hid_service:
            # Send custom reports
            for char in hid_service.characteristics:
                if char.uuid == "00002a4d-0000-1000-8000-00805f9b34fb":
                    # Send test report
                    await client.write_gatt_char(char, bytearray([0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]))
```

## Integration with ninjaUSB-util

The test programs are designed to be compatible with the ninjaUSB-util ecosystem:

- **Report Format**: Uses the exact same HID report formats
- **Service Discovery**: Compatible with the standard HID service implementation
- **Error Handling**: Provides detailed error information for debugging

## Key Concepts Demonstrated

The test programs serve as reference implementations demonstrating:

- Proper GATT service and characteristic discovery
- Report Reference descriptor reading for characteristic identification
- Correct HID report construction with Report IDs
- Timing considerations for key press/release sequences
- Error handling and connection management
- BLE security and authorization handling

## Next Steps for Firmware

Based on test results, the firmware should address:

1. **Security Configuration**: Enable appropriate BLE security for HID characteristics
2. **Bonding Support**: Implement pairing if required by the HID specification
3. **Characteristic Permissions**: Ensure write permissions are properly configured
4. **Report Reference Descriptors**: Verify descriptor values are correctly set

## Related Documentation

- `BLE_HID_REPORTS.md` - Detailed report format documentation
- `requirements.txt` - Python dependencies
- Main project README - Overall project information

## Contributing

When adding new tests or modifying existing ones:

1. Follow the existing code structure and naming conventions
2. Add appropriate error handling and logging
3. Include both positive and negative test cases
4. Update this documentation accordingly
5. Test with both the comprehensive and simple test programs

The test programs provide a solid foundation for validating the ninjaUSB BLE HID service and can serve as reference implementations for client applications.
