#!/usr/bin/env python3
"""
Start Web Flasher - Local development server for the web flasher tool
Automatically handles port conflicts and starts the server
"""

import argparse
import json
import os
import re
import sys
import signal
import subprocess
import socket
from pathlib import Path
import http.server
import socketserver
from urllib.parse import unquote, urlparse

DEFAULT_PORT = 8000
ENV_NAME = "waveshare-esp32s3-touch-amoled-164"
FIRMWARE_OFFSET = 0x320000

def is_port_in_use(port):
    """Check if a port is already in use."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(('', port))
            return False
        except OSError:
            return True

def get_process_using_port(port):
    """Get the PID of the process using the specified port."""
    try:
        # macOS/Linux
        result = subprocess.run(
            ['lsof', '-ti', f':{port}'],
            capture_output=True,
            text=True,
            timeout=2
        )
        if result.returncode == 0 and result.stdout.strip():
            return int(result.stdout.strip().split('\n')[0])
    except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
        pass

    return None

def kill_process_on_port(port):
    """Kill the process using the specified port."""
    pid = get_process_using_port(port)
    if pid:
        try:
            print(f"[INFO] Killing process {pid} using port {port}")
            os.kill(pid, signal.SIGTERM)

            # Wait for process to actually die (up to 3 seconds)
            import time
            for i in range(30):
                time.sleep(0.1)
                if not is_port_in_use(port):
                    print(f"[OK] Port {port} is now free")
                    return True

            # If SIGTERM didn't work, try SIGKILL
            print(f"[WARNING] Process didn't respond to SIGTERM, trying SIGKILL")
            os.kill(pid, signal.SIGKILL)
            time.sleep(0.5)
            return True

        except ProcessLookupError:
            print(f"[WARNING] Process {pid} not found")
            return False
        except PermissionError:
            print(f"[ERROR] Permission denied to kill process {pid}")
            return False
    return False

class QuietHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP request handler with reduced logging."""

    project_dir = Path(__file__).resolve().parent.parent
    webflasher_dir = Path(__file__).resolve().parent / "web-flasher"
    build_dir = project_dir / ".pio" / "build" / ENV_NAME

    def log_message(self, format, *args):
        """Override to show cleaner logs."""
        # Only log actual requests, not every resource
        if 'GET' in format or 'POST' in format:
            client = self.address_string()
            sys.stdout.write(f"[{self.log_date_time_string()}] {client} - {format % args}\n")
            sys.stdout.flush()

    def do_GET(self):
        """Serve static flasher files plus generated local firmware metadata."""
        path = unquote(urlparse(self.path).path)

        if path == "/firmware/index.json":
            self.serve_firmware_index()
            return
        if path == "/firmware/local.manifest.json":
            self.serve_local_manifest()
            return
        if path == "/firmware/local/firmware.bin":
            self.serve_file(self.get_local_firmware_path())
            return
        if path == "/firmware/local/bootloader.bin":
            self.serve_file(self.build_dir / "bootloader.bin")
            return
        if path == "/firmware/local/partitions.bin":
            self.serve_file(self.build_dir / "partitions.bin")
            return

        super().do_GET()

    def read_define(self, path, name):
        """Read a simple C preprocessor define value from a file."""
        try:
            content = path.read_text(encoding="utf-8")
        except OSError:
            return None

        quoted_match = re.search(rf"#define\s+{re.escape(name)}\s+\"([^\"]*)\"", content)
        if quoted_match:
            return quoted_match.group(1)

        match = re.search(rf"#define\s+{re.escape(name)}\s+(.+)", content)
        if not match:
            return None

        value = match.group(1).split("//", 1)[0].strip()
        if value.startswith('"') and value.endswith('"'):
            return value[1:-1]
        return value

    def get_build_number(self):
        value = self.read_define(self.project_dir / "include" / "git_info.h", "BUILD_NUMBER")
        return value or "local"

    def get_firmware_version(self):
        value = self.read_define(self.project_dir / "src" / "config" / "build_info.h", "BUILD_FIRMWARE_VERSION")
        return value or "local"

    def get_local_firmware_path(self):
        firmware_path = self.build_dir / "firmware.bin"
        if firmware_path.exists():
            return firmware_path

        cached = sorted((self.project_dir / "firmware_cache").glob("build_*.bin"))
        return cached[-1] if cached else firmware_path

    def get_local_index_entry(self):
        firmware_path = self.get_local_firmware_path()
        if not firmware_path.exists():
            return None

        build_number = self.get_build_number()
        version = self.get_firmware_version()
        display = f"Local Build #{build_number}"
        manifest_path = self.build_dir / "bootloader.bin"
        partitions_path = self.build_dir / "partitions.bin"
        has_usb_parts = manifest_path.exists() and partitions_path.exists()

        return {
            "tag": f"local-build-{build_number}",
            "version": version,
            "display": display,
            "prerelease": False,
            "local": True,
            "manifest": "firmware/local.manifest.json" if has_usb_parts else None,
            "ota": "firmware/local/firmware.bin",
        }

    def serve_json(self, status, payload):
        body = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def serve_firmware_index(self):
        entries = []
        local_entry = self.get_local_index_entry()
        if local_entry:
            entries.append(local_entry)

        static_index = self.webflasher_dir / "firmware" / "index.json"
        if static_index.exists():
            try:
                static_entries = json.loads(static_index.read_text(encoding="utf-8"))
                entries.extend(static_entries)
            except (OSError, json.JSONDecodeError):
                pass

        self.serve_json(200, entries)

    def serve_local_manifest(self):
        local_entry = self.get_local_index_entry()
        if not local_entry or not local_entry.get("manifest"):
            self.send_error(404, "Local firmware build artifacts not found. Run python3 tools/grinder.py build first.")
            return

        version = local_entry["version"]
        manifest = {
            "name": "Smart Grind By Weight",
            "version": version,
            "home_assistant_domain": "grinder",
            "new_install_skip_erase": True,
            "builds": [
                {
                    "chipFamily": "ESP32-S3",
                    "parts": [
                        {"path": "local/bootloader.bin", "offset": 0},
                        {"path": "local/partitions.bin", "offset": 0x8000},
                        {"path": "blank_8KB.bin", "offset": 0xE000},
                        {"path": "local/firmware.bin", "offset": FIRMWARE_OFFSET},
                    ],
                }
            ],
        }
        self.serve_json(200, manifest)

    def serve_file(self, path):
        if not path or not path.exists() or not path.is_file():
            self.send_error(404, "File not found")
            return

        try:
            with path.open("rb") as file:
                data = file.read()
        except OSError:
            self.send_error(500, "Could not read file")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

def start_server(port, directory):
    """Start the HTTP server."""
    os.chdir(directory)

    # Use a custom handler with cleaner output
    handler = QuietHTTPRequestHandler

    # Allow address reuse to avoid "Address already in use" errors
    socketserver.TCPServer.allow_reuse_address = True

    try:
        with socketserver.TCPServer(("", port), handler) as httpd:
            print(f"\n{'='*60}")
            print(f"[OK] Web Flasher Server Running")
            print(f"{'='*60}")
            print(f"   URL:  http://localhost:{port}/")
            print(f"   Dir:  {directory}")
            print(f"\n   Press Ctrl+C to stop the server")
            print(f"{'='*60}\n")

            try:
                httpd.serve_forever()
            except KeyboardInterrupt:
                print(f"\n\n[INFO] Server stopped by user")
                sys.exit(0)

    except OSError as e:
        print(f"[ERROR] Could not start server: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description="Start the Web Flasher local development server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 start-webflasher.py              # Start on port 8000 (prompts if port busy)
  python3 start-webflasher.py --port 3000  # Start on custom port
        """
    )

    parser.add_argument(
        '--port',
        type=int,
        default=DEFAULT_PORT,
        help=f'Port to run the server on (default: {DEFAULT_PORT})'
    )

    args = parser.parse_args()

    # Determine web-flasher directory
    script_dir = Path(__file__).parent
    webflasher_dir = script_dir / "web-flasher"

    if not webflasher_dir.exists():
        print(f"[ERROR] Web flasher directory not found: {webflasher_dir}")
        sys.exit(1)

    if not (webflasher_dir / "index.html").exists():
        print(f"[ERROR] index.html not found in: {webflasher_dir}")
        sys.exit(1)

    # Check if port is in use
    if is_port_in_use(args.port):
        pid = get_process_using_port(args.port)
        pid_info = f" (PID: {pid})" if pid else ""

        # Prompt user to kill the process
        print(f"[WARNING] Port {args.port} is already in use{pid_info}")
        try:
            response = input(f"Kill the process and start server? [y/N]: ").strip().lower()
            if response in ['y', 'yes']:
                if not kill_process_on_port(args.port):
                    print(f"[ERROR] Could not free up port {args.port}")
                    sys.exit(1)

                # Double-check the port is now free
                if is_port_in_use(args.port):
                    print(f"[ERROR] Port {args.port} still in use after attempting to kill process")
                    sys.exit(1)
            else:
                print(f"[INFO] Cancelled. You can manually kill the process using: lsof -ti :{args.port} | xargs kill")
                sys.exit(0)
        except (KeyboardInterrupt, EOFError):
            print(f"\n[INFO] Cancelled by user")
            sys.exit(0)

    # Start the server
    start_server(args.port, webflasher_dir)

if __name__ == "__main__":
    main()
