#include "nimble_idf.h"
#include <string.h>

static uint8_t s_val[20];
static int s_len;
static int s_written;

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

int klin_ble_gatt_set(const uint8_t *data, int len)
{
    if (data == NULL || len < 0 || len > 20) {
        return -1;
    }
    if (len > 0) {
        memcpy(s_val, data, (size_t)len);
    }
    s_len = len;
    return 0;
}

int klin_ble_gatt_get(uint8_t *out, int max_len)
{
    int n;
    if (out == NULL || max_len < 0) {
        return -1;
    }
    n = s_len;
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        memcpy(out, s_val, (size_t)n);
    }
    return n;
}

int klin_ble_gatt_len(void) { return s_len; }
int klin_ble_gatt_notify(void) { return 0; }
int klin_ble_gatt_written(void)
{
    int w = s_written;
    s_written = 0;
    return w;
}
