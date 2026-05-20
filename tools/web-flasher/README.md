# Smart Grind By Weight - Web Flasher

A browser-based firmware flashing tool for the Smart Grind By Weight ESP32 coffee grinder.

## Features

### 🔌 Initial Setup (USB)
- First-time firmware installation via ESP Web Tools
- Uses Web Serial API for direct USB connection
- Perfect for factory setup or recovery
- Powered by [ESP Web Tools](https://esphome.github.io/esp-web-tools/) for browser-based flashing

### 📶 OTA Updates (WiFi)
- Over-the-air updates for installed grinders
- HTTP upload to the grinder's built-in WiFi update server
- Full firmware updates
- Progress tracking and status updates
- Legacy BLE OTA remains available from the transport dropdown for older firmware

## Browser Support

- ✅ **Chrome** (Desktop & Android) - Full support
- ✅ **Microsoft Edge** (Desktop) - Full support  
- ⚠️ **Firefox** - WiFi OTA can work locally, but USB flashing and BLE fallback are not supported
- ❌ **Safari/iOS** - USB flashing and BLE fallback are not supported

## Usage

### For Initial Setup
1. Open the web flasher in Chrome/Edge
2. Go to "Initial Setup (USB)" tab
3. Enter firmware URL from GitHub release
4. Click "Flash via USB" - opens ESP Web Tools
5. Connect device via USB and flash
6. After first boot, join the `GrindByWeight-Setup` WiFi network and open `http://192.168.4.1` to save your home WiFi credentials

### For OTA Updates
1. Ensure grinder is powered and connected to WiFi
2. Go to "OTA Update" tab
3. Select `WiFi` transport
4. Use `http://grindbyweight.local` or the device IP address
5. Click "Flash Firmware over WiFi"

## Firmware Sources

The firmware list is pulled straight from GitHub Releases—no files are stored in this repo. If you need the exact asset mapping, see [DOC.md](../../docs/DOC.md).

## Technical Details

### WiFi Endpoints
- **Status**: `GET /status`
- **Setup**: `GET /` and `POST /wifi`
- **OTA**: `POST /ota` with multipart `firmware` file and optional `version`

### Protocol
- Uses the ESP32 Arduino WiFi/WebServer stack
- Writes firmware directly to the next OTA app partition
- Reboots automatically after a successful upload

## Development

The web flasher is automatically deployed via GitHub Pages when pushed to main branch.

### Local Testing

**Quick Start (Recommended):**
```bash
# From the tools directory
python3 start-webflasher.py

# If port 8000 is busy, you'll be prompted to kill the process
# Use a custom port
python3 start-webflasher.py --port 3000
```

The script will automatically:
- Check if the port is available
- Prompt to kill any conflicting process
- Start the server and display the URL
- Handle cleanup on exit

**Manual Start:**
```bash
# Serve locally for WiFi OTA
python3 -m http.server 8000 --directory tools/web-flasher
# Open http://localhost:8000
```

**Note:** Run the flasher locally for WiFi OTA. Browsers may block HTTPS pages from posting firmware to a local HTTP device.

## Security

- WiFi OTA runs on the local network
- Firmware is downloaded directly from GitHub releases
- No credentials or keys stored locally
