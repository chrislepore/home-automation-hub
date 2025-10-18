#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <mqtt/async_client.h>

using json = nlohmann::json;
using sdbus::ObjectPath;
using sdbus::Variant;

//SDBUS constants
const std::string BLUEZ_SERVICE_NAME = "org.bluez";
const std::string DBUS_OM_IFACE = "org.freedesktop.DBus.ObjectManager";
const std::string ADAPTER_PATH = "/org/bluez/hci0";
const std::string ADAPTER_IFACE = "org.bluez.Adapter1";
const std::string DEVICE_IFACE = "org.bluez.Device1";
const std::string Service_IFACE = "org.bluez.GattService1";
const std::string Characteristic_IFACE = "org.bluez.GattCharacteristic1";
const std::string Descriptor_IFACE = "org.bluez.GattDescriptor1";
const std::string PROPERTIES_IFACE = "org.freedesktop.DBus.Properties";

//MQTT output topic
const std::string OUTPUT_TOPIC{"home-automation/hub"};

//strusts & enum
struct BLEDevice {
    std::string address;       // MAC address
    std::string path;          // D-Bus object path
    std::string name;
    bool discovered = false;
    bool connected = false;
    bool paired = false;
    bool trusted = false;
    std::unordered_map<std::string, std::string> characteristics; //key=UUID value=Path
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

    void setCharacteristics(const std::unordered_map<std::string, std::string>& value) {
        std::lock_guard<std::mutex> lock(mtx);
        characteristics = value;
    }

    std::unordered_map<std::string, std::string> getCharacteristics() {
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

//prototypes 
bool set_bool_property(const std::string& devicePath, const std::string& propertyName, bool value);
bool get_bool_property(const std::string& devicePath, std::string propertyName);
std::string get_string_property(const std::string& devicePath, std::string propertyName);

ScanHandle scanDevices(std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>& discovered, std::mutex& discoveredMutex,
                       int scanDurationMs = 0); // 0 = run until manually stopped
bool pairDevice(const std::shared_ptr<BLEDevice>& device, int maxRetries = 3, int timeoutMs = 5000);
bool connectDevice(const std::shared_ptr<BLEDevice>& device, int maxRetries = 3, int timeoutMs = 5000);
bool DisconnectDevice(BLEDevice& device);
void Link_Devices(int scanTimeMs = 20000);
std::unordered_map<std::string, std::string> getCharacteristics(
    const sdbus::ObjectPath& devPath,
    const std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>& managedObjects);
void handleDevicePropertiesChanged(
    std::weak_ptr<BLEDevice> weakDev,
    std::weak_ptr<sdbus::IConnection> weakCon,
    const std::string& interface,
    const std::map<std::string, sdbus::Variant>& changed,
    const std::vector<std::string>& invalidated);

void add_device(const std::string mac);
void remove_device(const std::string mac);
std::shared_ptr<BLEDevice> get_device(const std::string mac);

//Global variables
std::shared_ptr<sdbus::IConnection> connection = sdbus::createSystemBusConnection();

std::unordered_map<std::string, std::shared_ptr<BLEDevice>> devices; //key = mac address
std::mutex devicesMutex;

mqtt::async_client client("tcp://localhost:1883", "BLE_handler");

void add_device(const std::string mac)
{
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        if (devices.find(mac) != devices.end()) return;
    }

    auto dev = std::make_shared<BLEDevice>();
    dev->address = mac;

    //see if device is discovered
    auto proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects;
    try {
        proxy->callMethod("GetManagedObjects")
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
                auto address = props.at("Address").get<std::string>();

                if (address == mac) 
                {
                    dev->path       = path;
                    dev->name       = props.count("Name") ? props.at("Name").get<std::string>() : "";
                    dev->discovered = true;
                    dev->connected  = props.count("Connected") ? props.at("Connected").get<bool>() : false;
                    dev->paired     = props.count("Paired") ? props.at("Paired").get<bool>() : false;
                    dev->trusted    = props.count("Trusted") ? props.at("Trusted").get<bool>() : false;
                    dev->characteristics = getCharacteristics(path, managedObjects);

                    //---create signal handler---
                    std::shared_ptr<sdbus::IProxy> proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);
                    dev->setProxy(proxy);

                    // Capture weak_ptr to device so handler won’t access dangling memory
                    std::weak_ptr<BLEDevice> weakDev = dev;
                    std::weak_ptr<sdbus::IConnection> weakCon = connection;

                    dev->proxy->uponSignal("PropertiesChanged")
                        .onInterface(PROPERTIES_IFACE)
                        .call([weakDev, weakCon](const std::string& interface,
                                const std::map<std::string, sdbus::Variant>& changed,
                                const std::vector<std::string>& invalidated) {
                            handleDevicePropertiesChanged(weakDev, weakCon, interface, changed, invalidated);
                    });
                    dev->proxy->finishRegistration();

                    continue;
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        if (devices.find(mac) != devices.end()) return;
        devices[mac] = dev;
    }

    std::cout << "Device added: " << mac << std::endl;
    json j;
    j["origin"] = "ble_handler";
    j["type"] = "device_added";
    {
        std::lock_guard<std::mutex> lock(dev->mtx);
        j["device_mac"] = dev->address;
        j["name"] = dev->name;
        j["discovered"] = dev->discovered;
        j["connected"] = dev->connected;
        j["paired"] = dev->paired;
        j["trusted"] = dev->trusted;
    }

    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
    client.publish(pubmsg);
}

void remove_device(const std::string mac)
{
    std::shared_ptr<BLEDevice> dev;
    json j;
    j["origin"] = "ble_handler";
    j["type"] = "device_removed";

    // Step 1: Lock and extract the device safely
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        auto it = devices.find(mac);
        if (it == devices.end())
        {
            std::cout << "[Error] Device removed: Device not found-> " << mac << std::endl;
            j["Error"] = "Device not found";
            mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
            client.publish(pubmsg);
            return;  // Device not found
        }

        dev = it->second;      // Keep a shared_ptr copy
        devices.erase(it);     // Erase from map immediately
    }

    // Step 2: Disconnect safely outside the devicesMutex
    // This avoids deadlocks if DisconnectDevice triggers signal callbacks
    if (dev) {
        DisconnectDevice(*dev);       // Disconnect BLE device
        dev->proxy.reset();           // Reset proxy to stop further calls
    }

    // Step 3: After this, dev will go out of scope, freeing memory safely

    std::cout << "Device removed: " << mac << std::endl;
    j["device_mac"] = mac;
    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
    client.publish(pubmsg);
}


std::shared_ptr<BLEDevice> get_device(const std::string mac)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    if (devices.find(mac) == devices.end()) return nullptr;
    std::shared_ptr<BLEDevice> dev = devices[mac];
    return dev;
}

void handleDevicePropertiesChanged(
    std::weak_ptr<BLEDevice> weakDev,
    std::weak_ptr<sdbus::IConnection> weakCon,
    const std::string& interface,
    const std::map<std::string, sdbus::Variant>& changed,
    const std::vector<std::string>& invalidated)
{
    if (interface != DEVICE_IFACE)
        return;

    if (auto device = weakDev.lock()) { // ✅ safe
        bool updated = false;
        json j;
        std::string address = device->getAddress();
        j["origin"] = "ble_handler";
        j["type"] = "device_update";
        j["device_mac"] = address;

        // Connected
        auto it = changed.find("Connected");
        if (it != changed.end()) {
            bool connected = it->second.get<bool>();
            device->setConnected(connected);
            std::cout << "Device " << address
                      << " updated Connected: " << connected << std::endl;
            updated = true;
            j["connected"] = connected;

            if (connected) 
                if(!device->getTrusted()) set_bool_property(device->getPath(), "Trusted", true);
            else 
                device->setCharacteristics({});
        }

        // ServicesResolved (if true get characteristics)
        it = changed.find("ServicesResolved");
        if (it != changed.end()) {
            bool resolved = it->second.get<bool>();
            if (resolved) {
                if (auto connection = weakCon.lock()) { // ✅ safe
                    auto slashProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
                    std::map<sdbus::ObjectPath, std::map<std::string,
                        std::map<std::string, sdbus::Variant>>> managedObjects;

                    try {
                        slashProxy->callMethod("GetManagedObjects")
                                .onInterface(DBUS_OM_IFACE)
                                .storeResultsTo(managedObjects);
                    }
                    catch (const sdbus::Error& e) {
                        std::cerr << "Failed to GetManagedObjects: "
                                << e.getName() << " - " << e.getMessage() << "\n";
                    }
                    device->setCharacteristics(getCharacteristics(device->getPath(), managedObjects));
                }
            } else {
                device->setCharacteristics({});
            }
        }

        // Paired
        it = changed.find("Paired");
        if (it != changed.end()) {
            bool paired = it->second.get<bool>();
            device->setPaired(paired);
            std::cout << "Device " << address
                      << " updated Paired: " << paired << std::endl;
            updated = true;
            j["paired"] = paired;
        }

        // Trusted
        it = changed.find("Trusted");
        if (it != changed.end()) {
            bool trusted = it->second.get<bool>();
            device->setTrusted(trusted);
            std::cout << "Device " << address
                      << " updated Trusted: " << trusted << std::endl;
            updated = true;
            j["trusted"] = trusted;
        }

        it = changed.find("ServiceData");
        if (it != changed.end()) {
            try {
                // Extract the dictionary {UUID -> Variant(ByteArray)}
                const auto& serviceDataMap = it->second.get<std::map<std::string, sdbus::Variant>>();

                for (const auto& [uuid, variant] : serviceDataMap) {
                    // Get the byte array
                    const auto& data = variant.get<std::vector<uint8_t>>();

                    // Convert data to hex string
                    std::ostringstream oss;
                    oss << std::hex << std::setfill('0');
                    for (uint8_t byte : data)
                        oss << std::setw(2) << static_cast<int>(byte) << " ";

                    std::string dataStr = oss.str();

                    // Trim trailing space (optional)
                    if (!dataStr.empty() && dataStr.back() == ' ')
                        dataStr.pop_back();

                    // Print broadcast bytes
                    std::cout << "ServiceData from " << address 
                              << ", UUID " << uuid 
                              << ": " << dataStr << std::endl;

                    j["type"] = "device_broadcast";
                    j["service_data"]["uuid"] = uuid;
                    j["service_data"]["data"] = dataStr;

                    updated = true;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error decoding ServiceData: " << e.what() << std::endl;
            }
        }

        if (updated) {
            // Publish changes to MQTT or other system here
            mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
            client.publish(pubmsg);
        }
    }
}

std::unordered_map<std::string, std::string> getCharacteristics(
    const sdbus::ObjectPath& devPath,
    const std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>& managedObjects)
{
    std::unordered_map<std::string, std::string> result;

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

ScanHandle scanDevices(std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>& discovered,
                       std::mutex& discoveredMutex,
                       int scanDurationMs)  // 0 = run until manually stopped
{
    ScanHandle handle;
    handle.proxy   = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");

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
                    json j;
                    j["origin"] = "ble_handler";
                    j["type"] = "scan_existing_devices";
                    j["device_mac"] = dev->address;
                    j["name"] = dev->name;
                    j["discovered"] = dev->discovered;
                    j["connected"] = dev->connected;
                    j["paired"] = dev->paired;
                    j["trusted"] = dev->trusted;

                    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
                    client.publish(pubmsg);
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
                    json j;
                    j["origin"] = "ble_handler";
                    j["type"] = "scan_added_device";
                    j["device_mac"] = dev->address;
                    j["name"] = dev->name;
                    j["discovered"] = dev->discovered;
                    j["connected"] = dev->connected;
                    j["paired"] = dev->paired;
                    j["trusted"] = dev->trusted;

                    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
                    client.publish(pubmsg);
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
                        json j;
                        j["origin"] = "ble_handler";
                        j["type"] = "scan_removed_device";
                        j["device_mac"] = it2->second->address;

                        mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
                        client.publish(pubmsg);
                        it2 = discovered->erase(it2);
                    } else {
                        ++it2;
                    }
                }
            }
    });
    handle.proxy->finishRegistration();

    std::cout << "Scanning started..." << std::endl;
    try {
        auto adapter = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/org/bluez/hci0");

        adapter->callMethod("StopDiscovery").onInterface(ADAPTER_IFACE);
        std::this_thread::sleep_for(std::chrono::seconds(2)); // let BlueZ reset

        adapter->callMethod("StartDiscovery").onInterface(ADAPTER_IFACE);
        std::cout << "Discovery restarted to refresh visible devices." << std::endl;
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Failed to restart discovery: "
                  << e.getName() << " - " << e.getMessage() << std::endl;
    }

    // (4) Launch worker thread
    handle.worker = std::thread([scanDurationMs, 
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
    });

    return handle;
}

/**********************************************************************
|   Link_Devices() function scans for all saved devices and            |
|   connects/pairs to the saved devices it found then updates the      |
|   status                                                             |
|                    MAY BE OUTDATED                                   |
***********************************************************************/
void Link_Devices(int scanTimeMs)
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

    auto handle = scanDevices(discovered, discoveredMutex, scanTimeMs);

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
        std::shared_ptr<BLEDevice> original;
        {
            std::lock_guard<std::mutex> lock(devicesMutex);
            if (devices.find(mac) == devices.end()) continue;
            original = devices[mac];
        }

        // Copy over fields from the newly discovered BLEDevice
        original->setPath(dev->getPath());
        original->setDiscovered(dev->getDiscovered());
        original->setConnected(dev->getConnected());
        original->setPaired(dev->getPaired());
        original->setTrusted(dev->getTrusted());
        original->setCharacteristics(dev->getCharacteristics());

        auto path = original->getPath();

        //---create signal handler---
        std::shared_ptr<sdbus::IProxy> proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);
        original->setProxy(proxy);

        // Capture weak_ptr to device so handler won’t access dangling memory
        std::weak_ptr<BLEDevice> weakDev = original;
        std::weak_ptr<sdbus::IConnection> weakCon = connection;

        original->proxy->uponSignal("PropertiesChanged")
            .onInterface(PROPERTIES_IFACE)
            .call([weakDev, weakCon](const std::string& interface,
                    const std::map<std::string, sdbus::Variant>& changed,
                    const std::vector<std::string>& invalidated) {
                handleDevicePropertiesChanged(weakDev, weakCon, interface, changed, invalidated);
        });
        original->proxy->finishRegistration();

        std::cout << "Added BLE device path: " << path << " to " << mac << std::endl;

        if (!original->getConnected()) connectDevice(original);
        if (!original->getPaired()) pairDevice(original);
    }
}

bool get_bool_property(const std::string& devicePath, std::string propertyName)
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

bool set_bool_property(const std::string& devicePath, const std::string& propertyName, bool value)
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

std::string get_string_property(const std::string& devicePath, std::string propertyName)
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

bool pairDevice(const std::shared_ptr<BLEDevice>& device, int maxRetries, int timeoutMs)
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

bool connectDevice(const std::shared_ptr<BLEDevice>& device, int maxRetries, int timeoutMs)
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

            auto slashProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
            std::map<sdbus::ObjectPath, std::map<std::string,
                std::map<std::string, sdbus::Variant>>> managedObjects;

            try {
                slashProxy->callMethod("GetManagedObjects")
                        .onInterface(DBUS_OM_IFACE)
                        .storeResultsTo(managedObjects);
            }
            catch (const sdbus::Error& e) {
                std::cerr << "Failed to GetManagedObjects: "
                        << e.getName() << " - " << e.getMessage() << "\n";
            }
            device->setCharacteristics(getCharacteristics(device->getPath(), managedObjects));
            
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

std::string ReadCharacteristic(BLEDevice& device, const std::string& uuid)
{
    bool connected = device.getConnected();
    if(!connected) {
        return "Error: Device not connected";
    }

    // Find the characteristic path from the device
    auto characteristics = device.getCharacteristics();
    auto it = characteristics.find(uuid);
    if (it == characteristics.end()) {
        return ("Characteristic " + uuid + " not found for device");
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
    j["origin"] = "ble_handler";
    j["type"] = "read_characteristic";
    j["device_mac"] = device.getAddress();
    j["uuid"]  = uuid;

    // Raw hex string
    std::ostringstream rawHex;
    for (auto b : response) {
        rawHex << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(b);
    }
    j["data"] = rawHex.str();

    return j.dump(); // return JSON string (ready to publish)
}

bool WriteCharacteristic(BLEDevice& device, const std::string& uuid, const std::vector<uint8_t>& value, bool withResponse = true)
{
    // Find the characteristic path from the device
    auto characteristics = device.getCharacteristics();
    auto it = characteristics.find(uuid);
    if (it == characteristics.end() || !device.getConnected()) {
        return false;
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
        return false;
    }
    return true;
}

class callback : public virtual mqtt::callback
{
public:
    // Called when the client successfully connects to the broker
    void connected(const std::string& cause) override {
        std::cout << "Connected: " << cause << std::endl;
    }

    // Called when the connection is lost
    void connection_lost(const std::string& cause) override {
        std::cout << "Connection lost: " << cause << std::endl;
    }

    // Called when a message arrives on a subscribed topic
    void message_arrived(mqtt::const_message_ptr msg) override {
        try {
            std::cout << "Message received on topic '" 
                      << msg->get_topic() << "': " 
                      << msg->to_string() << std::endl;

            // Parse JSON
            auto j = json::parse(msg->to_string());

            if (!j.contains("command")) {
                std::cerr << "No command found in message!" << std::endl;
                return;
            }

            std::string command = j["command"];

            // Dispatch logic based on command type
            if (command == "add_devices") {
                for (const auto& mac : j["mac"]) {
                    std::cout << "Adding device " << mac << std::endl;
                    add_device(mac);
                }
            }
            else if (command == "add_discovered") {
                
            }
            else if (command == "remove_devices") {
                for (const auto& mac : j["mac"]) {
                    std::cout << "Removing device " << mac << std::endl;
                    remove_device(mac);
                }
            }
            else if (command == "print") {
                std::cout << "Device List---\n";
                {
                    std::lock_guard<std::mutex> lock(devicesMutex);
                    for(const auto& device : devices)
                    {
                        std::cout << "device: " << device.second->getAddress() << std::endl;
                        std::cout << "path: " << device.second->getPath() << std::endl;
                        std::cout << "discovered: " << device.second->getDiscovered() << std::endl;
                        std::cout << "connected: " << device.second->getConnected() << std::endl;
                        std::cout << "trusted: " << device.second->getTrusted() << std::endl;
                        std::cout << "paired: " << device.second->getPaired() << std::endl;
                        std::cout << "characteristics: " << std::endl;
                        for(const auto& [uuid, path] : device.second->getCharacteristics())
                        {
                            std::cout << "characteristic: " << uuid << "- " << path << std::endl;
                        }
                        std::cout  << std::endl;
                    }
                }
            }
            /*else if (command == "link_devices") {
                std::cout << "linking devices" << std::endl;
                std::thread([] {
                    Link_Devices();
                }).detach();
            }*/
            else if (command == "read_characteristic") {
                std::string mac = j["mac"];
                std::string uuid = j["uuid"];
                std::cout << "Reading characteristic " << uuid 
                        << " from device " << mac << std::endl;

                // Run BLE read in a separate thread to avoid blocking the MQTT callback
                std::thread([mac, uuid]() {
                    auto dev = get_device(mac);
                    if (!dev) {
                        std::cerr << "Device " << mac << " not found" << std::endl;
                        return;
                    }

                    // Check if device is connected before attempting read
                    if (!dev->getConnected()) {
                        json j_resp;
                        j_resp["origin"] = "ble_handler";
                        j_resp["type"] = "read_characteristic";
                        j_resp["device_mac"] = mac;
                        j_resp["uuid"] = uuid;
                        j_resp["error"] = "Device not connected";
                        mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j_resp.dump());
                        client.publish(pubmsg);  // async publish
                        return;
                    }

                    std::string response;
                    try {
                        response = ReadCharacteristic(*dev, uuid);  // already returns JSON string
                    } catch (const std::exception& e) {
                        json j_resp;
                        j_resp["origin"] = "ble_handler";
                        j_resp["type"] = "read_characteristic";
                        j_resp["device_mac"] = mac;
                        j_resp["uuid"] = uuid;
                        j_resp["error"] = e.what();
                        mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j_resp.dump());
                        client.publish(pubmsg);
                        return;
                    }

                    // Publish asynchronously
                    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, response);
                    client.publish(pubmsg);  // no ->wait(), async
                }).detach();  // detach thread
            }
            else if (command == "write_characteristic") {
                std::string mac = j["mac"];
                std::string uuid = j["uuid"];
                std::string value = j["value"];
                std::cout << "Writing " << value << " to characteristic " << uuid 
                          << " on device " << mac << std::endl;
                std::vector<uint8_t> bytes(value.begin(), value.end());
                WriteCharacteristic(*get_device(mac), uuid, {5, 0});
            }
            else if (command == "scan_devices_on") {
                std::cout << "Scanning devices..." << std::endl;
                // Start scanning logic
            }
            else if (command == "scan_devices_off") {
                std::cout << "Scanning devices stop" << std::endl;
                // Stop scanning logic
            }
            else if (command == "connect_device") {
                std::string mac = j["mac"];
                std::cout << "Connecting device " << mac << std::endl;
                connectDevice(get_device(mac), 1);
            }
            else if (command == "pair_device") {
                std::string mac = j["mac"];
                std::cout << "Pairing device " << mac << std::endl;
                pairDevice(get_device(mac), 1);
            }
            else {
                std::cerr << "Unknown command: " << command << std::endl;
            }

        } catch (const json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error handling MQTT message: " << e.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[])
{
    auto Proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    Proxy->uponSignal("InterfacesAdded")
        .onInterface(DBUS_OM_IFACE)
        .call([](const sdbus::ObjectPath& path,
                const std::map<std::string, std::map<std::string, sdbus::Variant>>& ifaces) {
            if (auto it = ifaces.find(DEVICE_IFACE); it != ifaces.end())
            {
                // Device discovered
                const auto& props = it->second;
                if (props.count("Address"))
                {
                    std::shared_ptr<BLEDevice> dev;
                    auto mac = props.at("Address").get<std::string>();
                    {
                        std::lock_guard<std::mutex> lock(devicesMutex);
                        auto devMap = devices.find(mac);
                        if(devMap == devices.end()) return;
                        dev = devMap->second;
                    }

                    dev->setPath        (path);
                    dev->setName        (props.count("Name") ? props.at("Name").get<std::string>() : "");
                    dev->setDiscovered  (true);
                    dev->setConnected   (props.count("Connected") ? props.at("Connected").get<bool>() : false);
                    dev->setPaired      (props.count("Paired") ? props.at("Paired").get<bool>() : false);
                    dev->setTrusted     (props.count("Trusted") ? props.at("Trusted").get<bool>() : false);

                    //---create signal handler---
                    std::shared_ptr<sdbus::IProxy> proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);
                    dev->setProxy(proxy);

                    // Capture weak_ptr to device so handler won’t access dangling memory
                    std::weak_ptr<BLEDevice> weakDev = dev;
                    std::weak_ptr<sdbus::IConnection> weakCon = connection;

                    dev->proxy->uponSignal("PropertiesChanged")
                        .onInterface(PROPERTIES_IFACE)
                        .call([weakDev, weakCon](const std::string& interface,
                                const std::map<std::string, sdbus::Variant>& changed,
                                const std::vector<std::string>& invalidated) {
                            handleDevicePropertiesChanged(weakDev, weakCon, interface, changed, invalidated);
                    });
                    dev->proxy->finishRegistration();

                    // publish "device added"
                    std::cout << "device discovered: " << path << std::endl;
                    json j;
                    j["origin"] = "ble_handler";
                    j["type"] = "device_update";
                    j["device_mac"] = dev->address;
                    j["name"] = dev->name;
                    j["discovered"] = dev->discovered;
                    j["connected"] = dev->connected;
                    j["paired"] = dev->paired;
                    j["trusted"] = dev->trusted;

                    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
                    client.publish(pubmsg);
                }
            }
    });
    Proxy->uponSignal("InterfacesRemoved")
        .onInterface(DBUS_OM_IFACE)
        .call([](const sdbus::ObjectPath& path,
                 const std::vector<std::string>& interfaces) {
            for(const auto& iface : interfaces)
            {
                if(iface == DEVICE_IFACE)
                {
                    auto pos = path.find("dev_");
                    if (pos == std::string::npos)
                        return;

                    std::string mac = path.substr(pos + 4); // skip "dev_"
                    std::replace(mac.begin(), mac.end(), '_', ':');
                    
                    std::shared_ptr<BLEDevice> dev;
                    {
                        std::lock_guard<std::mutex> lock(devicesMutex);
                        auto devMap = devices.find(mac);
                        if(devMap == devices.end()) return;
                        dev = devMap->second;
                    }
                    dev->setConnected(false);
                    dev->setPaired(false);
                    dev->setDiscovered(false);
                    dev->getProxy().reset();

                    std::cout << "device undiscovered: " << path << std::endl;
                    json j;
                    j["origin"] = "ble_handler";
                    j["type"] = "device_update";
                    j["device_mac"] = dev->address;
                    j["name"] = dev->name;
                    j["discovered"] = dev->discovered;
                    j["connected"] = dev->connected;
                    j["paired"] = dev->paired;
                    j["trusted"] = dev->trusted;

                    mqtt::message_ptr pubmsg = mqtt::make_message(OUTPUT_TOPIC, j.dump());
                    client.publish(pubmsg);
                }
            }
    });
    Proxy->finishRegistration();
    
    // Run the event loop in a background thread
    std::thread loopThread([&] {
        connection->enterEventLoop();
    });

    auto adapter = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, ADAPTER_PATH);
    try {
        adapter->callMethod("StartDiscovery").onInterface(ADAPTER_IFACE);
    } 
    catch (const sdbus::Error& e) {
        std::cerr << "Faild to start discovery: " << e.getName() << " - " << e.getMessage() << "\n";
    }

    //write code here
    const std::string INPUT_TOPIC{"home-automation/ble_handler"};  // Topic to subscribe to

    callback cb;
    client.set_callback(cb);

    // Connect to broker
    mqtt::connect_options connOpts;
    try {
        std::cout << "Connecting to the MQTT broker..." << std::endl;
        client.connect(connOpts)->wait();

        // Subscribe
        std::cout << "Subscribing to topic: " << INPUT_TOPIC << std::endl;
        client.subscribe(INPUT_TOPIC, 1)->wait();

        // Keep the program alive to receive messages
        while (true) {
            //std::this_thread::sleep_for(std::chrono::seconds(1));
            std::string cmd;
            std::cin >> cmd; 
            if(cmd == "exit") break;
        }

        // client.disconnect()->wait();
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        return 1;
    }

    try {
        adapter->callMethod("StopDiscovery").onInterface(ADAPTER_IFACE);
        std::cout << "Scanning stopped." << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "StopDiscovery failed: " << ex.what() << std::endl;
    }

    //close threads & exit loop
    connection->leaveEventLoop();
    loopThread.join();

    for (auto& [mac, dev] : devices) {
        dev->proxy.reset();
    }
    Proxy.reset();

    return 0;
}