/* NimBLE peripheral bring-up for Klin under ESP-IDF v5.x.
 * Requires sdkconfig: CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED.
 */
#include "nimble_idf.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"

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

static int klin_ble_start_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
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

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return rc;
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
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
            }
        } else {
            /* Connection failed — resume advertising. */
            (void)klin_ble_start_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = 0;
        if (s_ble_events != NULL) {
            xEventGroupClearBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
        }
        /* Documented: peripheral restarts advertising after disconnect. */
        (void)klin_ble_start_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = 0;
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

    s_synced = 0;
    s_advertising = 0;
    s_connected = 0;
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
    return rc;
}
