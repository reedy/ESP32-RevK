idf_component_register(
			SRCS "revk.c" "jo.c" "lwmqtt.c"
			INCLUDE_DIRS "include"
			REQUIRES nvs_flash app_update esp_http_client esp-tls
			EMBED_FILES "../../build/partition_table/partition-table.bin"
)
add_definitions(-DBUILD_ESP32_USING_CMAKE)
set_property(SOURCE "../../build/partition_table/partition-table.bin" PROPERTY GENERATED 1)
