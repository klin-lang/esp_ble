#include "nimble_idf.h"
#include <string.h>

static uint8_t s_val[20];
static int s_len;
static int s_written;
static int s_scan_n;
static uint8_t s_addr[6] = {1, 2, 3, 4, 5, 6};
static char s_scan_name[] = "stub";
static int s_central;

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

int klin_ble_scan_start(int duration_ms)
{
    (void)duration_ms;
    s_scan_n = 1;
    return 0;
}
int klin_ble_scan_stop(void) { return 0; }
int klin_ble_scan_count(void) { return s_scan_n; }
int klin_ble_scan_rssi(int index)
{
    (void)index;
    return -40;
}
int klin_ble_scan_addr_type(int index)
{
    (void)index;
    return 0;
}
int klin_ble_scan_addr(int index, uint8_t *out6)
{
    (void)index;
    if (out6 == NULL) {
        return -1;
    }
    memcpy(out6, s_addr, 6);
    return 0;
}
int klin_ble_scan_name(int index, uint8_t *out, int max_len)
{
    int n = (int)strlen(s_scan_name);
    (void)index;
    if (out == NULL || max_len < 0) {
        return -1;
    }
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        memcpy(out, s_scan_name, (size_t)n);
    }
    return n;
}
int klin_ble_central_connect(int index, int timeout_ms)
{
    (void)index;
    (void)timeout_ms;
    s_central = 1;
    return 0;
}
int klin_ble_central_connected(void) { return s_central; }
int klin_ble_central_wait_connected(int timeout_ms)
{
    (void)timeout_ms;
    return s_central ? 0 : -1;
}
int klin_ble_central_disconnect(void)
{
    s_central = 0;
    return 0;
}
