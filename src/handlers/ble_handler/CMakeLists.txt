cmake_minimum_required(VERSION 3.10)
project(ble_handler)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUS REQUIRED sdbus-c++)

include_directories(${SDBUS_INCLUDE_DIRS})
link_directories(${SDBUS_LIBRARY_DIRS})

add_executable(ble_handler ble_handler.cpp)
target_link_libraries(ble_handler ${SDBUS_LIBRARIES} )