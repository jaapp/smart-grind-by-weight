// Smart Grind By Weight - Web Bluetooth Flasher
// Based on your existing Python BLE implementation

// GitHub release metadata
const REPO_OWNER = 'jaapp';
const REPO_NAME = 'smart-grind-by-weight';
const RELEASES_API_URL = `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases?per_page=50`;
const PAGES_FIRMWARE_BASE = 'https://jaapp.github.io/smart-grind-by-weight/firmware/';

function buildAssetInfo(asset) {
    if (!asset) {
        return null;
    }

    return {
        ...asset,
        downloadUrl: asset.browser_download_url,
        apiUrl: asset.url,
        size: asset.size,
    };
}

function getAssetType(asset) {
    if (!asset || !asset.label) {
        return null;
    }

    const match = asset.label.toLowerCase().match(/type\s*[:=]\s*([a-z0-9_-]+)/);
    return match ? match[1] : null;
}

function isMainFirmwareAsset(asset) {
    return Boolean(asset?.name?.endsWith('.bin')) &&
        asset.name.startsWith('smart-grind-by-weight') &&
        !asset.name.includes('-bootloader') &&
        !asset.name.includes('-partitions') &&
        !asset.name.includes('-web-ota');
}

function isManifestAsset(asset) {
    return Boolean(asset?.name?.endsWith('.manifest.json')) && asset.name.startsWith('smart-grind-by-weight');
}

function isOtaAsset(asset) {
    return Boolean(asset?.name?.includes('-web-ota.bin'));
}

function isBootloaderAsset(asset) {
    return Boolean(asset?.name?.includes('-bootloader.bin'));
}

function isPartitionsAsset(asset) {
    return Boolean(asset?.name?.includes('-partitions.bin'));
}

function isBlankAsset(asset) {
    return asset?.name === 'blank_8KB.bin';
}

function normalizeRelease(release) {
    if (!release || !Array.isArray(release.assets)) {
        return null;
    }

    const assetsByType = {};
    for (const asset of release.assets) {
        const type = getAssetType(asset);
        if (type && !assetsByType[type]) {
            assetsByType[type] = buildAssetInfo(asset);
        }
    }

    let manifest = assetsByType.manifest;
    let firmware = assetsByType.firmware;
    let ota = assetsByType.ota || null;
    let bootloader = assetsByType.bootloader || null;
    let partitions = assetsByType.partitions || null;
    let blankNvs = assetsByType.blank_nvs || null;

    if (!manifest) {
        const fallbackManifest = release.assets.find(isManifestAsset);
        if (fallbackManifest) {
            manifest = buildAssetInfo(fallbackManifest);
        }
    }

    if (!firmware) {
        const fallbackFirmware = release.assets.find(isMainFirmwareAsset);
        if (fallbackFirmware) {
            firmware = buildAssetInfo(fallbackFirmware);
        }
    }

    if (!ota) {
        const fallbackOta = release.assets.find(isOtaAsset);
        if (fallbackOta) {
            ota = buildAssetInfo(fallbackOta);
        }
    }

    if (!bootloader) {
        const fallbackBootloader = release.assets.find(isBootloaderAsset);
        if (fallbackBootloader) {
            bootloader = buildAssetInfo(fallbackBootloader);
        }
    }

    if (!partitions) {
        const fallbackPartitions = release.assets.find(isPartitionsAsset);
        if (fallbackPartitions) {
            partitions = buildAssetInfo(fallbackPartitions);
        }
    }

    if (!blankNvs) {
        const fallbackBlank = release.assets.find(isBlankAsset);
        if (fallbackBlank) {
            blankNvs = buildAssetInfo(fallbackBlank);
        }
    }

    if (!manifest || !firmware) {
        return null;
    }

    const rawTag = release.tag_name || '';
    let version = rawTag.replace(/^v/, '');

    if (!version && manifest.name) {
        const manifestMatch = manifest.name.match(/smart-grind-by-weight-v(.+)\.manifest\.json$/i);
        if (manifestMatch) {
            version = manifestMatch[1];
        }
    }

    if (!version && firmware.name) {
        const firmwareMatch = firmware.name.match(/smart-grind-by-weight-v(.+)\.bin$/i);
        if (firmwareMatch) {
            version = firmwareMatch[1];
        }
    }

    const labelVersion = rawTag || (version ? `v${version}` : 'Unknown');
    const baseLabel = labelVersion.startsWith('v') ? labelVersion : `v${labelVersion}`;
    const tag = rawTag || baseLabel;
    const encodedTag = encodeURIComponent(tag);
    const pagesBase = `${PAGES_FIRMWARE_BASE}${encodedTag}/`;

    const manifestPagesUrl = manifest ? `${pagesBase}${manifest.name}` : null;
    const otaPagesUrl = ota ? `${pagesBase}${ota.name}` : null;
    const firmwarePagesUrl = firmware ? `${pagesBase}${firmware.name}` : null;
    const bootloaderPagesUrl = bootloader ? `${pagesBase}${bootloader.name}` : null;
    const partitionsPagesUrl = partitions ? `${pagesBase}${partitions.name}` : null;
    const blankPagesUrl = blankNvs ? `${pagesBase}${blankNvs.name}` : null;

    return {
        id: release.id,
        tag,
        version,
        prerelease: Boolean(release.prerelease),
        publishedAt: release.published_at || release.created_at || null,
        manifest: manifest ? { ...manifest, pagesUrl: manifestPagesUrl } : null,
        firmware: firmware ? { ...firmware, pagesUrl: firmwarePagesUrl } : null,
        ota: ota ? { ...ota, pagesUrl: otaPagesUrl } : null,
        bootloader: bootloader ? { ...bootloader, pagesUrl: bootloaderPagesUrl } : null,
        partitions: partitions ? { ...partitions, pagesUrl: partitionsPagesUrl } : null,
        blankNvs: blankNvs ? { ...blankNvs, pagesUrl: blankPagesUrl } : null,
        pagesBase,
        displayLabel: baseLabel,
    };
}

// BLE Service UUIDs (from your bluetooth.h config)
const BLE_OTA_SERVICE_UUID = '12345678-1234-1234-1234-123456789abc';
const BLE_OTA_DATA_CHAR_UUID = '87654321-4321-4321-4321-cba987654321';
const BLE_OTA_CONTROL_CHAR_UUID = '11111111-2222-3333-4444-555555555555';
const BLE_OTA_STATUS_CHAR_UUID = 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee';
const BLE_OTA_BUILD_NUMBER_CHAR_UUID = '66666666-7777-8888-9999-000000000000';

// Commands and status codes (from your Python implementation)
const BLE_OTA_CMD_START = 0x01;
const BLE_OTA_CMD_END = 0x03;
const BLE_OTA_CMD_ABORT = 0x04;

const BLE_OTA_IDLE = 0x00;
const BLE_OTA_READY = 0x01;
const BLE_OTA_RECEIVING = 0x02;
const BLE_OTA_SUCCESS = 0x03;
const BLE_OTA_ERROR = 0x04;

const DEVICE_NAME = 'GrindByWeight';
const CHUNK_SIZE = 512; // Browser BLE limit - cannot exceed 512 bytes per write

// Global state
let device = null;
let server = null;
let otaService = null;
let currentOtaStatus = BLE_OTA_IDLE;
let statusCharacteristic = null;
let cachedReleaseMetadata = null;

// Browser support check and load releases
window.addEventListener('load', () => {
    if (!('bluetooth' in navigator)) {
        document.getElementById('browserWarning').style.display = 'block';
        // Disable OTA tab if no Web Bluetooth
        const otaTab = document.querySelector('[onclick="showTab(\'ota\')"]');
        if (otaTab) {
            otaTab.disabled = true;
            otaTab.style.opacity = '0.5';
        }
    }
    
    // Load available releases
    loadReleases();
});

// Tab switching
function showTab(tabName) {
    // Hide all tab contents
    const tabContents = document.querySelectorAll('.tab-content');
    tabContents.forEach(content => {
        content.classList.remove('active');
    });
    
    // Remove active class from all tabs
    const tabs = document.querySelectorAll('.tab');
    tabs.forEach(tab => {
        tab.classList.remove('active');
    });
    
    // Show selected tab content
    document.getElementById(tabName + 'Tab').classList.add('active');
    
    // Add active class to selected tab
    event.target.classList.add('active');
}

// Status update functions
function updateStatus(message, type = 'info') {
    const statusDiv = document.getElementById('status');
    statusDiv.textContent = message;
    statusDiv.className = `status ${type}`;
    statusDiv.style.display = 'block';
    console.log(`[${type.toUpperCase()}] ${message}`);
}

function updateProgress(percent) {
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');
    
    if (percent > 0) {
        progressContainer.style.display = 'block';
        progressBar.style.width = percent + '%';
    } else {
        progressContainer.style.display = 'none';
    }
}

// Utility functions
function arrayBufferToHex(buffer) {
    return Array.from(new Uint8Array(buffer))
        .map(b => b.toString(16).padStart(2, '0'))
        .join('');
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// Firmware download
async function downloadFirmware(primaryUrl, fallbackUrls = []) {
    const sources = [primaryUrl, ...fallbackUrls.filter(Boolean)];
    let lastError = null;

    for (const source of sources) {
        if (!source) {
            continue;
        }

        updateStatus(`Downloading firmware from ${source}`, 'info');
        updateProgress(0);

        try {
            const response = await fetch(source);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const contentLength = parseInt(response.headers.get('content-length'));
            const reader = response.body.getReader();
            const chunks = [];
            let receivedBytes = 0;

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;

                chunks.push(value);
                receivedBytes += value.length;

                if (contentLength) {
                    const progress = (receivedBytes / contentLength) * 100;
                    updateProgress(progress);
                }
            }

            const firmware = new Uint8Array(receivedBytes);
            let offset = 0;
            for (const chunk of chunks) {
                firmware.set(chunk, offset);
                offset += chunk.length;
            }

            updateStatus(`Downloaded ${Math.round(firmware.length / 1024)}KB firmware`, 'success');
            return firmware;
        } catch (error) {
            console.error('Firmware download attempt failed:', source, error);
            lastError = error;
        }
    }

    const errorMessage = lastError ? lastError.message : 'Unknown download error';
    updateStatus(`Download failed: ${errorMessage}`, 'error');
    throw lastError || new Error(errorMessage);
}

// BLE Connection
async function connectDevice() {
    if (!('bluetooth' in navigator)) {
        updateStatus('Web Bluetooth not supported in this browser', 'error');
        return false;
    }
    
    try {
        updateStatus('Scanning for device...', 'info');
        
        device = await navigator.bluetooth.requestDevice({
            filters: [{ name: DEVICE_NAME }],
            optionalServices: [BLE_OTA_SERVICE_UUID]
        });
        
        updateStatus('Connecting to device...', 'info');
        server = await device.gatt.connect();
        
        updateStatus('Getting OTA service...', 'info');
        otaService = await server.getPrimaryService(BLE_OTA_SERVICE_UUID);
        
        // Set up status notifications
        statusCharacteristic = await otaService.getCharacteristic(BLE_OTA_STATUS_CHAR_UUID);
        await statusCharacteristic.startNotifications();
        statusCharacteristic.addEventListener('characteristicvaluechanged', handleStatusUpdate);
        
        updateStatus('Connected successfully!', 'success');
        
        // Enable flash button
        document.getElementById('connectBtn').disabled = true;
        document.getElementById('flashBtn').disabled = false;
        
        return true;
        
    } catch (error) {
        updateStatus(`Connection failed: ${error.message}`, 'error');
        console.error('Connection error:', error);
        return false;
    }
}

// Status notification handler
function handleStatusUpdate(event) {
    const value = new Uint8Array(event.target.value.buffer);
    if (value.length > 0) {
        currentOtaStatus = value[0];
        console.log(`OTA Status: ${currentOtaStatus}`);
        
        switch (currentOtaStatus) {
            case BLE_OTA_READY:
                updateStatus('Device ready for firmware', 'info');
                break;
            case BLE_OTA_RECEIVING:
                updateStatus('Device receiving firmware...', 'info');
                break;
            case BLE_OTA_SUCCESS:
                updateStatus('Firmware update successful!', 'success');
                updateProgress(100);
                break;
            case BLE_OTA_ERROR:
                updateStatus('Firmware update failed', 'error');
                break;
        }
    }
}

// Wait for specific OTA status
async function waitForOtaStatus(expectedStatus, timeoutMs = 30000) {
    const startTime = Date.now();
    
    return new Promise((resolve, reject) => {
        const checkStatus = () => {
            if (currentOtaStatus === expectedStatus) {
                resolve(true);
                return;
            }
            
            if (Date.now() - startTime > timeoutMs) {
                reject(new Error(`Timeout waiting for OTA status ${expectedStatus}`));
                return;
            }
            
            setTimeout(checkStatus, 100);
        };
        
        checkStatus();
    });
}

// Extract firmware version from firmware URL
function extractVersionFromUrl(url, fallbackVersion = null) {
    if (fallbackVersion) {
        return fallbackVersion;
    }
    // Extract version directly from GitHub release URL
    const versionMatch = url.match(/\/releases\/download\/v?(\d+\.\d+\.\d+(?:-[\w\.]+)?)/);
    if (versionMatch) {
        return versionMatch[1];
    }
    return null;
}

// Web flasher always does full updates (no delta compression)
function prepareFirmwareData(firmwareData) {
    return firmwareData;
}

// Main firmware flash function
async function flashFirmware() {
    const firmwareSelect = document.getElementById('firmwareSelect');
    const flashBtn = document.getElementById('flashBtn');
    
    if (!firmwareSelect) {
        updateStatus('Firmware selection element not found', 'error');
        return;
    }
    
    const selectedOption = firmwareSelect.selectedOptions[0];
    const firmwareUrl = selectedOption ? (selectedOption.dataset?.ota || firmwareSelect.value) : firmwareSelect.value;
    const firmwareFallbacks = [];
    if (selectedOption?.dataset?.otaFallback) {
        firmwareFallbacks.push(selectedOption.dataset.otaFallback);
    }
    if (!firmwareUrl) {
        updateStatus('Please select a firmware version', 'error');
        return;
    }
    
    if (!server || !server.connected) {
        updateStatus('Not connected to device', 'error');
        return;
    }
    
    // Disable and gray out flash button
    flashBtn.disabled = true;
    
    try {
        // Download firmware
        const firmwareData = await downloadFirmware(firmwareUrl, firmwareFallbacks);
        const patchData = prepareFirmwareData(firmwareData);
        
        updateStatus('Starting firmware update...', 'info');
        updateProgress(0);
        
        // Get current device build number (optional)
        let deviceBuild = null;
        try {
            const buildChar = await otaService.getCharacteristic(BLE_OTA_BUILD_NUMBER_CHAR_UUID);
            const buildData = await buildChar.readValue();
            deviceBuild = new TextDecoder().decode(buildData).trim();
            if (deviceBuild && deviceBuild !== 'no_build_number') {
                updateStatus(`Current device build: #${deviceBuild}`, 'info');
            }
        } catch (e) {
            console.log('Could not read device build number');
        }
        
        // Extract expected version from URL
        const expectedVersion = extractVersionFromUrl(firmwareUrl, selectedOption?.dataset?.version);
        if (expectedVersion) {
            updateStatus(`Installing version: ${expectedVersion}`, 'info');
        }
        
        // Send OTA start command
        const controlChar = await otaService.getCharacteristic(BLE_OTA_CONTROL_CHAR_UUID);
        const dataChar = await otaService.getCharacteristic(BLE_OTA_DATA_CHAR_UUID);
        
        // Build start command: [CMD][patch_size:4][is_full_update:1][build_number_length:1][build_number:N][firmware_version_length:1][firmware_version:M]
        const buildNumberBytes = new TextEncoder().encode("1"); // Web flasher always sends build #1
        const versionBytes = expectedVersion ? new TextEncoder().encode(expectedVersion) : new Uint8Array(0);
        
        const startData = new ArrayBuffer(1 + 4 + 1 + 1 + buildNumberBytes.length + 1 + versionBytes.length);
        const startView = new DataView(startData);
        let offset = 0;
        
        startView.setUint8(offset, BLE_OTA_CMD_START);
        offset += 1;
        
        startView.setUint32(offset, patchData.length, true); // little-endian
        offset += 4;
        
        startView.setUint8(offset, 0); // is_full_update = 0 (use delta path with detools patch)
        offset += 1;
        
        // Always send build number "1" for web flasher
        startView.setUint8(offset, buildNumberBytes.length);
        offset += 1;
        new Uint8Array(startData, offset).set(buildNumberBytes);
        offset += buildNumberBytes.length;
        
        // Send firmware version if available
        if (expectedVersion) {
            startView.setUint8(offset, versionBytes.length);
            offset += 1;
            new Uint8Array(startData, offset).set(versionBytes);
            updateStatus(`Sending expected firmware version: ${expectedVersion}`, 'info');
        } else {
            startView.setUint8(offset, 0); // no firmware version
        }
        
        await controlChar.writeValue(startData);
        updateStatus('Sent start command, waiting for device...', 'info');
        
        // Wait for device to be ready
        await waitForOtaStatus(BLE_OTA_RECEIVING, 15000);
        
        // Send firmware data in chunks
        updateStatus('Uploading firmware...', 'info');
        const totalChunks = Math.ceil(patchData.length / CHUNK_SIZE);
        let chunkCount = 0;
        
        for (let i = 0; i < patchData.length; i += CHUNK_SIZE) {
            const chunk = patchData.slice(i, i + CHUNK_SIZE);
            await dataChar.writeValue(chunk);
            
            chunkCount++;
            // Update progress every 10 chunks (much faster UI)
            if (chunkCount % 10 === 0 || i + CHUNK_SIZE >= patchData.length) {
                const progress = Math.round(((i + chunk.length) / patchData.length) * 90); // Reserve 10% for apply phase
                updateProgress(progress);
                updateStatus(`Uploading: ${progress}%`, 'info');
            }
            
            // No delay - let browser BLE handle flow control naturally
        }
        
        updateStatus('Upload complete, applying update...', 'info');
        updateProgress(95);
        
        // Send end command
        try {
            const endCommand = new Uint8Array([BLE_OTA_CMD_END]);
            await controlChar.writeValue(endCommand);
            
            // Wait for completion or device disconnect (both are success indicators)
            try {
                await waitForOtaStatus(BLE_OTA_SUCCESS, 15000);
                updateStatus('Firmware update completed successfully!', 'success');
            } catch (statusError) {
                // Timeout or disconnect during final phase is normal - device is rebooting
                updateStatus('Firmware update completed - device rebooting', 'success');
            }
            
        } catch (endError) {
            // If END command fails, device likely already disconnected (success!)
            if (endError.message.includes('GATT') || endError.message.includes('disconnect')) {
                updateStatus('Firmware update completed - device rebooting', 'success');
            } else {
                throw endError;
            }
        }
        
        updateProgress(100);
        
        // Reset UI
        setTimeout(() => {
            document.getElementById('connectBtn').disabled = false;
            document.getElementById('flashBtn').disabled = true;
            updateProgress(0);
        }, 3000);
        
    } catch (error) {
        updateStatus(`Flash failed: ${error.message}`, 'error');
        console.error('Flash error:', error);
        
        // Re-enable flash button on error
        flashBtn.disabled = false;
        
        // Try to abort OTA on error
        try {
            if (otaService) {
                const controlChar = await otaService.getCharacteristic(BLE_OTA_CONTROL_CHAR_UUID);
                const abortCommand = new Uint8Array([BLE_OTA_CMD_ABORT]);
                await controlChar.writeValue(abortCommand);
            }
        } catch (abortError) {
            console.error('Could not send abort command:', abortError);
        }
    }
}

// Update OTA selected firmware display when dropdown changes
function updateOtaSelectedFirmware() {
    const select = document.getElementById('firmwareSelect');
    const selectedDisplay = document.getElementById('otaSelectedFile');
    const selectedOption = select.selectedOptions[0];
    const displayLabel = selectedOption?.dataset?.display || select.value;

    if (selectedOption && selectedOption.value) {
        selectedDisplay.textContent = `Selected: ${displayLabel}`;
        selectedDisplay.className = 'status info';
        selectedDisplay.style.display = 'block';
    } else {
        selectedDisplay.style.display = 'none';
    }
}

// Update manifest firmware when dropdown changes
function updateManifestFirmware() {
    const select = document.getElementById('usbFirmwareSelect');
    const selectedOption = select.selectedOptions[0];
    const manifestUrl = selectedOption?.dataset?.manifest || select.value;
    const manifestFallback = selectedOption?.dataset?.manifestFallback;
    const displayLabel = selectedOption?.dataset?.display || manifestUrl;

    if (manifestUrl) {
        console.log('Selected USB firmware:', displayLabel);
        console.log('Using manifest:', manifestUrl);

        document.getElementById('usbStatus').textContent = `Selected: ${displayLabel}`;
        document.getElementById('usbStatus').className = 'status info';
        document.getElementById('usbStatus').style.display = 'block';

        // Update the ESP Web Tools button's manifest attribute (use proxy when needed for CORS)
        const installButton = document.getElementById('usbInstallButton');
        installButton.setAttribute('manifest', manifestUrl);
        if (manifestFallback) {
            installButton.dataset.manifestFallback = manifestFallback;
        } else {
            delete installButton.dataset.manifestFallback;
        }
    } else {
        document.getElementById('usbStatus').style.display = 'none';
    }
}

// Firmware selection is now handled directly by dropdown


// Load firmware releases from GitHub API using asset labels for metadata
async function loadReleases() {
    const usbSelect = document.getElementById('usbFirmwareSelect');
    const otaSelect = document.getElementById('firmwareSelect');
    const showRC = document.getElementById('showReleaseCandidate').checked;
    const showRCOTA = document.getElementById('showReleaseCandidateOTA').checked;

    if (!usbSelect || !otaSelect) {
        console.error('Firmware select elements not found');
        return;
    }

    try {
        usbSelect.innerHTML = '<option value="">Loading firmware...</option>';
        otaSelect.innerHTML = '<option value="">Loading firmware...</option>';

        if (!Array.isArray(cachedReleaseMetadata)) {
            const response = await fetch(RELEASES_API_URL);
            if (!response.ok) {
                throw new Error(`GitHub API returned ${response.status}`);
            }

            const releases = await response.json();
            cachedReleaseMetadata = releases
                .map(normalizeRelease)
                .filter(Boolean)
                .sort((a, b) => {
                    const dateA = a.publishedAt ? new Date(a.publishedAt) : 0;
                    const dateB = b.publishedAt ? new Date(b.publishedAt) : 0;
                    return dateB - dateA;
                });
        }

        const normalized = cachedReleaseMetadata;

        usbSelect.innerHTML = '';
        otaSelect.innerHTML = '';

        normalized.forEach(release => {
            const label = release.prerelease ? `${release.displayLabel} (pre-release)` : release.displayLabel;

            if (release.manifest && (!release.prerelease || showRC)) {
                const manifestPrimary = release.manifest.pagesUrl || release.manifest.downloadUrl;
                const manifestFallback = release.manifest.downloadUrl && release.manifest.downloadUrl !== manifestPrimary
                    ? release.manifest.downloadUrl
                    : '';

                const option = document.createElement('option');
                option.value = manifestPrimary || '';
                option.textContent = label;
                option.dataset.display = label;
                option.dataset.version = release.version;
                option.dataset.releaseTag = release.tag;
                option.dataset.manifest = manifestPrimary || '';
                if (manifestFallback) {
                    option.dataset.manifestFallback = manifestFallback;
                }
                if (release.firmware?.pagesUrl) {
                    option.dataset.firmware = release.firmware.pagesUrl;
                } else if (release.firmware?.downloadUrl) {
                    option.dataset.firmware = release.firmware.downloadUrl;
                }
                option.dataset.prerelease = release.prerelease ? 'true' : 'false';
                usbSelect.appendChild(option);
            }

            if (release.ota && (!release.prerelease || showRCOTA)) {
                const otaPrimary = release.ota.pagesUrl || release.ota.downloadUrl;
                const otaFallback = release.ota.downloadUrl && release.ota.downloadUrl !== otaPrimary
                    ? release.ota.downloadUrl
                    : '';

                const option = document.createElement('option');
                option.value = otaPrimary || '';
                option.textContent = label;
                option.dataset.display = label;
                option.dataset.version = release.version;
                option.dataset.releaseTag = release.tag;
                option.dataset.ota = otaPrimary || '';
                if (otaFallback) {
                    option.dataset.otaFallback = otaFallback;
                }
                if (release.firmware?.pagesUrl) {
                    option.dataset.firmware = release.firmware.pagesUrl;
                } else if (release.firmware?.downloadUrl) {
                    option.dataset.firmware = release.firmware.downloadUrl;
                }
                option.dataset.prerelease = release.prerelease ? 'true' : 'false';
                otaSelect.appendChild(option);
            }
        });

        if (!usbSelect.children.length) {
            usbSelect.innerHTML = '<option value="">No firmware available</option>';
        } else {
            usbSelect.selectedIndex = 0;
            updateManifestFirmware();
        }

        if (!otaSelect.children.length) {
            otaSelect.innerHTML = '<option value="">No firmware available</option>';
        } else {
            otaSelect.selectedIndex = 0;
            updateOtaSelectedFirmware();
        }
    } catch (error) {
        console.error('Failed to load releases from GitHub:', error);
        usbSelect.innerHTML = '<option value="">Unable to load releases</option>';
        otaSelect.innerHTML = '<option value="">Unable to load releases</option>';
        updateStatus('Failed to load firmware list from GitHub releases. Please check your connection or try again later.', 'error');
    }
}

// Handle disconnection
window.addEventListener('beforeunload', () => {
    if (device && device.gatt.connected) {
        device.gatt.disconnect();
    }
});
