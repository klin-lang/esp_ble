# ESP32-S3 BLE GATT client (`esp_ble` @v0.4.0)

Central: scan → connect → discover svc **0xFFF0** / chr **0xFFF1** →
subscribe / read / write.

Use a second board (or phone) running `examples/gatt_s3` as the peripheral.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
# IDF_PATH set:
make build && make flash
```
