# ESP32-S3 GATT peripheral (`esp_ble` @v0.2.0)

Advertise as `klin-gatt`. Service **0xFFF0**, characteristic **0xFFF1**
(read / write / notify). Value is one byte that increments; writes from a
central update the counter.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build
make flash
```

Requires ESP-IDF v5.x with NimBLE enabled (`sdkconfig.defaults`).
