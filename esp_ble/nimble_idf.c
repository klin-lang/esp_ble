/* NimBLE peripheral + GATT MVP for Klin under ESP-IDF v5.x.
 * Requires sdkconfig: CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED.
 *
 * Fixed GATT (documented):
 *   service 0xFFF0, characteristic 0xFFF1 — read | write | notify
 *   value buffer max KLIN_BLE_GATT_VALUE_MAX bytes (caller copies in/out)
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

#define KLIN_BLE_SYNC_BIT      BIT0
#define KLIN_BLE_CONNECTED_BIT BIT1
#define KLIN_BLE_NAME_MAX      28

static EventGroupHandle_t s_ble_events;
static int s_inited;
static int s_synced;
static int s_advertising;
static int s_connected;
static uint8_t s_own_addr_type;
static char s_name[KLIN_BLE_NAME_MAX];

static uint16_t s_conn_handle;
static uint16_t s_chr_val_handle;
static uint8_t s_gatt_value[KLIN_BLE_GATT_VALUE_MAX];
static uint16_t s_gatt_len;
static int s_gatt_written;
static int s_gatt_notify_enabled;

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
        /* Adv payload may not fit name + UUID — retry name-only. */
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
    return 0;
}

static int klin_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = 1;
            s_advertising = 0;
            s_conn_handle = event->connect.conn_handle;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
            }
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            (void)klin_ble_start_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = 0;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_gatt_notify_enabled = 0;
        if (s_ble_events != NULL) {
            xEventGroupClearBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
        }
        /* Documented: peripheral restarts advertising after disconnect. */
        (void)klin_ble_start_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = 0;
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_chr_val_handle) {
            s_gatt_notify_enabled = event->subscribe.cur_notify ? 1 : 0;
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
    /* No NVS bonding store in MVP — pairing/bonding later. */
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
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_gatt_len = 0;
    s_gatt_written = 0;
    s_gatt_notify_enabled = 0;
    memset(s_gatt_value, 0, sizeof(s_gatt_value));
    s_name[0] = '\0';
    xEventGroupClearBits(s_ble_events,
                         KLIN_BLE_SYNC_BIT | KLIN_BLE_CONNECTED_BIT);

    nimble_port_freertos_init(klin_ble_host_task);
    s_inited = 1;
    return (int)ESP_OK;
}

int klin_ble_advertise(const char *name)
{
    TickType_t ticks;
    EventBits_t bits;
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

    ticks = pdMS_TO_TICKS(5000);
    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_SYNC_BIT, pdFALSE,
                               pdFALSE, ticks);
    if ((bits & KLIN_BLE_SYNC_BIT) == 0) {
        return (int)ESP_ERR_TIMEOUT;
    }

    return klin_ble_start_advertise();
}

int klin_ble_stop_advertise(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
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
    (void)ble_gap_adv_stop();
    s_advertising = 0;
    rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    }
    s_inited = 0;
    s_synced = 0;
    s_connected = 0;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
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
