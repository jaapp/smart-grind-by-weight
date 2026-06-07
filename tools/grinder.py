#!/usr/bin/env python3
"""
Unified Grinder Tool - Cross-platform Python replacement for bash script
Single script for all grinder operations: build, upload, export, analyze, report
"""

import argparse
import asyncio
import concurrent.futures
import glob
import ipaddress
import os
import sys
import subprocess
import platform
import socket
import venv
from pathlib import Path
from typing import Optional, List, Dict, Any, Set, Tuple
import shutil
import stat
import json
import re
import time
import urllib.error
import urllib.request

# Color support for cross-platform output
try:
    from colorama import init, Fore, Style
    init(autoreset=True)
    COLORS = {
        'RED': Fore.RED,
        'GREEN': Fore.GREEN,
        'YELLOW': Fore.YELLOW,
        'BLUE': Fore.BLUE,
        'PURPLE': Fore.MAGENTA,
        'CYAN': Fore.CYAN,
        'RESET': Style.RESET_ALL
    }
except ImportError:
    # Fallback for systems without colorama
    COLORS = {k: '' for k in ['RED', 'GREEN', 'YELLOW', 'BLUE', 'PURPLE', 'CYAN', 'RESET']}

class ThrottledBytesReader:
    """File-like reader used to avoid overwhelming weak ESP32 WiFi links."""

    def __init__(self, data: bytes, rate_bps: int = 0, progress_callback=None):
        self._data = data
        self._offset = 0
        self._rate_bps = max(0, int(rate_bps or 0))
        self._started_at = time.monotonic()
        self._progress_callback = progress_callback
        self._next_progress = 10

    def read(self, size: int = -1) -> bytes:
        if self._offset >= len(self._data):
            return b""

        if size is None or size < 0:
            size = len(self._data) - self._offset
        if self._rate_bps:
            size = min(size, 8192)

        end = min(self._offset + size, len(self._data))
        chunk = self._data[self._offset:end]
        self._offset = end

        if self._rate_bps:
            target_elapsed = self._offset / float(self._rate_bps)
            delay = self._started_at + target_elapsed - time.monotonic()
            if delay > 0:
                time.sleep(delay)

        if self._progress_callback and self._data:
            progress = int((self._offset * 100) / len(self._data))
            while progress >= self._next_progress and self._next_progress <= 100:
                self._progress_callback(self._next_progress)
                self._next_progress += 10

        return chunk

class GrinderTool:
    """Unified grinder tool for cross-platform operations."""
    
    def __init__(self):
        self.script_dir = Path(__file__).parent
        self.project_dir = self.script_dir.parent
        self.venv_dir = Path(os.environ.get("SG_TOOL_VENV", self.script_dir / "venv"))
        
        # Platform-specific paths
        if platform.system() == "Windows":
            self.venv_python = self.venv_dir / "Scripts" / "python.exe"
            self.venv_pip = self.venv_dir / "Scripts" / "pip.exe"
            self.venv_streamlit = self.venv_dir / "Scripts" / "streamlit.exe"
        else:
            self.venv_python = self.venv_dir / "bin" / "python3"
            self.venv_pip = self.venv_dir / "bin" / "pip"
            self.venv_streamlit = self.venv_dir / "bin" / "streamlit"
        
        self.ble_tool = self.script_dir / "ble" / "grinder-ble.py"
        self.streamlit_dir = self.script_dir / "streamlit-reports"
        self.db_path = self.script_dir / "database" / "grinder_data.db"
        self.requirements_txt = self.script_dir / "requirements.txt"
        self.default_wifi_host = "grindbyweight.local"
        self.wifi_cache_path = self.project_dir / ".pio" / "grinder_wifi_host.json"
    
    def safe_print(self, text: str):
        """Print text with proper encoding handling for all platforms."""
        try:
            print(text)
        except UnicodeEncodeError:
            # Replace problematic Unicode chars for Windows
            safe_text = text.encode('ascii', 'replace').decode('ascii')
            print(safe_text)
    
    def print_header(self, message: str):
        """Print a formatted header."""
        self.safe_print(f"{COLORS['BLUE']}=== {message} ==={COLORS['RESET']}")
    
    def print_success(self, message: str):
        """Print a success message."""
        self.safe_print(f"{COLORS['GREEN']}[OK] {message}{COLORS['RESET']}")
    
    def print_error(self, message: str):
        """Print an error message."""
        self.safe_print(f"{COLORS['RED']}[ERROR] {message}{COLORS['RESET']}")
    
    def print_warning(self, message: str):
        """Print a warning message."""
        self.safe_print(f"{COLORS['YELLOW']}[WARNING] {message}{COLORS['RESET']}")
    
    def print_info(self, message: str):
        """Print an info message."""
        self.safe_print(f"{COLORS['CYAN']}[INFO] {message}{COLORS['RESET']}")
    
    def check_venv(self) -> bool:
        """Check if virtual environment exists and is properly set up."""
        if not self.venv_python.exists():
            self.print_warning("Virtual environment not found, setting up automatically...")
            return self.install_dependencies()
        return True
    
    def install_dependencies(self) -> bool:
        """Create virtual environment and install dependencies."""
        try:
            if not self.venv_dir.exists():
                self.print_info("Creating virtual environment...")
                
                # Find Python executable
                python_cmd = None
                for cmd in ["python3", "python"]:
                    if shutil.which(cmd):
                        python_cmd = cmd
                        break
                
                if not python_cmd:
                    self.print_error("Python not found. Please install Python 3.8+")
                    return False
                
                # Create virtual environment
                venv.create(self.venv_dir, with_pip=True)
            
            self.print_info("Installing Python packages...")
            if not self.venv_pip.exists():
                self.print_error(f"Virtual environment pip not found at: {self.venv_pip}")
                self.print_info(f"Platform: {platform.system()}, Expected venv structure may be incorrect")
                return False
            
            # Install requirements
            result = subprocess.run([
                str(self.venv_pip), "install", "-q", "-r", str(self.requirements_txt)
            ], capture_output=True, text=True)
            
            if result.returncode != 0:
                self.print_error(f"Failed to install dependencies: {result.stderr}")
                return False
            
            # Also install colorama if not already present
            subprocess.run([
                str(self.venv_pip), "install", "-q", "colorama"
            ], capture_output=True, text=True)
            
            return True
            
        except Exception as e:
            self.print_error(f"Failed to set up environment: {e}")
            return False
    
    def platformio_env(self) -> Dict[str, str]:
        """Return a local PlatformIO environment for reproducible builds."""
        env = os.environ.copy()
        env.setdefault("PLATFORMIO_CORE_DIR", str(self.project_dir / ".pio-core"))
        env.setdefault("PLATFORMIO_SETTING_ENABLE_TELEMETRY", "no")
        return env

    def run_command(self, cmd: List[str], cwd: Optional[Path] = None, capture_output: bool = False,
                    env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
        """Run a command with proper error handling."""
        try:
            return subprocess.run(
                cmd, 
                cwd=cwd or self.project_dir,
                capture_output=capture_output,
                text=True,
                check=False,
                env=env
            )
        except FileNotFoundError as e:
            self.print_error(f"Command not found: {cmd[0]}")
            raise e
    
    async def run_async_command(self, cmd: List[str], cwd: Optional[Path] = None) -> int:
        """Run an async command (for BLE operations)."""
        try:
            process = await asyncio.create_subprocess_exec(
                *cmd,
                cwd=cwd or self.project_dir,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT
            )
            
            # Stream output in real-time
            while True:
                line = await process.stdout.readline()
                if not line:
                    break
                print(line.decode().rstrip())
            
            await process.wait()
            return process.returncode
            
        except Exception as e:
            self.print_error(f"Command failed: {e}")
            return 1
    
    def cmd_build(self, args: argparse.Namespace) -> int:
        """Build firmware using PlatformIO."""
        self.print_header("Building Firmware")
        
        if not self.check_venv():
            return 1
        
        # Use PlatformIO from the project venv
        result = self.run_command([
            str(self.venv_python), "-m", "platformio", "run", 
            "-e", "waveshare-esp32s3-touch-amoled-164"
        ], env=self.platformio_env())
        
        if result.returncode == 0:
            self.print_success("Firmware build completed")
        else:
            self.print_error("Build failed")
        
        return result.returncode

    def cmd_build_rescue(self, args: argparse.Namespace) -> int:
        """Build the small recovery firmware used as a reliable OTA bridge."""
        self.print_header("Building Rescue OTA Firmware")

        if not self.check_venv():
            return 1

        result = self.run_command([
            str(self.venv_python), "-m", "platformio", "run",
            "-e", "waveshare-esp32s3-touch-amoled-164-rescue-ota"
        ], env=self.platformio_env())

        if result.returncode == 0:
            rescue_dir = self.project_dir / "firmware_cache" / "rescue"
            latest = rescue_dir / "rescue_latest.bin"
            if latest.exists():
                self.print_success(f"Rescue OTA firmware ready: {latest}")
            else:
                self.print_success("Rescue OTA firmware build completed")
        else:
            self.print_error("Rescue OTA build failed")

        return result.returncode

    def cmd_preview(self, args: argparse.Namespace) -> int:
        """Build and run the native LVGL touchscreen preview."""
        self.print_header("LVGL Touchscreen Preview")

        if not self.check_venv():
            return 1

        env = self.platformio_env()
        result = self.run_command([
            str(self.venv_python), "-m", "platformio", "run", "-e", "lvgl-sdl-preview"
        ], env=env)

        if result.returncode != 0:
            self.print_error("Preview build failed")
            return result.returncode

        program_name = "program.exe" if platform.system() == "Windows" else "program"
        program = self.project_dir / ".pio" / "build" / "lvgl-sdl-preview" / program_name
        if not program.exists():
            self.print_error(f"Preview executable not found: {program}")
            return 1

        if args.click_test:
            result = self.run_command([str(program), "--click-test"], env=env)
            if result.returncode != 0:
                self.print_error("Touchscreen click-dummy test failed")
                return result.returncode
            self.print_success("Touchscreen click-dummy test passed")

        scenes = args.scenes or ["ready", "list", "feedback"]
        if args.interactive:
            scene = scenes[0]
            self.print_info(f"Starting interactive SDL preview: {scene}")
            return self.run_command([str(program), scene], env=env).returncode

        output_dir = Path(args.output_dir) if args.output_dir else self.project_dir / ".pio" / "preview"
        output_dir.mkdir(parents=True, exist_ok=True)

        converter = shutil.which("sips") or shutil.which("magick")
        for scene in scenes:
            ppm = output_dir / f"{scene}.ppm"
            result = self.run_command([
                str(program), scene, "--screenshot", str(ppm)
            ], env=env)
            if result.returncode != 0:
                self.print_error(f"Failed to capture preview scene: {scene}")
                return result.returncode

            display_path = ppm
            if converter:
                png = output_dir / f"{scene}.png"
                if Path(converter).name == "sips":
                    convert_cmd = [converter, "-s", "format", "png", str(ppm), "--out", str(png)]
                else:
                    convert_cmd = [converter, str(ppm), str(png)]
                convert_result = self.run_command(convert_cmd, capture_output=True, env=env)
                if convert_result.returncode == 0 and png.exists():
                    display_path = png

            self.print_success(f"Captured {scene}: {display_path}")

        return 0
    
    async def cmd_upload(self, args: argparse.Namespace) -> int:
        """Upload firmware via WiFi or BLE OTA."""
        firmware_path = args.firmware
        
        if not firmware_path:
            self.print_info("Finding latest firmware file...")
            build_dir = self.project_dir / ".pio" / "build"

            production_firmware = build_dir / "waveshare-esp32s3-touch-amoled-164" / "firmware.bin"
            if production_firmware.exists():
                firmware_path = production_firmware
            else:
                self.print_error("No production firmware file found")
                self.print_info("Run: python3 grinder.py build")
                return 1
        
        transport = getattr(args, 'transport', 'wifi')
        self.print_header("WiFi OTA Upload" if transport == 'wifi' else "BLE OTA Upload")
        self.print_info(f"Using firmware: {firmware_path}")
        
        if not self.check_venv():
            return 1

        if transport == 'wifi':
            if not getattr(args, "skip_safety_checks", False) and not self.verify_source_safety_guards():
                return 1
            host = getattr(args, 'host', self.default_wifi_host)
            recover_usb = not getattr(args, "no_usb_recover", False)
            usb_port = getattr(args, "usb_port", None)
            return self.upload_via_wifi(Path(firmware_path), host, recover_usb=recover_usb, usb_port=usb_port)
        
        cmd = [str(self.venv_python), str(self.ble_tool), "upload", str(firmware_path)]
        
        # Add additional arguments
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        if hasattr(args, 'force_full') and args.force_full:
            cmd.append("--force-full")
        
        return await self.run_async_command(cmd)

    def normalize_wifi_base_url(self, host: str) -> str:
        """Normalize a host/IP/user URL into a base HTTP URL."""
        value = (host or self.default_wifi_host).strip()
        if not value.startswith(("http://", "https://")):
            value = "http://" + value
        return value.rstrip("/")

    def is_default_wifi_host(self, host: str) -> bool:
        value = (host or "").strip().lower()
        return value in ("", "auto", self.default_wifi_host)

    def load_cached_wifi_host(self) -> Optional[str]:
        """Read the last working WiFi host so mDNS failures do not break OTA."""
        try:
            data = json.loads(self.wifi_cache_path.read_text())
        except (OSError, json.JSONDecodeError):
            return None
        host = str(data.get("host") or "").strip()
        return host or None

    def save_cached_wifi_host(self, host: str, status: Optional[Dict[str, Any]] = None):
        """Persist a known-good WiFi host for the next upload attempt."""
        value = (host or "").strip()
        if not value:
            return
        if status and status.get("ip"):
            value = self.normalize_wifi_base_url(str(status["ip"]))
        try:
            self.wifi_cache_path.parent.mkdir(parents=True, exist_ok=True)
            data = {
                "host": value,
                "updated_at": int(time.time()),
            }
            if status:
                data["ip"] = status.get("ip", "")
                data["version"] = status.get("version", "")
                data["build"] = status.get("build", "")
            self.wifi_cache_path.write_text(json.dumps(data, indent=2) + "\n")
        except OSError:
            pass

    def validate_wifi_firmware_path(self, firmware_path: Path) -> bool:
        """Avoid remote flashing known non-production artifacts."""
        normalized_parts = [part.lower() for part in firmware_path.resolve().parts]
        if any(part == "mock" or part.endswith("-mock") for part in normalized_parts):
            self.print_error(f"Refusing WiFi OTA of mock firmware: {firmware_path}")
            return False
        if firmware_path.suffix.lower() != ".bin":
            self.print_error(f"Firmware must be a .bin file: {firmware_path}")
            return False
        return True

    def get_firmware_version(self) -> str:
        """Read the firmware version from build_info.h for post-OTA validation."""
        build_info = self.project_dir / "src" / "config" / "build_info.h"
        try:
            content = build_info.read_text()
            match = re.search(r'#define\s+BUILD_FIRMWARE_VERSION\s+"([^"]+)"', content)
            if match:
                return match.group(1)
        except OSError:
            pass
        return ""

    def verify_source_safety_guards(self) -> bool:
        """Check source-level recovery guards before any remote-only deployment."""
        checks = [
            (
                self.project_dir / "src" / "config" / "system.h",
                "#define SYS_CONNECTIVITY_USE_TASK 0",
                "WiFi/HTTP must be serviced from the main loop on the production UI build",
            ),
            (
                self.project_dir / "src" / "connectivity" / "manager.cpp",
                "handle_setup_recovery_root",
                "Setup AP recovery page must be present",
            ),
            (
                self.project_dir / "src" / "connectivity" / "manager.cpp",
                'server_.on("/ping", HTTP_GET',
                "HTTP ping endpoint must be present",
            ),
            (
                self.project_dir / "src" / "connectivity" / "manager.cpp",
                'server_.on("/generate_204", HTTP_GET',
                "Captive portal probe route must be present",
            ),
            (
                self.project_dir / "src" / "connectivity" / "manager.cpp",
                "setup_ap_active_ && server_.method() == HTTP_GET",
                "Unknown setup AP GET routes must fall back to recovery page",
            ),
            (
                self.project_dir / "src" / "main.cpp",
                "task_manager.should_service_connectivity_in_loop()",
                "Main loop connectivity fallback must be active",
            ),
            (
                self.project_dir / "src" / "controllers" / "grind_controller.cpp",
                "grind_mode != GrindMode::TIME",
                "Time grind must remain available when HX711 is disconnected",
            ),
        ]

        for path, needle, message in checks:
            try:
                content = path.read_text()
            except OSError as exc:
                self.print_error(f"Safety guard check failed: cannot read {path}: {exc}")
                return False
            if needle not in content:
                self.print_error(f"Safety guard missing: {message}")
                self.print_info(f"Expected marker not found in {path}: {needle}")
                return False

        self.print_success("Source safety guards present")
        return True

    def cmd_safety_check(self, args: argparse.Namespace) -> int:
        """Run local safety checks required before remote-only deployments."""
        self.print_header("Firmware Safety Check")

        if not self.verify_source_safety_guards():
            return 1

        if not getattr(args, "skip_web_ui", False):
            result = self.run_command(["node", "tools/test-web-ui.mjs"])
            if result.returncode != 0:
                self.print_error("Embedded web UI harness failed")
                return result.returncode

        if not getattr(args, "skip_preview", False):
            preview_args = argparse.Namespace(
                click_test=True,
                scenes=["ready", "list", "feedback"],
                interactive=False,
                output_dir=None,
            )
            result = self.cmd_preview(preview_args)
            if result != 0:
                return result

        self.print_success("Firmware safety check passed")
        return 0

    def fetch_text(self, url: str, timeout: float = 8) -> str:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.read().decode("utf-8", errors="replace")

    def fetch_json_url(self, url: str, timeout: float = 8) -> Dict[str, Any]:
        return json.loads(self.fetch_text(url, timeout=timeout))

    def local_ipv4_addresses(self) -> Set[str]:
        """Best-effort local IPv4 discovery without platform-specific tools."""
        addresses: Set[str] = set()
        try:
            hostname = socket.gethostname()
            for address in socket.gethostbyname_ex(hostname)[2]:
                if not address.startswith("127."):
                    addresses.add(address)
        except OSError:
            pass

        for target in ("192.168.0.1", "192.168.1.1", "8.8.8.8"):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(0.2)
            try:
                sock.connect((target, 80))
                address = sock.getsockname()[0]
                if address and not address.startswith("127."):
                    addresses.add(address)
            except OSError:
                pass
            finally:
                sock.close()

        return addresses

    def wifi_scan_targets(self) -> List[str]:
        networks: Set[ipaddress.IPv4Network] = {
            ipaddress.ip_network("192.168.0.0/24"),
            ipaddress.ip_network("192.168.1.0/24"),
        }
        for address in self.local_ipv4_addresses():
            try:
                networks.add(ipaddress.ip_network(f"{address}/24", strict=False))
            except ValueError:
                pass

        targets = ["192.168.4.1"]  # setup AP address
        for network in sorted(networks, key=lambda item: str(item.network_address)):
            targets.extend(str(host) for host in network.hosts())
        return list(dict.fromkeys(targets))

    def probe_wifi_host(self, host: str, timeout: float = 0.7) -> Optional[Tuple[str, Dict[str, Any]]]:
        base_url = self.normalize_wifi_base_url(host)
        try:
            ping_text = self.fetch_text(base_url + "/ping", timeout=timeout).strip().lower()
            if ping_text != "ok":
                return None
            status = self.fetch_json_url(base_url + "/api/status", timeout=timeout)
            if status.get("device") != "GrindByWeight":
                return None
            return base_url, status
        except Exception:
            return None

    def discover_wifi_device(self) -> Optional[Tuple[str, Dict[str, Any]]]:
        """Scan likely LAN ranges for a GrindByWeight HTTP endpoint."""
        targets = self.wifi_scan_targets()
        self.print_info(f"Scanning {len(targets)} WiFi targets for GrindByWeight")
        with concurrent.futures.ThreadPoolExecutor(max_workers=80) as executor:
            futures = {executor.submit(self.probe_wifi_host, target): target for target in targets}
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                if result:
                    base_url, status = result
                    self.print_success(f"Found device at {base_url}")
                    self.save_cached_wifi_host(base_url, status)
                    return result
        return None

    def usb_serial_ports(self) -> List[str]:
        """Return likely ESP32 USB serial ports."""
        ports: List[str] = []
        try:
            from serial.tools import list_ports
            for port in list_ports.comports():
                text = f"{port.device} {port.description} {port.hwid}".lower()
                if "303a" in text or "esp32" in text or "usb jtag/serial" in text or "usbmodem" in text:
                    ports.append(port.device)
        except Exception:
            pass

        for pattern in ("/dev/cu.usbmodem*", "/dev/ttyACM*", "/dev/ttyUSB*"):
            ports.extend(glob.glob(pattern))
        return list(dict.fromkeys(ports))

    def reset_wifi_via_usb(self, usb_port: Optional[str] = None, read_seconds: int = 14) -> Optional[str]:
        """
        Open the ESP32 USB serial port to trigger the reliable USB-CDC reset path,
        then watch boot logs for the WiFi IP.
        """
        ports = self.usb_serial_ports()
        port = usb_port or (ports[0] if ports else None)
        if not port:
            self.print_warning("No ESP32 USB serial port found for WiFi recovery")
            return None

        try:
            import serial
        except ImportError:
            self.print_warning("pyserial is not installed; cannot reset over USB")
            return None

        self.print_info(f"Resetting device via USB serial: {port}")
        text_buffer = ""
        try:
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                ser.dtr = False
                ser.rts = False
                deadline = time.time() + read_seconds
                while time.time() < deadline:
                    data = ser.read(4096)
                    if not data:
                        continue
                    text_buffer += data.decode("utf-8", errors="replace")
                    match = re.search(r"WiFi:\s+Connected to .*?, IP ((?:\d{1,3}\.){3}\d{1,3})", text_buffer)
                    if match:
                        ip = match.group(1)
                        self.print_success(f"Device reported WiFi IP {ip}")
                        return ip
        except Exception as exc:
            self.print_warning(f"USB serial recovery failed: {exc}")
            return None

        self.print_warning("USB reset completed, but no WiFi IP appeared in serial logs")
        return None

    def resolve_wifi_base_url(self, host: str, recover_usb: bool = False,
                              usb_port: Optional[str] = None) -> Optional[str]:
        """Resolve mDNS/default host through cache, active probing, LAN scan, and USB reset."""
        candidates: List[str] = []
        cached_host = self.load_cached_wifi_host()
        if self.is_default_wifi_host(host) and cached_host:
            candidates.append(cached_host)
        candidates.append(host or self.default_wifi_host)

        for candidate in list(dict.fromkeys(candidates)):
            base_url = self.normalize_wifi_base_url(candidate)
            try:
                status = self.preflight_wifi_device(base_url, timeout=4)
                self.save_cached_wifi_host(base_url, status)
                return base_url
            except Exception as exc:
                self.print_warning(f"WiFi preflight failed at {base_url}: {exc}")

        if self.is_default_wifi_host(host):
            discovered = self.discover_wifi_device()
            if discovered:
                return discovered[0]

        if recover_usb:
            recovered_host = self.reset_wifi_via_usb(usb_port=usb_port)
            if recovered_host:
                base_url = self.normalize_wifi_base_url(recovered_host)
                try:
                    status = self.preflight_wifi_device(base_url, timeout=8)
                    self.save_cached_wifi_host(base_url, status)
                    return base_url
                except Exception as exc:
                    self.print_warning(f"Recovered USB IP did not pass HTTP preflight: {exc}")

        return None

    def preflight_wifi_device(self, base_url: str, timeout: float = 8) -> Dict[str, Any]:
        """Verify the currently running firmware still exposes recovery-critical HTTP routes."""
        ping_url = base_url + "/ping"
        ping_text = self.fetch_text(ping_url, timeout=timeout).strip().lower()
        if ping_text != "ok":
            raise RuntimeError(f"{ping_url} returned {ping_text!r}, expected 'ok'")

        status = None
        last_status_error: Optional[Exception] = None
        for path in ("/api/status", "/status"):
            try:
                status = self.fetch_json_url(base_url + path, timeout=timeout)
                break
            except Exception as exc:
                last_status_error = exc
        if status is None:
            raise RuntimeError(f"status endpoint unavailable: {last_status_error}")

        settings = self.fetch_json_url(base_url + "/api/settings", timeout=timeout)
        if "settings" not in settings:
            raise RuntimeError("/api/settings did not return a settings object")

        beans = self.fetch_json_url(base_url + "/api/beans", timeout=timeout)
        if "beans" not in beans:
            raise RuntimeError("/api/beans did not return a beans array")

        self.print_success("Device HTTP preflight passed")
        return status

    def wait_for_wifi_device(self, base_url: str, expected_version: str = "", timeout_s: int = 90) -> bool:
        """Wait for the device to return after OTA and verify basic HTTP service."""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            try:
                status = self.preflight_wifi_device(base_url)
                if expected_version and status.get("version") != expected_version:
                    self.print_warning(
                        f"Device is reachable, but version is {status.get('version')} "
                        f"instead of expected {expected_version}"
                    )
                    return False
                self.print_success("Device returned after OTA with HTTP recovery endpoints alive")
                return True
            except Exception:
                time.sleep(3)
        return False

    def upload_via_wifi(self, firmware_path: Path, host: str, recover_usb: bool = False,
                        usb_port: Optional[str] = None) -> int:
        """Upload a firmware binary to the device's WiFi OTA endpoint."""
        if not firmware_path.exists():
            self.print_error(f"Firmware file not found: {firmware_path}")
            return 1
        if not self.validate_wifi_firmware_path(firmware_path):
            return 1

        base_url = self.resolve_wifi_base_url(host, recover_usb=recover_usb, usb_port=usb_port)
        if not base_url:
            self.print_error("Could not reach WiFi device by cached host, mDNS, LAN scan, or USB recovery")
            self.print_info("OTA aborted. Connect USB or pass --host with the current IP, then run recover-wifi.")
            return 1
        ota_url = base_url + "/ota"
        version = self.get_firmware_version()

        try:
            self.print_info(f"Checking device at {base_url}")
            status = self.preflight_wifi_device(base_url)
            supports_raw_ota = bool(status.get("ota_raw"))
            self.print_info(
                f"Device: {status.get('device', 'unknown')} "
                f"v{status.get('version', '?')} at {status.get('ip', host)}"
            )
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, RuntimeError) as exc:
            self.print_error(f"Could not reach WiFi device: {exc}")
            self.print_info("OTA aborted. Keep the existing firmware until /ping, /api/status, /api/settings, and /api/beans respond.")
            return 1

        if supports_raw_ota:
            self.print_info("Device advertises raw OTA, but using throttled multipart for reliability")
        else:
            self.print_warning("Device does not advertise raw OTA support; using legacy multipart upload")

        firmware = firmware_path.read_bytes()
        boundary = f"----GrindByWeight{int(time.time())}"
        fields = []
        if version:
            fields.append(
                (
                    f"--{boundary}\r\n"
                    'Content-Disposition: form-data; name="version"\r\n\r\n'
                    f"{version}\r\n"
                ).encode("utf-8")
            )
        fields.append(
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="firmware"; filename="{firmware_path.name}"\r\n'
                "Content-Type: application/octet-stream\r\n\r\n"
            ).encode("utf-8")
        )
        fields.append(firmware)
        fields.append(f"\r\n--{boundary}--\r\n".encode("utf-8"))
        body = b"".join(fields)
        upload_rate_bps = 32 * 1024
        upload_reader = ThrottledBytesReader(
            body,
            rate_bps=upload_rate_bps,
            progress_callback=lambda progress: self.print_info(f"Upload progress: {progress}%"),
        )

        request = urllib.request.Request(
            ota_url,
            data=upload_reader,
            method="POST",
            headers={
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Content-Length": str(len(body)),
            },
        )

        try:
            self.print_info(
                f"Uploading {firmware_path.stat().st_size / 1024:.1f} KB to {ota_url} "
                f"(multipart, limited to {upload_rate_bps // 1024} KB/s)"
            )
            with urllib.request.urlopen(request, timeout=600) as response:
                response_text = response.read().decode("utf-8", errors="replace")
            self.print_success("WiFi OTA upload complete; device is rebooting")
            if response_text:
                self.print_info(response_text)
            if not self.wait_for_wifi_device(base_url, version):
                self.print_warning("Post-OTA HTTP verification did not complete. Check the device before starting another update.")
            return 0
        except urllib.error.HTTPError as exc:
            self.print_error(f"WiFi OTA failed: HTTP {exc.code} {exc.reason}")
            details = exc.read().decode("utf-8", errors="replace")
            if details:
                self.print_info(details)
            return 1
        except (urllib.error.URLError, TimeoutError) as exc:
            self.print_error(f"WiFi OTA failed: {exc}")
            return 1
    
    async def cmd_build_upload(self, args: argparse.Namespace) -> int:
        """Build firmware and upload via selected transport."""
        if not getattr(args, "skip_safety_checks", False):
            safety_result = self.cmd_safety_check(argparse.Namespace(skip_preview=False, skip_web_ui=False))
            if safety_result != 0:
                return safety_result

        build_result = self.cmd_build(args)
        if build_result != 0:
            return build_result
        
        # Set firmware to None so cmd_upload will auto-detect the latest firmware
        args.firmware = None
        return await self.cmd_upload(args)
    
    async def cmd_export(self, args: argparse.Namespace) -> int:
        """Export grind data from device."""
        self.print_header("Exporting Grind Data")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "export"]
        
        if hasattr(args, 'db') and args.db:
            cmd.extend(["--db", args.db])
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_analyze(self, args: argparse.Namespace) -> int:
        """Export data and launch Streamlit report."""
        self.print_header("Data Analysis Workflow")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "analyse"]
        
        if hasattr(args, 'db') and args.db:
            cmd.extend(["--db", args.db])
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    def cmd_report(self, args: argparse.Namespace) -> int:
        """Launch Streamlit report from existing data."""
        self.print_header("Launching Streamlit Report")
        
        # Determine database file
        db_file = self.db_path
        if hasattr(args, 'db') and args.db:
            if Path(args.db).is_absolute():
                db_file = Path(args.db)
            else:
                db_file = self.script_dir / "database" / args.db
        
        if not db_file.exists():
            self.print_error(f"Database file not found: {db_file}")
            self.print_info("Run: python3 grinder.py export")
            return 1
        
        if not self.check_venv():
            return 1
        
        # Check if streamlit exists in venv
        if not self.venv_streamlit.exists():
            self.print_warning("Streamlit not found, installing...")
            result = subprocess.run([
                str(self.venv_pip), "install", "streamlit>=1.28.0", "plotly>=5.15.0"
            ], capture_output=True, text=True)
            
            if result.returncode != 0:
                self.print_error(f"Failed to install Streamlit: {result.stderr}")
                return 1
        
        self.print_info(f"Using database: {db_file}")
        self.print_info("Opening at: http://localhost:8501")
        self.print_info("Press Ctrl+C to stop the server")
        
        # Set environment variables and launch streamlit
        env = os.environ.copy()
        env["GRIND_DB_PATH"] = str(db_file)
        env["PYTHONPATH"] = str(self.streamlit_dir)
        
        try:
            result = subprocess.run([
                str(self.venv_python), "-m", "streamlit", "run", "grind_report.py"
            ], cwd=self.streamlit_dir, env=env)
            return result.returncode
        except KeyboardInterrupt:
            self.print_info("Streamlit server stopped")
            return 0
    
    async def cmd_scan(self, args: argparse.Namespace) -> int:
        """Scan for BLE devices."""
        self.print_header("Scanning for BLE Devices")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "scan"]
        return await self.run_async_command(cmd)
    
    async def cmd_connect(self, args: argparse.Namespace) -> int:
        """Connect to grinder device."""
        self.print_header("Connecting to Grinder")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "connect"]
        
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_debug(self, args: argparse.Namespace) -> int:
        """Stream live debug logs from device."""
        self.print_header("Debug Monitor")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "debug"]
        
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_info(self, args: argparse.Namespace) -> int:
        """Get device system information."""
        self.print_header("Device System Information")

        if not self.check_venv():
            return 1

        cmd = [str(self.venv_python), str(self.ble_tool), "info"]

        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])

        return await self.run_async_command(cmd)

    def cmd_recover_wifi(self, args: argparse.Namespace) -> int:
        """Find or revive the WiFi HTTP endpoint using LAN discovery and optional USB reset."""
        self.print_header("WiFi Recovery")

        if not self.check_venv():
            return 1

        host = getattr(args, "host", self.default_wifi_host)
        usb_port = getattr(args, "usb_port", None)
        scan_only = getattr(args, "scan_only", False)

        if scan_only:
            discovered = self.discover_wifi_device()
            if not discovered:
                self.print_error("No GrindByWeight WiFi endpoint found")
                return 1
            base_url, status = discovered
        else:
            base_url = self.resolve_wifi_base_url(host, recover_usb=True, usb_port=usb_port)
            if not base_url:
                self.print_error("Could not recover the WiFi HTTP endpoint")
                return 1
            try:
                status = self.preflight_wifi_device(base_url)
            except Exception as exc:
                self.print_error(f"Recovered endpoint failed HTTP preflight: {exc}")
                return 1

        self.save_cached_wifi_host(base_url, status)
        self.print_success(
            f"WiFi ready: {base_url} "
            f"(v{status.get('version', '?')}, build {status.get('build', '?')})"
        )
        return 0

    async def cmd_diagnostics(self, args: argparse.Namespace) -> int:
        """Get comprehensive diagnostic report."""
        self.print_header("Diagnostic Report")

        if not self.check_venv():
            return 1

        cmd = [str(self.venv_python), str(self.ble_tool), "diagnostics"]

        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])

        if hasattr(args, 'save') and args.save:
            cmd.extend(["--save", args.save])

        return await self.run_async_command(cmd)

    def cmd_install(self, args: argparse.Namespace) -> int:
        """Manually install Python dependencies."""
        self.print_header("Installing Dependencies")
        
        if self.install_dependencies():
            self.print_success("Dependencies installed")
            return 0
        else:
            return 1
    
    def cmd_clean(self, args: argparse.Namespace) -> int:
        """Clean build artifacts."""
        self.print_header("Cleaning Build Artifacts")
        
        if not self.check_venv():
            return 1
        
        # Use PlatformIO from the project venv
        result = self.run_command([
            str(self.venv_python), "-m", "platformio", "run", "--target", "clean"
        ], env=self.platformio_env())
        
        # Also remove .pio/build directory
        build_dir = self.project_dir / ".pio" / "build"
        if build_dir.exists():
            shutil.rmtree(build_dir)
        
        if result.returncode == 0:
            self.print_success("Build artifacts cleaned")
        
        return result.returncode
    
    def cmd_release(self, args: argparse.Namespace) -> int:
        """Create a tagged release using the release helper script."""
        self.print_header("Creating Tagged Release")
        
        release_script = self.script_dir / "release.py"
        
        # Make sure the release script exists
        if not release_script.exists():
            self.print_error("Release script not found!")
            self.print_info("The release.py script should be in the tools/ directory")
            return 1
        
        # Run the release script
        try:
            result = subprocess.run([sys.executable, str(release_script)])
            return result.returncode
        except Exception as e:
            self.print_error(f"Failed to run release script: {e}")
            return 1

def create_parser() -> argparse.ArgumentParser:
    """Create the argument parser with all subcommands."""
    parser = argparse.ArgumentParser(
        description="Unified Grinder Tool - All-in-one grinder operations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
{COLORS['YELLOW']}Examples:{COLORS['RESET']}
  python3 grinder.py build-upload              # Build and upload firmware
  python3 grinder.py preview                   # Capture LVGL touchscreen screenshots
  python3 grinder.py preview --interactive     # Open native SDL touchscreen preview
  python3 grinder.py preview --click-test      # Run LVGL click-dummy flow test
  python3 grinder.py build-upload --force-full # Build and force full firmware update
  python3 grinder.py recover-wifi             # Find/revive WiFi endpoint and cache its IP
  python3 grinder.py analyze                   # Export data and show interactive report
  python3 grinder.py report                    # Just show report from existing data
  python3 grinder.py export --db session1.db  # Export to custom database
  python3 grinder.py upload --device MyGrinder # Upload to specific device
  python3 grinder.py connect                   # Connect to grinder device
  python3 grinder.py info                      # Get device system information
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', required=True, help='Available commands')
    
    # Build & Upload Commands
    build_parser = subparsers.add_parser('build', help='Build firmware using PlatformIO')
    build_rescue_parser = subparsers.add_parser('build-rescue', help='Build lightweight rescue OTA firmware')

    preview_parser = subparsers.add_parser('preview', help='Build and run the native LVGL touchscreen preview')
    preview_parser.add_argument('scenes', nargs='*', help='Preview scenes to capture/open (default: ready list feedback)')
    preview_parser.add_argument('--interactive', action='store_true', help='Open the SDL preview window instead of screenshots')
    preview_parser.add_argument('--click-test', action='store_true', help='Run scripted LVGL click-dummy navigation checks before screenshots')
    preview_parser.add_argument('--output-dir', help='Screenshot output directory (default: .pio/preview)')
    
    upload_parser = subparsers.add_parser('upload', help='Upload firmware via WiFi or BLE OTA')
    upload_parser.add_argument('firmware', nargs='?', help='Path to firmware .bin file (finds latest if not specified)')
    upload_parser.add_argument('--transport', choices=['wifi', 'ble'], default='wifi', help='OTA transport (default: wifi)')
    upload_parser.add_argument('--host', default='grindbyweight.local', help='WiFi device host/IP for --transport wifi')
    upload_parser.add_argument('--force-full', action='store_true', help='Force full firmware update (skip delta patching)')
    upload_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    upload_parser.add_argument('--skip-safety-checks', action='store_true', help='Bypass local source guard checks before WiFi OTA')
    upload_parser.add_argument('--usb-port', help='USB serial port to use if WiFi discovery needs recovery')
    upload_parser.add_argument('--no-usb-recover', action='store_true', help='Do not reset over USB if WiFi discovery fails')
    
    build_upload_parser = subparsers.add_parser('build-upload', help='Build firmware and upload via WiFi or BLE')
    build_upload_parser.add_argument('--transport', choices=['wifi', 'ble'], default='wifi', help='OTA transport (default: wifi)')
    build_upload_parser.add_argument('--host', default='grindbyweight.local', help='WiFi device host/IP for --transport wifi')
    build_upload_parser.add_argument('--force-full', action='store_true', help='Force full firmware update (skip delta patching)')
    build_upload_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    build_upload_parser.add_argument('--skip-safety-checks', action='store_true', help='Bypass local safety checks before build/upload')
    build_upload_parser.add_argument('--usb-port', help='USB serial port to use if WiFi discovery needs recovery')
    build_upload_parser.add_argument('--no-usb-recover', action='store_true', help='Do not reset over USB if WiFi discovery fails')

    safety_parser = subparsers.add_parser('safety-check', help='Run local release safety checks before remote-only deployment')
    safety_parser.add_argument('--skip-preview', action='store_true', help='Skip LVGL SDL click-dummy preview')
    safety_parser.add_argument('--skip-web-ui', action='store_true', help='Skip embedded web UI harness')

    recover_wifi_parser = subparsers.add_parser('recover-wifi', help='Find/revive the WiFi HTTP endpoint and cache its IP')
    recover_wifi_parser.add_argument('--host', default='grindbyweight.local', help='Preferred WiFi host/IP before scanning')
    recover_wifi_parser.add_argument('--usb-port', help='USB serial port to reset/read if WiFi is not reachable')
    recover_wifi_parser.add_argument('--scan-only', action='store_true', help='Only scan LAN; do not reset over USB')
    
    # Data & Analysis Commands
    export_parser = subparsers.add_parser('export', help='Export grind data from device to database')
    export_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    export_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    analyze_parser = subparsers.add_parser('analyze', help='Export data and launch Streamlit report')
    analyze_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    analyze_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    report_parser = subparsers.add_parser('report', help='Launch Streamlit report (no data export)')
    report_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    
    analyze_offline_parser = subparsers.add_parser('analyze-offline', help='Alias for report - uses existing database')
    analyze_offline_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    
    # BLE Commands
    scan_parser = subparsers.add_parser('scan', help='Scan for BLE devices')
    
    connect_parser = subparsers.add_parser('connect', help='Connect to grinder device')
    connect_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    debug_parser = subparsers.add_parser('debug', help='Stream live debug logs from device')
    debug_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    info_parser = subparsers.add_parser('info', help='Get comprehensive device system information')
    info_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')

    diagnostics_parser = subparsers.add_parser('diagnostics', help='Get comprehensive diagnostic report for GitHub issues')
    diagnostics_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    diagnostics_parser.add_argument('--save', metavar='FILE', help='Save report to file (default: print to console)')

    # Development Commands
    install_parser = subparsers.add_parser('install', help='Manually install Python dependencies (auto-setup when needed)')
    monitor_parser = subparsers.add_parser('monitor', help='Monitor live debug output via BLE on legacy firmware (alias for debug)')
    monitor_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    clean_parser = subparsers.add_parser('clean', help='Clean build artifacts')
    release_parser = subparsers.add_parser('release', help='Create tagged release (triggers automated GitHub release)')
    
    return parser

async def main():
    """Main entry point."""
    parser = create_parser()
    args = parser.parse_args()
    
    tool = GrinderTool()
    
    try:
        # Map commands to methods
        if args.command == 'build':
            return tool.cmd_build(args)
        elif args.command == 'build-rescue':
            return tool.cmd_build_rescue(args)
        elif args.command == 'preview':
            return tool.cmd_preview(args)
        elif args.command == 'upload':
            return await tool.cmd_upload(args)
        elif args.command == 'build-upload':
            return await tool.cmd_build_upload(args)
        elif args.command == 'safety-check':
            return tool.cmd_safety_check(args)
        elif args.command == 'recover-wifi':
            return tool.cmd_recover_wifi(args)
        elif args.command == 'export':
            return await tool.cmd_export(args)
        elif args.command in ['analyze', 'analyse']:
            return await tool.cmd_analyze(args)
        elif args.command == 'report':
            return tool.cmd_report(args)
        elif args.command in ['analyze-offline', 'analyse-offline']:
            return tool.cmd_report(args)  # Same as report
        elif args.command == 'scan':
            return await tool.cmd_scan(args)
        elif args.command == 'connect':
            return await tool.cmd_connect(args)
        elif args.command in ['debug', 'monitor']:
            return await tool.cmd_debug(args)
        elif args.command == 'info':
            return await tool.cmd_info(args)
        elif args.command == 'diagnostics':
            return await tool.cmd_diagnostics(args)
        elif args.command == 'install':
            return tool.cmd_install(args)
        elif args.command == 'clean':
            return tool.cmd_clean(args)
        elif args.command == 'release':
            return tool.cmd_release(args)
        else:
            tool.print_error(f"Unknown command: {args.command}")
            parser.print_help()
            return 1
            
    except KeyboardInterrupt:
        tool.print_info("Interrupted by user")
        return 1
    except Exception as e:
        tool.print_error(f"Unexpected error: {e}")
        return 1

if __name__ == "__main__":
    if platform.system() == "Windows":
        # On Windows, set the event loop policy to avoid issues
        asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())
    
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except Exception as e:
        print(f"{COLORS['RED']}An unexpected error occurred: {e}{COLORS['RESET']}")
        sys.exit(1)
