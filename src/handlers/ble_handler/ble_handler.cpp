#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>

using sdbus::ObjectPath;
using sdbus::Variant;

const std::string BLUEZ_SERVICE_NAME = "org.bluez";
const std::string DBUS_OM_IFACE = "org.freedesktop.DBus.ObjectManager";
const std::string ADAPTER_PATH = "/org/bluez/hci0";
const std::string ADAPTER_IFACE = "org.bluez.Adapter1";
const std::string DEVICE_IFACE = "org.bluez.Device1";
const std::string PROPERTIES_IFACE = "org.freedesktop.DBus.Properties";

// Hold discovered device info by path
std::unordered_map<std::string, std::map<std::string, std::map<std::string, sdbus::Variant>>> discovered;

// Keep device proxies alive to receive PropertiesChanged signals
std::unordered_map<std::string, std::unique_ptr<sdbus::IProxy>> deviceProxies;

void handlePropertiesChanged(const std::string& interface,
                             const std::map<std::string, sdbus::Variant>& changed,
                             const std::vector<std::string>& /*invalidated*/,
                             const ObjectPath& objectPath) //not signal variable
{
    if (interface != DEVICE_IFACE) return;

    auto& props = discovered[objectPath][DEVICE_IFACE];

    for (const auto& [key, val] : changed) props[key] = val;

    std::cout << "CHG path : " << objectPath << std::endl;
    if(props.count("Address"))
        std::cout  << "CHG bdaddr: " << props.at("Address").get<std::string>() << std::endl;
    if(props.count("Name"))
        std::cout  << "CHG name: " << props.at("Name").get<std::string>() << std::endl;
    if(props.count("RSSI"))
        std::cout  << "CHG RSSI: " << props.at("RSSI").get<int16_t>() << std::endl;

    std::cout << "---------------------------------" << std::endl;
}

void handleInterfacesAdded(const sdbus::ObjectPath& objectPath,
                           const std::map<std::string, std::map<std::string, sdbus::Variant>>& interfaces,
                           const std::shared_ptr<sdbus::IConnection>& connection) //not signal variable
{
    auto it = interfaces.find(DEVICE_IFACE);
    if (it == interfaces.end())
        return;

    std::cout << "- New device found at: " << objectPath << "\n";
    const auto& props = it->second;

    if (props.count("Address"))
        std::cout << "   Address: " << props.at("Address").get<std::string>() << "\n";

    if (props.count("Name"))
        std::cout << "   Name   : " << props.at("Name").get<std::string>() << "\n";

    if (props.count("RSSI"))
        std::cout << "   RSSI   : " << props.at("RSSI").get<int16_t>() << "\n";

    std::cout << "--------------------------\n";

    // Store it
    discovered[objectPath] = interfaces;

    // Register PropertiesChanged handler for this device
    auto deviceProxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, objectPath);

    deviceProxy->uponSignal("PropertiesChanged")
                .onInterface(PROPERTIES_IFACE)
                .call([objectPath](const std::string& interface,
                                   const std::map<std::string, sdbus::Variant>& changed,
                                   const std::vector<std::string>& invalidated) {
                    handlePropertiesChanged(interface, changed, invalidated, objectPath);
    });

    deviceProxy->finishRegistration();

    deviceProxies[objectPath] = std::move(deviceProxy);
}

void handleInterfacesRemoved(const sdbus::ObjectPath& objectPath,
                              const std::vector<std::string>& interfaces)
{
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
                    if (props2.count("Address")) 
                    {
                        std::string address = props2.at("Address").get<std::string>();
                        std::cout << "DEL bdaddr: " << address << std::endl;
                    } 
                    else 
                    {
                        std::cout << "DEL path: " << objectPath << std::endl;
                    }
                }

                discovered.erase(it);
                deviceProxies.erase(objectPath);

                std::cout << "--------------------------------" << std::endl;
            }
        }
    }
}

int main(int argc, char* argv[])
{

    if(argc != 2) {
        std::cerr << "Usage Error. syntex= ./ble_handler [scan_time_in_seconds]\n";
        return 1;
    }
    int scanTimeSec = std::stoi(argv[1]);

    try {
        
        std::shared_ptr<sdbus::IConnection> connection = sdbus::createSystemBusConnection();

        // Subscribe to InterfacesAdded signal
        auto proxy = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, "/");
        proxy->uponSignal("InterfacesAdded")
            .onInterface(DBUS_OM_IFACE)
            .call([connection](const ObjectPath& objectPath,
                               const std::map<std::string, std::map<std::string, Variant>>& interfaces) {
                handleInterfacesAdded(objectPath, interfaces, connection);
        });
        proxy->uponSignal("InterfacesRemoved")
            .onInterface(DBUS_OM_IFACE)
            .call(&handleInterfacesRemoved);
        proxy->finishRegistration();

        // Start discovery on hci0
        auto adapter = sdbus::createProxy(*connection, BLUEZ_SERVICE_NAME, ADAPTER_PATH);
        adapter->callMethod("StartDiscovery").onInterface(ADAPTER_IFACE);

        std::cout << "Scanning for Bluetooth devices...\n";

        std::atomic<bool> done = false;

        //Launch timer thread to stop discovery after timeout
        std::thread timerThread([&adapter, &connection, &done, scanTimeSec]() {
            std::this_thread::sleep_for(std::chrono::seconds(scanTimeSec));

            std::cout << "\n Time is up! Stopping discovery...\n";

            try {
                adapter->callMethod("StopDiscovery").onInterface(ADAPTER_IFACE);
            } 
            catch (const sdbus::Error& e) {
                std::cerr << "Faild to stop discovery: " << e.getName() << " - " << e.getMessage() << "\n";
            }

            done = true;
            connection->leaveEventLoop();
        });

        connection->enterEventLoop();

        discovered.clear();
        deviceProxies.clear();

        timerThread.join();
        std::cout << "Discovery complete. \n";
        
    }
    catch (const sdbus::Error &e)
    {
        std::cerr << "D-Bus error: " << e.getName() << " - " << e.getMessage() << std::endl;
        return 1;
    }

    return 0;
}


/*
// Connect to the system bus
        auto connection = sdbus::createSystemBusConnection();

        // Create proxy to BlueZ root object
        auto objectManager = sdbus::createProxy(*connection, "org.bluez", "/");
        objectManager->uponSignal("InterfacesAdded")
            .onInterface("org.freedesktop.DBus.ObjectManager")
            .call([](const sdbus::ObjectPath &objectPath, const std::map<std::string, std::map<std::string, sdbus::Variant>> &) {
                std::cout << "New interface added at: " << objectPath << std::endl;
            });

        // Call GetManagedObjects to retrieve all managed BlueZ interfaces
        std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects;
        objectManager->callMethod("GetManagedObjects")
            .onInterface("org.freedesktop.DBus.ObjectManager")
            .storeResultsTo(managedObjects);

        std::cout << "Available Bluetooth adapters:\n";

        for (const auto &obj : managedObjects) {
            const auto &objectPath = obj.first;
            const auto &interfaces = obj.second;

            if (interfaces.find("org.bluez.Adapter1") != interfaces.end()) {
                std::cout << " - " << objectPath << "\n";
            }
        }
*/

//38:39:8F:82:18:7E motion detector address