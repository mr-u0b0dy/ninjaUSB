#!/usr/bin/env python3
"""
Comprehensive test program for ninjaUSB BLE HID service

This script provides comprehensive testing of the universal BLE-to-USB HID bridge
by sending various HID reports (keyboard, mouse, consumer control) via Bluetooth LE.

Features:
- Device discovery and connection
- Comprehensive HID report testing
- Fallback mechanisms for characteristic discovery
- Detailed logging and error reporting
- Support for selective testing

Requirements:
    pip install bleak

Usage:
    python test_ninja_hid.py [options]
    
Options:
    -h, --help          Show this help message
    -d, --device NAME   Device name to connect to (default: "ninjaUSB")
    -t, --timeout SEC   Scan timeout in seconds (default: 10)
    -v, --verbose       Enable verbose output
    --scan-only         Only scan for devices, don't connect or test
    --test TYPE         Run specific test type: keyboard, mouse, consumer, or all (default: all)
    --fallback          Use fallback mode (send to all characteristics)
    --diagnose          Run diagnostic mode to analyze connection and authorization issues
"""

import argparse
import asyncio
import sys
import time
import subprocess
from typing import Optional, List, Dict, Any
from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

# HID Service and characteristic UUIDs
HID_SERVICE_UUID = "00001812-0000-1000-8000-00805f9b34fb"
HID_REPORT_UUID = "00002a4d-0000-1000-8000-00805f9b34fb"
HID_REPORT_REF_UUID = "00002908-0000-1000-8000-00805f9b34fb"

# Report IDs
REPORT_ID_KEYBOARD = 1
REPORT_ID_MOUSE = 2
REPORT_ID_CONSUMER = 3

# HID key codes for common keys
KEY_CODES = {
    'a': 0x04, 'b': 0x05, 'c': 0x06, 'd': 0x07, 'e': 0x08,
    'f': 0x09, 'g': 0x0A, 'h': 0x0B, 'i': 0x0C, 'j': 0x0D,
    'k': 0x0E, 'l': 0x0F, 'm': 0x10, 'n': 0x11, 'o': 0x12,
    'p': 0x13, 'q': 0x14, 'r': 0x15, 's': 0x16, 't': 0x17,
    'u': 0x18, 'v': 0x19, 'w': 0x1A, 'x': 0x1B, 'y': 0x1C,
    'z': 0x1D, '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21,
    '5': 0x22, '6': 0x23, '7': 0x24, '8': 0x25, '9': 0x26,
    '0': 0x27, ' ': 0x2C, '\n': 0x28, '\t': 0x2B
}

# Consumer control codes
CONSUMER_CODES = {
    'volume_up': 0x00E9,
    'volume_down': 0x00EA,
    'mute': 0x00E2,
    'play_pause': 0x00CD,
    'next_track': 0x00B5,
    'prev_track': 0x00B6,
    'stop': 0x00B7
}

class NinjaHIDTester:
    """Comprehensive BLE HID tester for ninjaUSB device."""
    
    def __init__(self, device_name: str = "ninjaUSB", scan_timeout: float = 10.0, 
                 verbose: bool = False, fallback_mode: bool = False):
        self.device_name = device_name
        self.scan_timeout = scan_timeout
        self.verbose = verbose
        self.fallback_mode = fallback_mode
        self.client: Optional[BleakClient] = None
        self.keyboard_char: Optional[BleakGATTCharacteristic] = None
        self.mouse_char: Optional[BleakGATTCharacteristic] = None
        self.consumer_char: Optional[BleakGATTCharacteristic] = None
        self.all_hid_chars: List[BleakGATTCharacteristic] = []
        self._auto_disconnect = True  # Enable automatic disconnection
    
    async def __aenter__(self):
        """Async context manager entry."""
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit with automatic cleanup."""
        if self._auto_disconnect:
            await self.disconnect()
        return False
        
    def log(self, message: str, verbose_only: bool = False):
        """Log a message, optionally only in verbose mode."""
        if not verbose_only or self.verbose:
            print(message)
        
    async def scan_devices(self) -> List[Dict[str, Any]]:
        """Scan for all available BLE devices."""
        self.log(f"🔍 Scanning for BLE devices (timeout: {self.scan_timeout}s)...")
        
        devices = await BleakScanner.discover(timeout=self.scan_timeout)
        found_devices = []
        
        for device in devices:
            device_info = {
                'name': device.name or 'Unknown',
                'address': device.address,
                'rssi': getattr(device, 'rssi', 'Unknown')
            }
            found_devices.append(device_info)
            
            if device.name and self.device_name.lower() in device.name.lower():
                self.log(f"🎯 Target device found: {device.name} ({device.address}) RSSI: {device_info['rssi']}")
            else:
                self.log(f"   {device.name} ({device.address}) RSSI: {device_info['rssi']}", verbose_only=True)
                
        return found_devices
        
    async def find_device(self) -> Optional[str]:
        """Find the target device by scanning for BLE devices."""
        devices = await self.scan_devices()
        
        for device_info in devices:
            if self.device_name.lower() in device_info['name'].lower():
                return device_info['address']
                
        self.log(f"❌ Device '{self.device_name}' not found")
        self.log(f"📋 Available devices ({len(devices)}):")
        for device_info in devices:
            self.log(f"  - {device_info['name']} ({device_info['address']})")
        return None
    
    async def connect(self, address: str) -> bool:
        """Connect to the BLE device and discover characteristics."""
        try:
            self.log(f"🔗 Connecting to {address}...")
            # Use a more conservative connection approach
            self.client = BleakClient(address, 
                                    timeout=20.0,  # Longer timeout
                                    use_cached_services=False)  # Force fresh service discovery
            await self.client.connect()
            self.log(f"✅ Connected to {address}")
            
            # Check if device is paired and attempt auto-pairing if needed
            try:
                pairing_needed = await self._check_pairing_status()
                if pairing_needed:
                    self.log("🔐 Attempting automatic pairing...")
                    if not await self._auto_pair_device(address):
                        self.log("⚠️  Auto-pairing failed, continuing with connection...")
            except Exception as e:
                self.log(f"⚠️  Could not check pairing status: {e}", verbose_only=True)
            
            # Discover services and characteristics with retry logic
            await self.discover_characteristics()
            return True
            
        except Exception as e:
            self.log(f"❌ Failed to connect: {e}")
            if self.verbose:
                import traceback
                traceback.print_exc()
            return False
    
    async def connect_with_auto_pair(self, address: str, max_retries: int = 3) -> bool:
        """Connect with automatic pairing and retry logic."""
        for attempt in range(max_retries):
            try:
                self.log(f"🔗 Connection attempt {attempt + 1}/{max_retries}...")
                
                # Clean up any existing connection
                if self.client and self.client.is_connected:
                    await self.disconnect()
                    await asyncio.sleep(1.0)
                
                # Attempt connection
                if await self.connect(address):
                    self.log("✅ Connection and setup successful")
                    return True
                else:
                    self.log(f"❌ Connection attempt {attempt + 1} failed")
                    
            except Exception as e:
                self.log(f"❌ Connection attempt {attempt + 1} error: {e}")
                if self.verbose:
                    import traceback
                    traceback.print_exc()
            
            # Wait before retry (except on last attempt)
            if attempt < max_retries - 1:
                wait_time = 2.0 * (attempt + 1)  # Progressive backoff
                self.log(f"⏳ Waiting {wait_time}s before retry...")
                await asyncio.sleep(wait_time)
        
        self.log(f"❌ All {max_retries} connection attempts failed")
        return False
    
    async def _check_pairing_status(self) -> bool:
        """Check if the device is paired and attempt pairing if needed."""
        if not self.client:
            return False
            
        try:
            # Try to read device information to test authorization
            services = self.client.services
            device_info_service = None
            
            for service in services:
                if service.uuid == "0000180a-0000-1000-8000-00805f9b34fb":  # Device Information Service
                    device_info_service = service
                    break
            
            if device_info_service:
                for char in device_info_service.characteristics:
                    if char.uuid == "00002a29-0000-1000-8000-00805f9b34fb":  # Manufacturer Name
                        try:
                            data = await self.client.read_gatt_char(char)
                            manufacturer = data.decode('utf-8', errors='ignore')
                            self.log(f"🏭 Device manufacturer: {manufacturer}", verbose_only=True)
                            return False  # No pairing needed
                        except Exception as e:
                            if "NotAuthorized" in str(e) or "Authorization" in str(e):
                                self.log("🔐 Device requires pairing for characteristic access")
                                return True  # Pairing needed
                            return False
            return False  # No device info service, assume no pairing needed
        except Exception as e:
            self.log(f"⚠️  Pairing check failed: {e}", verbose_only=True)
            return False
    
    async def _auto_pair_device(self, device_address: str) -> bool:
        """Attempt automatic pairing with the device."""
        self.log("🔐 Attempting automatic pairing...")
        
        # First try Bleak's built-in pairing if available
        try:
            if self.client and hasattr(self.client, 'pair') and callable(getattr(self.client, 'pair')):
                await self.client.pair()
                self.log("✅ Bleak pairing successful")
                return True
        except Exception as e:
            self.log(f"⚠️  Bleak pairing failed: {e}", verbose_only=True)
        
        # Try system-level pairing using bluetoothctl
        try:
            success = await self._pair_via_bluetoothctl(device_address)
            if success:
                self.log("✅ System-level pairing successful")
                return True
        except Exception as e:
            self.log(f"⚠️  System pairing failed: {e}", verbose_only=True)
        
        self.log("❌ Automatic pairing failed - manual pairing may be required")
        return False
    
    async def _pair_via_bluetoothctl(self, device_address: str) -> bool:
        """Attempt pairing via bluetoothctl with retry logic."""
        try:
            # Check if bluetoothctl is available
            success, _, _ = self._run_bluetoothctl_command("version")
            if not success:
                return False
            
            # First, try to remove any existing pairing
            self.log("🔄 Removing any existing pairing...", verbose_only=True)
            self._run_bluetoothctl_command(f"remove {device_address}")
            await asyncio.sleep(1.0)
            
            # Start scanning to ensure device is discoverable
            self.log("🔍 Starting device scan...", verbose_only=True)
            self._run_bluetoothctl_command("scan on")
            await asyncio.sleep(2.0)  # Give time for scan to find device
            
            # Try to pair
            self.log(f"🔗 Pairing with {device_address}...", verbose_only=True)
            success, stdout, stderr = self._run_bluetoothctl_command(f"pair {device_address}")
            
            if success:
                # Also trust the device
                self.log("🤝 Trusting device...", verbose_only=True)
                self._run_bluetoothctl_command(f"trust {device_address}")
                
                # Stop scanning
                self._run_bluetoothctl_command("scan off")
                return True
            else:
                self.log(f"❌ Pairing failed: {stderr}", verbose_only=True)
                
            # Stop scanning
            self._run_bluetoothctl_command("scan off")
            return False
        except Exception as e:
            self.log(f"❌ Pairing exception: {e}", verbose_only=True)
            return False
    
    async def discover_characteristics(self):
        """Discover and categorize HID report characteristics with extra stability measures."""
        if not self.client:
            return
            
        self.log("🔍 Discovering HID characteristics...")
        
        try:
            # Add longer delay to let the connection stabilize
            self.log("⏳ Stabilizing connection...", verbose_only=True)
            await asyncio.sleep(2.0)
            
            # Check if still connected
            if not self.client.is_connected:
                raise Exception("Connection lost during service discovery")
            
            # Get all services with retry logic
            services = None
            for retry in range(3):
                try:
                    self.log(f"  Service discovery attempt {retry + 1}/3...", verbose_only=True)
                    services = self.client.services
                    if services:
                        break
                    await asyncio.sleep(1.0)
                except Exception as e:
                    self.log(f"  Service discovery attempt {retry + 1} failed: {e}", verbose_only=True)
                    if retry < 2:
                        await asyncio.sleep(2.0)
                    else:
                        raise
                        
            if not services:
                raise Exception("Failed to discover services after 3 attempts")
                
            hid_service = None
            service_list = list(services)
            self.log(f"Found {len(service_list)} services", verbose_only=True)
            
            for service in service_list:
                self.log(f"  Service: {service.uuid}", verbose_only=True)
                if service.uuid == HID_SERVICE_UUID:
                    hid_service = service
                    break
                    
            if not hid_service:
                available_services = [str(s.uuid) for s in service_list]
                raise Exception(f"HID service ({HID_SERVICE_UUID}) not found. Available: {available_services}")
                
            self.log(f"✅ Found HID service with {len(hid_service.characteristics)} characteristics")
            
            # Find all HID report characteristics
            for char in hid_service.characteristics:
                self.log(f"  Characteristic: {char.uuid}, Properties: {char.properties}", verbose_only=True)
                if char.uuid == HID_REPORT_UUID:
                    self.all_hid_chars.append(char)
                    
            self.log(f"📝 Found {len(self.all_hid_chars)} HID report characteristics")
            
            if len(self.all_hid_chars) == 0:
                all_chars = [str(c.uuid) for c in hid_service.characteristics]
                raise Exception(f"No HID report characteristics found! Available: {all_chars}")
            
            # Always use fallback mode to avoid connection issues with descriptor reading
            self.log("✅ Using fallback mode for maximum compatibility")
            self.fallback_mode = True
                
        except Exception as e:
            self.log(f"❌ Error during service discovery: {e}")
            if self.verbose:
                import traceback
                traceback.print_exc()
            raise
                
        # Summary of discovered characteristics
        if not self.fallback_mode:
            chars_found = [
                ("Keyboard", self.keyboard_char),
                ("Mouse", self.mouse_char), 
                ("Consumer", self.consumer_char)
            ]
            
            self.log("\n📋 Characteristic Discovery Summary:")
            for name, char in chars_found:
                status = "✅" if char else "❌"
                self.log(f"{status} {name}: {'Found' if char else 'Not found'}")
        else:
            self.log(f"\n📋 Fallback Mode: Using {len(self.all_hid_chars)} HID characteristics")
    
    async def _identify_characteristics_by_descriptors(self):
        """Try to identify characteristics using Report Reference descriptors."""
        self.log("🔍 Attempting to identify characteristics by descriptors...", verbose_only=True)
        
        if not self.client:
            return
        
        for i, char in enumerate(self.all_hid_chars):
            try:
                self.log(f"  Processing characteristic {i+1}/{len(self.all_hid_chars)}: {char.handle}", verbose_only=True)
                
                # Look for report reference descriptor
                report_ref_desc = None
                for desc in char.descriptors:
                    if desc.uuid == HID_REPORT_REF_UUID:
                        report_ref_desc = desc
                        break
                
                if report_ref_desc:
                    # Try to read the report reference
                    ref_data = await self.client.read_gatt_descriptor(report_ref_desc)
                    report_id = ref_data[0]
                    report_type = ref_data[1]  # 0x01 = Input, 0x02 = Output
                    
                    self.log(f"    Report ID: {report_id}, Type: {report_type}", verbose_only=True)
                    
                    # Assign characteristics based on report ID
                    if report_id == REPORT_ID_KEYBOARD and report_type == 0x01:
                        self.keyboard_char = char
                        self.log("  ⌨️  Keyboard input characteristic")
                    elif report_id == REPORT_ID_MOUSE and report_type == 0x01:
                        self.mouse_char = char
                        self.log("  🖱️  Mouse input characteristic")
                    elif report_id == REPORT_ID_CONSUMER and report_type == 0x01:
                        self.consumer_char = char
                        self.log("  🎵 Consumer input characteristic")
                else:
                    self.log(f"    No report reference descriptor found", verbose_only=True)
                        
            except Exception as e:
                self.log(f"    ❌ Error reading descriptor: {e}", verbose_only=True)
    
    async def send_report(self, report_data: bytearray, report_name: str) -> bool:
        """Send a HID report using the appropriate method."""
        if self.fallback_mode:
            return await self._send_report_fallback(report_data, report_name)
        else:
            return await self._send_report_targeted(report_data, report_name)
    
    async def _send_report_targeted(self, report_data: bytearray, report_name: str) -> bool:
        """Send report to specific characteristic based on Report ID."""
        if not self.client:
            self.log(f"❌ No client connection available for {report_name}")
            return False
            
        report_id = report_data[0]
        
        target_char = None
        if report_id == REPORT_ID_KEYBOARD:
            target_char = self.keyboard_char
        elif report_id == REPORT_ID_MOUSE:
            target_char = self.mouse_char
        elif report_id == REPORT_ID_CONSUMER:
            target_char = self.consumer_char
        
        if not target_char:
            self.log(f"❌ No characteristic available for {report_name} (Report ID: {report_id})")
            return False
        
        try:
            await self.client.write_gatt_char(target_char, report_data, response=False)
            self.log(f"✅ Sent {report_name}: {report_data.hex()}", verbose_only=True)
            return True
        except Exception as e:
            self.log(f"❌ Failed to send {report_name}: {e}")
            return False
    
    async def _send_report_fallback(self, report_data: bytearray, report_name: str) -> bool:
        """Send report to all HID characteristics (fallback mode)."""
        if not self.client:
            self.log(f"❌ No client connection available for {report_name}")
            return False
            
        self.log(f"📤 Sending {report_name}: {report_data.hex()}", verbose_only=True)
        
        success_count = 0
        auth_errors = 0
        connection_errors = 0
        
        for i, char in enumerate(self.all_hid_chars):
            try:
                await self.client.write_gatt_char(char, report_data, response=False)
                self.log(f"  ✅ Sent to characteristic {i+1} (handle {char.handle})", verbose_only=True)
                success_count += 1
            except Exception as e:
                error_str = str(e)
                if "NotAuthorized" in error_str:
                    auth_errors += 1
                    self.log(f"  🔐 Authorization required for characteristic {i+1}: {e}", verbose_only=True)
                elif "Service Discovery" in error_str:
                    connection_errors += 1
                    self.log(f"  📡 Connection lost for characteristic {i+1}: {e}", verbose_only=True)
                else:
                    self.log(f"  ❌ Failed to send to characteristic {i+1}: {e}", verbose_only=True)
                
        success = success_count > 0
        status = "✅" if success else "❌"
        
        # Provide specific diagnostics
        if auth_errors > 0 and success_count == 0:
            self.log(f"🔐 {report_name}: Authorization required - device needs pairing or security configuration")
        elif connection_errors > 0:
            self.log(f"📡 {report_name}: Connection unstable - device disconnected during operation")
        else:
            self.log(f"{status} {report_name}: {success_count}/{len(self.all_hid_chars)} characteristics")
            
        return success
    
    async def test_keyboard(self):
        """Test keyboard functionality."""
        self.log("\n⌨️  === Testing Keyboard ===")
        
        # Test individual keys
        self.log("Testing individual key presses...")
        for key in ['a', 'b', 'c']:
            key_code = KEY_CODES[key]
            self.log(f"  Pressing '{key}'")
            
            # Key press
            report = bytearray([REPORT_ID_KEYBOARD, 0x00, 0x00, key_code, 0x00, 0x00, 0x00, 0x00, 0x00])
            await self.send_report(report, f"Key press '{key}'")
            await asyncio.sleep(0.1)
            
            # Key release
            report = bytearray([REPORT_ID_KEYBOARD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
            await self.send_report(report, f"Key release '{key}'")
            await asyncio.sleep(0.1)
        
        # Test modifier keys
        self.log("Testing Ctrl+C...")
        report = bytearray([REPORT_ID_KEYBOARD, 0x01, 0x00, KEY_CODES['c'], 0x00, 0x00, 0x00, 0x00, 0x00])
        await self.send_report(report, "Ctrl+C press")
        await asyncio.sleep(0.1)
        
        report = bytearray([REPORT_ID_KEYBOARD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        await self.send_report(report, "Ctrl+C release")
        await asyncio.sleep(0.5)
        
        # Test string typing
        await self.type_string("Hello")
    
    async def type_string(self, text: str, delay_ms: int = 50):
        """Type a string with proper key press/release timing."""
        self.log(f"⌨️  Typing: '{text}'")
        
        for char in text.lower():
            if char in KEY_CODES:
                key_code = KEY_CODES[char]
                modifier = 0x02 if char != char.lower() else 0x00  # Shift for uppercase
                
                # Press key
                report = bytearray([REPORT_ID_KEYBOARD, modifier, 0x00, key_code, 0x00, 0x00, 0x00, 0x00, 0x00])
                await self.send_report(report, f"Type '{char}' press")
                await asyncio.sleep(delay_ms / 1000.0)
                
                # Release key
                report = bytearray([REPORT_ID_KEYBOARD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
                await self.send_report(report, f"Type '{char}' release")
                await asyncio.sleep(delay_ms / 1000.0)
            else:
                self.log(f"⚠️  Warning: No key code for character '{char}'")
    
    async def test_mouse(self):
        """Test mouse functionality."""
        self.log("\n🖱️  === Testing Mouse ===")
        
        # Test mouse buttons
        self.log("Testing left click...")
        report = bytearray([REPORT_ID_MOUSE, 0x01, 0x00, 0x00, 0x00])
        await self.send_report(report, "Mouse left click")
        await asyncio.sleep(0.1)
        
        report = bytearray([REPORT_ID_MOUSE, 0x00, 0x00, 0x00, 0x00])
        await self.send_report(report, "Mouse button release")
        await asyncio.sleep(0.2)
        
        self.log("Testing right click...")
        report = bytearray([REPORT_ID_MOUSE, 0x02, 0x00, 0x00, 0x00])
        await self.send_report(report, "Mouse right click")
        await asyncio.sleep(0.1)
        
        report = bytearray([REPORT_ID_MOUSE, 0x00, 0x00, 0x00, 0x00])
        await self.send_report(report, "Mouse button release")
        await asyncio.sleep(0.2)
        
        # Test mouse movement
        self.log("Testing mouse movement...")
        movements = [(10, 0), (0, 10), (-10, 0), (0, -10)]
        for x, y in movements:
            x_byte = x & 0xFF if x >= 0 else (256 + x) & 0xFF
            y_byte = y & 0xFF if y >= 0 else (256 + y) & 0xFF
            report = bytearray([REPORT_ID_MOUSE, 0x00, x_byte, y_byte, 0x00])
            await self.send_report(report, f"Mouse move ({x}, {y})")
            await asyncio.sleep(0.1)
        
        # Test scroll wheel
        self.log("Testing scroll wheel...")
        report = bytearray([REPORT_ID_MOUSE, 0x00, 0x00, 0x00, 0x03])
        await self.send_report(report, "Scroll up")
        await asyncio.sleep(0.2)
        
        report = bytearray([REPORT_ID_MOUSE, 0x00, 0x00, 0x00, 0xFD])  # -3 as unsigned
        await self.send_report(report, "Scroll down")
        await asyncio.sleep(0.2)
    
    async def test_consumer(self):
        """Test consumer control functionality."""
        self.log("\n🎵 === Testing Consumer Control ===")
        
        controls = [
            ('volume_up', "Volume Up"),
            ('volume_down', "Volume Down"),
            ('mute', "Mute"),
            ('play_pause', "Play/Pause"),
            ('next_track', "Next Track"),
            ('prev_track', "Previous Track")
        ]
        
        for control, description in controls:
            self.log(f"Testing {description}...")
            usage_code = CONSUMER_CODES[control]
            
            # Press
            report = bytearray([REPORT_ID_CONSUMER, usage_code & 0xFF, (usage_code >> 8) & 0xFF])
            await self.send_report(report, description)
            await asyncio.sleep(0.1)
            
            # Release
            report = bytearray([REPORT_ID_CONSUMER, 0x00, 0x00])
            await self.send_report(report, f"{description} release")
            await asyncio.sleep(0.3)
    
    def _run_bluetoothctl_command(self, command: str):
        """Run a bluetoothctl command and return the result."""
        try:
            result = subprocess.run(
                ['bluetoothctl'] + command.split(),
                capture_output=True,
                text=True,
                timeout=10
            )
            return result.returncode == 0, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return False, "", "Command timed out"
        except FileNotFoundError:
            return False, "", "bluetoothctl not found"
    
    async def run_pairing_helper(self, device_address: Optional[str] = None) -> bool:
        """Integrated pairing helper functionality."""
        if device_address is None:
            device_address = "DA:BB:6B:E1:DA:D4"  # Default ninjaUSB address
        
        self.log("🔐 ninjaUSB BLE Pairing Helper")
        self.log(f"Device: {device_address}")
        self.log("=" * 40)
        
        # Check if bluetoothctl is available
        success, stdout, stderr = self._run_bluetoothctl_command("version")
        if not success:
            self.log("❌ bluetoothctl not available")
            self.log("💡 Install bluez-utils or similar package")
            self.log("\n🔧 Manual pairing steps:")
            self.log("1. Open system Bluetooth settings")
            self.log("2. Scan for devices")
            self.log("3. Pair with 'NinjaUSB' device")
            self.log("4. Trust the device")
            return False
        
        self.log("✅ bluetoothctl available")
        
        # Scan for the device
        self.log("🔍 Scanning for device...")
        success, stdout, stderr = self._run_bluetoothctl_command("scan on")
        if success:
            self.log("✅ Scanning started")
        else:
            self.log("⚠️  Could not start scanning")
        
        # Try to pair
        self.log(f"🔗 Attempting to pair with {device_address}...")
        success, stdout, stderr = self._run_bluetoothctl_command(f"pair {device_address}")
        
        if success:
            self.log("✅ Pairing successful!")
        else:
            self.log(f"❌ Pairing failed: {stderr}")
            self.log("\n💡 Manual steps:")
            self.log("1. Run: bluetoothctl")
            self.log("2. scan on")
            self.log("3. Wait for device to appear")
            self.log(f"4. pair {device_address}")
            self.log(f"5. trust {device_address}")
            self.log("6. exit")
            return False
        
        # Trust the device
        self.log("🤝 Setting device as trusted...")
        success, stdout, stderr = self._run_bluetoothctl_command(f"trust {device_address}")
        
        if success:
            self.log("✅ Device trusted")
        else:
            self.log(f"⚠️  Could not trust device: {stderr}")
        
        self.log("\n🎉 Pairing process completed!")
        self.log("📝 You can now run the HID tests:")
        self.log("   python test/test_ninja_hid.py --test keyboard")
        
        return True
    
    async def run_diagnostics(self) -> bool:
        """Run comprehensive diagnostics to identify connection and authorization issues."""
        self.log("\n🔬 === Running BLE HID Diagnostics ===")
        
        # Find and connect to device with auto-pairing
        address = await self.find_device()
        if not address:
            return False
            
        if not await self.connect_with_auto_pair(address):
            return False
        
        try:
            # Test 1: Check basic connection
            self.log("\n📡 Test 1: Connection Status")
            is_connected = self.client and self.client.is_connected
            self.log(f"   Client exists: {self.client is not None}")
            if self.client:
                self.log(f"   Connection status: {self.client.is_connected}")
            
            if not is_connected:
                self.log("❌ BLE connection not active")
                return False
            else:
                self.log("✅ BLE connection established")
            
            # Test 2: Service discovery
            self.log("\n🔍 Test 2: Service Discovery")
            if not self.client:
                self.log("❌ Client not available")
                return False
                
            services = self.client.services
            hid_service = None
            for service in services:
                if service.uuid == HID_SERVICE_UUID:
                    hid_service = service
                    break
            
            if hid_service:
                self.log(f"✅ HID service found with {len(hid_service.characteristics)} characteristics")
            else:
                self.log("❌ HID service not found")
                return False
            
            # Test 3: Characteristic analysis
            self.log("\n📝 Test 3: Characteristic Analysis")
            report_chars = []
            for char in hid_service.characteristics:
                if char.uuid == HID_REPORT_UUID:
                    report_chars.append(char)
                    self.log(f"   Report Char: Handle {char.handle}, Properties: {char.properties}")
            
            if report_chars:
                self.log(f"✅ Found {len(report_chars)} HID report characteristics")
            else:
                self.log("❌ No HID report characteristics found")
                return False
            
            # Test 4: Authorization test
            self.log("\n🔐 Test 4: Authorization Test")
            test_report = bytearray([REPORT_ID_KEYBOARD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
            
            auth_success = 0
            auth_failed = 0
            other_errors = 0
            
            for i, char in enumerate(report_chars):
                try:
                    if self.client:
                        await self.client.write_gatt_char(char, test_report, response=False)
                        self.log(f"   ✅ Characteristic {i+1}: Write successful")
                        auth_success += 1
                except Exception as e:
                    error_str = str(e)
                    if "NotAuthorized" in error_str or "Authorization" in error_str:
                        self.log(f"   🔐 Characteristic {i+1}: Authorization required")
                        auth_failed += 1
                    else:
                        self.log(f"   ❌ Characteristic {i+1}: {e}")
                        other_errors += 1
            
            # Test 5: Summary and recommendations
            self.log("\n📋 Test 5: Diagnostic Summary")
            
            if auth_success > 0:
                self.log(f"✅ {auth_success} characteristics accept writes (device ready for testing)")
            elif auth_failed > 0:
                self.log(f"🔐 {auth_failed} characteristics require authorization")
                self.log("\n💡 Recommended Solutions:")
                self.log("   1. Pair the device manually:")
                self.log(f"      bluetoothctl pair {address}")
                self.log("   2. Or modify firmware to allow unauthenticated HID writes")
                self.log("   3. Or implement proper BLE security in firmware")
            elif other_errors > 0:
                self.log(f"❌ {other_errors} characteristics have other errors")
                self.log("💡 Check firmware HID characteristic configuration")
            
            return auth_success > 0
            
        except Exception as e:
            self.log(f"❌ Diagnostics failed: {e}")
            if self.verbose:
                import traceback
                traceback.print_exc()
            return False
        finally:
            await self.disconnect()
    
    async def run_tests(self, test_type: str = "all") -> bool:
        """Run HID tests with automatic connection and pairing."""
        # Find and connect to device with auto-pairing
        address = await self.find_device()
        if not address:
            return False
            
        if not await self.connect_with_auto_pair(address):
            return False
        
        try:
            # Check if we have any way to send reports
            if not self.fallback_mode and not any([self.keyboard_char, self.mouse_char, self.consumer_char]):
                self.log("⚠️  No specific characteristics identified, enabling fallback mode")
                self.fallback_mode = True
                
            if self.fallback_mode and len(self.all_hid_chars) == 0:
                self.log("❌ No HID characteristics available for testing")
                return False
            
            # Run tests based on type
            test_mapping = {
                "keyboard": self.test_keyboard,
                "mouse": self.test_mouse,
                "consumer": self.test_consumer,
                "all": self._run_all_tests
            }
            
            if test_type not in test_mapping:
                self.log(f"❌ Unknown test type: {test_type}")
                self.log(f"Available types: {', '.join(test_mapping.keys())}")
                return False
                
            await test_mapping[test_type]()
            
            self.log("\n🎉 === All tests completed! ===")
            return True
            
        except Exception as e:
            self.log(f"❌ Test failed: {e}")
            if self.verbose:
                import traceback
                traceback.print_exc()
            return False
        finally:
            await self.disconnect()
    
    async def _run_all_tests(self):
        """Run all available tests."""
        await self.test_keyboard()
        await self.test_mouse()
        await self.test_consumer()
    
    async def disconnect(self):
        """Safely disconnect from the device."""
        if self.client:
            try:
                if self.client.is_connected:
                    self.log("🔌 Disconnecting from device...")
                    await self.client.disconnect()
                    self.log("✅ Disconnected successfully")
                else:
                    self.log("ℹ️  Device already disconnected")
            except Exception as e:
                self.log(f"⚠️  Error during disconnect: {e}", verbose_only=True)
            finally:
                self.client = None
    
    async def __aenter__(self):
        """Async context manager entry."""
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit - ensures proper cleanup."""
        await self.disconnect()
        return False
    
async def main():
    """Main function."""
    parser = argparse.ArgumentParser(
        description="Comprehensive test program for ninjaUSB BLE HID service",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python test_ninja_hid.py                    # Test default device with all tests
  python test_ninja_hid.py -d "My Device"     # Test specific device name
  python test_ninja_hid.py --scan-only        # Only scan for devices
  python test_ninja_hid.py --test keyboard    # Test only keyboard functionality
  python test_ninja_hid.py -v                 # Enable verbose output
  python test_ninja_hid.py --fallback         # Use fallback mode
  python test_ninja_hid.py --diagnose         # Run diagnostic mode for troubleshooting
  python test_ninja_hid.py --pair             # Run BLE pairing helper
        """
    )
    
    parser.add_argument('-d', '--device', default='ninjaUSB',
                        help='Device name to connect to (default: ninjaUSB)')
    parser.add_argument('-t', '--timeout', type=float, default=10.0,
                        help='Scan timeout in seconds (default: 10)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable verbose output')
    parser.add_argument('--scan-only', action='store_true',
                        help='Only scan for devices, don\'t connect or test')
    parser.add_argument('--test', choices=['keyboard', 'mouse', 'consumer', 'all'],
                        default='all', help='Test type to run (default: all)')
    parser.add_argument('--fallback', action='store_true',
                        help='Use fallback mode (send to all characteristics)')
    parser.add_argument('--diagnose', action='store_true',
                        help='Run diagnostic mode to analyze connection and authorization issues')
    parser.add_argument('--pair', action='store_true',
                        help='Run BLE pairing helper to resolve authorization issues')
    
    args = parser.parse_args()
    
    print("🥷 ninjaUSB BLE HID Comprehensive Tester")
    print(f"Target device: {args.device}")
    if args.fallback:
        print("Mode: Fallback (send to all characteristics)")
    print("Features: Auto-pairing, Auto-disconnect, Connection retry")
    print("=" * 50)
    
    tester = NinjaHIDTester(args.device, args.timeout, args.verbose, args.fallback)
    
    # Use async context manager for automatic cleanup
    async with tester:
        try:
            if args.scan_only:
                # Just scan for devices
                devices = await tester.scan_devices()
                print(f"\n📋 Found {len(devices)} devices:")
                for device in devices:
                    icon = "🎯" if args.device.lower() in device['name'].lower() else "  "
                    print(f"{icon} {device['name']} ({device['address']}) RSSI: {device['rssi']}")
                return
            
            if args.diagnose:
                # Run diagnostic mode
                success = await tester.run_diagnostics()
                sys.exit(0 if success else 1)
            
            if args.pair:
                # Run pairing helper
                print("\n🔐 === BLE Pairing Helper ===")
                success = await tester.run_pairing_helper()
                sys.exit(0 if success else 1)
            
            # Run the tests
            success = await tester.run_tests(args.test)
            sys.exit(0 if success else 1)
            
        except KeyboardInterrupt:
            print("\n⚠️  Test interrupted by user")
            sys.exit(1)
        except Exception as e:
            print(f"❌ Unexpected error: {e}")
            if args.verbose:
                import traceback
                traceback.print_exc()
            sys.exit(1)

if __name__ == "__main__":
    asyncio.run(main())
