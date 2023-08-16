set(SOURCES "revk.c" "jo.c" "lwmqtt.c")
set(RECS nvs_flash app_update esp_http_client esp-tls esp_http_server spi_flash esp_wifi esp_timer esp_system driver bt)

# Add extra dependancies

# Unfortunately we can't simply test for ${CONFIG_IDF_TARGET_ESP8266} here
# because requirements are evaluated in a separate pass, where any CONFIG_
# settings haven't been evaluated yet.
# The same applied to mdns check below
if(EXISTS $ENV{IDF_PATH}/components/esp8266)
	set(SOURCES ${SOURCES} "esp8266_compat.c")
	set(RECS nvs_flash app_update esp_http_client esp-tls esp_http_server esp8266 spi_flash)
endif()

if(EXISTS "../managed_components/espressif__mdns")
	set(RECS ${RECS} mdns)
endif()

idf_component_register(
			SRCS ${SOURCES}
			INCLUDE_DIRS "include"
			REQUIRES ${RECS}
			EMBED_FILES "../../build/partition_table/partition-table.bin"
)

add_definitions(-DBUILD_ESP32_USING_CMAKE)
set_property(SOURCE "../../build/partition_table/partition-table.bin" PROPERTY GENERATED 1)

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(CONFIG_REVK_MATTER)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    if("${IDF_TARGET}" STREQUAL "esp32" OR "${IDF_TARGET}" STREQUAL "")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32_devkit_c)
    elseif("${IDF_TARGET}" STREQUAL "esp32c3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c3_devkit_m)
    elseif("${IDF_TARGET}" STREQUAL "esp32c2")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c2_devkit_m)
    elseif("${IDF_TARGET}" STREQUAL "esp32h2")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32h2_devkit_c)
    elseif("${IDF_TARGET}" STREQUAL "esp32s3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32s3_devkit_c)
    elseif("${IDF_TARGET}" STREQUAL "esp32c6")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c6_devkit_c)
    else()
        message(FATAL_ERROR "Unsupported IDF_TARGET")
    endif()
endif(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append})

# WARNING: This is just an example for using key for decrypting the encrypted OTA image
# Please do not use it as is.
if(CONFIG_ENABLE_ENCRYPTED_OTA)
    target_add_binary_data(light.elf "esp_image_encryption_key.pem" TEXT)
endif()

if(CONFIG_IDF_TARGET_ESP32C2)
    include(relinker)
endif()

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++14;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

endif()
