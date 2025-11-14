# Release Guide

This project uses automated releases triggered by Git tags. Here's how it works:

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
2. Let you choose version increment type (patch/minor/major/RC/custom)
3. Update firmware version in `src/config/build_info.h` and commit the change
4. Create annotated git tag
5. Push both the commit and tag to trigger automated GitHub Actions


### What Happens Automatically

Once you push a tag (like `v1.0.0`):

1. **GitHub Actions builds the firmware**
   - Compiles for the ESP32-S3 target with the committed version
   - Creates release artifacts (.bin files and packages)
   - Generates OTA patches with heatshrink compression

2. **Updates Web Flasher**
   - Generates the ESP Web Tools manifest alongside bootloader, partition, OTA, and blank NVS images
   - Uploads each artifact to the GitHub Release
   - The hosted web flasher queries GitHub releases directly, so no binary files are committed back to `main`

3. **Creates GitHub Release (as draft)**
   - Includes automatic changelog from commits since last tag
   - Uploads firmware binaries for download
   - Marks as pre-release if tag contains `beta`, `alpha`, or `rc`
   - **You must manually publish the draft release**

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
