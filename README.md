# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

## Status (`@v0.9.0`)

| API | Notes |
|---|---|
| `privacy_enable` / `privacy_disable` | Host RPA after `init`, before advertise/scan |
| `privacy_enabled` / `own_addr_type` / `own_addr` | Query own address |
| Prior APIs | GAP / GATT / multi-svc / UUID128 / client / bond / passkey |

`version()` → `9`. Bonding already distributes IRK (`bond_enable` / `bond_passkey`) for peer resolution.

## Usage (privacy / RPA)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  e = ble.privacy_enable()
  e = ble.advertise("klin-rpa")
}
```

```sh
klin get github/klin-lang/esp_ble@v0.9.0
```

## Contract

- Call `privacy_enable` **before** advertise / scan / connect (not while radio is active).
- Host-based RPA (`ble_hs_pvcy_rpa_config`); rotation interval = IDF `CONFIG_BT_NIMBLE_RPA_TIMEOUT`.
- No Klin GC / hidden heap. Errors are `i32` (0 = OK).
