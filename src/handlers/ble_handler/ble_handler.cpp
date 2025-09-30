#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using sdbus::ObjectPath;
using sdbus::Variant;

const std::string BLUEZ_SERVICE_NAME = "org.bluez";
const std::string DBUS_OM_IFACE = "org.freedesktop.DBus.ObjectManager";
const std::string ADAPTER_PATH = "/org/bluez/hci0";
const std::string ADAPTER_IFACE = "org.bluez.Adapter1";
const std::string DEVICE_IFACE = "org.bluez.Device1";
const std::string Service_IFACE = "org.bluez.GattService1";
const std::string Characteristic_IFACE = "org.bluez.GattCharacteristic1";
const std::string Descriptor_IFACE = "org.bluez.GattDescriptor1";
const std::string PROPERTIES_IFACE = "org.freedesktop.DBus.Properties";

//strusts & enum
struct BLEDevice {
    std::string address;       // MAC address
    std::string path;          // D-Bus object path
    std::string name;
    bool discovered = false;
    bool connected = false;
    bool paired = false;
    bool trusted = false;
    std::map<std::string, std::string> characteristics; //key=UUID value=Path
    std::shared_ptr<sdbus::IProxy> proxy;
    std::mutex mtx;

    void setAddress(const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx);
        address = value;
    }

    std::string getAddress() {
        std::lock_guard<std::mutex> lock(mtx);
        return address;
    }

    void setPath(const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx);
        path = value;
    }

    std::string getPath() {
        std::lock_guard<std::mutex> lock(mtx);
        return path;
    }

    void setName(const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx);
        name = value;
    }

    std::string getName() {
        std::lock_guard<std::mutex> lock(mtx);
        return name;
    }

    void setDiscovered(const bool& value) {
        std::lock_guard<std::mutex> lock(mtx);
        discovered = value;
    }

    bool getDiscovered() {
        std::lock_guard<std::mutex> lock(mtx);
        return discovered;
    }

    void setConnected(const bool& value) {
        std::lock_guard<std::mutex> lock(mtx);
        connected = value;
    }

    bool getConnected() {
        std::lock_guard<std::mutex> lock(mtx);
        return connected;
    }

    void setPaired(const bool& value) {
        std::lock_guard<std::mutex> lock(mtx);
        paired = value;
    }

    bool getPaired() {
        std::lock_guard<std::mutex> lock(mtx);
        return paired;
    }

    void setTrusted(const bool& value) {
        std::lock_guard<std::mutex> lock(mtx);
        trusted = value;
    }

    bool getTrusted() {
        std::lock_guard<std::mutex> lock(mtx);
        return trusted;
    }

    void setCharacteristics(const std::map<std::string, std::string>& value) {
        std::lock_guard<std::mutex> lock(mtx);
        characteristics = value;
    }

    std::map<std::string, std::string> getCharacteristics() {
        std::lock_guard<std::mutex> lock(mtx);
        return characteristics;
    }

    void setProxy(const std::shared_ptr<sdbus::IProxy>& value) {
        std::lock_guard<std::mutex> lock(mtx);
        proxy = value;
    }

    std::shared_ptr<sdbus::IProxy> getProxy() {
        std::lock_guard<std::mutex> lock(mtx);
        return proxy;
    }
};

struct ScanHandle {
    std::shared_ptr<sdbus::IProxy> proxy;
    std::thread worker;
    std::shared_ptr<std::atomic<bool>> stopRequested;

    ScanHandle() : stopRequested(std::make_shared<std::atomic<bool>>(false)) {}

    ~ScanHandle() {
        stop(); // ensure the scan stops and thread joins
    }

    // Not copyable
    ScanHandle(const ScanHandle&) = delete;
    ScanHandle& operator=(const ScanHandle&) = delete;

    // Movable
    ScanHandle(ScanHandle&& other) noexcept
        : proxy(std::move(other.proxy)),
          worker(std::move(other.worker)),
          stopRequested(std::move(other.stopRequested)) {}

    ScanHandle& operator=(ScanHandle&& other) noexcept {
        if (this != &other) {
            stop();
            proxy = std::move(other.proxy);
            worker = std::move(other.worker);
            stopRequested = std::move(other.stopRequested);
        }
        return *this;
    }

    // Explicit stop method
    void stop() {
        if (stopRequested) stopRequested->store(true); // checks if stopRequested pointer is not null then -> request stop
        if (worker.joinable()) {
            try {
                worker.join(); // wait for the worker thread to finish
            } catch (const std::system_error& e) {
                std::cerr << "Error joining scan thread: " << e.what() << std::endl;
            }
        }
    }
};

enum class ValueType {
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    FLOAT32,
    STRING,
    HEX
};

//prototypes 
bool set_bool_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, const std::string& propertyName, bool value);
bool get_bool_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName);
std::string get_string_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName);
ScanHandle scanDevices(const std::shared_ptr<sdbus::IConnection>& connection,
                       std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>& discovered, std::mutex& discoveredMutex,
                       int scanDurationMs = 0); // 0 = run until manually stopped
bool pairDevice(const std::shared_ptr<sdbus::IConnection>& connection, const std::shared_ptr<BLEDevice>& device,
                int maxRetries = 3, int timeoutMs = 10000);
bool connectDevice(const std::shared_ptr<sdbus::IConnection>& connection, const std::shared_ptr<BLEDevice>& device,
                   int maxRetries = 3, int timeoutMs = 10000);
bool DisconnectDevice(BLEDevice& device);
void Link_Devices(const std::shared_ptr<sdbus::IConnection>& connection, int scanTimeMs = 40000);
void add_device(const std::string mac);

std::unordered_map<std::string, std::shared_ptr<BLEDevice>> devices; //key = mac address
std::mutex devicesMutex;

void add_device(const std::string mac)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    if (devices.find(mac) == devices.end()) 
    {
        auto dev = std::make_shared<BLEDevice>();
        dev->address = mac;
        devices[mac] = dev;
        std::cout << "saved BLE device to devices: " << mac  << std::endl;
    }
}

std::map<std::string, std::string> getCharacteristics(
    const sdbus::ObjectPath& devPath,
    const std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>& managedObjects)
{
    std::map<std::string, std::string> result;

    for (const auto& [path, interfaces] : managedObjects)
    {
        auto it = interfaces.find(Characteristic_IFACE);
        if (it != interfaces.end())
        {
            // Characteristic belongs to this device if path starts with the device path
            if (path.find(devPath) == 0)
            {
                const auto& props = it->second;
                if (props.count("UUID"))
                {
                    auto uuid = props.at("UUID").get<std::string>();
                    result[uuid] = path;
                }
            }
        }
    }
    return result;
}

ScanHandle scanDevices(const std::shared_ptr<sdbus::IConnection>& connection,
                       std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>& discovered,
                       std::mutex& discoveredMutex,
                       int scanDurationMs)  // 0 = run until manually stopped
{
    ScanHandle handle;
    handle.proxy   = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    auto adapter   = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, ADAPTER_PATH);

    // 1. Populate with already-known devices
    std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects;
    try {
        handle.proxy->callMethod("GetManagedObjects")
            .onInterface(DBUS_OM_IFACE)
            .storeResultsTo(managedObjects);
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Faild to GetManagedObjects: " << e.getName() << " - " << e.getMessage() << "\n";
    }

    for (const auto& [path, interfaces] : managedObjects) 
    {
        if (auto it = interfaces.find(DEVICE_IFACE); it != interfaces.end()) 
        {
            const auto& props = it->second;
            if (props.count("Address")) 
            {
                auto mac = props.at("Address").get<std::string>();

                std::lock_guard<std::mutex> lock(discoveredMutex);
                if (discovered->find(mac) == discovered->end()) 
                {
                    auto dev        = std::make_shared<BLEDevice>();
                    dev->address    = mac;
                    dev->path       = path;
                    dev->name       = props.count("Name") ? props.at("Name").get<std::string>() : "";
                    dev->discovered = true;
                    dev->connected  = props.count("Connected") ? props.at("Connected").get<bool>() : false;
                    dev->paired     = props.count("Paired") ? props.at("Paired").get<bool>() : false;
                    dev->trusted    = props.count("Trusted") ? props.at("Trusted").get<bool>() : false;
                    dev->characteristics = getCharacteristics(path, managedObjects);

                    discovered->emplace(mac, dev);
                    // publish "already known device found"
                    std::cout << "publish already known device discovered: " << path << std::endl;
                }
            }
        }
    }

    // 2. Register signal handlers for ongoing discovery
    handle.proxy->uponSignal("InterfacesAdded")
    .onInterface(DBUS_OM_IFACE)
    .call([discovered, &discoveredMutex](const sdbus::ObjectPath& path,
              const std::map<std::string, std::map<std::string, sdbus::Variant>>& ifaces) {
        if (auto it = ifaces.find(DEVICE_IFACE); it != ifaces.end())
        {
            // Device discovered
            const auto& props = it->second;
            if (props.count("Address"))
            {
                auto mac = props.at("Address").get<std::string>();
                std::lock_guard<std::mutex> lock(discoveredMutex);
                if (discovered->find(mac) == discovered->end())
                {
                    auto dev        = std::make_shared<BLEDevice>();
                    dev->address    = mac;
                    dev->path       = path;
                    dev->name       = props.count("Name") ? props.at("Name").get<std::string>() : "";
                    dev->discovered = true;
                    dev->connected  = props.count("Connected") ? props.at("Connected").get<bool>() : false;
                    dev->paired     = props.count("Paired") ? props.at("Paired").get<bool>() : false;
                    dev->trusted    = props.count("Trusted") ? props.at("Trusted").get<bool>() : false;

                    discovered->emplace(mac, dev);
                    // publish "device added"
                    std::cout << "device discovered: " << path << std::endl;
                }
            }
        }
    });
    handle.proxy->uponSignal("InterfacesRemoved")
        .onInterface(DBUS_OM_IFACE)
        .call([discovered, &discoveredMutex](const sdbus::ObjectPath& path,
                  const std::map<std::string, std::map<std::string, sdbus::Variant>>& ifaces) {
            auto it = ifaces.find(DEVICE_IFACE);
            if (it != ifaces.end()) 
            {
                std::lock_guard<std::mutex> lock(discoveredMutex);
                for (auto it2 = discovered->begin(); it2 != discovered->end();) 
                {
                    if (it2->second->path == path) 
                    {
                        // publish "device removed"
                        std::cout << "device removed from discovered: " << path << std::endl;
                        it2 = discovered->erase(it2);
                    } else {
                        ++it2;
                    }
                }
            }
    });
    handle.proxy->finishRegistration();

    // (3) Start discovery
    try {
        adapter->callMethod("StartDiscovery").onInterface(ADAPTER_IFACE);
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Faild to start discovery: " << e.getName() << " - " << e.getMessage() << "\n";
    }
    std::cout << "Scanning started..." << std::endl;

    // (4) Launch worker thread
    handle.worker = std::thread([adapter = std::move(adapter), 
                             scanDurationMs, 
                             stop = handle.stopRequested]() mutable {
        auto start = std::chrono::steady_clock::now();

        while (!stop->load()) {
            if (scanDurationMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start);
                if (elapsed.count() >= scanDurationMs)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        stop->store(true);  // tell the outside world we’re done
        
        try {
            adapter->callMethod("StopDiscovery").onInterface(ADAPTER_IFACE);
            std::cout << "Scanning stopped." << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "StopDiscovery failed: " << ex.what() << std::endl;
        }
    });

    return handle;
}

/**********************************************************************
|   Link_Devices() function scans for all saved devices and            |
|   connects/pairs to the saved devices it found then updates the      |
|   status                                                             |
***********************************************************************/
void Link_Devices(const std::shared_ptr<sdbus::IConnection>& connection, int scanTimeMs)
{
    auto discovered = std::make_shared<
        std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>();
    std::mutex discoveredMutex;

    // Build device_list with expected MACs from devices
    std::vector<std::string> Devices_list;
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        for(const auto& [mac, device]: devices) Devices_list.push_back(mac);
    }

    auto handle = scanDevices(connection, discovered, discoveredMutex, scanTimeMs);

    while (!handle.stopRequested->load()) 
    {
        bool foundAll = true;
        {
            std::lock_guard<std::mutex> lock(discoveredMutex);
            for(const auto& mac : Devices_list) {
                if(discovered->find(mac) == discovered->end()) {
                    foundAll = false;
                    break;
                }
            }
        }

        if (foundAll) {
            // Grace period to catch in-flight signals
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            handle.stop(); // join worker + stop discovery
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    for(const auto& [mac, dev] : *discovered)
    {
        {
            std::lock_guard<std::mutex> lock(devicesMutex);
            if (devices.find(mac) == devices.end()) continue;
            devices[mac] = dev;
        }
        auto path = dev->getPath();

        //---create signal handler---
        std::shared_ptr<sdbus::IProxy> proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);
        dev->setProxy(proxy);

        // Capture weak_ptr to device so handler won’t access dangling memory
        std::weak_ptr<BLEDevice> weakDev = dev;

        dev->proxy->uponSignal("PropertiesChanged")
            .onInterface(PROPERTIES_IFACE)
            .call([weakDev](const std::string& interface,
                    const std::map<std::string, sdbus::Variant>& changed,
                    const std::vector<std::string>& invalidated) {

                if (interface != DEVICE_IFACE)
                    return;

                if (auto device = weakDev.lock()) { // ✅ safe
                    bool updated = false;

                    // Connected
                    auto it = changed.find("Connected");
                    if (it != changed.end()) {
                        bool connected = it->second.get<bool>();
                        device->setConnected(connected);
                        std::cout << "Device " << device->getAddress()
                                << " updated Connected: " << connected << std::endl;
                        updated = true;
                    }

                    // Paired
                    it = changed.find("Paired");
                    if (it != changed.end()) {
                        bool paired = it->second.get<bool>();
                        device->setPaired(paired);
                        std::cout << "Device " << device->getAddress()
                                << " updated Paired: " << paired << std::endl;
                        updated = true;
                    }

                    // Trusted
                    it = changed.find("Trusted");
                    if (it != changed.end()) {
                        bool trusted = it->second.get<bool>();
                        device->setTrusted(trusted);
                        std::cout << "Device " << device->getAddress()
                                << " updated Trusted: " << trusted << std::endl;
                        updated = true;
                    }

                    if (updated) {
                        // Publish changes to MQTT or other system here
                    }
                }
        });
        dev->proxy->finishRegistration();

        std::cout << "Added BLE device path: " << path << " to " << mac << std::endl;

        if (!dev->getConnected()) connectDevice(connection, dev);
        if (!dev->getPaired()) pairDevice(connection, dev);
    }
}

bool get_bool_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName)
{
    bool value;
    auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, devicePath);

    try {
        sdbus::Variant var;
        deviceProxy->callMethod("Get")
            .onInterface(PROPERTIES_IFACE)
            .withArguments(DEVICE_IFACE, propertyName)
            .storeResultsTo(var);

        value = var.get<bool>();
        return value;
    } catch (const sdbus::Error& e) {
        std::cerr << "Faild to get " << propertyName << " var: " << e.getName() << " - " << e.getMessage() << "\n";
        return false;
    }
}

bool set_bool_property(const std::shared_ptr<sdbus::IConnection>& connection,
                       const std::string& devicePath,
                       const std::string& propertyName,
                       bool value)
{
    try {
        auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, devicePath);

        // BlueZ expects Variant for Set method
        sdbus::Variant variantValue = value;

        deviceProxy->callMethod("Set")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.bluez.Device1", propertyName, variantValue);

        std::cout << "Set " << propertyName << " = "
                  << (value ? "true" : "false")
                  << " for device " << devicePath << std::endl;

        return true;
    }
    catch (const sdbus::Error& e) {
        std::cerr << "Failed to set property '" << propertyName
                  << "' on " << devicePath
                  << ": " << e.getName() << " - " << e.getMessage()
                  << std::endl;
        return false;
    }
}

std::string get_string_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName)
{
    std::string value;
    auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, devicePath);

    try {
        sdbus::Variant var;
        deviceProxy->callMethod("Get")
            .onInterface(PROPERTIES_IFACE)
            .withArguments(DEVICE_IFACE, propertyName)
            .storeResultsTo(var);

        value = var.get<std::string>();
        return value;
    } catch (const sdbus::Error& e) {
        std::cerr << "Faild to get " << propertyName << " var: " << e.getName() << " - " << e.getMessage() << "\n";
        return "";
    }
}

bool pairDevice(const std::shared_ptr<sdbus::IConnection>& connection,
                const std::shared_ptr<BLEDevice>& device,
                int maxRetries,
                int timeoutMs)
{
    auto proxy = device->getProxy();
    if (!proxy) return false; // must have a proxy

    // Already paired? Nothing to do
    if (device->getPaired()) return true;

    std::string path = device->getPath();
    if (!device->getDiscovered() || path.empty()) 
    {
        std::cerr << "[WARN] Device " << device->getAddress() 
                  << " not discovered yet, skipping.\n";
        return false;
    }

    for (int attempt = 1; attempt <= maxRetries; ++attempt) 
    {
        std::cout << "[INFO] Pair attempt " << attempt 
                  << " for " << path << std::endl;

        try {
            // Initiate Pair call
            proxy->callMethod("Pair")
                 .onInterface(DEVICE_IFACE)
                 .withTimeout(std::chrono::milliseconds(timeoutMs));
        } 
        catch (const sdbus::Error& e) 
        {
            std::cerr << "[ERROR] Pair attempt " << attempt
                      << " failed: " << e.getName() << " - " << e.getMessage() << std::endl;
        }

        // Wait for signal to update device->paired
        auto start = std::chrono::steady_clock::now();
        while (!device->getPaired() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start
               ).count() < timeoutMs) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (device->getPaired()) 
        {
            std::cout << "[OK] Device paired successfully on attempt " 
                      << attempt << std::endl;

            if(!device->getTrusted()) set_bool_property(connection, path, "Trusted", true);

            return true;
        }

        // Retry delay
        if (attempt < maxRetries) 
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    std::cerr << "[FAIL] Device failed to pair after " << maxRetries << " attempts" << std::endl;
    return false;
}

bool connectDevice(const std::shared_ptr<sdbus::IConnection>& connection,
                   const std::shared_ptr<BLEDevice>& device,
                   int maxRetries,
                   int timeoutMs)
{
    auto proxy = device->getProxy();
    if (!proxy) return false; // must have a proxy

    // Already connected? Nothing to do
    if (device->getConnected()) return true;

    std::string path = device->getPath();
    if (!device->getDiscovered() || path.empty()) 
    {
        std::cerr << "[WARN] Device " << device->getAddress() 
                  << " not discovered yet, skipping.\n";
        return false;
    }

    for (int attempt = 1; attempt <= maxRetries; ++attempt) 
    {
        std::cout << "[INFO] Connect attempt " << attempt 
                  << " for " << path << std::endl;

        try {
            // Initiate Connect call
            proxy->callMethod("Connect")
                         .onInterface(DEVICE_IFACE)
                         .withTimeout(std::chrono::milliseconds(timeoutMs));
        } 
        catch (const sdbus::Error& e) 
        {
            std::cerr << "[ERROR] Connect attempt " << attempt
                      << " failed: " << e.getName() << " - " << e.getMessage() << std::endl;
        }

        // Wait for signal to update device->connected
        auto start = std::chrono::steady_clock::now();
        while (!device->getConnected() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start
               ).count() < timeoutMs) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (device->getConnected()) 
        {
            std::cout << "[OK] Device connected successfully on attempt " 
                      << attempt << std::endl;

            // --search for characteristics--
            auto slashProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
            std::map<std::string, std::string> characteristics;
            std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects;
            try {
                slashProxy->callMethod("GetManagedObjects")
                    .onInterface(DBUS_OM_IFACE)
                    .storeResultsTo(managedObjects);
            } 
            catch (const sdbus::Error& e) {
                std::cerr << "Faild to GetManagedObjects: " << e.getName() << " - " << e.getMessage() << "\n";
            }

            for (const auto& [objPath, interfaces] : managedObjects) {
                // Only include characteristics belonging to this device
                if (objPath.find(path) != 0)
                    continue;

                auto itChar = interfaces.find(Characteristic_IFACE);
                if (itChar != interfaces.end()) {
                    const auto& props = itChar->second;
                    if (props.count("UUID")) {
                        std::string uuid = props.at("UUID").get<std::string>();
                        characteristics[uuid] = objPath; // path of characteristic
                    }
                }
            }
            device->setCharacteristics(characteristics);
            
            return true;
        }

        // Disconnect before next retry
        if (attempt < maxRetries) 
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            try { proxy->callMethod("Disconnect").onInterface(DEVICE_IFACE); }
            catch (const sdbus::Error& e) { std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << "\n"; }
        }
    }

    std::cerr << "[FAIL] Device failed to connect after " << maxRetries << " attempts" << std::endl;
    return false;
}

bool DisconnectDevice(BLEDevice& device) {
    auto proxy = device.getProxy();
    if (!proxy) {
        std::cerr << "No proxy for device " << device.getAddress() << std::endl;
        return false;
    }

    try {
        proxy->callMethod("Disconnect").onInterface(DEVICE_IFACE);
        std::cout << "Disconnect requested for " << device.getAddress() << std::endl;
        return true;
    } catch (const sdbus::Error& e) {
        std::cerr << "Failed to Disconnect: " << e.getName() << " - " << e.getMessage() << std::endl;
        return false;
    }
}

std::string ReadCharacteristic(const std::shared_ptr<sdbus::IConnection>& connection,
                               BLEDevice& device,
                               const std::string& uuid,
                               ValueType type)
{
    // Find the characteristic path from the device
    auto characteristics = device.getCharacteristics();
    auto it = characteristics.find(uuid);
    if (it == characteristics.end()) {
        throw std::runtime_error("Characteristic " + uuid + " not found for device");
    }
    std::string path = it->second;

    // Create D-Bus proxy to the characteristic
    auto characteristicProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);

    std::map<std::string, sdbus::Variant> options{};
    std::vector<uint8_t> response;

    try {
        characteristicProxy->callMethod("ReadValue")
                    .onInterface(Characteristic_IFACE)
                    .withArguments(options)
                    .storeResultsTo(response);
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Faild to ReadValue: " << e.getName() << " - " << e.getMessage() << "\n";
    }

    // Build JSON result
    json j;
    j["device"] = device.getAddress();
    j["uuid"]   = uuid;
    j["type"]   = ""; // will fill below

    // Raw hex string
    std::ostringstream rawHex;
    for (auto b : response) {
        rawHex << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(b);
    }
    j["raw"] = rawHex.str();

    // Parsed value depending on type
    switch (type) {
        case ValueType::UINT8:
            j["type"] = "UINT8";
            if (!response.empty())
                j["value"] = static_cast<unsigned>(response[0]);
            break;

        case ValueType::INT16:
            j["type"] = "INT16";
            if (response.size() >= 2) {
                int16_t val = static_cast<int16_t>(response[0] | (response[1] << 8));
                j["value"] = val;
            }
            break;

        case ValueType::UINT16:
            j["type"] = "UINT16";
            if (response.size() >= 2) {
                uint16_t val = static_cast<uint16_t>(response[0] | (response[1] << 8));
                j["value"] = val;
            }
            break;

        case ValueType::INT32:
            j["type"] = "INT32";
            if (response.size() >= 4) {
                int32_t val = response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24);
                j["value"] = val;
            }
            break;

        case ValueType::UINT32:
            j["type"] = "UINT32";
            if (response.size() >= 4) {
                uint32_t val = response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24);
                j["value"] = val;
            }
            break;

        case ValueType::FLOAT32:
            j["type"] = "FLOAT32";
            if (response.size() >= 4) {
                float val;
                uint32_t raw = response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24);
                std::memcpy(&val, &raw, sizeof(val));
                j["value"] = val;
            }
            break;

        case ValueType::STRING:
            j["type"] = "STRING";
            j["value"] = std::string(response.begin(), response.end());
            break;

        case ValueType::HEX:
        default:
            j["type"] = "HEX";
            j["value"] = j["raw"]; // same as raw hex
            break;
    }

    return j.dump(); // return JSON string (ready to publish)
}

void WriteCharacteristic(const std::shared_ptr<sdbus::IConnection>& connection,
                         BLEDevice& device,
                         const std::string& uuid,
                         const std::vector<uint8_t>& value,
                         bool withResponse = true)
{
    // Find the characteristic path from the device
    auto characteristics = device.getCharacteristics();
    auto it = characteristics.find(uuid);
    if (it == characteristics.end()) {
        throw std::runtime_error("Characteristic " + uuid + " not found for device");
    }
    std::string path = it->second;

    // Create D-Bus proxy to the characteristic
    auto characteristicProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);

    // Options map can include "type" = "request" (write with response) or "command" (write without response)
    std::map<std::string, sdbus::Variant> options;
    options["type"] = sdbus::Variant(withResponse ? std::string("request") 
                                                  : std::string("command"));

    try {
        // Perform the WriteValue call
        characteristicProxy->callMethod("WriteValue")
            .onInterface(Characteristic_IFACE)
            .withArguments(value, options);
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Faild to ReadValue: " << e.getName() << " - " << e.getMessage() << "\n";
    }
}

int main(int argc, char* argv[])
{
    std::shared_ptr<sdbus::IConnection> connection = sdbus::createSystemBusConnection();

    // Monitor when devices are removed
    auto Proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    Proxy->uponSignal("InterfacesAdded")
        .onInterface(DBUS_OM_IFACE)
        .call([](const sdbus::ObjectPath& path,
                const std::vector<std::string>& ifaces) {
            std::lock_guard<std::mutex> lock(devicesMutex);
            for (auto& [mac, device] : devices) {
                if (device->getPath() == path) {
                    device->setDiscovered(true);
                    std::cout << "Device " << mac << " readded to devices!" << std::endl;
                }
            }
    });
    // Monitor when devices are added back (reappear)
    Proxy->uponSignal("InterfacesRemoved")
        .onInterface(DBUS_OM_IFACE)
        .call([](const sdbus::ObjectPath& path,
                const std::vector<std::string>& ifaces) {
            std::lock_guard<std::mutex> lock(devicesMutex);
            for (auto& [mac, device] : devices) {
                if (device->getPath() == path) {
                    device->setConnected(false);
                    device->setPaired(false);
                    device->setDiscovered(false);
                    std::cout << "Device " << mac << " removed from devices!" << std::endl;
                }
            }
    });
    Proxy->finishRegistration();

    
    // Run the event loop in a background thread
    std::thread loopThread([&] {
        connection->enterEventLoop();
    });

    std::string mac = "38:39:8F:82:18:7E";
    std::string ch = "d52246df-98ac-4d21-be1b-70d5f66a5ddb";
    add_device(mac);

    std::cout << "Device List---\n";
    for(const auto& device : devices)
    {
        std::cout << "device: " << device.second->getAddress() << std::endl;
        std::cout << "path: " << device.second->getPath() << std::endl;
        std::cout << "discovered: " << device.second->getDiscovered() << std::endl;
        std::cout << "connected: " << device.second->getConnected() << std::endl;
        std::cout << "paired: " << device.second->getPaired() << std::endl << std::endl;
    }

    while(true)
    {
        std::string cmd;
        //std::cout << "Enter cmd: ";
        std::cin >> cmd; 

        if(cmd == "link")
        {
            Link_Devices(connection);

            std::cout << "Device List---\n";
            std::lock_guard<std::mutex> lock(devicesMutex);
            for(const auto& device : devices)
            {
                std::cout << "device: " << device.second->getAddress() << std::endl;
                std::cout << "path: " << device.second->getPath() << std::endl;
                std::cout << "discovered: " << device.second->getDiscovered() << std::endl;
                std::cout << "connected: " << device.second->getConnected() << std::endl;
                std::cout << "paired: " << device.second->getPaired() << std::endl << std::endl;
            }
        }
        if(cmd == "print")
        {
            std::cout << "Device List---\n";
            std::lock_guard<std::mutex> lock(devicesMutex);
            for(const auto& device : devices)
            {
                std::cout << "device: " << device.second->getAddress() << std::endl;
                std::cout << "path: " << device.second->getPath() << std::endl;
                std::cout << "discovered: " << device.second->getDiscovered() << std::endl;
                std::cout << "connected: " << device.second->getConnected() << std::endl;
                std::cout << "trusted: " << device.second->getTrusted() << std::endl;
                std::cout << "paired: " << device.second->getPaired() << std::endl;
                for(const auto& [uuid, path] : device.second->getCharacteristics())
                {
                    if(uuid == ch) std::cout << "---";
                    std::cout << "characteristic: " << uuid << "- " << path << std::endl;
                }
                std::cout << std::endl;
            }
        }
        else if(cmd == "connect")
        {
            std::shared_ptr<BLEDevice> dev;
            { 
                std::lock_guard<std::mutex> lock(devicesMutex);
                dev = devices[mac];
            }
            connectDevice(connection, dev);
        }
        else if(cmd == "disconnect")
        {
            std::shared_ptr<BLEDevice> dev;
            { 
                std::lock_guard<std::mutex> lock(devicesMutex);
                dev = devices[mac];
            }
            DisconnectDevice(*dev);
        }
        else if(cmd == "pair")
        {
            std::shared_ptr<BLEDevice> dev;
            { 
                std::lock_guard<std::mutex> lock(devicesMutex);
                dev = devices[mac];
            }
            pairDevice(connection, dev);
        }
        else if(cmd == "read")
        {
            std::shared_ptr<BLEDevice> dev;
            { 
                std::lock_guard<std::mutex> lock(devicesMutex);
                dev = devices[mac];
            }
            std::string ans = ReadCharacteristic(connection, *dev, ch, ValueType::HEX);
            std::cout << ans << std::endl;
        }
        else if(cmd == "exit") break;

    }

    connection->leaveEventLoop();
    loopThread.join();

    for (auto& [mac, dev] : devices) {
        dev->proxy.reset();
    }
    Proxy.reset();

    return 0;
}