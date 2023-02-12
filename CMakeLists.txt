if(CONFIG_MDNS_MAX_INTERFACES)
idf_component_register(
			SRCS "revk.c" "jo.c" "lwmqtt.c"
			INCLUDE_DIRS "include"
			REQUIRES nvs_flash app_update esp_http_client esp-tls esp_http_server esp_wifi esp_timer esp_system driver spi_flash mdns
			EMBED_FILES "../../build/partition_table/partition-table.bin"
)
else()
idf_component_register(
			SRCS "revk.c" "jo.c" "lwmqtt.c"
			INCLUDE_DIRS "include"
			REQUIRES nvs_flash app_update esp_http_client esp-tls esp_http_server esp_wifi esp_timer esp_system driver spi_flash
			EMBED_FILES "../../build/partition_table/partition-table.bin"
)
endif()
add_definitions(-DBUILD_ESP32_USING_CMAKE)
set_property(SOURCE "../../build/partition_table/partition-table.bin" PROPERTY GENERATED 1)
