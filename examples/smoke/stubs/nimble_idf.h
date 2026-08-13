#pragma once
#include <stdint.h>
int klin_ble_init(void);
int klin_ble_advertise(const char *name);
int klin_ble_stop_advertise(void);
int klin_ble_connected(void);
int klin_ble_advertising(void);
int klin_ble_wait_connected(int timeout_ms);
int klin_ble_stop(void);
