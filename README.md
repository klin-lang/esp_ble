# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

The radio is in the **silicon**; this package does **not** belong in
[`machine_esp`](https://github.com/klin-lang/machine_esp). Sibling of
[`esp_wifi`](https://github.com/klin-lang/esp_wifi) / [`esp_eth`](https://github.com/klin-lang/esp_eth).

Decision: Klin [issue 106](https://github.com/klin-lang/klin/blob/main/issues/106-esp-ble-idf.md).

## Status (`@v0.7.0`)

| API | Notes |
|---|---|
| `init` / `advertise` / … | Peripheral GAP |
| `gatt_*` / `gatt_uuid16` | GATT server; optional UUID16 before `init` |
| `scan_*` / `central_*` / `gattc_*` | Central scan + GATT client |
| `bond_enable` / `bond_start` / `wait_bonded` | Just Works bonding |
| `bond_passkey(pin)` / `passkey` / `passkey_inject` | **Fixed PIN** bonding (MITM) (`@v0.7.0`) |
| 128-bit UUID tables / mesh | **Out of scope** (later) |

`version()` → `7`.

## Usage (passkey / PIN)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  if e != ble.err_ok() {
    return
  }
  e = ble.bond_passkey(123456)
  if e != ble.err_ok() {
    return
  }
  e = ble.advertise("klin-pin")
  if e != ble.err_ok() {
    return
  }
  e = ble.wait_connected(120000)
  if e != ble.err_ok() {
    return
  }
  e = ble.bond_start()
  e = ble.wait_bonded(60000)
}
```

```sh
klin get github/klin-lang/esp_ble@v0.7.0
```

## Contract

- Passkey is `0..=999999` (6 digits). Injected automatically on pair.
- `bond_enable` = Just Works; `bond_passkey` = MITM + PIN (replaces JW config).
- Errors are `i32` (0 = OK).

## Links

- Wi‑Fi sibling: https://github.com/klin-lang/esp_wifi
- Ethernet sibling: https://github.com/klin-lang/esp_eth
- Chip MMIO: https://github.com/klin-lang/machine_esp
