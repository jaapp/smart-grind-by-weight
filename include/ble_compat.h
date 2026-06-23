#pragma once
// =============================================================================
// ble_compat.h — Arduino BLE API compatibility layer over ESP-IDF 5.x NimBLE
//
// Implements the subset of the Arduino BLE API used by BluetoothManager:
//   BLEDevice, BLEServer, BLEService, BLECharacteristic, BLE2902,
//   BLEUUID, BLEServerCallbacks, BLECharacteristicCallbacks,
//   BLEAdvertising, BLEAdvertisementData
//
// The NimBLE host runs on its own FreeRTOS task (started by BLEDevice::init).
// All GATT services must be registered via ble_gatts_add_svcs() before or
// right after the stack syncs; BLEService::start() handles that.
// =============================================================================

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <cstring>
#include <cstdint>

// =============================================================================
// Forward declarations
// =============================================================================
class BLECharacteristic;
class BLEService;
class BLEServer;
class BLE2902;
class BLEAdvertising;

// =============================================================================
// BLEUUID
// =============================================================================
class BLEUUID {
public:
    // Parse "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" or 4-char short UUID strings.
    BLEUUID(const char* uuid_str);
    // 16-bit short UUID (e.g. 0x2902 for CCCD)
    BLEUUID(uint16_t uuid16);
    // Default constructor (produces an invalid/zero UUID)
    BLEUUID();

    const ble_uuid_t* get_ble_uuid() const { return &m_uuid.u; }
    std::string toString() const { return m_str; }

private:
    ble_uuid_any_t m_uuid;
    char           m_str[37];
};

// =============================================================================
// BLECharacteristicCallbacks
// =============================================================================
class BLECharacteristicCallbacks {
public:
    virtual ~BLECharacteristicCallbacks() = default;
    virtual void onWrite(BLECharacteristic* characteristic) {}
    virtual void onRead(BLECharacteristic* characteristic)  {}
};

// =============================================================================
// BLECharacteristic
// =============================================================================
class BLECharacteristic {
public:
    // Property flags — values must match NimBLE ble_gatt_chr_flags directly so
    // the property bitmask can be forwarded verbatim when building the svc table.
    static const uint32_t PROPERTY_READ      = BLE_GATT_CHR_F_READ;
    static const uint32_t PROPERTY_WRITE     = BLE_GATT_CHR_F_WRITE;
    static const uint32_t PROPERTY_WRITE_NR  = BLE_GATT_CHR_F_WRITE_NO_RSP;
    static const uint32_t PROPERTY_NOTIFY    = BLE_GATT_CHR_F_NOTIFY;
    static const uint32_t PROPERTY_INDICATE  = BLE_GATT_CHR_F_INDICATE;

    BLECharacteristic(BLEUUID uuid, uint32_t properties);

    // Value accessors
    void        setValue(const uint8_t* data, size_t len);
    void        setValue(const std::string& val);
    void        setValue(const char* val) { setValue(std::string(val)); }
    std::string getValue();
    size_t      getLength()  const { return m_value.size(); }
    const uint8_t* getData() const { return m_value.data(); }

    // Send a notification to the connected client.
    void notify();

    void setCallbacks(BLECharacteristicCallbacks* callbacks) { m_callbacks = callbacks; }
    void addDescriptor(BLE2902* descriptor);   // No-op: NimBLE auto-adds CCCD for NOTIFY chars

    BLEUUID  getUUID()       const { return m_uuid; }
    uint16_t getHandle()     const { return m_attr_handle; }
    void     setHandle(uint16_t h) { m_attr_handle = h; }
    void     setConnHandle(uint16_t h) { m_conn_handle = h; }
    uint32_t getProperties() const { return m_properties; }

    BLECharacteristicCallbacks* getCallbacks() { return m_callbacks; }

    // Return pointer to attr handle — NimBLE val_handle needs a uint16_t* to write the
    // assigned attribute handle back after ble_gatts_add_svcs().
    uint16_t* getAttrHandlePtr() { return &m_attr_handle; }

    // Allow BLEService to write directly to m_attr_handle via NimBLE val_handle pointer.
    friend class BLEService;

private:
    BLEUUID                     m_uuid;
    uint32_t                    m_properties;
    std::vector<uint8_t>        m_value;
    uint16_t                    m_attr_handle;
    uint16_t                    m_conn_handle;
    BLECharacteristicCallbacks* m_callbacks;
};

// =============================================================================
// BLE2902 (CCCD descriptor)
// NimBLE automatically adds the CCCD when BLE_GATT_CHR_F_NOTIFY is set; this
// class exists only so that existing addDescriptor() calls compile.
// =============================================================================
class BLE2902 {
public:
    BLE2902() = default;
};

// =============================================================================
// BLEService
// =============================================================================
class BLEService {
public:
    explicit BLEService(BLEUUID uuid);

    BLECharacteristic* createCharacteristic(BLEUUID uuid, uint32_t properties);
    BLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties) {
        return createCharacteristic(BLEUUID(uuid), properties);
    }

    // Register this service and all its characteristics with NimBLE.
    // Must be called after all createCharacteristic() calls and while the NimBLE
    // stack is running (i.e. after BLEDevice::init has synced).
    void start();

    BLEUUID getUUID() const { return m_uuid; }
    std::vector<BLECharacteristic*>& getCharacteristics() { return m_characteristics; }

private:
    BLEUUID                          m_uuid;
    std::vector<BLECharacteristic*>  m_characteristics;
    bool                             m_started;

    // Heap-allocated NimBLE table entries kept alive for the lifetime of the service.
    // NimBLE holds a pointer into these arrays; they must outlive deinit.
    std::vector<ble_gatt_chr_def>    m_chr_defs;   // characteristic definitions
    std::vector<ble_gatt_svc_def>    m_svc_defs;   // service table (2 entries: svc + terminator)
    std::vector<ble_uuid_any_t>      m_chr_uuids;  // storage for per-chr UUID copies
};

// =============================================================================
// BLEServerCallbacks
// =============================================================================
class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() = default;
    virtual void onConnect(BLEServer* server)    {}
    virtual void onDisconnect(BLEServer* server) {}
};

// =============================================================================
// BLEAdvertisementData
// Used to set the name in advertising and scan-response packets.
// =============================================================================
class BLEAdvertisementData {
public:
    BLEAdvertisementData() = default;
    void setName(const char* name)        { m_name = name ? name : ""; }
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const    { return m_name; }
private:
    std::string m_name;
};

// =============================================================================
// BLEAdvertising
// =============================================================================
class BLEAdvertising {
public:
    BLEAdvertising();

    void addServiceUUID(BLEUUID uuid)     { m_service_uuids.push_back(uuid); }
    void addServiceUUID(const char* uuid) { m_service_uuids.push_back(BLEUUID(uuid)); }
    void setAppearance(uint16_t app)      { m_appearance = app; }
    void setScanResponse(bool enable)     { m_scan_response = enable; }
    void setMinPreferred(uint16_t ms)     {}   // advisory only — no-op
    void setMaxPreferred(uint16_t ms)     {}   // advisory only — no-op
    void setName(const char* name);
    void setAdvertisementData(const BLEAdvertisementData& data);
    void setScanResponseData(const BLEAdvertisementData& data);

    void start();
    void stop();

    // Re-start after a disconnect (called internally by GAP event handler).
    static void restart();

    std::vector<BLEUUID>& getServiceUUIDs() { return m_service_uuids; }

private:
    std::vector<BLEUUID> m_service_uuids;
    uint16_t             m_appearance;
    bool                 m_scan_response;
    std::string          m_name;         // device name to embed in adv data
};

// =============================================================================
// BLEServer
// =============================================================================
class BLEServer {
public:
    BLEServer();

    BLEService*  createService(BLEUUID uuid);
    BLEService*  createService(const char* uuid) { return createService(BLEUUID(uuid)); }
    void         setCallbacks(BLEServerCallbacks* callbacks) { m_callbacks = callbacks; }
    BLEAdvertising* getAdvertising();
    void         startAdvertising();
    void         disconnect();

    uint16_t     getConnId() const { return m_conn_handle; }
    void         setConnId(uint16_t h);

    std::vector<BLEService*>& getServices() { return m_services; }
    BLEServerCallbacks*       getCallbacks() { return m_callbacks; }

    static BLEServer* getInstance() { return s_instance; }

    // BLEDevice::deinit() needs to clear s_instance.
    friend class BLEDevice;

private:
    std::vector<BLEService*> m_services;
    BLEServerCallbacks*      m_callbacks;
    uint16_t                 m_conn_handle;
    static BLEServer*        s_instance;
};

// =============================================================================
// BLEDevice  (all-static, mirrors Arduino's BLEDevice)
// =============================================================================
class BLEDevice {
public:
    static void init(const std::string& name);
    static void init(const char* name)      { init(std::string(name)); }

    // Create (or return existing) server singleton.
    static BLEServer*      createServer();
    static BLEServer*      getServer()      { return s_server; }

    // Advertising helpers
    static BLEAdvertising* getAdvertising();
    static void            startAdvertising();
    static void            stopAdvertising();

    // Optional MTU request (best-effort; client may ignore it).
    static void setMTU(uint16_t mtu);

    static void deinit(bool release_memory = false);

    // Internal: called by BLE host sync callback once NimBLE is ready.
    static void on_stack_synced();

private:
    static BLEServer*      s_server;
    static BLEAdvertising* s_advertising;
    static std::string     s_device_name;
    static uint16_t        s_requested_mtu;
    static bool            s_initialized;
};
