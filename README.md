# esp_ble

Thin **ESP-IDF NimBLE** bindings for [Klin](https://github.com/klin-lang/klin).

## Status (`@v0.10.0`)

| API | Notes |
|---|---|
| `mesh_enable` | NimBLE Mesh node (Gen OnOff); needs `CONFIG_BT_NIMBLE_MESH` |
| `mesh_provisioned` / `mesh_primary_addr` / `mesh_onoff*` / `mesh_oob_number` / `mesh_reset` | Node state |
| Prior APIs | GAP / GATT / multi-svc / UUID128 / client / bond / passkey / privacy |

`version()` → `10`. Mesh is **not** the same path as `advertise` — enable mesh after `init`, then provision from a phone/app.

## Usage (Mesh OnOff node)

```klin
import "github/klin-lang/esp_ble" ble

@[cexport, codename("klin_app_main")]
fn app() {
  let mut e = ble.init()
  e = ble.mesh_enable()
  while true {
    if ble.mesh_onoff_changed() {
      let _o = ble.mesh_onoff()
    }
  }
}
```

```sh
klin get github/klin-lang/esp_ble@v0.10.0
```

## Contract

- Mesh requires IDF `CONFIG_BT_NIMBLE_MESH` (+ PB-ADV / PB-GATT as needed).
- Without that Kconfig, `mesh_enable` returns not-supported.
- No Klin GC / hidden heap. Errors are `i32` (0 = OK).
