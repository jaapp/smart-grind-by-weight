#!/usr/bin/env python3
"""
Grinder BLE Tool - Unified interface for all BLE operations

This single script handles:
- Firmware uploads (OTA updates)
- Data export from grinder
- Device scanning and connection
- Interactive BLE sessions
- Live debug logging

Usage:
    ./grinder-ble upload firmware.bin          # Upload firmware via BLE OTA
    ./grinder-ble export [--db file.db]        # Export grind data to SQLite
    ./grinder-ble analyse [--db file.db]       # Export data and launch Streamlit report
    ./grinder-ble scan                         # Scan for BLE devices
    ./grinder-ble connect [--interactive]      # Connect and run commands
    ./grinder-ble debug                        # Stream live debug logs
    ./grinder-ble info                         # Get comprehensive device information
"""

import argparse
import asyncio
import sys
import os
import struct
import time
import subprocess
import tempfile
import sqlite3
import json
from typing import List, Dict, Optional, Tuple
from pathlib import Path

# Add tools directory to path for shared modules
sys.path.insert(0, str(Path(__file__).parent))

def ensure_venv_requirements():
    """Ensure all required packages are installed in the project venv"""
    venv_dir = Path(__file__).parent.parent / "venv"
    requirements_file = Path(__file__).parent.parent / "requirements.txt"
    
    if not venv_dir.exists():
        print(f"[ERROR] Virtual environment not found at {venv_dir}")
        print("Run: python3 -m venv tools/venv && source tools/venv/bin/activate && pip install -r tools/requirements.txt")
        return False
    
    pip_cmd = str(venv_dir / "bin" / "pip")
    
    # Check if requirements.txt exists
    if not requirements_file.exists():
        print(f"[WARNING] Requirements file not found at {requirements_file}")
        return True
    
    # Check and install missing requirements
    try:
        print("[INFO] Checking venv dependencies...")
        result = subprocess.run([pip_cmd, "install", "-r", str(requirements_file)], 
                              capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[WARNING] Some dependencies may not be installed: {result.stderr}")
        else:
            print("[OK] All venv dependencies verified")
        return True
    except Exception as e:
        print(f"[ERROR] Failed to check dependencies: {e}")
        return False

# Ensure dependencies before imports
if not ensure_venv_requirements():
    sys.exit(1)

try:
    from bleak import BleakClient, BleakScanner, BleakError
    from bleak.backends.characteristic import BleakGATTCharacteristic
    from bleak.backends.device import BLEDevice
except ImportError:
    print("[ERROR] Required package 'bleak' not found.")
    print("Install with: pip3 install bleak --user")
    sys.exit(1)

# BLE Configuration - must match ESP32 bluetooth/config.h
BLE_OTA_SERVICE_UUID = "12345678-1234-1234-1234-123456789abc"
BLE_OTA_DATA_CHAR_UUID = "87654321-4321-4321-4321-cba987654321"
BLE_OTA_CONTROL_CHAR_UUID = "11111111-2222-3333-4444-555555555555"
BLE_OTA_STATUS_CHAR_UUID = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
BLE_OTA_BUILD_NUMBER_CHAR_UUID = "66666666-7777-8888-9999-000000000000"

BLE_DATA_SERVICE_UUID = "22334455-6677-8899-aabb-ccddeeffffaa"
BLE_DATA_CONTROL_CHAR_UUID = "33445566-7788-99aa-bbcc-ddeeffaabbcc"
BLE_DATA_TRANSFER_CHAR_UUID = "44556677-8899-aabb-ccdd-eeffaabbccdd"
BLE_DATA_STATUS_CHAR_UUID = "55667788-99aa-bbcc-ddee-ffaabbccddee"

# Nordic UART Service (NUS) for Debug Logging
BLE_DEBUG_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
BLE_DEBUG_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
BLE_DEBUG_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

# System Information Service
BLE_SYSINFO_SERVICE_UUID = "77889900-aabb-ccdd-eeff-112233445566"
BLE_SYSINFO_SYSTEM_CHAR_UUID = "88990011-bbcc-ddee-ff11-223344556677"
BLE_SYSINFO_PERFORMANCE_CHAR_UUID = "99001122-ccdd-eeff-1122-334455667788"
BLE_SYSINFO_HARDWARE_CHAR_UUID = "00112233-ddee-ff11-2233-445566778899"
BLE_SYSINFO_SESSIONS_CHAR_UUID = "11223344-eeff-1122-3344-556677889900"

# Commands and status codes
BLE_OTA_CMD_START = 0x01
BLE_OTA_CMD_END = 0x03
BLE_OTA_CMD_ABORT = 0x04

BLE_DATA_CMD_STOP_EXPORT = 0x11
BLE_DATA_CMD_GET_COUNT = 0x12
BLE_DATA_CMD_CLEAR_DATA = 0x13
BLE_DATA_CMD_GET_FILE_LIST = 0x14
BLE_DATA_CMD_REQUEST_FILE = 0x15

BLE_DEBUG_CMD_ENABLE = 0x01
BLE_DEBUG_CMD_DISABLE = 0x02

BLE_OTA_IDLE = 0x00

# Binary log schema definitions (must match firmware)
LOG_SCHEMA_VERSION = 2
SESSION_STRUCT_SIZE = 80
EVENT_STRUCT_SIZE = 44
MEASUREMENT_STRUCT_SIZE = 24
BLE_OTA_READY = 0x01
BLE_OTA_RECEIVING = 0x02
BLE_OTA_SUCCESS = 0x03
BLE_OTA_ERROR = 0x04

BLE_DATA_IDLE = 0x20
BLE_DATA_EXPORTING = 0x21
BLE_DATA_COMPLETE = 0x22
BLE_DATA_ERROR = 0x23

DEVICE_NAME = "GrindByWeight"
CHUNK_SIZE = 512
DATA_CHUNK_SIZE = 500

class GrinderBLETool:
    """Unified BLE tool for all grinder operations."""
    
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.connected = False
        self.current_ota_status = BLE_OTA_IDLE
        self.current_data_status = BLE_DATA_IDLE
        self.status_updated = asyncio.Event()
        self.data_chunks = []
        self.session_count = 0
        self.receiving_data = False
        self.debug_buffer = ""
        self.last_debug_flush = time.time()
        
        self.firmware_cache_dir = Path(__file__).parent.parent.parent / "firmware_cache"
    
    @staticmethod
    def find_firmware_file() -> Optional[str]:
        project_root = Path(__file__).parent.parent.parent
        build_dirs = [
            project_root / ".pio" / "build" / "waveshare-esp32s3-touch-amoled-164",
            project_root / ".pio" / "build" / "native",
        ]
        
        for build_dir in build_dirs:
            firmware_path = build_dir / "firmware.bin"
            if firmware_path.exists():
                self.safe_print(f"[INFO] Auto-detected firmware: {firmware_path}")
                return str(firmware_path)
        
        pio_build_dir = project_root / ".pio" / "build"
        if pio_build_dir.exists():
            for env_dir in pio_build_dir.iterdir():
                if env_dir.is_dir():
                    firmware_path = env_dir / "firmware.bin"
                    if firmware_path.exists():
                        self.safe_print(f"[INFO] Auto-detected firmware: {firmware_path}")
                        return str(firmware_path)
        
        return None
    
    def _update_status(self, message: str):
        # Safe print that handles Unicode encoding issues on Windows
        try:
            sys.stdout.write(f"\r\033[K{message}")
            sys.stdout.flush()
        except UnicodeEncodeError:
            # Fallback for Windows console encoding issues
            safe_message = message.encode('ascii', 'replace').decode('ascii')
            sys.stdout.write(f"\r\033[K{safe_message}")
            sys.stdout.flush()
    
    def safe_print(self, message: str):
        """Print with encoding safety for Windows."""
        try:
            print(message)
        except UnicodeEncodeError:
            safe_message = message.encode('ascii', 'replace').decode('ascii')
            print(safe_message)
    
    # === Device Discovery ===
    async def scan_devices(self) -> List[BLEDevice]:
        self.safe_print("[INFO] Scanning for grinder devices...")
        try:
            devices_with_adv = await BleakScanner.discover(timeout=10, return_adv=True)
            grinder_devices = []
            
            for device, adv_data in devices_with_adv.values():
                if device.name and "grind" in device.name.lower():
                    grinder_devices.append(device)
                self.safe_print(f"   [DEVICE] Found: {device.name} ({device.address}) - RSSI: {adv_data.rssi} dBm")
            
            if not grinder_devices:
                self.safe_print("[ERROR] No grinder devices found.")
            return grinder_devices
        except Exception as e:
            self.safe_print(f"[ERROR] Scan error: {e}")
            return []
    
    async def find_device(self, device_name: str = DEVICE_NAME, timeout: int = 10) -> Optional[str]:
        self.safe_print(f"[INFO] Scanning for {device_name}...")
        try:
            devices_with_adv = await asyncio.wait_for(
                BleakScanner.discover(timeout=timeout, return_adv=True),
                timeout=timeout + 5
            )
            for device, adv_data in devices_with_adv.values():
                if device.name == device_name:
                    self.safe_print(f"[OK] Found {device_name}")
                    return device.address
            self.safe_print(f"[ERROR] {device_name} not found")
            return None
        except asyncio.TimeoutError:
            self.safe_print(f"[ERROR] Scan timeout")
            return None
    
    # === Connection Management ===
    async def connect_to_device(self, device_name: str = DEVICE_NAME) -> bool:
        address = await self.find_device(device_name)
        
        if not address:
            return False
        
        try:
            self.client = BleakClient(address)
            await asyncio.wait_for(self.client.connect(), timeout=10)
            
            if not self.client.is_connected:
                self.safe_print("[ERROR] Connection failed")
                return False
            
            self.safe_print(f"[OK] Connected")
            
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self.on_ota_status)
            await self.client.start_notify(BLE_DATA_TRANSFER_CHAR_UUID, self.on_data_received)
            await self.client.start_notify(BLE_DATA_STATUS_CHAR_UUID, self.on_data_status)
            await self.client.start_notify(BLE_DEBUG_TX_CHAR_UUID, self.on_debug_message)
            
            self.connected = True
            await asyncio.sleep(0.5)
            return True
        except Exception as e:
            self.safe_print(f"[ERROR] Connection error: {e}")
            return False
    
    async def disconnect(self):
        if self.client and self.connected:
            try:
                await self.client.disconnect()
                self.connected = False
                self.safe_print("[INFO] Disconnected")
            except Exception as e:
                self.safe_print(f"[WARNING] Disconnect error: {e}")
    
    # === Notification Handlers ===
    async def on_ota_status(self, _: BleakGATTCharacteristic, data: bytearray):
        if len(data) > 0:
            self.current_ota_status = data[0]
            self.status_updated.set()
    
    def on_data_received(self, _: BleakGATTCharacteristic, data: bytearray):
        if self.receiving_data:
            self.data_chunks.append(bytes(data))
    
    def on_debug_message(self, _: BleakGATTCharacteristic, data: bytearray):
        """Handles incoming debug messages with improved buffering."""
        try:
            message = data.decode('utf-8', errors='replace')
            self.debug_buffer += message
            
            current_time = time.time()
            # Flush buffer if it gets large or if it's been a while since last flush
            if len(self.debug_buffer) > 1024 or (current_time - self.last_debug_flush) > 0.1:
                sys.stdout.write(self.debug_buffer)
                sys.stdout.flush()
                self.debug_buffer = ""
                self.last_debug_flush = current_time
                
        except Exception as e:
            print(f"\n[Debug handler error: {e}]", file=sys.stderr)
    
    async def on_data_status(self, _: BleakGATTCharacteristic, data: bytearray):
        if not data: return
        status = data[0]
        self.current_data_status = status
        
        if status == BLE_DATA_EXPORTING:
            # Only show ESP32 progress if it's meaningful (> 0%)
            # Otherwise let client-side progress tracking handle it
            progress = data[1] if len(data) > 1 else 0
            if progress > 0:
                self._update_status(f"[EXPORT] exporting data... ({progress}%)")
        elif status == BLE_DATA_COMPLETE:
            self.receiving_data = False
            self._update_status("[EXPORT] exporting data... (100%)")
            self.safe_print("\n[OK] Data export complete.")
        elif status == BLE_DATA_ERROR:
            self.receiving_data = False
            self.safe_print("\n[ERROR] Grinder reported an error during export.")
        elif len(data) == 2:
            self.session_count = data[0] | (data[1] << 8)
            self.safe_print(f"[INFO] Grinder has {self.session_count} sessions stored.")
    
    async def wait_for_ota_status(self, expected_status: int, timeout: int = 30) -> bool:
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.current_ota_status == expected_status:
                return True
        
            self.status_updated.clear()
            try:
                await asyncio.wait_for(self.status_updated.wait(), timeout=1)
            except asyncio.TimeoutError:
                pass
        return False
    
    # === OTA Upload Functions (Unchanged) ===
    async def get_device_build_number(self) -> Optional[str]:
        try:
            build_data = await self.client.read_gatt_char(BLE_OTA_BUILD_NUMBER_CHAR_UUID)
            build_number = build_data.decode('utf-8').strip()
            return build_number if build_number and build_number != "no_build_number" else None
        except Exception:
            return None

    def find_cached_firmware(self, build_number: str) -> Optional[Path]:
        firmware_file = self.firmware_cache_dir / f"build_{int(build_number):03d}.bin"
        return firmware_file if firmware_file.exists() else None

    def _get_firmware_build_number(self, firmware_path: str) -> Optional[str]:
        """Extract build number from git_info.h in project directory."""
        try:
            # Firmware path: .pio/build/waveshare-esp32s3-touch-amoled-164/firmware.bin
            # Project dir should be 3 levels up from firmware.bin
            firmware_file = Path(firmware_path)
            if ".pio" in firmware_file.parts:
                # Find project root by going up until we find .pio, then up one more level
                current = firmware_file.parent
                while current.name != ".pio" and current.parent != current:
                    current = current.parent
                if current.name == ".pio":
                    project_dir = current.parent
                    git_info_path = project_dir / "src" / "config" / "git_info.h"
                    if git_info_path.exists():
                        with open(git_info_path, 'r') as f:
                            content = f.read()
                            import re
                            match = re.search(r'#define BUILD_NUMBER (\d+)', content)
                            if match:
                                return match.group(1)
        except Exception:
            pass
        return None

    def generate_delta_patch(self, old_firmware_path: Path, new_firmware_data: bytes) -> Optional[bytes]:
        try:
            with tempfile.NamedTemporaryFile(delete=False) as new_file:
                new_file.write(new_firmware_data)
                new_firmware_path = new_file.name
            
            
            patch_file = tempfile.NamedTemporaryFile(delete=False)
            patch_path = patch_file.name
            patch_file.close()
            
            # Use detools from the project venv (ensured by ensure_venv_requirements)
            venv_dir = Path(__file__).parent.parent / "venv"
            detools_cmd = str(venv_dir / "bin" / "detools")

            cmd = [detools_cmd, 'create_patch', '-c', 'heatshrink', str(old_firmware_path), new_firmware_path, patch_path]
            result = subprocess.run(cmd, capture_output=True, text=True)

            if result.returncode != 0:
                print(f"[ERROR] detools patch creation failed: {result.stderr}")
                print("[INFO] If detools is missing, the dependency check should have installed it")
                return None
            with open(patch_path, 'rb') as f: patch_data = f.read()
            
            os.unlink(new_firmware_path)
            os.unlink(patch_path)
            return patch_data
        except FileNotFoundError:
            return None
        except Exception:
            return None

    async def upload_firmware(self, firmware_path: str, force_full: bool = False) -> bool:
        firmware_file = Path(firmware_path)
        if not firmware_file.exists(): return False
        
        with open(firmware_file, 'rb') as f: firmware_data = f.read()
        original_size = len(firmware_data)
        self.safe_print(f"[INFO] Firmware: {firmware_file.name} ({original_size//1024}KB)")
        
        # Get new firmware build number from git info
        new_build = self._get_firmware_build_number(firmware_path)
        
        use_delta, patch_data, device_build, full_reason = False, None, None, ""
        
        if not force_full:
            device_build = await self.get_device_build_number()
            if device_build:
                self.safe_print(f"[INFO] Current device build: #{device_build}")
                if new_build:
                    self.safe_print(f"[INFO] Upgrading to build: #{new_build}")
                cached_firmware = self.find_cached_firmware(device_build)
                if cached_firmware:
                    patch_data = self.generate_delta_patch(cached_firmware, firmware_data)
                    if patch_data and len(patch_data) < original_size * 0.8:
                        use_delta = True
                    else: full_reason = "delta not beneficial"
                else: full_reason = "no cached firmware"
            else: 
                full_reason = "no device build info"
                if new_build:
                    self.safe_print(f"[INFO] Installing build: #{new_build}")
        else: 
            full_reason = "forced full update"
            if new_build:
                self.safe_print(f"[INFO] Installing build: #{new_build}")
        
        if not use_delta:
            with tempfile.NamedTemporaryFile() as empty_file:
                patch_data = self.generate_delta_patch(Path(empty_file.name), firmware_data)
            if not patch_data: return False
        
        self.update_method = "delta" if use_delta else "full"
        self.full_reason = full_reason if not use_delta else None
        self.device_build = device_build
        self.firmware_size = original_size
        self.patch_size = len(patch_data)
        
        return await self.send_ota_update(patch_data, new_build)

    async def send_ota_update(self, patch_data: bytes, expected_build: str = None) -> bool:
        patch_size = len(patch_data)
        if self.update_method == "delta":
            reduction = 100.0 * (1.0 - patch_size / self.firmware_size)
            self.safe_print(f"[INFO] Delta update: {patch_size//1024}KB ({reduction:.0f}% smaller)")
        else:
            self.safe_print(f"[INFO] Full update: {patch_size//1024}KB ({self.full_reason})")
        
        # Protocol: [CMD][patch_size:4][is_full_update:1][build_number_length:1][build_number:N]
        start_data = struct.pack('<I', patch_size)
        
        # Add full update flag (1 byte: 1 for full update, 0 for delta)
        is_full_update = 1 if self.update_method == "full" else 0
        start_data += struct.pack('<B', is_full_update)
        
        if expected_build:
            # Include expected build number length and string
            build_bytes = expected_build.encode('utf-8')
            start_data += struct.pack('<B', len(build_bytes)) + build_bytes
            self.safe_print(f"[INFO] Sending expected build number: #{expected_build}")
        else:
            # No build number
            start_data += struct.pack('<B', 0)
            
        self.safe_print(f"[INFO] Sending {'full' if is_full_update else 'delta'} update flag")
        await self.client.write_gatt_char(BLE_OTA_CONTROL_CHAR_UUID, bytes([BLE_OTA_CMD_START]) + start_data)
        
        if not await self.wait_for_ota_status(BLE_OTA_RECEIVING, timeout=15): return False
        
        start_time = time.time()
        try:
            for i in range(0, len(patch_data), CHUNK_SIZE):
                chunk = patch_data[i:i + CHUNK_SIZE]
                await self.client.write_gatt_char(BLE_OTA_DATA_CHAR_UUID, chunk)
                progress = int(((i + len(chunk)) / patch_size) * 100)
                if progress % 5 == 0:
                    self._update_status(f"[UPLOAD] Uploading: {progress}%")
                await asyncio.sleep(0.01)
        except Exception:
            await self.client.write_gatt_char(BLE_OTA_CONTROL_CHAR_UUID, bytes([BLE_OTA_CMD_ABORT]))
            return False
        
        self.safe_print(f"\n[OK] Upload complete in {time.time() - start_time:.1f}s")
        self.safe_print("[INFO] Applying update...")
        try:
            await self.client.write_gatt_char(BLE_OTA_CONTROL_CHAR_UUID, bytes([BLE_OTA_CMD_END]))
            return await self.wait_for_ota_status(BLE_OTA_SUCCESS, timeout=30)
        except BleakError:
            return True

    # === Data Export Functions (Refactored) ===
    async def get_session_count(self) -> int:
        await self.client.write_gatt_char(BLE_DATA_CONTROL_CHAR_UUID, bytes([BLE_DATA_CMD_GET_COUNT]))
        await asyncio.sleep(1)
        return self.session_count
    
    async def export_data(self, db_path: str = None) -> bool:
        if db_path is None:
            # Default to tools/database/grinder_data.db
            tools_dir = Path(__file__).parent.parent
            db_path = str(tools_dir / "database" / "grinder_data.db")
        
        self.safe_print("[INFO] Starting per-file data export...")
        
        # Step 1: Get the list of available session files
        self.safe_print("[INFO] Requesting session file list...")
        
        # Set up reception state BEFORE sending command to avoid race condition
        self.data_chunks = []
        self.receiving_data = True
        
        # Small delay to ensure the state is properly set
        await asyncio.sleep(0.1)
        
        await self.client.write_gatt_char(BLE_DATA_CONTROL_CHAR_UUID, bytes([BLE_DATA_CMD_GET_FILE_LIST]))
        
        timeout_seconds = 10
        start_time = time.time()
        while self.receiving_data and (time.time() - start_time) < timeout_seconds:
            await asyncio.sleep(0.1)
        
        if self.receiving_data:
            self.safe_print("[ERROR] Timeout waiting for file list")
            return False
        
        if not self.data_chunks:
            self.safe_print("[ERROR] No file list received")
            return False
        
        # Parse file list: [session_count:4][session_id1:4][session_id2:4]...
        file_list_data = b"".join(self.data_chunks)
        if len(file_list_data) < 4:
            self.safe_print("[ERROR] Invalid file list data")
            return False
        
        session_count = struct.unpack('<I', file_list_data[:4])[0]
        if session_count == 0:
            self.safe_print("[OK] No sessions to export")
            return True
        
        self.safe_print(f"[INFO] Found {session_count} session files to export")
        
        # Extract session IDs
        session_ids = []
        for i in range(session_count):
            offset = 4 + (i * 4)
            if offset + 4 <= len(file_list_data):
                session_id = struct.unpack('<I', file_list_data[offset:offset+4])[0]
                session_ids.append(session_id)
        
        self.safe_print(f"[INFO] Session IDs: {session_ids}")
        
        # Step 2: Request each file individually and process it
        all_sessions = []
        all_events = []
        all_measurements = []
        
        for i, session_id in enumerate(session_ids):
            self.safe_print(f"[INFO] Requesting session file {session_id} ({i+1}/{len(session_ids)})")
            
            # Request individual file
            request_data = bytes([BLE_DATA_CMD_REQUEST_FILE]) + struct.pack('<I', session_id)
            await self.client.write_gatt_char(BLE_DATA_CONTROL_CHAR_UUID, request_data)
            
            # Wait for file transfer
            self.data_chunks = []
            self.receiving_data = True
            
            file_start_time = time.time()
            timeout_seconds = 30
            
            while self.receiving_data and (time.time() - file_start_time) < timeout_seconds:
                await asyncio.sleep(0.1)
            
            if self.receiving_data:
                self.safe_print(f"[ERROR] Timeout waiting for session file {session_id}")
                continue
            
            if not self.data_chunks:
                self.safe_print(f"[ERROR] No data received for session {session_id}")
                continue
            
            # Process individual file
            try:
                file_data = b"".join(self.data_chunks)
                self.safe_print(f"[INFO] Received {len(file_data)} bytes for session {session_id}")
                
                # Parse this single session file
                sessions, events, measurements = self._parse_single_file_data(file_data, session_id)
                all_sessions.extend(sessions)
                all_events.extend(events) 
                all_measurements.extend(measurements)
                self.safe_print(f"[OK] Successfully processed session {session_id}")
                
            except Exception as e:
                self.safe_print(f"[WARNING] Failed to process session {session_id}: {e}")
                # Save corrupted file for debugging
                with open(f"failed_session_{session_id}.bin", "wb") as f:
                    f.write(file_data)
                self.safe_print(f"[INFO] Saved corrupted session to failed_session_{session_id}.bin")
                continue
        
        # Step 3: Store all successfully processed data
        if all_sessions:
            self.safe_print(f"\n[INFO] Storing data: {len(all_sessions)} sessions, {len(all_events)} events, {len(all_measurements)} measurements")
            self._store_data(all_sessions, all_events, all_measurements, db_path)
            self.safe_print("[OK] Data export completed successfully!")
            return True
        else:
            self.safe_print("[ERROR] No sessions were successfully processed")
            return False
    
    def _parse_single_file_data(self, file_data: bytes, session_id: int) -> Tuple[List[Dict], List[Dict], List[Dict]]:
        """Parse data from a single session file.
        Format on device (LittleFS):
        [TimeSeriesSessionHeader (24 bytes)]
        [GrindSession (80 bytes)]
        [GrindEvent x event_count (44 bytes each)]
        [GrindMeasurement x measurement_count (24 bytes each)]
        """
        if len(file_data) < (24 + SESSION_STRUCT_SIZE):
            raise ValueError(f"File data too small: {len(file_data)} bytes")

        PHASE_NAMES = {
            0: "IDLE", 1: "INITIALIZING", 2: "SETUP", 3: "TARING", 4: "TARE_CONFIRM", 
            5: "PREDICTIVE", 6: "PULSE_DECISION", 7: "PULSE_EXECUTE", 8: "PULSE_SETTLING",
            9: "FINAL_SETTLING", 10: "COMPLETED", 11: "TIMEOUT",
        }
        
        offset = 0
        
        # Parse 24-byte TimeSeriesSessionHeader
        hdr_session_id, hdr_session_ts, hdr_session_size, hdr_checksum, event_count, measurement_count, \
            schema_version, _reserved = struct.unpack_from('<IIIIHHHH', file_data, offset)
        offset += 24

        if hdr_session_id != session_id:
            raise ValueError(f"Header session ID mismatch: expected {session_id}, got {hdr_session_id}")
        if schema_version != LOG_SCHEMA_VERSION:
            self.safe_print(
                f"[WARNING] Session {session_id} uses schema {schema_version}, expected {LOG_SCHEMA_VERSION}. Attempting to parse anyway."
            )
        
        # Extract full GrindSession struct at exact offsets (80-byte struct)
        session_bytes = file_data[offset:offset + SESSION_STRUCT_SIZE]

        parsed_session_id = struct.unpack_from('<I', session_bytes, 0)[0]
        session_timestamp = struct.unpack_from('<I', session_bytes, 4)[0]
        target_time_ms = struct.unpack_from('<I', session_bytes, 8)[0]
        total_time_ms = struct.unpack_from('<I', session_bytes, 12)[0]
        total_motor_on_time_ms = struct.unpack_from('<I', session_bytes, 16)[0]
        time_error_ms = struct.unpack_from('<i', session_bytes, 20)[0]

        target_weight = struct.unpack_from('<f', session_bytes, 24)[0]
        tolerance = struct.unpack_from('<f', session_bytes, 28)[0]
        final_weight = struct.unpack_from('<f', session_bytes, 32)[0]
        error_grams = struct.unpack_from('<f', session_bytes, 36)[0]
        start_weight = struct.unpack_from('<f', session_bytes, 40)[0]

        initial_motor_stop_offset = struct.unpack_from('<f', session_bytes, 44)[0]
        latency_to_coast_ratio = struct.unpack_from('<f', session_bytes, 48)[0]
        flow_rate_threshold = struct.unpack_from('<f', session_bytes, 52)[0]

        profile_id = struct.unpack_from('<B', session_bytes, 56)[0]
        grind_mode = struct.unpack_from('<B', session_bytes, 57)[0]
        max_pulse_attempts = struct.unpack_from('<B', session_bytes, 58)[0]
        pulse_count = struct.unpack_from('<B', session_bytes, 59)[0]
        termination_reason = struct.unpack_from('<B', session_bytes, 60)[0]

        result_bytes = session_bytes[64:80]

        # Extract result_status from byte array and clean it
        result_status = result_bytes.decode('utf-8', errors='ignore').rstrip('\x00')

        # VALIDATION 1: Verify session ID matches what we requested
        if parsed_session_id != session_id:
            raise ValueError(f"Session ID mismatch: expected {session_id}, got {parsed_session_id}")
        
        offset += SESSION_STRUCT_SIZE

        session = {
            'session_id': parsed_session_id,
            'session_timestamp': session_timestamp,
            'profile_id': profile_id,
            'grind_mode': grind_mode,
            'target_weight': target_weight,
            'target_time_ms': target_time_ms,
            'tolerance': tolerance,
            'final_weight': final_weight,
            'start_weight': start_weight,
            'error_grams': error_grams,
            'time_error_ms': time_error_ms,
            'total_time_ms': total_time_ms,
            'total_motor_on_time_ms': total_motor_on_time_ms,
            'pulse_count': pulse_count,
            'max_pulse_attempts': max_pulse_attempts,
            'termination_reason': termination_reason,
            'latency_to_coast_ratio': latency_to_coast_ratio,
            'flow_rate_threshold': flow_rate_threshold,
            'schema_version': schema_version,
            'result_status': result_status,
            'session_size_bytes': hdr_session_size,
            'checksum': hdr_checksum
        }
        
        events = []
        EVENT_SIZE = EVENT_STRUCT_SIZE
        expected_event_sequence = 0  # Events should start at 0 and increment

        for event_idx in range(event_count):
            if offset + EVENT_SIZE > len(file_data):
                raise ValueError(f"File too small for event at offset {offset}")

            event_bytes = file_data[offset:offset + EVENT_SIZE]
            timestamp_ms = struct.unpack_from('<I', event_bytes, 0)[0]
            duration_ms = struct.unpack_from('<I', event_bytes, 4)[0]
            grind_latency_ms = struct.unpack_from('<I', event_bytes, 8)[0]
            settling_duration_ms = struct.unpack_from('<I', event_bytes, 12)[0]
            start_weight = struct.unpack_from('<f', event_bytes, 16)[0]
            end_weight = struct.unpack_from('<f', event_bytes, 20)[0]
            motor_stop_target_weight = struct.unpack_from('<f', event_bytes, 24)[0]
            pulse_duration_ms = struct.unpack_from('<f', event_bytes, 28)[0]
            pulse_flow_rate = struct.unpack_from('<f', event_bytes, 32)[0]
            event_sequence_id = struct.unpack_from('<H', event_bytes, 36)[0]
            loop_count = struct.unpack_from('<H', event_bytes, 38)[0]
            phase_id = struct.unpack_from('<B', event_bytes, 40)[0]
            pulse_attempt_number = struct.unpack_from('<B', event_bytes, 41)[0]
            event_flags = struct.unpack_from('<B', event_bytes, 42)[0]

            if timestamp_ms == 0xFFFFFFFF or phase_id == 0xFF:  # Skip invalid/empty events
                expected_event_sequence += 1
                offset += EVENT_SIZE
                continue

            if event_sequence_id != expected_event_sequence:
                raise ValueError(
                    f"Event sequence out of order: expected {expected_event_sequence}, got {event_sequence_id} at event {event_idx}"
                )

            offset += EVENT_SIZE
            event = {
                'session_id': parsed_session_id,
                'timestamp_ms': timestamp_ms,
                'phase_id': phase_id,
                'phase_name': PHASE_NAMES.get(phase_id, 'UNKNOWN'),
                'pulse_attempt_number': pulse_attempt_number,
                'event_sequence_id': event_sequence_id,
                'duration_ms': duration_ms,
                'start_weight': start_weight,
                'end_weight': end_weight,
                'motor_stop_target_weight': motor_stop_target_weight,
                'pulse_duration_ms': pulse_duration_ms,
                'grind_latency_ms': grind_latency_ms,
                'settling_duration_ms': settling_duration_ms,
                'pulse_flow_rate': pulse_flow_rate,
                'loop_count': loop_count,
                'event_flags': event_flags
            }
            events.append(event)
            expected_event_sequence += 1

        measurements = []
        MEASUREMENT_SIZE = MEASUREMENT_STRUCT_SIZE
        expected_measurement_sequence = 0  # Measurements should start at 0 and increment

        for meas_idx in range(measurement_count):
            if offset + MEASUREMENT_SIZE > len(file_data):
                raise ValueError(f"File too small for measurement at offset {offset}")

            meas_bytes = file_data[offset:offset + MEASUREMENT_SIZE]
            timestamp_ms = struct.unpack_from('<I', meas_bytes, 0)[0]
            weight_grams = struct.unpack_from('<f', meas_bytes, 4)[0]
            weight_delta = struct.unpack_from('<f', meas_bytes, 8)[0]
            flow_rate_g_per_s = struct.unpack_from('<f', meas_bytes, 12)[0]
            motor_stop_target_weight = struct.unpack_from('<f', meas_bytes, 16)[0]
            sequence_id = struct.unpack_from('<H', meas_bytes, 20)[0]
            motor_is_on = struct.unpack_from('<B', meas_bytes, 22)[0]
            phase_id = struct.unpack_from('<B', meas_bytes, 23)[0]

            if timestamp_ms == 0xFFFFFFFF or weight_grams == -999.0:  # Skip invalid measurements
                expected_measurement_sequence += 1
                offset += MEASUREMENT_SIZE
                continue

            if sequence_id != expected_measurement_sequence:
                self.safe_print(
                    f"[WARNING] Measurement sequence mismatch in session {parsed_session_id}: expected {expected_measurement_sequence}, got {sequence_id}"
                )

            offset += MEASUREMENT_SIZE
            measurement = {
                'session_id': parsed_session_id,
                'sequence_id': sequence_id,
                'timestamp_ms': timestamp_ms,
                'weight_grams': weight_grams,
                'weight_delta': weight_delta,
                'flow_rate_g_per_s': flow_rate_g_per_s,
                'motor_is_on': motor_is_on,
                'phase_id': phase_id,
                'phase_name': PHASE_NAMES.get(phase_id, 'UNKNOWN'),
                'motor_stop_target_weight': motor_stop_target_weight
            }
            measurements.append(measurement)
            expected_measurement_sequence += 1
        
        # VALIDATION 4: Final validation - ensure we processed expected number of records
        if len(events) > event_count:
            raise ValueError(f"Event count validation failed: processed {len(events)}, expected max {event_count}")
        if len(measurements) > measurement_count:
            raise ValueError(f"Measurement count validation failed: processed {len(measurements)}, expected max {measurement_count}")
        
        self.safe_print(f"[OK] Session {parsed_session_id} validation passed: {len(events)} events, {len(measurements)} measurements")
            
        return [session], events, measurements
    
    
    
    def _store_data(self, sessions: List[Dict], events: List[Dict], measurements: List[Dict], db_path: str):
        if os.path.exists(db_path):
            os.remove(db_path)
        
        with sqlite3.connect(db_path) as conn:
            cursor = conn.cursor()
            
            cursor.execute("""
                CREATE TABLE grind_sessions (
                    session_id INTEGER PRIMARY KEY,
                    session_timestamp INTEGER,
                    profile_id INTEGER,
                    grind_mode INTEGER,
                    target_weight REAL,
                    target_time_ms INTEGER,
                    tolerance REAL,
                    final_weight REAL,
                    start_weight REAL,
                    error_grams REAL,
                    time_error_ms INTEGER,
                    total_time_ms INTEGER,
                    total_motor_on_time_ms INTEGER,
                    pulse_count INTEGER,
                    max_pulse_attempts INTEGER,
                    termination_reason INTEGER,
                    latency_to_coast_ratio REAL,
                    flow_rate_threshold REAL,
                    schema_version INTEGER,
                    result_status TEXT,
                    checksum INTEGER,
                    session_size_bytes INTEGER,
                    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                );""")

            cursor.execute("""
                CREATE TABLE grind_events (
                    session_id INTEGER, event_sequence_id INTEGER, timestamp_ms INTEGER, 
                    phase_id INTEGER, phase_name TEXT, pulse_attempt_number INTEGER, 
                    duration_ms INTEGER, start_weight REAL, end_weight REAL,
                    motor_stop_target_weight REAL, pulse_duration_ms REAL, grind_latency_ms INTEGER,
                    settling_duration_ms INTEGER, pulse_flow_rate REAL, loop_count INTEGER,
                    event_flags INTEGER,
                    FOREIGN KEY (session_id) REFERENCES grind_sessions(session_id),
                    PRIMARY KEY (session_id, event_sequence_id)
                );""")

            cursor.execute("""
                CREATE TABLE grind_measurements (
                    session_id INTEGER, sequence_id INTEGER, timestamp_ms INTEGER,
                    weight_grams REAL, weight_delta REAL, flow_rate_g_per_s REAL, motor_is_on BOOLEAN, 
                    phase_id INTEGER, phase_name TEXT, motor_stop_target_weight REAL,
                    FOREIGN KEY (session_id) REFERENCES grind_sessions(session_id),
                    PRIMARY KEY (session_id, sequence_id)
                );""")
            
            # Insert data
            
            # Build placeholders dynamically to match column count
            _cols_sessions = """session_id, session_timestamp, profile_id, grind_mode, target_weight, target_time_ms, tolerance, final_weight, start_weight, error_grams, time_error_ms, total_time_ms, total_motor_on_time_ms, pulse_count, max_pulse_attempts, termination_reason, latency_to_coast_ratio, flow_rate_threshold, schema_version, result_status, checksum, session_size_bytes"""
            _params_sessions = [
                    (
                        s['session_id'], s['session_timestamp'], s['profile_id'], s.get('grind_mode', 0),
                        s['target_weight'], s.get('target_time_ms', 0), s['tolerance'],
                        s['final_weight'], s.get('start_weight', 0.0), s['error_grams'], s.get('time_error_ms', 0),
                        s['total_time_ms'], s['total_motor_on_time_ms'], s['pulse_count'], s.get('max_pulse_attempts', 0),
                        s.get('termination_reason', 255), s.get('latency_to_coast_ratio', 0.0), s.get('flow_rate_threshold', 0.0),
                        s.get('schema_version', LOG_SCHEMA_VERSION),
                        s['result_status'], s.get('checksum', 0), s.get('session_size_bytes', 0)
                    )
                    for s in sessions
                ]
            if _params_sessions:
                _ph_sessions = "(" + ",".join(["?"] * len(_params_sessions[0])) + ")"
                cursor.executemany(f"INSERT INTO grind_sessions ({_cols_sessions}) VALUES {_ph_sessions}", _params_sessions)
            else:
                pass

            cursor.executemany("INSERT INTO grind_events VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                [(e['session_id'], e['event_sequence_id'], e['timestamp_ms'], e['phase_id'], 
                  e['phase_name'], e['pulse_attempt_number'], e['duration_ms'],
                  e['start_weight'], e['end_weight'], e['motor_stop_target_weight'], 
                  e['pulse_duration_ms'], e['grind_latency_ms'], e['settling_duration_ms'],
                  e['pulse_flow_rate'], e['loop_count'], e.get('event_flags', 0)) for e in events])

            cursor.executemany("INSERT INTO grind_measurements VALUES (?,?,?,?,?,?,?,?,?,?)",
                [(m['session_id'], m['sequence_id'], m['timestamp_ms'], m['weight_grams'], m['weight_delta'], m['flow_rate_g_per_s'], m['motor_is_on'], m['phase_id'], m['phase_name'], m['motor_stop_target_weight']) for m in measurements])
            
            conn.commit()
    
    # === Analyze Data (Export + Streamlit Report) ===
    async def analyze_data(self, db_path: str = None, skip_export: bool = False) -> bool:
        """Export data from grinder and launch Streamlit report."""
        # Get the absolute path to ensure the database can be found
        tools_dir = Path(__file__).parent.parent
        streamlit_dir = tools_dir / "streamlit-reports"
        
        if db_path is None:
            # Default to tools/database/grinder_data.db
            db_full_path = tools_dir / "database" / "grinder_data.db"
        elif Path(db_path).is_absolute():
            db_full_path = Path(db_path)
        else:
            # Relative path, resolve relative to tools directory
            db_full_path = tools_dir / db_path
        
        self.safe_print("[INFO] Starting data analysis workflow...")
        
        if not skip_export:
            # First, export the data
            if not await self.export_data(str(db_full_path)):
                self.safe_print("[ERROR] Data export failed, cannot launch report")
                return False
            
            # Close BLE connection after successful data export
            self.safe_print("[INFO] Closing BLE connection...")
            await self.disconnect()
        else:
            self.safe_print("[INFO] Skipping data export, using existing database")
        
        self.safe_print("[INFO] Launching Streamlit report...")
        
        # Change to streamlit-reports directory and launch streamlit
        try:
            # Platform-specific venv paths (same logic as main grinder tool)
            import platform
            venv_dir = tools_dir / "venv"
            if platform.system() == "Windows":
                venv_python = venv_dir / "Scripts" / "python.exe"
                venv_streamlit = venv_dir / "Scripts" / "streamlit.exe"
            else:
                venv_python = venv_dir / "bin" / "python"
                venv_streamlit = venv_dir / "bin" / "streamlit"
            
            if not venv_streamlit.exists():
                self.safe_print("[INFO] Streamlit not found in venv. Installing...")
                result = subprocess.run([str(venv_python), "-m", "pip", "install", "streamlit>=1.28.0", "plotly>=5.15.0"], 
                                      capture_output=True, text=True)
                if result.returncode != 0:
                    self.safe_print(f"[ERROR] Failed to install Streamlit: {result.stderr}")
                    return False
            
            self.safe_print(f"[INFO] Opening report at http://localhost:8501")
            self.safe_print(f"[INFO] Using database: {db_full_path}")
            self.safe_print(f"[INFO] Press Ctrl+C to stop the server")
            
            # Launch streamlit in the reports directory with proper environment
            os.chdir(str(streamlit_dir))
            env_vars = {
                **os.environ, 
                "PYTHONPATH": str(streamlit_dir),
                "GRIND_DB_PATH": str(db_full_path)
            }
            subprocess.run([str(venv_python), "-m", "streamlit", "run", "grind_report.py"], env=env_vars)
            
        except Exception as e:
            self.safe_print(f"[ERROR] Failed to launch Streamlit report: {e}")
            self.safe_print("[INFO] You can manually run: streamlit run tools/streamlit-reports/grind_report.py")
            return False
        
        return True
    
    # === Debug Monitor ===
    async def debug_monitor(self):
        """Connect and display live debug messages from the grinder."""
        self.safe_print("[INFO] Enabling debug stream...")
        
        try:
            # Enable debug stream on the device
            await self.client.write_gatt_char(BLE_DEBUG_RX_CHAR_UUID, bytes([BLE_DEBUG_CMD_ENABLE]))
            self.safe_print("[OK] Debug stream active. Press Ctrl+C to exit.")
            
            # Keep the script alive to receive notifications
            while self.connected:
                await asyncio.sleep(1)
        except (asyncio.CancelledError, KeyboardInterrupt):
            pass # Will be handled in finally
        finally:
            # Flush any remaining debug buffer
            if self.debug_buffer:
                sys.stdout.write(self.debug_buffer)
                sys.stdout.flush()
                self.debug_buffer = ""
                
            self.safe_print("\n[INFO] Disabling debug stream...")
            if self.client and self.client.is_connected:
                try:
                    await self.client.write_gatt_char(BLE_DEBUG_RX_CHAR_UUID, bytes([BLE_DEBUG_CMD_DISABLE]))
                except BleakError as e:
                    self.safe_print(f"[WARNING] Could not disable debug stream cleanly: {e}")
    
    # === System Information Functions ===
    async def get_system_info(self) -> Dict:
        """Get comprehensive system information from the device."""
        try:
            system_data = await self.client.read_gatt_char(BLE_SYSINFO_SYSTEM_CHAR_UUID)
            performance_data = await self.client.read_gatt_char(BLE_SYSINFO_PERFORMANCE_CHAR_UUID)
            hardware_data = await self.client.read_gatt_char(BLE_SYSINFO_HARDWARE_CHAR_UUID)
            sessions_data = await self.client.read_gatt_char(BLE_SYSINFO_SESSIONS_CHAR_UUID)
            
            system_info = json.loads(system_data.decode('utf-8'))
            performance_info = json.loads(performance_data.decode('utf-8'))
            hardware_info = json.loads(hardware_data.decode('utf-8'))
            sessions_info = json.loads(sessions_data.decode('utf-8'))
            
            return {
                'system': system_info,
                'performance': performance_info,
                'hardware': hardware_info,
                'sessions': sessions_info
            }
        except Exception as e:
            self.safe_print(f"[ERROR] Error reading system info: {e}")
            return {}
    
    def print_system_info(self, info: Dict):
        """Print formatted system information."""
        if not info:
            self.safe_print("[ERROR] No system information available")
            return
        
        system = info.get('system', {})
        performance = info.get('performance', {})
        hardware = info.get('hardware', {})
        sessions = info.get('sessions', {})
        
        self.safe_print("\n" + "="*60)
        self.safe_print("[SYSTEM] GRINDER SYSTEM INFORMATION")
        self.safe_print("="*60)
        
        # System Information
        self.safe_print(f"[FIRMWARE]:")
        self.safe_print(f"   Version:      {system.get('version', 'Unknown')}")
        self.safe_print(f"   Build:        #{system.get('build', 'Unknown')}")
        self.safe_print(f"   Uptime:       {system.get('uptime_h', 0):02d}:{system.get('uptime_m', 0):02d}:{system.get('uptime_s', 0):02d}")
        self.safe_print(f"   CPU Freq:     {system.get('cpu_freq', 'Unknown')} MHz")
        
        # Memory Information  
        self.safe_print(f"[MEMORY]:")
        heap_free = system.get('heap_free', 0)
        heap_total = system.get('heap_total', 0)
        flash_size = system.get('flash_size', 0)
        heap_used_pct = system.get('heap_used_pct', 0)
        self.safe_print(f"   Heap Free:    {heap_free//1024:,} KB")
        self.safe_print(f"   Heap Total:   {heap_total//1024:,} KB")
        self.safe_print(f"   Heap Used:    {heap_used_pct:.1f}%")
        self.safe_print(f"   Flash Size:   {flash_size//1024//1024:,} MB")
        
        # Performance Information
        self.safe_print(f"[PERFORMANCE]:")
        self.safe_print(f"   System:       {'[HEALTHY]' if performance.get('system_healthy') else '[STRESSED]'}")
        self.safe_print(f"   Tasks:        {performance.get('tasks_registered', 0)} registered")
        self.safe_print(f"   Load Cell:    {performance.get('load_cell_freq_hz', 0)} Hz")
        self.safe_print(f"   Grind Ctrl:   {performance.get('grind_control_freq_hz', 0)} Hz")
        self.safe_print(f"   UI Updates:   {performance.get('ui_freq_hz', 0)} Hz")
        
        # Hardware Status
        self.safe_print(f"[HARDWARE]:")
        hw_status = []
        if hardware.get('load_cell_active'): hw_status.append("[OK] Load Cell")
        if hardware.get('motor_available'): hw_status.append("[OK] Motor")
        if hardware.get('display_active'): hw_status.append("[OK] Display")
        if hardware.get('touch_active'): hw_status.append("[OK] Touch")
        if hardware.get('ble_enabled'): hw_status.append("[OK] Bluetooth")
        if not hardware.get('wifi_available'): hw_status.append("[ERROR] WiFi")
        
        self.safe_print(f"   Status:       {', '.join(hw_status)}")
        
        # Session Statistics
        self.safe_print(f"[SESSION DATA]:")
        total_sessions = sessions.get('total_sessions', 0)
        self.safe_print(f"   Total:        {total_sessions} sessions")
        self.safe_print(f"   Data Avail:   {'[YES]' if sessions.get('data_available') else '[NO]'}")
        self.safe_print(f"   Export:       {'[ACTIVE]' if sessions.get('export_active') else '[IDLE]'}")
        
        self.safe_print("="*60 + "\n")

    # === Interactive Mode (Unchanged) ===
    async def interactive_session(self):
        self.safe_print("\n[INFO] Interactive Session (type 'help' for commands)")
        while self.connected:
            try:
                cmd = input("> ").strip().lower()
                if cmd in ["exit", "quit", "disconnect"]: break
                elif cmd == "help":
                    print("Commands: count, export, clear, status, sysinfo, disconnect")
                elif cmd == "count":
                    await self.get_session_count()
                elif cmd == "export":
                    await self.export_data()
                elif cmd == "clear":
                    await self.client.write_gatt_char(BLE_DATA_CONTROL_CHAR_UUID, bytes([BLE_DATA_CMD_CLEAR_DATA]))
                    self.safe_print("[OK] Data cleared")
                elif cmd == "status":
                    print(f"Connected: {self.connected}")
                elif cmd == "sysinfo":
                    info = await self.get_system_info()
                    self.print_system_info(info)
                elif cmd:
                    self.safe_print(f"[ERROR] Unknown command: {cmd}")
            except (KeyboardInterrupt, EOFError):
                break
        await self.disconnect()

async def main():
    parser = argparse.ArgumentParser(description="Unified grinder BLE tool", formatter_class=argparse.RawTextHelpFormatter)
    subparsers = parser.add_subparsers(dest='command', required=True)
    
    subparsers.add_parser('scan', help='Scan for BLE devices')
    upload_parser = subparsers.add_parser('upload', help='Upload firmware via BLE OTA')
    upload_parser.add_argument('firmware', nargs='?', help='Path to firmware .bin file')
    upload_parser.add_argument('--force-full', action='store_true', help='Force full update')
    export_parser = subparsers.add_parser('export', help='Export grind data')
    export_parser.add_argument('--db', default=None, help='Output database file (default: tools/database/grinder_data.db)')
    analyse_parser = subparsers.add_parser('analyse', help='Export data and launch Streamlit report')
    analyse_parser.add_argument('--db', default=None, help='Output database file (default: tools/database/grinder_data.db)')
    connect_parser = subparsers.add_parser('connect', help='Connect to device')
    debug_parser = subparsers.add_parser('debug', help='Stream live debug logs from the device')
    sysinfo_parser = subparsers.add_parser('info', help='Get comprehensive device system information')
    
    for p in [upload_parser, export_parser, analyse_parser, connect_parser, debug_parser, sysinfo_parser]:
        p.add_argument('--device', default=DEVICE_NAME, help='Device name to connect to')

    args = parser.parse_args()
    tool = GrinderBLETool()
    
    try:
        if args.command == 'scan':
            await tool.scan_devices()
        
        elif args.command in ['upload', 'export', 'analyse', 'connect', 'debug', 'info']:
            if not await tool.connect_to_device(args.device): return 1
            
            if args.command == 'upload':
                firmware_path = args.firmware or tool.find_firmware_file()
                if not firmware_path:
                    tool.safe_print("[ERROR] No firmware file found.")
                    return 1
                await tool.upload_firmware(firmware_path, args.force_full)
            elif args.command == 'export':
                await tool.export_data(args.db)
            elif args.command == 'analyse':
                await tool.analyze_data(args.db, False)
                # analyze_data handles its own disconnection after data export
                return 0
            elif args.command == 'connect':
                tool.safe_print("[OK] Connected to device.")
            elif args.command == 'debug':
                await tool.debug_monitor()
            elif args.command == 'info':
                info = await tool.get_system_info()
                tool.print_system_info(info)

            await tool.disconnect()
            
    except KeyboardInterrupt:
        tool.safe_print("\n[INFO] Interrupted by user")
    finally:
        if tool.connected: await tool.disconnect()

if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except Exception as e:
        print(f"[ERROR] An unexpected error occurred: {e}")
        sys.exit(1)
