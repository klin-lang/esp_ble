# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md).

## Status (`@v0.2.0`)

| API | Notes |
|---|---|
| `init` | NVS + NimBLE host + **fixed GATT** registration |
| `advertise(name)` | Undirected connectable GAP advertise (+ service UUID when it fits) |
| `wait_connected` / `connected` / `advertising` | Connection / adv state |
| `gatt_set` / `gatt_get` / `gatt_len` / `gatt_notify` / `gatt_written` | Char **0xFFF1** on svc **0xFFF0** (R/W/Notify), max **20** bytes |
| `stop_advertise` / `stop` | Stop adv / tear down NimBLE |
| Central / bonding / custom UUID tables | **Out of scope** (later) |

`version()` → `2`.

After disconnect, the peripheral **restarts advertising** (documented in C).
GATT writes are polled via `gatt_written()` — no Klin callbacks.

## Requirements

- Klin compiler
- ESP-IDF **v5.x** with `CONFIG_BT_ENABLED` + `CONFIG_BT_NIMBLE_ENABLED`
- Board with BLE radio (ESP32-C3 / S3 / …)

## Layout

```text
esp_ble/
  version.kl
  advertise.kl        # Klin API (GAP + GATT)
  nimble_idf.c / .h   # IDF glue (@[link])
examples/advertise_s3/
examples/gatt_s3/
examples/smoke/
```

## Usage (GATT)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  if e != ble.err_ok() {
    return
  }
  let mut v: [1]u8
  v[0] = 42
  e = ble.gatt_set(cast(*u8, &v[0]), 1)
  if e != ble.err_ok() {
    return
  }
  e = ble.advertise("klin-gatt")
  if e != ble.err_ok() {
    return
  }
  e = ble.wait_connected(60000)
  if e != ble.err_ok() {
    return
  }
  while true {
    if ble.gatt_written() {
      let mut buf: [20]u8
      let _n = ble.gatt_get(cast(*mut u8, &buf[0]), ble.gatt_value_max())
    }
    let _n = ble.gatt_notify()
  }
}
```

```sh
klin get github/klin-lang/esp_ble@v0.2.0
```

## Contract

- No Klin GC / hidden heap — device name and GATT payloads are buffers you pass in.
- NimBLE host task / controller buffers are IDF contracts.
- GATT value max = 20 bytes (`gatt_value_max()`); UUIDs fixed for MVP.
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
