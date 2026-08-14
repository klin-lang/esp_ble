/* Thin NimBLE helpers for Klin — ESP-IDF v5.x.
 * Heap / NVS / NimBLE host task are IDF contracts, not Klin magic.
 *
 * Peripheral GATT: up to KLIN_BLE_GATT_SVC_MAX services (1 chr each),
 * 16-bit or 128-bit UUIDs via gatt_uuid* / gatt_add_* before init.
 * Default single svc 0xFFF0 / chr 0xFFF1 when unset.
 * Central: scan + connect + gattc_discover (slot select or gattc_uuid*).
 * Bonding: Just Works or fixed passkey. Mesh later.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KLIN_BLE_GATT_VALUE_MAX 20
#define KLIN_BLE_GATT_SVC_MAX 4
#define KLIN_BLE_GATT_SVC_UUID16 0xFFF0
#define KLIN_BLE_GATT_CHR_UUID16 0xFFF1

/** Max scan results kept (deduped by address). Caller-visible contract. */
#define KLIN_BLE_SCAN_MAX 16

/** Max GAP name bytes stored per scan result (NUL-terminated in C). */
#define KLIN_BLE_SCAN_NAME_MAX 28

int klin_ble_init(void);

/** Replace slot 0 with 16-bit svc/chr. Before `init`. */
int klin_ble_gatt_uuid16(int svc_uuid16, int chr_uuid16);
/** Replace slot 0 with 128-bit svc/chr (16 bytes LE each). Before `init`. */
int klin_ble_gatt_uuid128(const uint8_t *svc16, const uint8_t *chr16);
/** Append 16-bit svc/chr (max KLIN_BLE_GATT_SVC_MAX). Before `init`. */
int klin_ble_gatt_add_uuid16(int svc_uuid16, int chr_uuid16);
/** Append 128-bit svc/chr. Before `init`. */
int klin_ble_gatt_add_uuid128(const uint8_t *svc16, const uint8_t *chr16);
/** Clear service table (must add before init). */
int klin_ble_gatt_clear(void);
/** Number of configured services (1 default before any calls). */
int klin_ble_gatt_svc_count(void);
/** Slot 0 service UUID16, or 0 if 128-bit / unset default path. */
int klin_ble_gatt_svc_uuid16(void);
/** Slot 0 characteristic UUID16, or 0 if 128-bit. */
int klin_ble_gatt_chr_uuid16(void);

int klin_ble_advertise(const char *name);
int klin_ble_stop_advertise(void);
int klin_ble_connected(void);
int klin_ble_advertising(void);
int klin_ble_wait_connected(int timeout_ms);
int klin_ble_stop(void);

int klin_ble_gatt_set(const uint8_t *data, int len);
int klin_ble_gatt_set_at(int index, const uint8_t *data, int len);
int klin_ble_gatt_get(uint8_t *out, int max_len);
int klin_ble_gatt_get_at(int index, uint8_t *out, int max_len);
int klin_ble_gatt_len(void);
int klin_ble_gatt_len_at(int index);
int klin_ble_gatt_notify(void);
int klin_ble_gatt_notify_at(int index);
int klin_ble_gatt_written(void);
int klin_ble_gatt_written_at(int index);

int klin_ble_scan_start(int duration_ms);
int klin_ble_scan_stop(void);
int klin_ble_scan_count(void);
int klin_ble_scan_rssi(int index);
int klin_ble_scan_addr_type(int index);
int klin_ble_scan_addr(int index, uint8_t *out6);
int klin_ble_scan_name(int index, uint8_t *out, int max_len);

int klin_ble_central_connect(int index, int timeout_ms);
int klin_ble_central_connected(void);
int klin_ble_central_wait_connected(int timeout_ms);
int klin_ble_central_disconnect(void);

/** Select local slot UUID for `gattc_discover`. */
int klin_ble_gattc_select(int index);
/** Override discover target (16-bit), independent of server table. */
int klin_ble_gattc_uuid16(int svc_uuid16, int chr_uuid16);
/** Override discover target (128-bit, 16 bytes LE each). */
int klin_ble_gattc_uuid128(const uint8_t *svc16, const uint8_t *chr16);

int klin_ble_gattc_discover(int timeout_ms);
int klin_ble_gattc_ready(void);
int klin_ble_gattc_read(int timeout_ms);
int klin_ble_gattc_write(const uint8_t *data, int len, int timeout_ms);
int klin_ble_gattc_subscribe(int timeout_ms);
int klin_ble_gattc_notified(void);
int klin_ble_gattc_get(uint8_t *out, int max_len);
int klin_ble_gattc_len(void);

int klin_ble_bond_enable(void);
int klin_ble_bond_passkey(int passkey);
int klin_ble_passkey(void);
int klin_ble_passkey_action(void);
int klin_ble_passkey_inject(int passkey);
int klin_ble_bond_start(void);
int klin_ble_bonded(void);
int klin_ble_wait_bonded(int timeout_ms);
int klin_ble_bond_count(void);
int klin_ble_bond_clear(void);

#ifdef __cplusplus
}
#endif
