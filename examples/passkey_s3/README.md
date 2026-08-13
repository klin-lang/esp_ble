# ESP32-S3 BLE passkey / PIN (`esp_ble` @v0.7.0)

Peripheral: `bond_passkey(123456)` → advertise → connect → `bond_start`.
Phone enters **123456** when pairing.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
make build && make flash
```
