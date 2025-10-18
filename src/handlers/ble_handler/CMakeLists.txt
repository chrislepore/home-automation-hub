cmake_minimum_required(VERSION 3.10)
project(ble_handler)

set(CMAKE_CXX_STANDARD 17)

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUS REQUIRED sdbus-c++)

find_package(nlohmann_json REQUIRED)

# --- Target ---
add_executable(ble_handler
    ble_handler.cpp
)

# --- Includes ---
target_include_directories(ble_handler PRIVATE
    ${SDBUS_INCLUDE_DIRS}
    /usr/include           # paho-mqtt-c headers
    /usr/include/paho-mqttpp3  # paho-mqttpp headers
)

# --- Libraries ---
target_link_libraries(ble_handler PRIVATE
    ${SDBUS_LIBRARIES}
    nlohmann_json::nlohmann_json
    paho-mqttpp3          # C++ wrapper
    paho-mqtt3a           # C library (non-SSL)
    # paho-mqtt3as        # C library (SSL/TLS) → uncomment if you need secure broker
)
