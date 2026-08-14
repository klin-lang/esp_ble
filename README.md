# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

## Status (`@v0.8.0`)

| API | Notes |
|---|---|
| `gatt_uuid16` / `gatt_uuid128` | Slot 0 UUIDs before `init` |
| `gatt_add_uuid16` / `gatt_add_uuid128` / `gatt_clear` | Up to **4** services (1 chr each) |
| `gatt_set_at` / `gatt_get_at` / `gatt_notify_at` / `gatt_written_at` | Per-service index |
| `gattc_select` / `gattc_uuid16` / `gattc_uuid128` | Client discover target |
| Prior APIs | GAP / GATT / scan / client / bond / passkey unchanged |

`version()` → `8`. Default remains svc **0xFFF0** / chr **0xFFF1** when unset.

## Usage (128-bit + two services)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.gatt_clear()
  e = ble.gatt_add_uuid16(0xA001, 0xA002)
  let mut svc: [16]u8
  let mut chr: [16]u8
  // fill LE 128-bit UUIDs…
  e = ble.gatt_add_uuid128(cast(*u8, &svc[0]), cast(*u8, &chr[0]))
  e = ble.init()
  e = ble.advertise("klin-multi")
}
```

```sh
klin get github/klin-lang/esp_ble@v0.8.0
```

## Contract

- Max **4** services; each has one R/W/Notify characteristic.
- UUID table frozen at `init`.
- 128-bit buffers are **16 bytes little-endian** (Bluetooth wire order).
- Errors are `i32` (0 = OK).
