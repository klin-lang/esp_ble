# ESP32-S3 BLE bonding (`esp_ble` @v0.5.0)

Peripheral: advertise → wait connect → **Just Works** `bond_start` /
`wait_bonded`. Keys stored in NVS.

```sh
make emit KLIN=/path/to/klin/bin/klin.dart
# IDF_PATH set:
make build && make flash
```

Connect from a phone (nRF Connect / system Bluetooth) and accept pairing
(no PIN).
