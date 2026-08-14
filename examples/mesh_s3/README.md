# ESP32-S3 NimBLE Mesh node (`esp_ble` @v0.10.0)

Generic OnOff server node — provisionable over ADV + GATT.

Requires `CONFIG_BT_NIMBLE_MESH=y` (see `sdkconfig.defaults`).

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build && make flash
```
