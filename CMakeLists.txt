cmake_minimum_required(VERSION 3.10)

# Fix Policy Warnings
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(isuzu_mfd)
set(CMAKE_C_STANDARD 99)

# Include Directories (First)
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lvgl
    ${CMAKE_SOURCE_DIR}/lv_drivers
    app
    can
    decode
    ui
)

# Configuration Variables
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h")
set(LV_LVGL_H_INCLUDE_SIMPLE ON)

# Disable Library Installation
set(LVGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LVGL_INSTALL_LIBRARY OFF CACHE BOOL "" FORCE)

# Add Subdirectories
add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

# Source Files
set(SOURCES
    app/main.c
    can/can_mgr.c
    decode/signals.c
    ui/ui.c
    ui/styles.c
)

add_executable(isuzu_mfd ${SOURCES})
target_link_libraries(isuzu_mfd lvgl lv_drivers m pthread)