# ESP32-S3 multi GATT + UUID128 (`esp_ble` @v0.8.0)

Registers three services (2× UUID16 + 1× UUID128) before `init`.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build && make flash
```
