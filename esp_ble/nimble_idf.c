/* NimBLE peripheral GATT + central scan/connect + GATT client + bonding
 * for Klin (ESP-IDF v5.x). Requires: CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED
 * (+ central/observer roles for scan/connect/client).
 */
#include "nimble_idf.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "host/ble_hs_pvcy.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

#define KLIN_BLE_SYNC_BIT           BIT0
#define KLIN_BLE_CONNECTED_BIT      BIT1
#define KLIN_BLE_SCAN_DONE_BIT      BIT2
#define KLIN_BLE_CENTRAL_CONN_BIT   BIT3
#define KLIN_BLE_GATTC_DISC_BIT     BIT4
#define KLIN_BLE_GATTC_OP_BIT       BIT5
#define KLIN_BLE_BOND_BIT           BIT6
#define KLIN_BLE_NAME_MAX           28
#define KLIN_BLE_CCCD_UUID16        0x2902
#define KLIN_BLE_GATT_SVC_MAX       4
#define KLIN_BLE_UUID_KIND_16       16
#define KLIN_BLE_UUID_KIND_128      128

typedef struct {
    ble_addr_t addr;
    int8_t rssi;
    char name[KLIN_BLE_SCAN_NAME_MAX];
} klin_ble_scan_row_t;

typedef struct {
    uint8_t kind; /* 16 or 128 */
    ble_uuid16_t svc16;
    ble_uuid16_t chr16;
    ble_uuid128_t svc128;
    ble_uuid128_t chr128;
    uint16_t val_handle;
    uint8_t value[KLIN_BLE_GATT_VALUE_MAX];
    uint16_t value_len;
    int written;
    int notify_enabled;
} klin_ble_gatt_slot_t;

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

static klin_ble_scan_row_t s_scan[KLIN_BLE_SCAN_MAX];
static int s_scan_count;

/* GATT client (central) — discovers selected svc/chr on the peer. */
static uint16_t s_gattc_svc_start;
static uint16_t s_gattc_svc_end;
static uint16_t s_gattc_val_handle;
static uint16_t s_gattc_cccd_handle;
static int s_gattc_ready;
static int s_gattc_op_rc;
static uint8_t s_gattc_buf[KLIN_BLE_GATT_VALUE_MAX];
static uint16_t s_gattc_buf_len;
static int s_gattc_notified;

/* Bonding / pairing. Keys live in IDF NVS via ble_store.
 * Just Works: bond_enable. Passkey/PIN: bond_passkey(pin).
 */
static int s_bond_enabled;
static int s_bonded; /* current link encrypted+bonded after ENC_CHANGE */
static int s_passkey_mode;
static uint32_t s_passkey;
static int s_passkey_action; /* last BLE_SM_IOACT_* seen */
static int s_privacy; /* host RPA enabled via privacy_enable */

/* Server GATT table (max KLIN_BLE_GATT_SVC_MAX). Built before init. */
static klin_ble_gatt_slot_t s_slots[KLIN_BLE_GATT_SVC_MAX];
static int s_slot_count;
static int s_gatt_registered;
static struct ble_gatt_chr_def s_chr_defs[KLIN_BLE_GATT_SVC_MAX][2];
static struct ble_gatt_svc_def s_gatt_svcs[KLIN_BLE_GATT_SVC_MAX + 1];

/* Client discover target (defaults to slot 0 / override via gattc_uuid*). */
static int s_gattc_sel;
static int s_gattc_override;
static uint8_t s_gattc_kind;
static ble_uuid16_t s_gattc_svc16;
static ble_uuid16_t s_gattc_chr16;
static ble_uuid128_t s_gattc_svc128;
static ble_uuid128_t s_gattc_chr128;

static const ble_uuid16_t s_cccd_uuid = BLE_UUID16_INIT(KLIN_BLE_CCCD_UUID16);

static int klin_ble_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const ble_uuid_t *klin_ble_slot_svc_uuid(int idx)
{
    if (idx < 0 || idx >= s_slot_count) {
        return NULL;
    }
    if (s_slots[idx].kind == KLIN_BLE_UUID_KIND_128) {
        return &s_slots[idx].svc128.u;
    }
    return &s_slots[idx].svc16.u;
}

static const ble_uuid_t *klin_ble_slot_chr_uuid(int idx)
{
    if (idx < 0 || idx >= s_slot_count) {
        return NULL;
    }
    if (s_slots[idx].kind == KLIN_BLE_UUID_KIND_128) {
        return &s_slots[idx].chr128.u;
    }
    return &s_slots[idx].chr16.u;
}

static const ble_uuid_t *klin_ble_gattc_svc_uuid(void)
{
    if (s_gattc_override) {
        if (s_gattc_kind == KLIN_BLE_UUID_KIND_128) {
            return &s_gattc_svc128.u;
        }
        return &s_gattc_svc16.u;
    }
    return klin_ble_slot_svc_uuid(s_gattc_sel);
}

static const ble_uuid_t *klin_ble_gattc_chr_uuid(void)
{
    if (s_gattc_override) {
        if (s_gattc_kind == KLIN_BLE_UUID_KIND_128) {
            return &s_gattc_chr128.u;
        }
        return &s_gattc_chr16.u;
    }
    return klin_ble_slot_chr_uuid(s_gattc_sel);
}

static int klin_ble_slot_find_handle(uint16_t attr_handle)
{
    int i;

    for (i = 0; i < s_slot_count; i++) {
        if (s_slots[i].val_handle == attr_handle) {
            return i;
        }
    }
    return -1;
}

static void klin_ble_slots_ensure_default(void)
{
    if (s_slot_count > 0) {
        return;
    }
    memset(&s_slots[0], 0, sizeof(s_slots[0]));
    s_slots[0].kind = KLIN_BLE_UUID_KIND_16;
    s_slots[0].svc16 = (ble_uuid16_t)BLE_UUID16_INIT(KLIN_BLE_GATT_SVC_UUID16);
    s_slots[0].chr16 = (ble_uuid16_t)BLE_UUID16_INIT(KLIN_BLE_GATT_CHR_UUID16);
    s_slot_count = 1;
}

static void klin_ble_build_gatt_svcs(void)
{
    int i;

    klin_ble_slots_ensure_default();
    memset(s_chr_defs, 0, sizeof(s_chr_defs));
    memset(s_gatt_svcs, 0, sizeof(s_gatt_svcs));
    for (i = 0; i < s_slot_count; i++) {
        s_chr_defs[i][0].uuid = klin_ble_slot_chr_uuid(i);
        s_chr_defs[i][0].access_cb = klin_ble_gatt_access;
        s_chr_defs[i][0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                                 BLE_GATT_CHR_F_NOTIFY;
        s_chr_defs[i][0].val_handle = &s_slots[i].val_handle;
        s_gatt_svcs[i].type = BLE_GATT_SVC_TYPE_PRIMARY;
        s_gatt_svcs[i].uuid = klin_ble_slot_svc_uuid(i);
        s_gatt_svcs[i].characteristics = s_chr_defs[i];
    }
}

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

static void klin_ble_gattc_reset(void)
{
    s_gattc_svc_start = 0;
    s_gattc_svc_end = 0;
    s_gattc_val_handle = 0;
    s_gattc_cccd_handle = 0;
    s_gattc_ready = 0;
    s_gattc_op_rc = 0;
    s_gattc_buf_len = 0;
    s_gattc_notified = 0;
    memset(s_gattc_buf, 0, sizeof(s_gattc_buf));
}

static uint16_t klin_ble_active_conn(void)
{
    if (s_central_connected &&
        s_central_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return s_central_conn_handle;
    }
    if (s_connected && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return s_conn_handle;
    }
    return BLE_HS_CONN_HANDLE_NONE;
}

static void klin_ble_bond_link_reset(void)
{
    s_bonded = 0;
    if (s_ble_events != NULL) {
        xEventGroupClearBits(s_ble_events, KLIN_BLE_BOND_BIT);
    }
}

static int klin_ble_gattc_wait_bit(EventBits_t bit, int timeout_ms)
{
    TickType_t ticks;
    EventBits_t bits;

    if (timeout_ms < 0) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }
    bits = xEventGroupWaitBits(s_ble_events, bit, pdTRUE, pdFALSE, ticks);
    if (bits & bit) {
        return (int)ESP_OK;
    }
    return (int)ESP_ERR_TIMEOUT;
}

static int klin_ble_gattc_on_dsc(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 uint16_t chr_val_handle,
                                 const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn_handle;
    (void)chr_val_handle;
    (void)arg;

    if (error->status == 0 && dsc != NULL) {
        if (ble_uuid_cmp(&dsc->uuid.u, &s_cccd_uuid.u) == 0) {
            s_gattc_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        s_gattc_ready = (s_gattc_val_handle != 0) ? 1 : 0;
        s_gattc_op_rc = s_gattc_ready ? 0 : BLE_HS_ENOENT;
        if (s_ble_events != NULL) {
            xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
        }
        return 0;
    }
    s_gattc_op_rc = error->status;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
    }
    return 0;
}

static int klin_ble_gattc_on_chr(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_chr *chr, void *arg)
{
    int rc;

    (void)arg;
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, klin_ble_gattc_chr_uuid()) == 0) {
            s_gattc_val_handle = chr->val_handle;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_gattc_val_handle == 0 || s_gattc_svc_end == 0) {
            s_gattc_op_rc = BLE_HS_ENOENT;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
            }
            return 0;
        }
        /* Descriptors after the value handle up to service end. */
        rc = ble_gattc_disc_all_dscs(conn_handle, s_gattc_val_handle,
                                     s_gattc_svc_end, klin_ble_gattc_on_dsc,
                                     NULL);
        if (rc != 0) {
            /* Char found; CCCD optional for read/write-only peers. */
            s_gattc_ready = 1;
            s_gattc_op_rc = 0;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
            }
        }
        return 0;
    }
    s_gattc_op_rc = error->status;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
    }
    return 0;
}

static int klin_ble_gattc_on_svc(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service, void *arg)
{
    int rc;

    (void)arg;
    if (error->status == 0 && service != NULL) {
        s_gattc_svc_start = service->start_handle;
        s_gattc_svc_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_gattc_svc_start == 0) {
            s_gattc_op_rc = BLE_HS_ENOENT;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
            }
            return 0;
        }
        rc = ble_gattc_disc_chrs_by_uuid(conn_handle, s_gattc_svc_start,
                                         s_gattc_svc_end,
                                         klin_ble_gattc_chr_uuid(),
                                         klin_ble_gattc_on_chr, NULL);
        if (rc != 0) {
            s_gattc_op_rc = rc;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
            }
        }
        return 0;
    }
    s_gattc_op_rc = error->status;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
    }
    return 0;
}

static int klin_ble_gattc_on_read(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg)
{
    uint16_t om_len;
    int rc;

    (void)conn_handle;
    (void)arg;
    if (error->status != 0) {
        s_gattc_op_rc = error->status;
        if (s_ble_events != NULL) {
            xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
        }
        return 0;
    }
    om_len = OS_MBUF_PKTLEN(attr->om);
    if (om_len > KLIN_BLE_GATT_VALUE_MAX) {
        om_len = KLIN_BLE_GATT_VALUE_MAX;
    }
    rc = ble_hs_mbuf_to_flat(attr->om, s_gattc_buf, om_len, NULL);
    if (rc != 0) {
        s_gattc_op_rc = rc;
        s_gattc_buf_len = 0;
    } else {
        s_gattc_buf_len = om_len;
        s_gattc_op_rc = 0;
    }
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
    }
    return 0;
}

static int klin_ble_gattc_on_write(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    s_gattc_op_rc = error->status;
    if (s_ble_events != NULL) {
        xEventGroupSetBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
    }
    return 0;
}

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
    int idx;

    (void)conn_handle;
    (void)arg;

    idx = klin_ble_slot_find_handle(attr_handle);
    if (idx < 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        rc = os_mbuf_append(ctxt->om, s_slots[idx].value,
                            s_slots[idx].value_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len > KLIN_BLE_GATT_VALUE_MAX) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        rc = ble_hs_mbuf_to_flat(ctxt->om, s_slots[idx].value, om_len, NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_slots[idx].value_len = om_len;
        s_slots[idx].written = 1;
        return 0;
    }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int klin_ble_start_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
    ble_uuid16_t uuids16[KLIN_BLE_GATT_SVC_MAX];
    ble_uuid128_t uuids128[1];
    int n16 = 0;
    int i;
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

    for (i = 0; i < s_slot_count; i++) {
        if (s_slots[i].kind == KLIN_BLE_UUID_KIND_16 &&
            n16 < KLIN_BLE_GATT_SVC_MAX) {
            uuids16[n16++] = s_slots[i].svc16;
        }
    }
    if (n16 > 0) {
        fields.uuids16 = uuids16;
        fields.num_uuids16 = (uint8_t)n16;
        fields.uuids16_is_complete = 1;
    } else if (s_slot_count > 0 &&
               s_slots[0].kind == KLIN_BLE_UUID_KIND_128) {
        uuids128[0] = s_slots[0].svc128;
        fields.uuids128 = uuids128;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
    }

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
            klin_ble_gattc_reset();
            klin_ble_bond_link_reset();
            if (s_ble_events != NULL) {
                xEventGroupClearBits(s_ble_events, KLIN_BLE_CENTRAL_CONN_BIT);
            }
        } else {
            int i;

            s_connected = 0;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            for (i = 0; i < s_slot_count; i++) {
                s_slots[i].notify_enabled = 0;
            }
            klin_ble_bond_link_reset();
            if (s_ble_events != NULL) {
                xEventGroupClearBits(s_ble_events, KLIN_BLE_CONNECTED_BIT);
            }
            if (s_restart_adv) {
                (void)klin_ble_start_advertise();
            }
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            s_bonded = 1;
            if (s_ble_events != NULL) {
                xEventGroupSetBits(s_ble_events, KLIN_BLE_BOND_BIT);
            }
        } else {
            s_bonded = 0;
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey;

        s_passkey_action = (int)event->passkey.params.action;
        if (!s_passkey_mode) {
            return 0;
        }

        memset(&pkey, 0, sizeof(pkey));
        pkey.action = event->passkey.params.action;
        if (pkey.action == BLE_SM_IOACT_DISP ||
            pkey.action == BLE_SM_IOACT_INPUT) {
            /* Fixed application PIN (000000..999999). */
            pkey.passkey = s_passkey;
            (void)ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        } else if (pkey.action == BLE_SM_IOACT_NUMCMP) {
            /* Stack-generated number; accept to complete pairing. */
            pkey.numcmp_accept = 1;
            (void)ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Replace old bond (explicit convenience policy for this MVP). */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t n;

        if (event->notify_rx.attr_handle == s_gattc_val_handle &&
            event->notify_rx.om != NULL) {
            n = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (n > KLIN_BLE_GATT_VALUE_MAX) {
                n = KLIN_BLE_GATT_VALUE_MAX;
            }
            if (ble_hs_mbuf_to_flat(event->notify_rx.om, s_gattc_buf, n,
                                   NULL) == 0) {
                s_gattc_buf_len = n;
                s_gattc_notified = 1;
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = 0;
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        int idx = klin_ble_slot_find_handle(event->subscribe.attr_handle);
        if (idx >= 0) {
            s_slots[idx].notify_enabled = event->subscribe.cur_notify ? 1 : 0;
        }
        return 0;
    }

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

    if (s_privacy) {
        /* Re-apply after controller reset/sync. */
        rc = ble_hs_pvcy_rpa_config(1);
        if (rc != 0) {
            printf("klin_ble: pvcy_rpa_config rc=%d\n", rc);
            return;
        }
        rc = ble_hs_util_ensure_addr(1);
        if (rc != 0) {
            printf("klin_ble: ensure_addr(random) rc=%d\n", rc);
            return;
        }
        s_own_addr_type = BLE_OWN_ADDR_RANDOM;
    } else {
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
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    klin_ble_build_gatt_svcs();
    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    s_gatt_registered = 1;

    /* NVS-backed key store (used when bond_enable + bond_start). */
    ble_store_config_init();

    s_synced = 0;
    s_advertising = 0;
    s_connected = 0;
    s_central_connected = 0;
    s_restart_adv = 0;
    s_scanning = 0;
    s_scan_count = 0;
    s_bond_enabled = 0;
    s_bonded = 0;
    s_passkey_mode = 0;
    s_passkey = 0;
    s_passkey_action = 0;
    s_privacy = 0;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_central_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_gattc_sel = 0;
    s_gattc_override = 0;
    {
        int i;
        for (i = 0; i < s_slot_count; i++) {
            s_slots[i].value_len = 0;
            s_slots[i].written = 0;
            s_slots[i].notify_enabled = 0;
            memset(s_slots[i].value, 0, sizeof(s_slots[i].value));
        }
    }
    memset(s_scan, 0, sizeof(s_scan));
    s_name[0] = '\0';
    klin_ble_gattc_reset();
    xEventGroupClearBits(s_ble_events,
                         KLIN_BLE_SYNC_BIT | KLIN_BLE_CONNECTED_BIT |
                             KLIN_BLE_SCAN_DONE_BIT | KLIN_BLE_CENTRAL_CONN_BIT |
                             KLIN_BLE_BOND_BIT);

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
    {
        int i;
        for (i = 0; i < KLIN_BLE_GATT_SVC_MAX; i++) {
            s_slots[i].notify_enabled = 0;
        }
    }
    return rc;
}

int klin_ble_gatt_set_at(int index, const uint8_t *data, int len)
{
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= s_slot_count) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (data == NULL || len < 0 || len > KLIN_BLE_GATT_VALUE_MAX) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (len > 0) {
        memcpy(s_slots[index].value, data, (size_t)len);
    }
    s_slots[index].value_len = (uint16_t)len;
    return (int)ESP_OK;
}

int klin_ble_gatt_set(const uint8_t *data, int len)
{
    return klin_ble_gatt_set_at(0, data, len);
}

int klin_ble_gatt_get_at(int index, uint8_t *out, int max_len)
{
    int n;

    if (!s_inited) {
        return -1;
    }
    if (index < 0 || index >= s_slot_count || out == NULL || max_len < 0) {
        return -1;
    }
    n = (int)s_slots[index].value_len;
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        memcpy(out, s_slots[index].value, (size_t)n);
    }
    return n;
}

int klin_ble_gatt_get(uint8_t *out, int max_len)
{
    return klin_ble_gatt_get_at(0, out, max_len);
}

int klin_ble_gatt_len_at(int index)
{
    if (index < 0 || index >= s_slot_count) {
        return 0;
    }
    return (int)s_slots[index].value_len;
}

int klin_ble_gatt_len(void)
{
    return klin_ble_gatt_len_at(0);
}

int klin_ble_gatt_notify_at(int index)
{
    struct os_mbuf *om;
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= s_slot_count) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (!s_connected || !s_slots[index].notify_enabled ||
        s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_OK;
    }

    om = ble_hs_mbuf_from_flat(s_slots[index].value, s_slots[index].value_len);
    if (om == NULL) {
        return (int)ESP_ERR_NO_MEM;
    }
    rc = ble_gatts_notify_custom(s_conn_handle, s_slots[index].val_handle, om);
    return rc;
}

int klin_ble_gatt_notify(void)
{
    return klin_ble_gatt_notify_at(0);
}

int klin_ble_gatt_written_at(int index)
{
    int w;

    if (index < 0 || index >= s_slot_count) {
        return 0;
    }
    w = s_slots[index].written;
    s_slots[index].written = 0;
    return w ? 1 : 0;
}

int klin_ble_gatt_written(void)
{
    return klin_ble_gatt_written_at(0);
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
        klin_ble_gattc_reset();
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

int klin_ble_gattc_discover(int timeout_ms)
{
    int rc;

    if (!s_inited || !s_central_connected ||
        s_central_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    klin_ble_gattc_reset();
    if (s_ble_events != NULL) {
        xEventGroupClearBits(s_ble_events, KLIN_BLE_GATTC_DISC_BIT);
    }

    {
        const ble_uuid_t *svc_uuid = klin_ble_gattc_svc_uuid();
        if (svc_uuid == NULL) {
            return (int)ESP_ERR_INVALID_STATE;
        }
        rc = ble_gattc_disc_svc_by_uuid(s_central_conn_handle, svc_uuid,
                                        klin_ble_gattc_on_svc, NULL);
    }
    if (rc != 0) {
        return rc;
    }

    rc = klin_ble_gattc_wait_bit(KLIN_BLE_GATTC_DISC_BIT, timeout_ms);
    if (rc != 0) {
        return rc;
    }
    if (s_gattc_ready && s_gattc_op_rc == 0) {
        return (int)ESP_OK;
    }
    return s_gattc_op_rc != 0 ? s_gattc_op_rc : BLE_HS_ENOENT;
}

int klin_ble_gattc_ready(void)
{
    return (s_gattc_ready && s_central_connected) ? 1 : 0;
}

int klin_ble_gattc_read(int timeout_ms)
{
    int rc;

    if (!s_inited || !s_gattc_ready || !s_central_connected ||
        s_gattc_val_handle == 0) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    if (s_ble_events != NULL) {
        xEventGroupClearBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
    }
    s_gattc_op_rc = 0;
    s_gattc_buf_len = 0;

    rc = ble_gattc_read(s_central_conn_handle, s_gattc_val_handle,
                        klin_ble_gattc_on_read, NULL);
    if (rc != 0) {
        return rc;
    }

    rc = klin_ble_gattc_wait_bit(KLIN_BLE_GATTC_OP_BIT, timeout_ms);
    if (rc != 0) {
        return rc;
    }
    return s_gattc_op_rc;
}

int klin_ble_gattc_write(const uint8_t *data, int len, int timeout_ms)
{
    int rc;

    if (!s_inited || !s_gattc_ready || !s_central_connected ||
        s_gattc_val_handle == 0) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || len < 0 || len > KLIN_BLE_GATT_VALUE_MAX) {
        return (int)ESP_ERR_INVALID_ARG;
    }

    if (s_ble_events != NULL) {
        xEventGroupClearBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
    }
    s_gattc_op_rc = 0;

    rc = ble_gattc_write_flat(s_central_conn_handle, s_gattc_val_handle, data,
                              (uint16_t)len, klin_ble_gattc_on_write, NULL);
    if (rc != 0) {
        return rc;
    }

    rc = klin_ble_gattc_wait_bit(KLIN_BLE_GATTC_OP_BIT, timeout_ms);
    if (rc != 0) {
        return rc;
    }
    return s_gattc_op_rc;
}

int klin_ble_gattc_subscribe(int timeout_ms)
{
    uint8_t cccd[2] = {0x01, 0x00};
    int rc;

    if (!s_inited || !s_gattc_ready || !s_central_connected) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_gattc_cccd_handle == 0) {
        return BLE_HS_ENOENT;
    }

    if (s_ble_events != NULL) {
        xEventGroupClearBits(s_ble_events, KLIN_BLE_GATTC_OP_BIT);
    }
    s_gattc_op_rc = 0;

    rc = ble_gattc_write_flat(s_central_conn_handle, s_gattc_cccd_handle, cccd,
                              2, klin_ble_gattc_on_write, NULL);
    if (rc != 0) {
        return rc;
    }

    rc = klin_ble_gattc_wait_bit(KLIN_BLE_GATTC_OP_BIT, timeout_ms);
    if (rc != 0) {
        return rc;
    }
    return s_gattc_op_rc;
}

int klin_ble_gattc_notified(void)
{
    int n = s_gattc_notified;
    s_gattc_notified = 0;
    return n;
}

int klin_ble_gattc_get(uint8_t *out, int max_len)
{
    int n;

    if (out == NULL || max_len < 0) {
        return -1;
    }
    n = (int)s_gattc_buf_len;
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        memcpy(out, s_gattc_buf, (size_t)n);
    }
    return n;
}

int klin_ble_gattc_len(void)
{
    return (int)s_gattc_buf_len;
}

int klin_ble_bond_enable(void)
{
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    /* Just Works: no display/keyboard; bonding stores LTK (+ IRK) in NVS. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    s_passkey_mode = 0;
    s_passkey = 0;
    s_passkey_action = 0;
    s_bond_enabled = 1;
    return (int)ESP_OK;
}

/**
 * Enable bonding with a fixed 6-digit passkey/PIN (0..999999). MITM + keyboard
 * /display IO. On PASSKEY_ACTION the PIN is injected automatically.
 */
int klin_ble_bond_passkey(int passkey)
{
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (passkey < 0 || passkey > 999999) {
        return (int)ESP_ERR_INVALID_ARG;
    }

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_DISP;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    s_passkey = (uint32_t)passkey;
    s_passkey_mode = 1;
    s_passkey_action = 0;
    s_bond_enabled = 1;
    return (int)ESP_OK;
}

/** Current configured passkey (0 if Just Works / unset). */
int klin_ble_passkey(void)
{
    return s_passkey_mode ? (int)s_passkey : 0;
}

/** Last PASSKEY_ACTION code (`BLE_SM_IOACT_*`), or 0. */
int klin_ble_passkey_action(void)
{
    return s_passkey_action;
}

/**
 * Inject a passkey for an outstanding INPUT/DISP action (override). Usually
 * unnecessary when `bond_passkey` auto-injects the configured PIN.
 */
int klin_ble_passkey_inject(int passkey)
{
    struct ble_sm_io pkey;
    uint16_t conn;

    if (!s_inited || !s_passkey_mode) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (passkey < 0 || passkey > 999999) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    conn = klin_ble_active_conn();
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    memset(&pkey, 0, sizeof(pkey));
    if (s_passkey_action == BLE_SM_IOACT_DISP ||
        s_passkey_action == BLE_SM_IOACT_INPUT) {
        pkey.action = (uint8_t)s_passkey_action;
    } else {
        pkey.action = BLE_SM_IOACT_INPUT;
    }
    pkey.passkey = (uint32_t)passkey;
    return ble_sm_inject_io(conn, &pkey);
}

int klin_ble_bond_start(void)
{
    uint16_t conn;
    int rc;

    if (!s_inited || !s_bond_enabled) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    conn = klin_ble_active_conn();
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    klin_ble_bond_link_reset();
    rc = ble_gap_security_initiate(conn);
    return rc;
}

int klin_ble_bonded(void)
{
    return s_bonded ? 1 : 0;
}

int klin_ble_wait_bonded(int timeout_ms)
{
    TickType_t ticks;
    EventBits_t bits;

    if (!s_inited || s_ble_events == NULL) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_bonded) {
        return (int)ESP_OK;
    }

    if (timeout_ms < 0) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }

    bits = xEventGroupWaitBits(s_ble_events, KLIN_BLE_BOND_BIT, pdFALSE,
                               pdFALSE, ticks);
    if (bits & KLIN_BLE_BOND_BIT) {
        return (int)ESP_OK;
    }
    return (int)ESP_ERR_TIMEOUT;
}

int klin_ble_bond_count(void)
{
    int n = 0;
    int rc;

    if (!s_inited) {
        return 0;
    }
    rc = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
    if (rc != 0) {
        return 0;
    }
    return n;
}

int klin_ble_bond_clear(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    rc = ble_store_clear();
    klin_ble_bond_link_reset();
    return rc;
}

/**
 * Replace slot 0 with a 16-bit svc/chr pair (creates slot 0 if empty).
 * Must be called before `init`.
 */
int klin_ble_gatt_uuid16(int svc_uuid16, int chr_uuid16)
{
    if (svc_uuid16 <= 0 || svc_uuid16 > 0xFFFF || chr_uuid16 <= 0 ||
        chr_uuid16 > 0xFFFF) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (s_gatt_registered || s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_slot_count < 1) {
        s_slot_count = 1;
        memset(&s_slots[0], 0, sizeof(s_slots[0]));
    }
    s_slots[0].kind = KLIN_BLE_UUID_KIND_16;
    s_slots[0].svc16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)svc_uuid16);
    s_slots[0].chr16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)chr_uuid16);
    s_gattc_override = 0;
    s_gattc_sel = 0;
    return (int)ESP_OK;
}

/** Replace slot 0 with a 128-bit svc/chr (16 bytes LE each). Before `init`. */
int klin_ble_gatt_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    if (svc16 == NULL || chr16 == NULL) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (s_gatt_registered || s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_slot_count < 1) {
        s_slot_count = 1;
        memset(&s_slots[0], 0, sizeof(s_slots[0]));
    }
    s_slots[0].kind = KLIN_BLE_UUID_KIND_128;
    s_slots[0].svc128.u.type = BLE_UUID_TYPE_128;
    s_slots[0].chr128.u.type = BLE_UUID_TYPE_128;
    memcpy(s_slots[0].svc128.value, svc16, 16);
    memcpy(s_slots[0].chr128.value, chr16, 16);
    s_gattc_override = 0;
    s_gattc_sel = 0;
    return (int)ESP_OK;
}

/** Append a 16-bit svc/chr (max KLIN_BLE_GATT_SVC_MAX). Before `init`. */
int klin_ble_gatt_add_uuid16(int svc_uuid16, int chr_uuid16)
{
    int i;

    if (svc_uuid16 <= 0 || svc_uuid16 > 0xFFFF || chr_uuid16 <= 0 ||
        chr_uuid16 > 0xFFFF) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (s_gatt_registered || s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_slot_count >= KLIN_BLE_GATT_SVC_MAX) {
        return (int)ESP_ERR_NO_MEM;
    }
    i = s_slot_count++;
    memset(&s_slots[i], 0, sizeof(s_slots[i]));
    s_slots[i].kind = KLIN_BLE_UUID_KIND_16;
    s_slots[i].svc16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)svc_uuid16);
    s_slots[i].chr16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)chr_uuid16);
    return (int)ESP_OK;
}

/** Append a 128-bit svc/chr. Before `init`. */
int klin_ble_gatt_add_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    int i;

    if (svc16 == NULL || chr16 == NULL) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (s_gatt_registered || s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_slot_count >= KLIN_BLE_GATT_SVC_MAX) {
        return (int)ESP_ERR_NO_MEM;
    }
    i = s_slot_count++;
    memset(&s_slots[i], 0, sizeof(s_slots[i]));
    s_slots[i].kind = KLIN_BLE_UUID_KIND_128;
    s_slots[i].svc128.u.type = BLE_UUID_TYPE_128;
    s_slots[i].chr128.u.type = BLE_UUID_TYPE_128;
    memcpy(s_slots[i].svc128.value, svc16, 16);
    memcpy(s_slots[i].chr128.value, chr16, 16);
    return (int)ESP_OK;
}

/** Clear the service table (must `gatt_add_*` / `gatt_uuid*` before init). */
int klin_ble_gatt_clear(void)
{
    if (s_gatt_registered || s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    s_slot_count = 0;
    memset(s_slots, 0, sizeof(s_slots));
    return (int)ESP_OK;
}

int klin_ble_gatt_svc_count(void)
{
    if (s_slot_count == 0 && !s_inited) {
        return 1; /* default will be installed at init */
    }
    return s_slot_count > 0 ? s_slot_count : (s_inited ? s_slot_count : 1);
}

int klin_ble_gatt_svc_uuid16(void)
{
    if (s_slot_count < 1) {
        return KLIN_BLE_GATT_SVC_UUID16;
    }
    if (s_slots[0].kind != KLIN_BLE_UUID_KIND_16) {
        return 0;
    }
    return (int)s_slots[0].svc16.value;
}

int klin_ble_gatt_chr_uuid16(void)
{
    if (s_slot_count < 1) {
        return KLIN_BLE_GATT_CHR_UUID16;
    }
    if (s_slots[0].kind != KLIN_BLE_UUID_KIND_16) {
        return 0;
    }
    return (int)s_slots[0].chr16.value;
}

/** Select server slot UUID for subsequent `gattc_discover`. */
int klin_ble_gattc_select(int index)
{
    if (index < 0 || index >= (s_slot_count > 0 ? s_slot_count : 1)) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    s_gattc_sel = index;
    s_gattc_override = 0;
    return (int)ESP_OK;
}

/** Override discover target with 16-bit UUIDs (central-only peers). */
int klin_ble_gattc_uuid16(int svc_uuid16, int chr_uuid16)
{
    if (svc_uuid16 <= 0 || svc_uuid16 > 0xFFFF || chr_uuid16 <= 0 ||
        chr_uuid16 > 0xFFFF) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    s_gattc_kind = KLIN_BLE_UUID_KIND_16;
    s_gattc_svc16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)svc_uuid16);
    s_gattc_chr16 = (ble_uuid16_t)BLE_UUID16_INIT((uint16_t)chr_uuid16);
    s_gattc_override = 1;
    return (int)ESP_OK;
}

/** Override discover target with 128-bit UUIDs (16 bytes LE each). */
int klin_ble_gattc_uuid128(const uint8_t *svc16, const uint8_t *chr16)
{
    if (svc16 == NULL || chr16 == NULL) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    s_gattc_kind = KLIN_BLE_UUID_KIND_128;
    s_gattc_svc128.u.type = BLE_UUID_TYPE_128;
    s_gattc_chr128.u.type = BLE_UUID_TYPE_128;
    memcpy(s_gattc_svc128.value, svc16, 16);
    memcpy(s_gattc_chr128.value, chr16, 16);
    s_gattc_override = 1;
    return (int)ESP_OK;
}

/**
 * Enable host-based RPA privacy. Call after `init`, before advertise/scan/connect.
 * Own address type becomes BLE_OWN_ADDR_RANDOM; NimBLE rotates RPA on timeout.
 */
int klin_ble_privacy_enable(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_advertising || s_scanning || s_connected || s_central_connected) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_pvcy_rpa_config(1);
    if (rc != 0) {
        return rc;
    }
    rc = ble_hs_util_ensure_addr(1);
    if (rc != 0) {
        return rc;
    }
    s_own_addr_type = BLE_OWN_ADDR_RANDOM;
    s_privacy = 1;
    return (int)ESP_OK;
}

/** Disable privacy; restore public/static address inference. */
int klin_ble_privacy_disable(void)
{
    int rc;

    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    if (s_advertising || s_scanning || s_connected || s_central_connected) {
        return (int)ESP_ERR_INVALID_STATE;
    }

    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_pvcy_rpa_config(0);
    if (rc != 0) {
        return rc;
    }
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        return rc;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        return rc;
    }
    s_privacy = 0;
    return (int)ESP_OK;
}

int klin_ble_privacy_enabled(void)
{
    return s_privacy ? 1 : 0;
}

int klin_ble_own_addr_type(void)
{
    return (int)s_own_addr_type;
}

int klin_ble_own_addr(uint8_t *out6)
{
    int rc;

    if (out6 == NULL) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return (int)ESP_ERR_INVALID_STATE;
    }
    rc = klin_ble_wait_sync();
    if (rc != 0) {
        return rc;
    }
    rc = ble_hs_id_copy_addr(s_own_addr_type, out6, NULL);
    return rc;
}
