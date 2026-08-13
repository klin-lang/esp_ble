# Advertise on ESP32-S3

Hardware demo for [`esp_ble`](../../README.md) `@v0.1.0` (NimBLE peripheral).

1. `. $IDF_PATH/export.sh` (ESP-IDF **v5.x**)
2. `make emit KLIN=…` / `make build` / `make flash`
3. Scan with a phone BLE app for name `klin-ble`

Needs `CONFIG_BT_NIMBLE_ENABLED` (set in `sdkconfig.defaults`).
