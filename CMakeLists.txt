cmake_minimum_required(VERSION 3.8)
project(ftdi_tool C)

set(CMAKE_C_STANDARD 11)

add_library(ftdi1 SHARED IMPORTED)
set_target_properties(ftdi1 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/libftdi1.so"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include")

find_path(LIBUSB_1_INCLUDE_DIR
        NAMES libusb.h
        PATHS
            /usr/include
            /usr/local/include
        PATH_SUFFIXES libusb-1.0)

find_library(LIBUSB_1_LIBRARY
        NAMES usb-1.0 usb
        PATHS
            /usr/lib
            /usr/local/lib)

set(SOURCES main.c)
add_executable(ftdi_tool ${SOURCES})
target_include_directories(ftdi_tool PRIVATE ${LIBUSB_1_INCLUDE_DIR})
target_link_libraries(ftdi_tool ${LIBUSB_1_LIBRARY} ftdi1)
