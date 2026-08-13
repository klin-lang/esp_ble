# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md).

## Status (`@v0.5.0`)

| API | Notes |
|---|---|
| `init` / `advertise` / `wait_connected` / … | Peripheral GAP (from `@v0.1.0`) |
| `gatt_set` / `gatt_get` / `gatt_notify` / `gatt_written` | Peripheral GATT svc **0xFFF0** / chr **0xFFF1** (`@v0.2.0`) |
| `scan_start` / `scan_count` / `scan_addr` / `scan_name` / `scan_rssi` | Active scan, max **16** results (deduped) |
| `central_connect` / `central_wait_connected` / `central_disconnect` | GAP central connect by scan index |
| `gattc_discover` / `gattc_read` / `gattc_write` / `gattc_subscribe` / `gattc_notified` | GATT **client** (`@v0.4.0`) |
| `bond_enable` / `bond_start` / `wait_bonded` / `bonded` / `bond_count` / `bond_clear` | **Just Works** bonding; keys in NVS (`@v0.5.0`) |
| Passkey IO / custom UUID tables / mesh | **Out of scope** (later) |

`version()` → `5`.

Peripheral disconnect still **restarts advertising** when that path was used.
`scan_start` / `central_connect` clear that auto-restart.

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
examples/scan_s3/
examples/gattc_s3/
examples/bond_s3/
examples/smoke/
```

## Usage (bond after connect)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  if e != ble.err_ok() {
    return
  }
  e = ble.bond_enable()
  if e != ble.err_ok() {
    return
  }
  e = ble.advertise("klin-bond")
  if e != ble.err_ok() {
    return
  }
  e = ble.wait_connected(120000)
  if e != ble.err_ok() {
    return
  }
  e = ble.bond_start()
  if e != ble.err_ok() {
    return
  }
  e = ble.wait_bonded(30000)
}
```

```sh
klin get github/klin-lang/esp_ble@v0.5.0
```

## Contract

- No Klin GC / hidden heap — names and GATT payloads are buffers you pass in.
- Scan table is fixed (16 rows); overflow drops new addresses.
- Client discover targets fixed **0xFFF0** / **0xFFF1** (same as server MVP).
- Bonding is **Just Works** (no passkey UI); keys via NimBLE `ble_store` → NVS.
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
