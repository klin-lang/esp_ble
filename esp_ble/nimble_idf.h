/* Thin NimBLE helpers for Klin — ESP-IDF v5.x.
 * Heap / NVS / NimBLE host task are IDF contracts, not Klin magic.
 *
 * Peripheral GATT MVP: default svc 0xFFF0 / chr 0xFFF1 (read/write/notify);
 * override with gatt_uuid16() before init.
 * Central: active scan + connect; GATT client discover/read/write/subscribe
 * against the configured UUIDs.
 * Bonding: Just Works (`bond_enable`) or fixed passkey (`bond_passkey`).
 * 128-bit UUID tables / mesh come later.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KLIN_BLE_GATT_VALUE_MAX 20
#define KLIN_BLE_GATT_SVC_UUID16 0xFFF0
#define KLIN_BLE_GATT_CHR_UUID16 0xFFF1

/** Max scan results kept (deduped by address). Caller-visible contract. */
#define KLIN_BLE_SCAN_MAX 16

/** Max GAP name bytes stored per scan result (NUL-terminated in C). */
#define KLIN_BLE_SCAN_NAME_MAX 28

int klin_ble_init(void);
/**
 * Set 16-bit svc/chr UUIDs (default 0xFFF0 / 0xFFF1). Call **before** `init`.
 * Affects peripheral GATT registration, advertising UUID, and `gattc_discover`.
 */
int klin_ble_gatt_uuid16(int svc_uuid16, int chr_uuid16);
/** Current service UUID16 (default or last `gatt_uuid16`). */
int klin_ble_gatt_svc_uuid16(void);
/** Current characteristic UUID16. */
int klin_ble_gatt_chr_uuid16(void);
int klin_ble_advertise(const char *name);
int klin_ble_stop_advertise(void);
int klin_ble_connected(void);
int klin_ble_advertising(void);
int klin_ble_wait_connected(int timeout_ms);
int klin_ble_stop(void);

int klin_ble_gatt_set(const uint8_t *data, int len);
int klin_ble_gatt_get(uint8_t *out, int max_len);
int klin_ble_gatt_len(void);
int klin_ble_gatt_notify(void);
int klin_ble_gatt_written(void);

/**
 * Stop advertising (if any), clear results, run active scan for `duration_ms`
 * (clamped; must be > 0). Blocks until discovery completes. Returns 0 on OK.
 */
int klin_ble_scan_start(int duration_ms);

/** Abort an in-progress scan (best-effort). */
int klin_ble_scan_stop(void);

/** Number of scan results (0..=KLIN_BLE_SCAN_MAX). */
int klin_ble_scan_count(void);

/** RSSI for result `index`, or 0 if out of range. */
int klin_ble_scan_rssi(int index);

/** Address type for result `index` (`BLE_ADDR_*`), or -1 if out of range. */
int klin_ble_scan_addr_type(int index);

/**
 * Copy 6-byte address into `out6`. Returns 0 on OK, negative if bad index /
 * NULL out.
 */
int klin_ble_scan_addr(int index, uint8_t *out6);

/**
 * Copy NUL-free name bytes into `out` (up to `max_len`). Returns length, or
 * negative on error. Empty name → 0.
 */
int klin_ble_scan_name(int index, uint8_t *out, int max_len);

/**
 * Connect as central to scan result `index`. `timeout_ms` for the connection
 * attempt (-1 = NimBLE default / long). GATT client ops need `gattc_discover`
 * after `central_wait_connected`.
 */
int klin_ble_central_connect(int index, int timeout_ms);

/** 1 while we (central) hold a connection we initiated. */
int klin_ble_central_connected(void);

/** Block until central connection or timeout. 0 = OK. */
int klin_ble_central_wait_connected(int timeout_ms);

/** Disconnect central connection (best-effort). */
int klin_ble_central_disconnect(void);

/**
 * Discover peer svc 0xFFF0 / chr 0xFFF1 (+ CCCD if present). Blocks.
 * Requires an active central connection. 0 = ready for read/write.
 */
int klin_ble_gattc_discover(int timeout_ms);

/** 1 after successful `gattc_discover` while still connected. */
int klin_ble_gattc_ready(void);

/** Blocking read of chr 0xFFF1 into the client buffer. */
int klin_ble_gattc_read(int timeout_ms);

/** Blocking write (with response) to chr 0xFFF1. `len` ≤ 20. */
int klin_ble_gattc_write(const uint8_t *data, int len, int timeout_ms);

/**
 * Enable notifications (write CCCD 0x2902 = 0x0001). Fails if peer has no CCCD.
 */
int klin_ble_gattc_subscribe(int timeout_ms);

/**
 * 1 if a notification arrived since last call (clears the flag). Payload is in
 * the client buffer (`gattc_get` / `gattc_len`).
 */
int klin_ble_gattc_notified(void);

/** Copy last read/notify payload. Returns length, or -1 on bad args. */
int klin_ble_gattc_get(uint8_t *out, int max_len);

/** Length of last read/notify payload (0..=20). */
int klin_ble_gattc_len(void);

/**
 * Enable Just Works bonding (SM config). Call after `init`, before
 * `bond_start`. Keys are stored in NVS via NimBLE ble_store.
 */
int klin_ble_bond_enable(void);

/**
 * Enable bonding with a fixed 6-digit passkey/PIN (`0..=999999`). MITM.
 * On PASSKEY_ACTION the PIN is injected automatically. Replaces Just Works
 * config from `bond_enable`.
 */
int klin_ble_bond_passkey(int passkey);

/** Configured passkey, or 0 if Just Works / unset. */
int klin_ble_passkey(void);

/** Last `BLE_SM_IOACT_*` from PASSKEY_ACTION (0 if none). */
int klin_ble_passkey_action(void);

/** Manual inject for INPUT/DISP (usually not needed with `bond_passkey`). */
int klin_ble_passkey_inject(int passkey);

/**
 * Start pairing/encryption on the active connection (central preferred,
 * else peripheral). Requires `bond_enable` or `bond_passkey`. Completes via
 * ENC_CHANGE.
 */
int klin_ble_bond_start(void);

/** 1 after successful encryption on the current link. */
int klin_ble_bonded(void);

/** Block until bonded/encrypted or timeout. 0 = OK. */
int klin_ble_wait_bonded(int timeout_ms);

/** Number of stored peer bonds (NVS). */
int klin_ble_bond_count(void);

/** Delete all stored bonds. */
int klin_ble_bond_clear(void);

#ifdef __cplusplus
}
#endif
