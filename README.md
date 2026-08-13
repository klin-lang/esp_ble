# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md).

## Status (`@v0.4.0`)

| API | Notes |
|---|---|
| `init` / `advertise` / `wait_connected` / … | Peripheral GAP (from `@v0.1.0`) |
| `gatt_set` / `gatt_get` / `gatt_notify` / `gatt_written` | Peripheral GATT svc **0xFFF0** / chr **0xFFF1** (`@v0.2.0`) |
| `scan_start` / `scan_count` / `scan_addr` / `scan_name` / `scan_rssi` | Active scan, max **16** results (deduped) |
| `central_connect` / `central_wait_connected` / `central_disconnect` | GAP central connect by scan index |
| `gattc_discover` / `gattc_read` / `gattc_write` / `gattc_subscribe` / `gattc_notified` | GATT **client** on the same fixed UUIDs (`@v0.4.0`) |
| Bonding / custom UUID tables | **Out of scope** (later) |

`version()` → `4`.

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
examples/smoke/
```

## Usage (central + GATT client)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  if e != ble.err_ok() {
    return
  }
  e = ble.scan_start(5000)
  if e != ble.err_ok() {
    return
  }
  if ble.scan_count() < 1 {
    return
  }
  e = ble.central_connect(0, 10000)
  if e != ble.err_ok() {
    return
  }
  e = ble.central_wait_connected(15000)
  if e != ble.err_ok() {
    return
  }
  e = ble.gattc_discover(10000)
  if e != ble.err_ok() {
    return
  }
  e = ble.gattc_subscribe(5000)
  e = ble.gattc_read(5000)
}
```

```sh
klin get github/klin-lang/esp_ble@v0.4.0
```

## Contract

- No Klin GC / hidden heap — names and GATT payloads are buffers you pass in.
- Scan table is fixed (16 rows); overflow drops new addresses.
- Client discover targets fixed **0xFFF0** / **0xFFF1** (same as server MVP).
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
