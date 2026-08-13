set(KLIN_C "${CMAKE_SOURCE_DIR}/out/gatt.c")
set(BLE_C "${CMAKE_SOURCE_DIR}/../../esp_ble/nimble_idf.c")

if(NOT EXISTS "${KLIN_C}")
    message(FATAL_ERROR
        "Missing ${KLIN_C}. From examples/gatt_s3 run: make emit KLIN=...")
endif()

idf_component_register(
    SRCS "app_main.c" "${KLIN_C}" "${BLE_C}"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/out" "${CMAKE_SOURCE_DIR}/../../esp_ble"
    REQUIRES nvs_flash bt
)
