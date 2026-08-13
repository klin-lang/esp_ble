#pragma once
#include <stdint.h>
int klin_ble_init(void);
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
