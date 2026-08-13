#include "nimble_idf.h"
int klin_ble_init(void) { return 0; }
int klin_ble_advertise(const char *name)
{
    (void)name;
    return 0;
}
int klin_ble_stop_advertise(void) { return 0; }
int klin_ble_connected(void) { return 1; }
int klin_ble_advertising(void) { return 1; }
int klin_ble_wait_connected(int timeout_ms)
{
    (void)timeout_ms;
    return 0;
}
int klin_ble_stop(void) { return 0; }
