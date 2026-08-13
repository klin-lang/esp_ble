# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md).

## Status (`@v0.6.0`)

| API | Notes |
|---|---|
| `init` / `advertise` / `wait_connected` / … | Peripheral GAP (from `@v0.1.0`) |
| `gatt_set` / `gatt_get` / `gatt_notify` / `gatt_written` | Peripheral GATT (default svc **0xFFF0** / chr **0xFFF1**) |
| `gatt_uuid16(svc, chr)` | Own **16-bit** UUIDs — call **before** `init` (`@v0.6.0`) |
| `scan_*` / `central_*` | Active scan + GAP connect |
| `gattc_*` | GATT client against configured UUIDs |
| `bond_*` / `wait_bonded` | Just Works bonding; keys in NVS |
| 128-bit UUID tables / passkey / mesh | **Out of scope** (later) |

`version()` → `6`.

## Requirements

- Klin compiler
- ESP-IDF **v5.x** with NimBLE (+ **central** / **observer** roles for scan)
- Board with BLE radio (ESP32-C3 / S3 / …)

## Layout

```text
esp_ble/
  version.kl
  advertise.kl
  nimble_idf.c / .h
examples/advertise_s3/
examples/gatt_s3/
examples/uuid_s3/
examples/scan_s3/
examples/gattc_s3/
examples/bond_s3/
examples/smoke/
```

## Usage (custom UUID16)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.gatt_uuid16(0xA001, 0xA002)
  if e != ble.err_ok() {
    return
  }
  e = ble.init()
  if e != ble.err_ok() {
    return
  }
  e = ble.advertise("klin-uuid")
}
```

```sh
klin get github/klin-lang/esp_ble@v0.6.0
```

## Contract

- No Klin GC / hidden heap — names and GATT payloads are buffers you pass in.
- `gatt_uuid16` before `init` only; after `init` → `ESP_ERR_INVALID_STATE`.
- Defaults remain **0xFFF0** / **0xFFF1** when unset.
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
