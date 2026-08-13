# ESP32-S3 custom GATT UUID16 (`esp_ble` @v0.6.0)

Sets svc **0xA001** / chr **0xA002** via `gatt_uuid16` **before** `init`,
then advertises `klin-uuid`.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build && make flash
```
