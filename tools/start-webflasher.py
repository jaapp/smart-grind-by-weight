#!/usr/bin/env python3
"""
Start Web Flasher - Local development server for the web flasher tool
Automatically handles port conflicts and starts the server
"""

import argparse
import os
import sys
import signal
import subprocess
import socket
from pathlib import Path
import http.server
import socketserver

DEFAULT_PORT = 8000

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

    def log_message(self, format, *args):
        """Override to show cleaner logs."""
        # Only log actual requests, not every resource
        if 'GET' in format or 'POST' in format:
            client = self.address_string()
            sys.stdout.write(f"[{self.log_date_time_string()}] {client} - {format % args}\n")
            sys.stdout.flush()

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
