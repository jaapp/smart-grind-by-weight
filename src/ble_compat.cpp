// =============================================================================
// ble_compat.cpp — Arduino BLE API shim over ESP-IDF 5.x NimBLE
//
// Implements the C++ wrapper classes declared in include/ble_compat.h.
//
// Lifecycle
// ---------
// 1. BLEDevice::init()         — starts NimBLE host task, waits for sync.
// 2. BLEDevice::createServer() — allocates BLEServer singleton.
// 3. server->createService()   — allocates BLEService objects.
// 4. service->createCharacteristic() — allocates BLECharacteristic objects.
// 5. service->start()          — builds NimBLE GATT table, calls ble_gatts_add_svcs().
// 6. BLEDevice::startAdvertising() — starts GAP advertising.
// 7. GAP events fire on_connect / on_disconnect via gap_event_cb().
// 8. BLEDevice::deinit()       — stops advertising, tears down NimBLE host.
// =============================================================================

#include "ble_compat.h"
#include <esp_log.h>
#include <freertos/semphr.h>
#include <cstdio>
#include <cstring>
#include <cassert>

static const char* TAG = "ble_compat";

// =============================================================================
// Static member definitions
// =============================================================================
BLEServer*      BLEServer::s_instance      = nullptr;
BLEServer*      BLEDevice::s_server        = nullptr;
BLEAdvertising* BLEDevice::s_advertising   = nullptr;
std::string     BLEDevice::s_device_name;
uint16_t        BLEDevice::s_requested_mtu = 517;
bool            BLEDevice::s_initialized   = false;
bool            BLEDevice::s_host_started  = false;

// =============================================================================
// Internal helpers
// =============================================================================

// Semaphore posted by the NimBLE sync callback so BLEDevice::init() can block
// until the stack is ready before returning to the caller.
static SemaphoreHandle_t s_sync_sem = nullptr;

// The NimBLE host task — runs nimble_port_run() until shutdown.
static void ble_host_task(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(nullptr);
}

// NimBLE calls this once the host has fully synchronised with the controller.
static void ble_on_sync(void) {
    ESP_LOGI(TAG, "NimBLE stack synced");

    // IDF NimBLE configures the device address automatically before calling
    // the sync callback — no explicit ensure_addr call needed.
    BLEDevice::on_stack_synced();

    // Unblock BLEDevice::init().
    if (s_sync_sem) {
        xSemaphoreGive(s_sync_sem);
    }
}

// NimBLE reset callback — called on fatal host error.
static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE reset; reason=%d", reason);
}

// =============================================================================
// GAP event callback — handles connect / disconnect
// =============================================================================
static int gap_event_cb(struct ble_gap_event* event, void* arg) {
    BLEServer* srv = BLEServer::getInstance();
    if (!srv) return 0;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                uint16_t h = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE connected, conn_handle=%d", h);
                srv->setConnId(h);
                if (srv->getCallbacks()) {
                    srv->getCallbacks()->onConnect(srv);
                }
            } else {
                // Connection attempt failed — restart advertising.
                ESP_LOGW(TAG, "BLE connect failed, status=%d", event->connect.status);
                BLEAdvertising::restart();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
            srv->setConnId(BLE_HS_CONN_HANDLE_NONE);
            if (srv->getCallbacks()) {
                srv->getCallbacks()->onDisconnect(srv);
            }
            // onDisconnect() typically calls start_advertising() → BLEDevice::startAdvertising()
            // which internally calls BLEAdvertising::start(). No need to restart here as well
            // because manager.cpp does it explicitly. Leave a fallback anyway:
            BLEAdvertising::restart();
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "BLE MTU update, conn=%d mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;

        default:
            break;
    }
    return 0;
}

// =============================================================================
// Shared GATT access callback — dispatched to every characteristic via arg ptr
// =============================================================================
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg) {
    auto* chr = static_cast<BLECharacteristic*>(arg);
    if (!chr) return BLE_ATT_ERR_UNLIKELY;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            std::vector<uint8_t> buf(len);
            uint16_t out_len = len;
            int rc = ble_hs_mbuf_to_flat(ctxt->om, buf.data(), len, &out_len);
            if (rc != 0) {
                ESP_LOGW(TAG, "mbuf_to_flat failed: %d", rc);
                return BLE_ATT_ERR_UNLIKELY;
            }
            chr->setValue(buf.data(), out_len);
            if (chr->getCallbacks()) {
                chr->getCallbacks()->onWrite(chr);
            }
            break;
        }

        case BLE_GATT_ACCESS_OP_READ_CHR: {
            if (chr->getCallbacks()) {
                chr->getCallbacks()->onRead(chr);
            }
            int rc = os_mbuf_append(ctxt->om, chr->getData(), chr->getLength());
            if (rc != 0) {
                ESP_LOGW(TAG, "os_mbuf_append failed: %d", rc);
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            break;
        }

        default:
            break;
    }
    return 0;
}

// =============================================================================
// BLEUUID implementation
// =============================================================================

BLEUUID::BLEUUID() {
    memset(&m_uuid, 0, sizeof(m_uuid));
    m_uuid.u.type = BLE_UUID_TYPE_128;
    m_str[0] = '\0';
}

BLEUUID::BLEUUID(uint16_t uuid16) {
    m_uuid.u16.u.type = BLE_UUID_TYPE_16;
    m_uuid.u16.value  = uuid16;
    snprintf(m_str, sizeof(m_str), "0x%04X", (unsigned)uuid16);
}

BLEUUID::BLEUUID(const char* uuid_str) {
    memset(&m_uuid, 0, sizeof(m_uuid));
    m_str[0] = '\0';

    if (!uuid_str) return;

    // Try 128-bit UUID: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    unsigned int b[16];
    // sscanf into 16 separate bytes (big-endian order as printed)
    int parsed = sscanf(uuid_str,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        &b[0],  &b[1],  &b[2],  &b[3],
        &b[4],  &b[5],
        &b[6],  &b[7],
        &b[8],  &b[9],
        &b[10], &b[11], &b[12], &b[13], &b[14], &b[15]);

    if (parsed == 16) {
        m_uuid.u128.u.type = BLE_UUID_TYPE_128;
        // NimBLE stores 128-bit UUIDs in little-endian byte order.
        for (int i = 0; i < 16; i++) {
            m_uuid.u128.value[i] = (uint8_t)b[15 - i];
        }
        snprintf(m_str, sizeof(m_str), "%s", uuid_str);
        return;
    }

    // Fallback: try 16-bit short UUID as a hex string ("XXXX")
    unsigned int short_val = 0;
    if (sscanf(uuid_str, "%04x", &short_val) == 1 || sscanf(uuid_str, "%04X", &short_val) == 1) {
        m_uuid.u16.u.type = BLE_UUID_TYPE_16;
        m_uuid.u16.value  = (uint16_t)short_val;
        snprintf(m_str, sizeof(m_str), "0x%04X", short_val);
        return;
    }

    ESP_LOGW(TAG, "BLEUUID: could not parse UUID string '%s'", uuid_str);
}

// =============================================================================
// BLECharacteristic implementation
// =============================================================================

BLECharacteristic::BLECharacteristic(BLEUUID uuid, uint32_t properties)
    : m_uuid(uuid)
    , m_properties(properties)
    , m_attr_handle(0)
    , m_conn_handle(BLE_HS_CONN_HANDLE_NONE)
    , m_callbacks(nullptr) {
}

void BLECharacteristic::setValue(const uint8_t* data, size_t len) {
    m_value.assign(data, data + len);
}

void BLECharacteristic::setValue(const std::string& val) {
    m_value.assign(val.begin(), val.end());
}

std::string BLECharacteristic::getValue() {
    return std::string(m_value.begin(), m_value.end());
}

void BLECharacteristic::notify() {
    if (m_conn_handle == BLE_HS_CONN_HANDLE_NONE || m_attr_handle == 0) return;
    if (m_value.empty()) return;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(m_value.data(), (uint16_t)m_value.size());
    if (!om) {
        ESP_LOGW(TAG, "notify: ble_hs_mbuf_from_flat failed (handle=%d)", m_attr_handle);
        return;
    }
    int rc = ble_gatts_notify_custom(m_conn_handle, m_attr_handle, om);
    if (rc != 0 && rc != BLE_HS_ENOTCONN && rc != BLE_HS_EDISABLED) {
        ESP_LOGW(TAG, "notify: ble_gatts_notify_custom rc=%d (handle=%d)", rc, m_attr_handle);
    }
}

void BLECharacteristic::addDescriptor(BLE2902* /*descriptor*/) {
    // NimBLE automatically adds the CCCD when NOTIFY is set — nothing to do.
}

// =============================================================================
// BLEService implementation
// =============================================================================

BLEService::BLEService(BLEUUID uuid)
    : m_uuid(uuid)
    , m_started(false) {
}

BLECharacteristic* BLEService::createCharacteristic(BLEUUID uuid, uint32_t properties) {
    auto* chr = new BLECharacteristic(uuid, properties);
    m_characteristics.push_back(chr);
    return chr;
}

void BLEService::start() {
    if (m_started) {
        ESP_LOGW(TAG, "BLEService::start() called twice");
        return;
    }
    if (m_characteristics.empty()) {
        ESP_LOGW(TAG, "BLEService::start() — no characteristics");
        return;
    }

    // We need stable pointers to UUIDs for the lifetime of the service.
    // Allocate one ble_uuid_any_t per characteristic (copying from BLEUUID).
    size_t n = m_characteristics.size();
    m_chr_uuids.resize(n);
    for (size_t i = 0; i < n; i++) {
        memcpy(&m_chr_uuids[i], m_characteristics[i]->getUUID().get_ble_uuid(),
               sizeof(ble_uuid_any_t));
    }

    // Build the ble_gatt_chr_def array.  The last entry must be zero-terminated.
    m_chr_defs.resize(n + 1);
    memset(m_chr_defs.data(), 0, (n + 1) * sizeof(ble_gatt_chr_def));

    for (size_t i = 0; i < n; i++) {
        BLECharacteristic* chr = m_characteristics[i];
        ble_gatt_chr_def& def  = m_chr_defs[i];

        def.uuid        = &m_chr_uuids[i].u;
        def.access_cb   = chr_access_cb;
        def.arg         = chr;
        def.flags       = (ble_gatt_chr_flags)chr->getProperties();
        def.val_handle  = m_characteristics[i]->getAttrHandlePtr();
        def.descriptors = nullptr;
        def.min_key_size = 0;
    }
    // Terminator already zeroed by memset above.

    // Build the ble_gatt_svc_def array (service + zero terminator).
    m_svc_defs.resize(2);
    memset(m_svc_defs.data(), 0, 2 * sizeof(ble_gatt_svc_def));

    m_svc_defs[0].type            = BLE_GATT_SVC_TYPE_PRIMARY;
    m_svc_defs[0].uuid            = m_uuid.get_ble_uuid();
    m_svc_defs[0].characteristics = m_chr_defs.data();
    // Terminator (index 1) already zeroed.

    // Register this service with NimBLE BEFORE the host task is started. The
    // canonical NimBLE flow (see ESP-IDF bleprph example) is:
    //   ble_gatts_count_cfg(svcs) + ble_gatts_add_svcs(svcs)  -- before host start
    //   ...then the host calls ble_gatts_start() itself on sync.
    // Calling ble_gatts_start() manually after the host had already synced (the
    // old approach) corrupted the attribute table once more than one service was
    // present, crashing in ble_gatts_start. BLEDevice defers the host start until
    // after every service has registered (BLEDevice::ensure_host_started()).
    int rc = ble_gatts_count_cfg(m_svc_defs.data());
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d (svc=%s)", rc, m_uuid.toString().c_str());
        return;
    }
    rc = ble_gatts_add_svcs(m_svc_defs.data());
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d (svc=%s)", rc, m_uuid.toString().c_str());
        return;
    }

    ESP_LOGI(TAG, "BLEService registered: %s (%zu chars)", m_uuid.toString().c_str(), n);
    m_started = true;
}

// =============================================================================
// BLEAdvertising implementation
// =============================================================================

BLEAdvertising::BLEAdvertising()
    : m_appearance(0)
    , m_scan_response(false) {
}

void BLEAdvertising::setName(const char* name) {
    m_name = name ? name : "";
}

void BLEAdvertising::setAdvertisementData(const BLEAdvertisementData& data) {
    // The primary advertisement name is already set via setName().
    // Additional fields from BLEAdvertisementData can override it.
    if (!data.getName().empty()) {
        m_name = data.getName();
    }
}

void BLEAdvertising::setScanResponseData(const BLEAdvertisementData& data) {
    // Scan-response name override — store for use in start().
    if (!data.getName().empty()) {
        m_name = data.getName();
    }
}

void BLEAdvertising::start() {
    // Start the NimBLE host on the first advertise (after all services have been
    // registered). The host registers the services and starts the GATT server on
    // sync; advertising/GAP calls below require a synced host.
    BLEDevice::ensure_host_started();

    // Build advertising data fields.
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Device name — use the name we've been given, falling back to the GAP name.
    const char* dev_name = m_name.empty()
        ? ble_svc_gap_device_name()
        : m_name.c_str();
    fields.name           = (uint8_t*)dev_name;
    fields.name_len       = (uint8_t)strlen(dev_name);
    fields.name_is_complete = 1;

    if (m_appearance != 0) {
        fields.appearance         = m_appearance;
        fields.appearance_is_present = 1;
    }

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_set_fields rc=%d (may be too long)", rc);
        // Retry without name if payload is too long.
        memset(&fields, 0, sizeof(fields));
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        ble_gap_adv_set_fields(&fields);
    }

    // Scan response: include the device name so iOS/macOS can show it.
    if (m_scan_response) {
        struct ble_hs_adv_fields rsp_fields;
        memset(&rsp_fields, 0, sizeof(rsp_fields));
        rsp_fields.name           = (uint8_t*)dev_name;
        rsp_fields.name_len       = (uint8_t)strlen(dev_name);
        rsp_fields.name_is_complete = 1;
        int rrc = ble_gap_adv_rsp_set_fields(&rsp_fields);
        if (rrc != 0) {
            ESP_LOGW(TAG, "ble_gap_adv_rsp_set_fields rc=%d", rrc);
        }
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Stop any current advertising before re-starting.
    ble_gap_adv_stop();

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started");
    }
}

void BLEAdvertising::stop() {
    ble_gap_adv_stop();
    ESP_LOGI(TAG, "BLE advertising stopped");
}

/*static*/ void BLEAdvertising::restart() {
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    if (adv) {
        adv->start();
    }
}

// =============================================================================
// BLEServer implementation
// =============================================================================

BLEServer::BLEServer()
    : m_callbacks(nullptr)
    , m_conn_handle(BLE_HS_CONN_HANDLE_NONE) {
    s_instance = this;
}

BLEService* BLEServer::createService(BLEUUID uuid) {
    auto* svc = new BLEService(uuid);
    m_services.push_back(svc);
    return svc;
}

BLEAdvertising* BLEServer::getAdvertising() {
    return BLEDevice::getAdvertising();
}

void BLEServer::startAdvertising() {
    BLEDevice::startAdvertising();
}

void BLEServer::disconnect() {
    if (m_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(m_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void BLEServer::setConnId(uint16_t h) {
    m_conn_handle = h;
    // Propagate connection handle to all characteristics so notify() works.
    for (BLEService* svc : m_services) {
        for (BLECharacteristic* chr : svc->getCharacteristics()) {
            chr->setConnHandle(h);
        }
    }
}

// =============================================================================
// BLEDevice implementation
// =============================================================================

/*static*/ void BLEDevice::init(const std::string& name) {
    if (s_initialized) {
        ESP_LOGW(TAG, "BLEDevice::init() called again — ignored");
        return;
    }

    s_device_name = name;

    // Initialise the NimBLE port (registers IDF BT controller).
    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }

    // Register standard GAP and GATT services.
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Set device name (max 31 bytes for advertising packet compatibility).
    rc = ble_svc_gap_device_name_set(name.c_str());
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    }

    // Register host callbacks.
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // NOTE: the NimBLE host task is NOT started here. Application services must
    // be registered (ble_gatts_count_cfg/add_svcs via BLEService::start) BEFORE
    // the host runs, because the host calls ble_gatts_start() itself on sync.
    // BLEDevice::ensure_host_started() starts the host on the first advertise,
    // after all services have registered.
    s_initialized = true;
    ESP_LOGI(TAG, "BLEDevice::init() complete (host start deferred), name='%s'", name.c_str());
}

/*static*/ void BLEDevice::ensure_host_started() {
    if (s_host_started) {
        return;
    }

    // One-shot semaphore posted by ble_on_sync().
    s_sync_sem = xSemaphoreCreateBinary();
    assert(s_sync_sem != nullptr);

    // Start the NimBLE host task. On sync the host registers all services that
    // were added before this point and starts the GATT server automatically.
    nimble_port_freertos_init(ble_host_task);

    // Block until the stack is synced (typically <500 ms).
    if (xSemaphoreTake(s_sync_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "ensure_host_started() timed out waiting for NimBLE sync");
    }

    vSemaphoreDelete(s_sync_sem);
    s_sync_sem = nullptr;
    s_host_started = true;
    ESP_LOGI(TAG, "NimBLE host started");
}

/*static*/ void BLEDevice::on_stack_synced() {
    // Request preferred MTU (best-effort).
    if (s_requested_mtu > 0) {
        ble_att_set_preferred_mtu(s_requested_mtu);
    }
}

/*static*/ BLEServer* BLEDevice::createServer() {
    if (!s_server) {
        s_server = new BLEServer();
    }
    return s_server;
}

/*static*/ BLEAdvertising* BLEDevice::getAdvertising() {
    if (!s_advertising) {
        s_advertising = new BLEAdvertising();
    }
    return s_advertising;
}

/*static*/ void BLEDevice::startAdvertising() {
    BLEAdvertising* adv = getAdvertising();
    if (adv) adv->start();
}

/*static*/ void BLEDevice::stopAdvertising() {
    BLEAdvertising* adv = getAdvertising();
    if (adv) adv->stop();
}

/*static*/ void BLEDevice::setMTU(uint16_t mtu) {
    s_requested_mtu = mtu;
    // If already synced, apply immediately.
    if (s_initialized) {
        ble_att_set_preferred_mtu(mtu);
    }
}

/*static*/ void BLEDevice::deinit(bool /*release_memory*/) {
    if (!s_initialized) return;

    ESP_LOGI(TAG, "BLEDevice::deinit() — stopping NimBLE host");

    // Stop advertising before stopping the host.
    ble_gap_adv_stop();

    // Ask NimBLE to stop.  nimble_port_stop() signals the host task to exit
    // its run loop; nimble_port_deinit() releases IDF BT resources.
    nimble_port_stop();
    nimble_port_deinit();

    // Free server and advertising singletons.
    delete s_server;
    s_server = nullptr;
    BLEServer::s_instance = nullptr;

    delete s_advertising;
    s_advertising = nullptr;

    s_initialized = false;
    // Must reset so a later re-enable (e.g. from Settings) actually restarts the
    // NimBLE host task. Otherwise ensure_host_started() short-circuits and the
    // device advertises but cannot service GATT connections until a reboot.
    s_host_started = false;
    ESP_LOGI(TAG, "BLEDevice::deinit() complete");
}
