/* Thin NimBLE peripheral helpers for Klin — ESP-IDF v5.x.
 * Heap / NVS / NimBLE host task are IDF contracts, not Klin magic.
 * GATT MVP: one primary service 0xFFF0, char 0xFFF1 (read/write/notify).
 * Central role / bonding / custom UUID tables come later.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max GATT characteristic value length (bytes). Caller-visible contract. */
#define KLIN_BLE_GATT_VALUE_MAX 20

/** Primary service UUID (16-bit). */
#define KLIN_BLE_GATT_SVC_UUID16 0xFFF0

/** Characteristic UUID (16-bit). */
#define KLIN_BLE_GATT_CHR_UUID16 0xFFF1

/**
 * NVS + `nimble_port_init` + GAP/GATT + host callbacks + NimBLE FreeRTOS task.
 * Registers the fixed MVP GATT service. Call once before advertise.
 * Returns `esp_err_t` as int (0 = OK).
 */
int klin_ble_init(void);

/**
 * Set GAP device name and start undirected connectable advertising
 * (includes service UUID 0xFFF0 in adv data when possible).
 * Blocks briefly until NimBLE host is synced. `name` is a C string (copied).
 */
int klin_ble_advertise(const char *name);

/** Stop advertising (does not tear down NimBLE). */
int klin_ble_stop_advertise(void);

/** 1 while a central is connected. */
int klin_ble_connected(void);

/** 1 while advertising is active. */
int klin_ble_advertising(void);

/** Block until connected or timeout_ms (-1 = forever). 0 = OK. */
int klin_ble_wait_connected(int timeout_ms);

/** Stop NimBLE host (best-effort). */
int klin_ble_stop(void);

/**
 * Copy `len` bytes from `data` into the GATT characteristic value (max
 * KLIN_BLE_GATT_VALUE_MAX). Does not send a notification by itself.
 * Returns 0 on success.
 */
int klin_ble_gatt_set(const uint8_t *data, int len);

/**
 * Copy current value into `out` (up to `max_len`). Returns byte count, or
 * negative on error.
 */
int klin_ble_gatt_get(uint8_t *out, int max_len);

/** Current value length (0..=KLIN_BLE_GATT_VALUE_MAX). */
int klin_ble_gatt_len(void);

/**
 * Notify subscribed centrals of the current value (no-op if nobody subscribed
 * or not connected). Returns 0 on success / nothing to do.
 */
int klin_ble_gatt_notify(void);

/**
 * 1 if a central wrote the characteristic since the last call (clears the
 * flag). Explicit poll — no Klin callbacks / hidden control flow.
 */
int klin_ble_gatt_written(void);

#ifdef __cplusplus
}
#endif
