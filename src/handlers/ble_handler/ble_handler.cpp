#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>

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

struct BLEDevice {
    std::string address;       // MAC address
    std::string path;          // D-Bus object path
    std::string name;
    bool discovered = false;
    bool connected = false;
    bool paired = false;
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
        if (stopRequested) stopRequested->store(true); // checks if stopRequested pointer is null then -> request stop
        if (worker.joinable()) {
            try {
                worker.join(); // wait for the worker thread to finish
            } catch (const std::system_error& e) {
                std::cerr << "Error joining scan thread: " << e.what() << std::endl;
            }
        }
    }
};

bool set_bool_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, const std::string& propertyName, bool value);
bool get_bool_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName);
std::string get_string_property(const std::shared_ptr<sdbus::IConnection>& connection, const std::string& devicePath, std::string propertyName);
bool pairDevice(const std::shared_ptr<sdbus::IConnection>& connection, BLEDevice& device,
                int maxRetries = 3, int timeoutMs = 10000);
bool connectDevice(const std::shared_ptr<sdbus::IConnection>& connection, std::shared_ptr<BLEDevice>& device,
                   int maxRetries = 3, int timeoutMs = 10000);
bool DisconnectDevice(const std::shared_ptr<sdbus::IConnection>& connection, BLEDevice& device);
void Link_Devices(const std::shared_ptr<sdbus::IConnection>& connection, int scanTimeSec = 40);
void add_device(const std::string mac, std::map<std::string, std::string> characteristics);

std::unordered_map<std::string, std::shared_ptr<BLEDevice>> devices; //key = mac address
std::mutex devicesMutex;

void add_device(const std::string mac, std::map<std::string, std::string> characteristics)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    if (devices.find(mac) == devices.end()) 
    {
        auto dev = std::make_shared<BLEDevice>();
        dev->address = mac;
        dev->characteristics = characteristics;
        devices[mac] = dev;
        std::cout << "saved BLE device: " << mac  << std::endl;
    }
}

ScanHandle scanDevices(const std::shared_ptr<sdbus::IConnection>& connection,
                       std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<BLEDevice>>>& discovered,
                       std::mutex& discoveredMutex,
                       int scanDurationMs = 0)  // 0 = run until manually stopped
{
    ScanHandle handle;
    handle.proxy   = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    auto adapter   = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, ADAPTER_PATH);

    // 1. Populate with already-known devices
    std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects;
    handle.proxy->callMethod("GetManagedObjects")
        .onInterface(DBUS_OM_IFACE)
        .storeResultsTo(managedObjects);

    for (const auto& [path, interfaces] : managedObjects) 
    {
        auto it = interfaces.find(DEVICE_IFACE);
        if (it != interfaces.end()) 
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

                    discovered->emplace(mac, dev);
                    // publish "already known device found"
                }
            }
        }
    }

    // 2. Register signal handlers for ongoing discovery
    handle.proxy->uponSignal("InterfacesAdded")
        .onInterface(DBUS_OM_IFACE)
        .call([&](const sdbus::ObjectPath& path,
                  const std::map<std::string, std::map<std::string, sdbus::Variant>>& ifaces) {
            auto it = ifaces.find(DEVICE_IFACE);
            if (it != ifaces.end()) 
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

                        discovered->emplace(mac, dev);
                        // publish "newly discovered device"
                    }
                }
            }
    });

    handle.proxy->uponSignal("InterfacesRemoved")
        .onInterface(DBUS_OM_IFACE)
        .call([&](const sdbus::ObjectPath& path,
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
                        it2 = discovered->erase(it2);
                    } else {
                        ++it2;
                    }
                }
            }
    });

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
void Link_Devices(const std::shared_ptr<sdbus::IConnection>& connection, int scanTimeSec)
{
    std::unordered_map<std::string, std::map<std::string, std::map<std::string, sdbus::Variant>>> discovered;
    std::vector<std::string> Devices_Mac;

    auto adapter = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, ADAPTER_PATH);
    std::atomic<bool> DiscoverDone{false};

    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        for(const auto& [mac, device]: devices) Devices_Mac.push_back(mac);
    }

    auto proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
    proxy->uponSignal("InterfacesAdded")
        .onInterface(DBUS_OM_IFACE)
        .call([&](const ObjectPath& objectPath,
                  const std::map<std::string, std::map<std::string, Variant>>& interfaces) {
            auto it = interfaces.find(DEVICE_IFACE);
            if (it != interfaces.end())
            {
                const auto& props = it->second;
                if (props.count("Address"))
                {
                    auto str = props.at("Address").get<std::string>();
                    auto it1 = std::find(Devices_Mac.begin(), Devices_Mac.end(), str);
                    if(it1 != Devices_Mac.end())
                    {
                        discovered[objectPath] = interfaces;
                        Devices_Mac.erase(it1);
                    }
                }
            }
            if(Devices_Mac.empty()) 
            {
                std::cout << "All BLE Devices found\n";
                DiscoverDone = true;
            }
    });
    proxy->uponSignal("InterfacesRemoved")
        .onInterface(DBUS_OM_IFACE)
        .call([&](const sdbus::ObjectPath& objectPath,
                  const std::vector<std::string>& interfaces){
            for(const auto& iface : interfaces)
            {
                if(iface == DEVICE_IFACE)
                {
                    auto it = discovered.find(objectPath);
                    if (it != discovered.end())
                    {
                        const auto& props = it->second;
                        auto it2 = props.find(DEVICE_IFACE);
                        if (it2 != props.end())
                        {
                            const auto& props2 = it2->second;
                            std::string address = props2.at("Address").get<std::string>();

                            discovered.erase(it);
                            Devices_Mac.push_back(address);
                        }
                    }
                }
            }
    });
    proxy->finishRegistration();

    std::map<ObjectPath, std::map<std::string, std::map<std::string, Variant>>> managedObjects;

    proxy->callMethod("GetManagedObjects")
        .onInterface(DBUS_OM_IFACE)
        .storeResultsTo(managedObjects);
    
    int managedDevices = 0;

    for (const auto& [path,interfaces] : managedObjects)
    {
        auto it = interfaces.find(DEVICE_IFACE);
        if(it != interfaces.end())
        {
            managedDevices++;

            const auto& props = it->second;
            if (props.count("Address"))
            {
                auto str = props.at("Address").get<std::string>();
                auto it1 = std::find(Devices_Mac.begin(), Devices_Mac.end(), str);
                if(it1 != Devices_Mac.end())
                {
                    discovered[path] = interfaces;
                    Devices_Mac.erase(it1);
                }
            }
        }
    }

    if(!Devices_Mac.empty())
    {
        //Launch timer thread to stop discovery after timeout
        std::thread timerThread([&DiscoverDone, scanTimeSec]() {
            for (int i = 0; i < scanTimeSec * 100; ++i) {  // check every 10ms
                if (DiscoverDone) return;  // exit early if devices found
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!DiscoverDone) {
                std::cout << "\nTime is up! Stopping discovery...\n";
                DiscoverDone = true;
            }
        });

        // Start discovery on hci0
        try {
            adapter->callMethod("StartDiscovery").onInterface(ADAPTER_IFACE);
        } 
        catch (const sdbus::Error& e) {
            std::cerr << "Faild to start discovery: " << e.getName() << " - " << e.getMessage() << "\n";
            return;
        }

        std::cout << "Scanning for Bluetooth devices...\n";

        while (!DiscoverDone) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        try {
            adapter->callMethod("StopDiscovery").onInterface(ADAPTER_IFACE);
        } 
        catch (const sdbus::Error& e) {
            std::cerr << "Faild to stop discovery: " << e.getName() << " - " << e.getMessage() << "\n";
        }

        std::cout << "scanning ended\n";
        timerThread.join();
    }
    else std::cout << "All Devices found (No scanning needed)\n";

    proxy.reset();

    if(!Devices_Mac.empty()){
        std::cout << "Bluetooth devices not found:\n";
        for(const auto& Mac : Devices_Mac)
        {
            std::cout << "Address: " << Mac << std::endl;;
        }
    }

    for(const auto& [path, ifaces] : discovered)
    {
        auto it = ifaces.find(DEVICE_IFACE);
        const auto& props = it->second;
        auto mac = props.at("Address").get<std::string>();
        std::shared_ptr<BLEDevice> dev;
        {
            std::lock_guard<std::mutex> lock(devicesMutex);
            if (devices.find(mac) == devices.end()) continue;
            dev = devices[mac];
        }

        {
            std::lock_guard<std::mutex> lock(dev->mtx);
            dev->discovered = true;
            dev->path = path;
            dev->name = props.at("Name").get<std::string>();
        }
        std::cout << "Added BLE device path: " << path << " to " << mac << std::endl;

        bool connected = get_bool_property(connection, path, "Connected");
        if (!connected) {
            connected = connectDevice(connection, dev);
        }

        bool paired = get_bool_property(connection, path, "Paired");
        if (!paired) {
            paired = pairDevice(connection, *dev);
        }

        //get characteristics
        /*it would be better to create BLE Device map and fill it first from the hub info and then call link devices*/
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
                     BLEDevice& device,
                     int maxRetries,
                     int timeoutMs)
{
    std::string path = device.getPath();

    if(!device.getDiscovered() || path.empty()) 
    {
        std::cerr << "[WARN] Device " << device.getAddress() << " not discovered yet, skipping.\n";
        return false;
    }

    if(get_bool_property(connection, path, "Paired")) 
    {
        std::cout << "[OK] Device already paired" << std::endl;
        device.setPaired(true);
        return true;
    }

    auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        std::cout << "[INFO] Pair attempt " << attempt << " for " << path << std::endl;
        bool paired = false;

        try {
            deviceProxy->callMethod("Pair")
                       .onInterface(DEVICE_IFACE)
                       .withTimeout(std::chrono::milliseconds(timeoutMs));

            // Check paired property after method call
            paired = deviceProxy->getProperty("Paired")
                                .onInterface(DEVICE_IFACE)
                                .get<bool>();
        } catch (const sdbus::Error& e) {
            std::cerr << "[ERROR] Pair attempt " << attempt
                      << " failed: " << e.getName() << " - " << e.getMessage() << std::endl;
            if(e.getName() == "org.bluez.Error.InProgress")
            {
                try {
                    deviceProxy->callMethod("CancelPairing").onInterface(DEVICE_IFACE);
                    continue;
                } catch (const sdbus::Error& e) {
                    std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << "\n"; 
                    // Ignore errors
                }
            }
        }

        if (paired) {
            std::cout << "[OK] Device paired successfully on attempt " << attempt << std::endl;
            device.setPaired(true);

            bool connected = get_bool_property(connection, path, "Connected");
            device.setConnected(connected);

            if(!get_bool_property(connection, path, "Trusted")) set_bool_property(connection, path, "Trusted", true);

            return true;
        }

        // If not last attempt, wait and retry
        if (attempt < maxRetries) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    bool connected = get_bool_property(connection, path, "Connected");
    device.setConnected(connected);

    std::cerr << "[FAIL] Device failed to pair after " << maxRetries << " attempts" << std::endl;
    return false;
}

bool connectDevice(const std::shared_ptr<sdbus::IConnection>& connection,
                        std::shared_ptr<BLEDevice>& device,
                        int maxRetries,
                        int timeoutMs)
{
    std::string path = device->getPath();

    if (!device->getDiscovered() || path.empty()) 
    {
        std::cerr << "[WARN] Device " << device->getAddress() << " not discovered yet, skipping.\n";
        return false;
    }

    if(get_bool_property(connection, path, "Connected")) 
    {
        std::cout << "[OK] Device already connected" << std::endl;
        device->setConnected(true);
        return true;
    }

    auto rawProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        std::cout << "[INFO] Connect attempt " << attempt << " for " << path << std::endl;
        bool connected = false;

        try {
            rawProxy->callMethod("Connect")
                       .onInterface(DEVICE_IFACE)
                       .withTimeout(std::chrono::milliseconds(timeoutMs));

            connected = rawProxy->getProperty("Connected")
                                   .onInterface(DEVICE_IFACE)
                                   .get<bool>();
        } catch (const sdbus::Error& e) {
            std::cerr << "[ERROR] Connect attempt " << attempt
                      << " failed: " << e.getName() << " - " << e.getMessage() << std::endl;
        }

        if (connected) {
            std::cout << "[OK] Device connected successfully on attempt " << attempt << std::endl;
            device->setConnected(true);

            // Convert unique_ptr -> shared_ptr
            auto proxyShared = std::shared_ptr<sdbus::IProxy>(std::move(rawProxy));
            device->setProxy(proxyShared);

            // Capture weak_ptr to device so handler won’t access dangling memory
            std::weak_ptr<BLEDevice> weakDev = device;

            //Add - listen for PropertiesChanged signal on connected to see if it's still connected.
            device->proxy->uponSignal("PropertiesChanged")
                .onInterface(PROPERTIES_IFACE)
                .call([&](const std::string& interface,
                          const std::map<std::string, sdbus::Variant>& changed,
                          const std::vector<std::string>& invalidated) {
                    if (interface == DEVICE_IFACE) {
                        auto it = changed.find("Connected"); //maybe check paired & service resolved too
                        if (it != changed.end()) {
                            if (auto dev = weakDev.lock()) { // ✅ safe
                                bool connected = it->second.get<bool>();
                                dev->setConnected(connected);
                                std::cout << "Device " << dev->getAddress()
                                          << " updated connection: " << connected << std::endl;
                            }
                        }
                    }
                    //Publish change to MQTT
            });
            device->proxy->finishRegistration();

            return true;
        }

        // If not last attempt, wait and retry
        if (attempt < maxRetries) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            try {
                rawProxy->callMethod("Disconnect").onInterface(DEVICE_IFACE);
            } catch (const sdbus::Error& e) {
                std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << "\n"; 
                // Ignore errors
            }
        }
    }

    std::cerr << "[FAIL] Device failed to connect after " << maxRetries << " attempts" << std::endl;
    return false;
}

bool DisconnectDevice(const std::shared_ptr<sdbus::IConnection>& connection, BLEDevice& device) {
    std::string path = device.getPath();
    try {
        auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, path);
        deviceProxy->callMethod("Disconnect").onInterface(DEVICE_IFACE);
        std::cout << "Disonnected OK to " << path << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        device.setConnected(get_bool_property(connection, path, "Connected"));
        return true;
    } catch (const sdbus::Error& e) {
        std::cerr << "Failed to Disconnect: " << e.getName() << " - " << e.getMessage() << std::endl;
        if (e.getName() == "org.freedesktop.DBus.Error.UnknownObject") {
            std::cerr << "Device not found. Try scanning first." << std::endl;
        }
        return false;
    }
}

int main(int argc, char* argv[])
{
    std::shared_ptr<sdbus::IConnection> connection = sdbus::createSystemBusConnection();
    
    // Run the event loop in a background thread
    std::thread loopThread([&] {
        connection->enterEventLoop();
    });

    std::string mac = "38:39:8F:82:18:7E";
    std::map<std::string, std::string> characteristics;
    add_device(mac, characteristics);

    Link_Devices(connection);

    std::cout << "Device List---\n";
    for(const auto& device : devices)
    {
        std::cout << "device: " << device.second->address << std::endl;
        std::cout << "path: " << device.second->path << std::endl;
        std::cout << "connected: " << device.second->connected << std::endl;
        std::cout << "paired: " << device.second->paired << std::endl << std::endl;
    }

    // Keep running, or wait for a key press
    std::cout << "Press ENTER to quit...\n";
    std::cin.get();

    connection->leaveEventLoop();
    loopThread.join();

    for (auto& [mac, dev] : devices) {
        dev->proxy.reset();
    }

    return 0;
}