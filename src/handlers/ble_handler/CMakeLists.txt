#cmake_minimum_required(VERSION 3.10)
#project(ble_handler)

#find_package(PkgConfig REQUIRED)
#pkg_check_modules(SDBUSPP REQUIRED sdbus-c++)

#add_executable(ble_handler ble_handler.cpp)

#target_include_directories(ble_handler PRIVATE ${SDBUSPP_INCLUDE_DIRS})
#target_link_libraries(ble_handler PRIVATE ${SDBUSPP_LIBRARIES})

# If using GLib loop
#pkg_check_modules(GLIB REQUIRED glib-2.0)
#target_include_directories(ble_handler PRIVATE ${GLIB_INCLUDE_DIRS})
#target_link_libraries(ble_handler PRIVATE ${GLIB_LIBRARIES})

# Optional for debugging:
#message(STATUS "Using sdbus-c++ from: ${SDBUSPP_INCLUDE_DIRS}")
#message(STATUS "Using GLib from: ${GLIB_INCLUDE_DIRS}")


cmake_minimum_required(VERSION 3.10)
project(ble_handler)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUS REQUIRED sdbus-c++)
pkg_check_modules(GLIB REQUIRED glib-2.0)

include_directories(${SDBUS_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS})
link_directories(${SDBUS_LIBRARY_DIRS} ${GLIB_LIBRARY_DIRS})

add_executable(ble_handler ble_handler.cpp)
target_link_libraries(ble_handler ${SDBUS_LIBRARIES} ${GLIB_LIBRARIES})