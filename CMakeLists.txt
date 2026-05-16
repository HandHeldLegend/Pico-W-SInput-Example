# Pico-W-SINPUT-Example build configuration
#
# Author: Mitchell Cairns
# Copyright (c) 2026 Hand Held Legend, LLC.
#
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International
# License (CC BY-NC 4.0). Non-commercial use with attribution; commercial use
# requires permission from Hand Held Legend, LLC. Licensing inquiries:
# support@handheldlegend.com
# Full terms: https://creativecommons.org/licenses/by-nc/4.0/legalcode
#
# SPDX-License-Identifier: CC-BY-NC-4.0

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Pico-W-SINPUT-Example C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Build the example firmware as a single executable with the two transport adapters.

add_executable(Pico-W-SINPUT-Example
    main.c 
    sinput_usb.c 
    sinput_btc.c 
    sinput_flash.c 
)

pico_set_program_name(Pico-W-SINPUT-Example "Pico-W-SINPUT-Example")
pico_set_program_version(Pico-W-SINPUT-Example "0.1")

# Keep UART stdio off GPIO0/GPIO1 so they can be used as boot straps.
target_compile_definitions(Pico-W-SINPUT-Example PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=12
    PICO_DEFAULT_UART_RX_PIN=13
)

# Core Pico runtime support used by the example and its simple platform hooks.
target_link_libraries(Pico-W-SINPUT-Example
        pico_stdlib
        pico_rand
)

# The example only needs CYW43 for Bluetooth, not Wi-Fi networking.
target_compile_definitions(Pico-W-SINPUT-Example
  INTERFACE
  CYW43_LWIP=0
)

# Local includes for the example sources.
target_include_directories(Pico-W-SINPUT-Example PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Import NS-LIB-HID library
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/external/SINPUT-LIB-HID")

# Link the protocol/descriptor library used by both USB and Bluetooth transports.
target_link_libraries(Pico-W-SINPUT-Example
    sinput_lib_hid
)

# TinyUSB powers the wired HID transport path.
target_link_libraries(Pico-W-SINPUT-Example 
    tinyusb_device
    tinyusb_board
)

# Pico W Bluetooth stack pieces required for the Classic HID path.
target_link_libraries(Pico-W-SINPUT-Example 
    pico_cyw43_arch_none 
    pico_cyw43_driver 
    pico_btstack_cyw43 
    pico_btstack_hci_transport_cyw43 
    pico_btstack_flash_bank
    pico_btstack_ble 
    pico_btstack_classic 
)

pico_add_extra_outputs(Pico-W-SINPUT-Example)

# Route debug prints to UART so GP0/GP1 remain available as boot-mode straps.
pico_enable_stdio_uart(Pico-W-SINPUT-Example 1)
pico_enable_stdio_usb(Pico-W-SINPUT-Example 0)
