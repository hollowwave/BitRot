idf_component_register(
    SRCS
        "main.c"
        "wifi_ops.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        driver
)

add_custom_command(
	TARGET ${COMPONENT_TARGET} PRE_LINK
	COMMAND ${CMAKE_OBJCOPY} --weaken-symbol=ieee80211_raw_frame_sanity_check "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32s3/libnet80211.a"
	COMMENT "Unbloking raw management frame restriction for ESP32-S3..."
)
