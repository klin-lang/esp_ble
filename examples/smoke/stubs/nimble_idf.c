#include "nimble_idf.h"
#include <string.h>

static uint8_t s_val[20];
static int s_len;
static int s_written;
static int s_scan_n;
static uint8_t s_addr[6] = {1, 2, 3, 4, 5, 6};
static char s_scan_name[] = "stub";
static int s_central;
static uint16_t s_svc_u = 0xFFF0;
static uint16_t s_chr_u = 0xFFF1;
static int s_inited_stub;
static int s_n_svc = 1;

int klin_ble_gatt_clear(void)
{
    if (s_inited_stub) {
        return -1;
    }
    s_n_svc = 0;
    return 0;
}
int klin_ble_gatt_uuid16(int svc_uuid16, int chr_uuid16)
{
    if (s_inited_stub) {
        return -1;
    }
    if (svc_uuid16 <= 0 || svc_uuid16 > 0xFFFF || chr_uuid16 <= 0 ||
        chr_uuid16 > 0xFFFF) {
        return -1;
    }
    s_svc_u = (uint16_t)svc_uuid16;
    s_chr_u = (uint16_t)chr_uuid16;
    if (s_n_svc < 1) {
        s_n_svc = 1;
    }
    return 0;
}
int klin_ble_gatt_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    (void)svc16;
    (void)chr16;
    if (s_inited_stub) {
        return -1;
    }
    if (s_n_svc < 1) {
        s_n_svc = 1;
    }
    s_svc_u = 0;
    s_chr_u = 0;
    return 0;
}
int klin_ble_gatt_add_uuid16(int svc_uuid16, int chr_uuid16)
{
    if (s_inited_stub || s_n_svc >= 4) {
        return -1;
    }
    if (svc_uuid16 <= 0 || svc_uuid16 > 0xFFFF || chr_uuid16 <= 0 ||
        chr_uuid16 > 0xFFFF) {
        return -1;
    }
    if (s_n_svc == 0) {
        s_svc_u = (uint16_t)svc_uuid16;
        s_chr_u = (uint16_t)chr_uuid16;
    }
    s_n_svc++;
    return 0;
}
int klin_ble_gatt_add_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    (void)svc16;
    (void)chr16;
    if (s_inited_stub || s_n_svc >= 4) {
        return -1;
    }
    if (s_n_svc == 0) {
        s_svc_u = 0;
        s_chr_u = 0;
    }
    s_n_svc++;
    return 0;
}
int klin_ble_gatt_svc_count(void) { return s_n_svc > 0 ? s_n_svc : 1; }
int klin_ble_gatt_svc_uuid16(void) { return (int)s_svc_u; }
int klin_ble_gatt_chr_uuid16(void) { return (int)s_chr_u; }

int klin_ble_init(void)
{
    s_inited_stub = 1;
    if (s_n_svc < 1) {
        s_n_svc = 1;
    }
    return 0;
}
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
    return klin_ble_gatt_set_at(0, data, len);
}
int klin_ble_gatt_set_at(int index, const uint8_t *data, int len)
{
    (void)index;
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
    return klin_ble_gatt_get_at(0, out, max_len);
}
int klin_ble_gatt_get_at(int index, uint8_t *out, int max_len)
{
    int n;
    (void)index;
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
int klin_ble_gatt_len_at(int index)
{
    (void)index;
    return s_len;
}
int klin_ble_gatt_notify(void) { return 0; }
int klin_ble_gatt_notify_at(int index)
{
    (void)index;
    return 0;
}
int klin_ble_gatt_written(void)
{
    int w = s_written;
    s_written = 0;
    return w;
}
int klin_ble_gatt_written_at(int index)
{
    (void)index;
    return klin_ble_gatt_written();
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

int klin_ble_gattc_select(int index)
{
    (void)index;
    return 0;
}
int klin_ble_gattc_uuid16(int svc_uuid16, int chr_uuid16)
{
    (void)svc_uuid16;
    (void)chr_uuid16;
    return 0;
}
int klin_ble_gattc_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    (void)svc16;
    (void)chr16;
    return 0;
}
int klin_ble_gattc_discover(int timeout_ms)
{
    (void)timeout_ms;
    return s_central ? 0 : -1;
}
int klin_ble_gattc_ready(void) { return s_central; }
int klin_ble_gattc_read(int timeout_ms)
{
    (void)timeout_ms;
    s_len = 1;
    s_val[0] = 7;
    return 0;
}
int klin_ble_gattc_write(const uint8_t *data, int len, int timeout_ms)
{
    (void)timeout_ms;
    return klin_ble_gatt_set(data, len);
}
int klin_ble_gattc_subscribe(int timeout_ms)
{
    (void)timeout_ms;
    return 0;
}
int klin_ble_gattc_notified(void) { return 0; }
int klin_ble_gattc_get(uint8_t *out, int max_len)
{
    return klin_ble_gatt_get(out, max_len);
}
int klin_ble_gattc_len(void) { return s_len; }

static int s_bond_on;
static int s_bond_ok;
static int s_pin_mode;
static int s_pin;

int klin_ble_bond_enable(void)
{
    s_bond_on = 1;
    s_pin_mode = 0;
    s_pin = 0;
    return 0;
}
int klin_ble_bond_passkey(int passkey)
{
    if (passkey < 0 || passkey > 999999) {
        return -1;
    }
    s_bond_on = 1;
    s_pin_mode = 1;
    s_pin = passkey;
    return 0;
}
int klin_ble_passkey(void) { return s_pin_mode ? s_pin : 0; }
int klin_ble_passkey_action(void) { return 0; }
int klin_ble_passkey_inject(int passkey)
{
    (void)passkey;
    return s_pin_mode ? 0 : -1;
}
int klin_ble_bond_start(void)
{
    if (!s_bond_on || !s_central) {
        return -1;
    }
    s_bond_ok = 1;
    return 0;
}
int klin_ble_bonded(void) { return s_bond_ok; }
int klin_ble_wait_bonded(int timeout_ms)
{
    (void)timeout_ms;
    return s_bond_ok ? 0 : -1;
}
int klin_ble_bond_count(void) { return s_bond_ok ? 1 : 0; }
int klin_ble_bond_clear(void)
{
    s_bond_ok = 0;
    return 0;
}
