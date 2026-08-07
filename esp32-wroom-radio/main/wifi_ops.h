#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Max APs reported back to the S3 per scan */
#define WROOM_SCAN_MAX_APS 20
#define WROOM_MAX_SSID_LEN 33

typedef struct {
    char    ssid[WROOM_MAX_SSID_LEN];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t  rssi;
} wroom_ap_t;

typedef struct {
    wroom_ap_t aps[WROOM_SCAN_MAX_APS];
    uint8_t    count;
} wroom_scan_result_t;

esp_err_t wifi_ops_init(void);
esp_err_t wifi_ops_scan(wroom_scan_result_t *out);
esp_err_t wifi_ops_start_deauth(const uint8_t bssid[6], uint8_t channel);
esp_err_t wifi_ops_start_beacon_flood(bool random_mode);
esp_err_t wifi_ops_stop(void);
uint32_t  wifi_ops_get_packets_sent(void);
uint32_t  wifi_ops_get_elapsed_sec(void);

#ifdef __cplusplus
}
#endif
