# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md)
(when merged).

## Status (`@v0.1.0`)

| API | Notes |
|---|---|
| `init` | NVS + `nimble_port_init` + NimBLE FreeRTOS host task |
| `advertise(name)` | Undirected connectable GAP advertise |
| `wait_connected` / `connected` / `advertising` | Connection / adv state |
| `stop_advertise` / `stop` | Stop adv / tear down NimBLE |
| GATT server / central / bonding | **Out of scope** (later) |

`version()` → `1`.

After disconnect, the peripheral **restarts advertising** (documented in C).

## Requirements

- Klin compiler
- ESP-IDF **v5.x** with `CONFIG_BT_ENABLED` + `CONFIG_BT_NIMBLE_ENABLED`
- Board with BLE radio (ESP32-C3 / S3 / …)

## Layout

```text
esp_ble/
  version.kl
  advertise.kl        # Klin API
  nimble_idf.c / .h   # IDF glue (@[link])
examples/advertise_s3/
examples/smoke/
```

## Usage

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  if e != ble.err_ok() {
    return
  }
  e = ble.advertise("klin-ble")
  if e != ble.err_ok() {
    return
  }
  e = ble.wait_connected(60000)
  if e != ble.err_ok() {
    return
  }
}
```

```sh
klin get github/klin-lang/esp_ble@v0.1.0
```

## Contract

- No Klin GC / hidden heap — device name is a C string you pass in.
- NimBLE host task / controller buffers are IDF contracts.
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
