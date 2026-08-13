/* NimBLE peripheral GATT + central scan/connect for Klin (ESP-IDF v5.x).
 * Requires: CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED
 * (+ central/observer roles for scan/connect).
 */
#include "nimble_idf.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define KLIN_BLE_SYNC_BIT           BIT0
#define KLIN_BLE_CONNECTED_BIT      BIT1
#define KLIN_BLE_SCAN_DONE_BIT      BIT2
#define KLIN_BLE_CENTRAL_CONN_BIT   BIT3
#define KLIN_BLE_NAME_MAX           28

typedef struct {
    ble_addr_t addr;
    int8_t rssi;
    char name[KLIN_BLE_SCAN_NAME_MAX];
} klin_ble_scan_row_t;

static EventGroupHandle_t s_ble_events;
static int s_inited;
static int s_synced;
static int s_advertising;
static int s_connected;          /* peripheral: peer connected to us */
static int s_central_connected;  /* we initiated the link */
static int s_restart_adv;        /* peripheral policy after disconnect */
static int s_scanning;
static uint8_t s_own_addr_type;
static char s_name[KLIN_BLE_NAME_MAX];

static uint16_t s_conn_handle;
static uint16_t s_central_conn_handle;
static uint16_t s_chr_val_handle;
static uint8_t s_gatt_value[KLIN_BLE_GATT_VALUE_MAX];
static uint16_t s_gatt_len;
static int s_gatt_written;
static int s_gatt_notify_enabled;

static klin_ble_scan_row_t s_scan[KLIN_BLE_SCAN_MAX];
static int s_scan_count;

static const ble_uuid16_t s_svc_uuid =
    BLE_UUID16_INIT(KLIN_BLE_GATT_SVC_UUID16);
static const ble_uuid16_t s_chr_uuid =
    BLE_UUID16_INIT(KLIN_BLE_GATT_CHR_UUID16);

static void klin_ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void klin_ble_on_reset(int reason)
{
    printf("klin_ble: reset reason=%d\n", reason);
}

static int klin_ble_gap_event(struct ble_gap_event *event, void *arg);

static int klin_ble_wait_sync(void)
{
    TickType_t ticks = pdMS_TO_TICKS(5000);
    EventBits_t bits;

    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_SYNC_BIT, pdFALSE,
                               pdFALSE, ticks);
    if ((bits & KLIN_BLE_SYNC_BIT) == 0) {
        return (int)ESP_ERR_TIMEOUT;
    }
    return (int)ESP_OK;
}

static int klin_ble_scan_find(const ble_addr_t *addr)
{
    int i;
    for (i = 0; i < s_scan_count; i++) {
        if (s_scan[i].addr.type == addr->type &&
            memcmp(s_scan[i].addr.val, addr->val, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void klin_ble_scan_add(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int idx;
    int rc;

    idx = klin_ble_scan_find(&disc->addr);
    if (idx < 0) {
        if (s_scan_count >= KLIN_BLE_SCAN_MAX) {
            return;
        }
        idx = s_scan_count++;
        memset(&s_scan[idx], 0, sizeof(s_scan[idx]));
        s_scan[idx].addr = disc->addr;
    }

    s_scan[idx].rssi = disc->rssi;

    memset(&fields, 0, sizeof(fields));
    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc == 0 && fields.name != NULL && fields.name_len > 0) {
        size_t n = fields.name_len;
        if (n >= KLIN_BLE_SCAN_NAME_MAX) {
            n = KLIN_BLE_SCAN_NAME_MAX - 1;
        }
        memcpy(s_scan[idx].name, fields.name, n);
        s_scan[idx].name[n] = '\0';
    }
}

static int klin_ble_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    (void)conn_handle;
    (void)arg;

    if (attr_handle != s_chr_val_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        rc = os_mbuf_append(ctxt->om, s_gatt_value, s_gatt_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len > KLIN_BLE_GATT_VALUE_MAX) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        rc = ble_hs_mbuf_to_flat(ctxt->om, s_gatt_value, om_len, NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_gatt_len = om_len;
        s_gatt_written = 1;
        return 0;
    }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_chr_uuid.u,
                    .access_cb = klin_ble_gatt_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_chr_val_handle,
                },
                {
                    0,
                },
            },
    },
    {
        0,
    },
};

static int klin_ble_start_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
    ble_uuid16_t uuids16[1];
    int rc;

    if (!s_synced) {
        return BLE_HS_EAGAIN;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    if (s_name[0] != '\0') {
        fields.name = (uint8_t *)s_name;
        fields.name_len = (uint8_t)strlen(s_name);
        fields.name_is_complete = 1;
    }
    uuids16[0] = s_svc_uuid;
    fields.uuids16 = uuids16;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        memset(&fields, 0, sizeof(fields));
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        if (s_name[0] != '\0') {
            fields.name = (uint8_t *)s_name;
            fields.name_len = (uint8_t)strlen(s_name);
            fields.name_is_complete = 1;
        }
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            return rc;
        }
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           klin_ble_gap_event, NULL);
    if (rc != 0) {
        s_advertising = 0;
        return rc;
    }
    s_advertising = 1;
    s_restart_adv = 1;
    return 0;
}

static int klin_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            if (s_restart_adv) {
                (void)klin_ble_start_advertise();
            }
            return 0;
        }
        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        if (rc != 0) {
            return 0;
        }
        if (desc.role == BLE_GAP_ROLE_MASTER) {
            s_central_connected = 1;
            s_central_conn_handle = event->connect.conn_handle;
            s_scanning = 0;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_CENTRAL_CONN_BIT);
            }
        } else {
            s_connected = 1;
            s_advertising = 0;
            s_conn_handle = event->connect.conn_handle;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.role == BLE_GAP_ROLE_MASTER ||
            event->disconnect.conn.conn_handle == s_central_conn_handle) {
            s_central_connected = 0;
            s_central_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            if (s_ble_events != NULL) {
                xEventGroupClearBits(s_ble_events, KLIN_BLE_CENTRAL_CONN_BIT);
            }
        } else {
            s_connected = 0;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_gatt_notify_enabled = 0;
            if (s_ble_events != NULL) {
                xEventGroupClearBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
            }
            if (s_restart_adv) {
                (void)klin_ble_start_advertise();
            }
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = 0;
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_chr_val_handle) {
            s_gatt_notify_enabled = event->subscribe.cur_notify ? 1 : 0;
        }
        return 0;

    case BLE_GAP_EVENT_DISC:
        klin_ble_scan_add(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = 0;
        if (s_ble_events != NULL) {
            xEventGroupSetBits(s_ble_events, KLIN_BLE_SCAN_DONE_BIT);
        }
        return 0;

    default:
        return 0;
    }
}

static void klin_ble_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        printf("klin_ble: ensure_addr rc=%d\n", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        printf("klin_ble: infer_addr rc=%d\n", rc);
        return;
    }

    s_synced = 1;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_SYNC_BIT);
    }
}

int klin_ble_init(void)
{
    esp_err_t err;
    int rc;

    if (s_inited) {
        return (int)ESP_OK;
    }

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return (int)err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return (int)err;
    }

    s_ble_events = xEventGroupCreate();
    if (s_ble_events == NULL) {
        return (int)ESP_ERR_NO_MEM;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        return (int)err;
    }

    ble_hs_cfg.reset_cb = klin_ble_on_reset;
    ble_hs_cfg.sync_cb = klin_ble_on_sync;
    ble_hs_cfg.store_status_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }

    s_synced = 0;
    s_advertising = 0;
    s_connected = 0;
    s_central_connected = 0;
    s_restart_adv = 0;
    s_scanning = 0;
    s_scan_count = 0;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_central_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_gatt_len = 0;
    s_gatt_written = 0;
    s_gatt_notify_enabled = 0;
    memset(s_gatt_value, 0, sizeof(s_gatt_value));
    memset(s_scan, 0, sizeof(s_scan));
    s_name[0] = '\0';
    xEventGroupClearBits(s_ble_events,
                         KLIN_BLE_SYNC_BIT | KLIN_BLE_CONNECTED_BIT |
                             KLIN_BLE_SCAN_DONE_BIT | KLIN_BLE_CENTRAL_CONN_BIT);

    nimble_port_freertos_init(klin_ble_host_task);
    s_inited = 1;
    return (int)ESP_OK;
}

int klin_ble_advertise(const char *name)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    if (name == NULL || name[0] == '\0') {
        strncpy(s_name, "klin-ble", sizeof(s_name) - 1);
    } else {
        strncpy(s_name, name, sizeof(s_name) - 1);
    }
    s_name[sizeof(s_name) - 1] = '\0';

    rc = ble_svc_gap_device_name_set(s_name);
    if (rc != 0) {
        return rc;
    }

    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }

    if (s_scanning) {
        (void)ble_gap_disc_cancel();
        s_scanning = 0;
    }

    return klin_ble_start_advertise();
}

int klin_ble_stop_advertise(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    s_restart_adv = 0;
    rc = ble_gap_adv_stop();
    s_advertising = 0;
    return rc;
}

int klin_ble_connected(void)
{
    return s_connected ? 1 : 0;
}

int klin_ble_advertising(void)
{
    return s_advertising ? 1 : 0;
}

int klin_ble_wait_connected(int timeout_ms)
{
    TickType_t ticks;
    EventBits_t bits;

    if (!s_inited || s_ble_events == NULL) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    if (timeout_ms < 0) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }

    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_CONNECTED_BIT, pdFALSE,
                               pdFALSE, ticks);
    if (bits & KLIN_BLE_CONNECTED_BIT) {
        return (int)ESP_OK;
    }
    return (int)ESP_ERR_TIMEOUT;
}

int klin_ble_stop(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    s_restart_adv = 0;
    (void)ble_gap_adv_stop();
    (void)ble_gap_disc_cancel();
    if (s_central_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_central_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_advertising = 0;
    s_scanning = 0;
    rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    }
    s_inited = 0;
    s_synced = 0;
    s_connected = 0;
    s_central_connected = 0;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_central_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_gatt_notify_enabled = 0;
    return rc;
}

int klin_ble_gatt_set(const uint8_t *data, int len)
{
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || len < 0 || len > KLIN_BLE_GATT_VALUE_MAX) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (len > 0) {
        memcpy(s_gatt_value, data, (size_t)len);
    }
    s_gatt_len = (uint16_t)len;
    return (int)ESP_OK;
}

int klin_ble_gatt_get(uint8_t *out, int max_len)
{
    int n;

    if (!s_inited) {
        return -1;
    }
    if (out == NULL || max_len < 0) {
        return -1;
    }
    n = (int)s_gatt_len;
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        memcpy(out, s_gatt_value, (size_t)n);
    }
    return n;
}

int klin_ble_gatt_len(void)
{
    return (int)s_gatt_len;
}

int klin_ble_gatt_notify(void)
{
    struct os_mbuf *om;
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (!s_connected || !s_gatt_notify_enabled ||
        s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_OK;
    }

    om = ble_hs_mbuf_from_flat(s_gatt_value, s_gatt_len);
    if (om == NULL) {
        return (int)ESP_ERR_NO_MEM;
    }
    rc = ble_gatts_notify_custom(s_conn_handle, s_chr_val_handle, om);
    return rc;
}

int klin_ble_gatt_written(void)
{
    int w = s_gatt_written;
    s_gatt_written = 0;
    return w ? 1 : 0;
}

int klin_ble_scan_start(int duration_ms)
{
    struct ble_gap_disc_params params;
    TickType_t ticks;
    EventBits_t bits;
    int32_t dur;
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (duration_ms <= 0) {
        return (int)ESP_ERR_INVALID_ARG;
    }

    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }

    /* Central path: do not auto-restart peripheral advertising. */
    s_restart_adv = 0;
    if (s_advertising) {
        (void)ble_gap_adv_stop();
        s_advertising = 0;
    }
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
        s_scanning = 0;
    }

    s_scan_count = 0;
    memset(s_scan, 0, sizeof(s_scan));
    xEventGroupClearBits(s_ble_events, KLIN_BLE_SCAN_DONE_BIT);

    memset(&params, 0, sizeof(params));
    /* Units: 0.625 ms. 0x10/0x10 ≈ 10 ms interval/window (active). */
    params.itvl = 0x0010;
    params.window = 0x0010;
    params.filter_policy = 0; /* no whitelist */
    params.limited = 0;
    params.passive = 0; /* active scan → scan RSP names */
    params.filter_duplicates = 1;

    /* NimBLE duration unit is 10 ms. */
    dur = (int32_t)((duration_ms + 9) / 10);
    if (dur < 1) {
        dur = 1;
    }

    s_scanning = 1;
    rc = ble_gap_disc(s_own_addr_type, dur, &params, klin_ble_gap_event, NULL);
    if (rc != 0) {
        s_scanning = 0;
        return rc;
    }

    ticks = pdMS_TO_TICKS((uint32_t)duration_ms + 2000u);
    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_SCAN_DONE_BIT, pdFALSE,
                               pdFALSE, ticks);
    s_scanning = 0;
    if ((bits & KLIN_BLE_SCAN_DONE_BIT) == 0) {
        (void)ble_gap_disc_cancel();
        return (int)ESP_ERR_TIMEOUT;
    }
    return (int)ESP_OK;
}

int klin_ble_scan_stop(void)
{
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (!s_scanning) {
        return (int)ESP_OK;
    }
    (void)ble_gap_disc_cancel();
    s_scanning = 0;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_SCAN_DONE_BIT);
    }
    return (int)ESP_OK;
}

int klin_ble_scan_count(void)
{
    return s_scan_count;
}

int klin_ble_scan_rssi(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return 0;
    }
    return (int)s_scan[index].rssi;
}

int klin_ble_scan_addr_type(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return -1;
    }
    return (int)s_scan[index].addr.type;
}

int klin_ble_scan_addr(int index, uint8_t *out6)
{
    if (index < 0 || index >= s_scan_count || out6 == NULL) {
        return -1;
    }
    memcpy(out6, s_scan[index].addr.val, 6);
    return 0;
}

int klin_ble_scan_name(int index, uint8_t *out, int max_len)
{
    size_t n;

    if (index < 0 || index >= s_scan_count || out == NULL || max_len < 0) {
        return -1;
    }
    n = strlen(s_scan[index].name);
    if ((int)n > max_len) {
        n = (size_t)max_len;
    }
    if (n > 0) {
        memcpy(out, s_scan[index].name, n);
    }
    return (int)n;
}

int klin_ble_central_connect(int index, int timeout_ms)
{
    int32_t to;
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= s_scan_count) {
        return (int)ESP_ERR_INVALID_ARG;
    }

    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }

    s_restart_adv = 0;
    if (s_advertising) {
        (void)ble_gap_adv_stop();
        s_advertising = 0;
    }
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
        s_scanning = 0;
    }
    if (s_central_connected &&
        s_central_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_central_conn_handle,
                                BLE_ERR_REM_USER_CONN_TERM);
        s_central_connected = 0;
        s_central_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    xEventGroupClearBits(s_ble_events, KLIN_BLE_CENTRAL_CONN_BIT);

    if (timeout_ms < 0) {
        to = BLE_HS_FOREVER;
    } else {
        to = (int32_t)timeout_ms;
    }

    rc = ble_gap_connect(s_own_addr_type, &s_scan[index].addr, to, NULL,
                         klin_ble_gap_event, NULL);
    return rc;
}

int klin_ble_central_connected(void)
{
    return s_central_connected ? 1 : 0;
}

int klin_ble_central_wait_connected(int timeout_ms)
{
    TickType_t ticks;
    EventBits_t bits;

    if (!s_inited || s_ble_events == NULL) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    if (timeout_ms < 0) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }

    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_CENTRAL_CONN_BIT, pdFALSE,
                               pdFALSE, ticks);
    if (bits & KLIN_BLE_CENTRAL_CONN_BIT) {
        return (int)ESP_OK;
    }
    return (int)ESP_ERR_TIMEOUT;
}

int klin_ble_central_disconnect(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (!s_central_connected ||
        s_central_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_OK;
    }
    rc = ble_gap_terminate(s_central_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return rc;
}
