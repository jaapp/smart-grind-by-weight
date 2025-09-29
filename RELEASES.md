# Release Guide

This project uses automated releases triggered by Git tags. Here's how it works:

## For Users

### Getting Stable Releases
- Go to the [Releases page](https://github.com/jaapp/smart-grind-by-weight/releases)
- Download the latest release for stable, tested firmware
- Each release includes:
  - Compiled firmware binaries (`.bin` files)
  - Complete firmware package (`.tar.gz`)
  - Automated changelog
  - Installation instructions

### Installing Releases
```bash
# Download and extract firmware package
tar -xzf firmware-v1.0.0.tar.gz

# Upload using the grinder tool
python3 tools/grinder.py upload firmware.bin
```

## For Development

### Branch Strategy
- `main` branch: Stable releases only
- `dev` branch or feature branches: Active development
- Pull requests: Reviewed before merging to main

### Creating Releases

When you've tested your code and want to create a release:

```bash
# Use the integrated release tool (recommended)
python3 tools/grinder.py release
```

The release tool will:
1. Show you recent commits since the last release
2. Let you choose version increment type (patch/minor/major/custom)
3. **Prompt for custom release notes** with markdown support
4. **Update firmware version locally** and commit the change
5. Create annotated git tag with your release notes
6. Push both the commit and tag to trigger automated GitHub Actions

#### Writing Release Notes

The tool supports multi-line markdown release notes. Example:

```
🚀 Smart Grind v1.2.3

## New Features
- Added time-based grinding mode with split-button interface
- Improved BLE connection stability and error handling

## Bug Fixes  
- Fixed load cell calibration drift over time
- Resolved UI freezing during long grind sessions

## Breaking Changes
- Settings format changed - will reset to defaults on upgrade
```

**Tips:**
- Use `Ctrl+D` (Unix/macOS) or `Ctrl+Z+Enter` (Windows) to finish entering release notes
- Leave empty for a basic release message
- Emoji and markdown formatting are supported
- The tool shows a preview before creating the release

### What Happens Automatically

Once you push a tag (like `v1.0.0`):

1. **GitHub Actions builds the firmware**
   - Compiles for the ESP32-S3 target with the committed version
   - Creates release artifacts (.bin files and packages)

2. **Creates GitHub Release**
   - Uses your **custom release notes** from the git tag message
   - **Includes automatic changelog** from commits since last tag
   - Uploads firmware binaries for download
   - Marks as pre-release if tag contains `beta`, `alpha`, or `rc`

3. **Users get notified**
   - Release appears on GitHub releases page with your custom notes
   - Users can download and install firmware
   - Device shows correct version after update

### Version Numbering

We use semantic versioning (semver):
- `v1.0.0` - Major release (breaking changes)
- `v1.1.0` - Minor release (new features)
- `v1.0.1` - Patch release (bug fixes)
- `v1.0.0-beta.1` - Pre-release (automatically marked as pre-release)

### Pre-releases

Tags containing `beta`, `alpha`, or `rc` are automatically marked as pre-releases:
- `v1.2.0-beta.1` - Beta release
- `v1.2.0-rc.1` - Release candidate
- `v1.2.0-alpha.1` - Alpha release

This keeps experimental versions separate from stable releases.