# ESP32-S3 BLE central scan (`esp_ble` @v0.3.0)

Active scan for 5 s (max 16 results), then GAP-connect to index 0 if any.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build
make flash
```

Requires ESP-IDF v5.x NimBLE with central + observer roles
(`sdkconfig.defaults`). GATT client read/write is **not** in this tag.
