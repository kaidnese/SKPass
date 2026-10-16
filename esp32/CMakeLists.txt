# ESP-IDF project based on the existing PN7150 I2C reader.
cmake_minimum_required(VERSION 3.16)

# SKPass HID requires the ESP32-S3 native USB-OTG peripheral.
if(NOT DEFINED IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET "esp32s3" CACHE STRING "IDF Build Target")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "This project requires ESP32-S3. Run: idf.py set-target esp32s3")
endif()
project(SKPass)
