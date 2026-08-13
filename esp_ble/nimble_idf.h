/* Thin NimBLE peripheral helpers for Klin — ESP-IDF v5.x.
 * Heap / NVS / NimBLE host task are IDF contracts, not Klin magic.
 * GATT services / central role come later.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NVS + `nimble_port_init` + host callbacks + NimBLE FreeRTOS task.
 * Call once before advertise. Returns `esp_err_t` as int (0 = OK).
 */
int klin_ble_init(void);

/**
 * Set GAP device name and start undirected connectable advertising.
 * Blocks briefly until NimBLE host is synced. `name` is a C string (copied).
 * Returns 0 on success; non-zero NimBLE / esp error otherwise.
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

#ifdef __cplusplus
}
#endif
