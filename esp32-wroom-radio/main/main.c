#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "protocol.h"
#include "wifi_ops.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wroom_main";

/* ---- UART config ---------------------------------------------------- */
#define UART_PORT    UART_NUM_2
#define PIN_RX       16   /* S3 TX → WROOM RX */
#define PIN_TX       17   /* S3 RX ← WROOM TX */
#define UART_BUF     1024

/* ---- Helpers -------------------------------------------------------- */
static void send_line(const char *msg)
{
    uart_write_bytes(UART_PORT, msg, strlen(msg));
    uart_write_bytes(UART_PORT, "\n", 1);
}

static bool read_line(char *buf, size_t len, uint32_t timeout_ms)
{
    size_t pos = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline && pos < len - 1) {
        uint8_t ch;
        int n = uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(10));
        if (n > 0) {
            if (ch == '\n') {
                buf[pos] = '\0';
                return pos > 0;
            }
            if (ch != '\r') buf[pos++] = (char)ch;
        }
    }
    buf[pos] = '\0';
    return false;
}

/* ---- Status sender -------------------------------------------------- */
/* Runs during an active attack, periodically pushing STATUS lines to S3 */
static volatile bool s_attack_active = false;

static void status_tx_task(void *pv)
{
    char line[PROTO_MAX_LINE];
    while (s_attack_active) {
        snprintf(line, sizeof(line), "STATUS:%lu,%lu",
                 (unsigned long)wifi_ops_get_packets_sent(),
                 (unsigned long)wifi_ops_get_elapsed_sec());
        send_line(line);
        vTaskDelay(pdMS_TO_TICKS(PROTO_STATUS_INTERVAL_MS));
    }
    vTaskDelete(NULL);
}

/* ---- Command handler ------------------------------------------------ */
static void handle_command(const char *cmd)
{
    ESP_LOGI(TAG, "cmd: %s", cmd);

    /* PING */
    if (strcmp(cmd, CMD_PING) == 0) {
        send_line(RESP_PONG);
        return;
    }

    /* STOP */
    if (strcmp(cmd, CMD_STOP) == 0) {
        s_attack_active = false;
        vTaskDelay(pdMS_TO_TICKS(100));
        wifi_ops_stop();
        send_line(RESP_ACK);
        return;
    }

    /* SCAN */
    if (strcmp(cmd, CMD_SCAN) == 0) {
        wroom_scan_result_t results = {0};
        if (wifi_ops_scan(&results) != ESP_OK) {
            send_line("ERR:scan_failed");
            return;
        }

        char line[PROTO_MAX_LINE];
        snprintf(line, sizeof(line), "SCAN_DONE:%d", results.count);
        send_line(line);

        for (int i = 0; i < results.count; i++) {
            wroom_ap_t *ap = &results.aps[i];
            snprintf(line, sizeof(line),
                     "AP:%s,%02X%02X%02X%02X%02X%02X,%d,%d",
                     ap->ssid,
                     ap->bssid[0], ap->bssid[1], ap->bssid[2],
                     ap->bssid[3], ap->bssid[4], ap->bssid[5],
                     ap->channel, ap->rssi);
            send_line(line);
        }
        return;
    }

    /* DEAUTH:<bssid_hex>,<channel> */
    if (strncmp(cmd, CMD_DEAUTH, strlen(CMD_DEAUTH)) == 0 &&
        cmd[strlen(CMD_DEAUTH)] == ':') {

        const char *args = cmd + strlen(CMD_DEAUTH) + 1;
        char bssid_hex[13] = {0};
        int  channel = 0;
        sscanf(args, "%12[^,],%d", bssid_hex, &channel);

        uint8_t bssid[6];
        for (int i = 0; i < 6; i++) {
            char b[3] = { bssid_hex[i*2], bssid_hex[i*2+1], '\0' };
            bssid[i] = (uint8_t)strtol(b, NULL, 16);
        }

        esp_err_t err = wifi_ops_start_deauth(bssid, (uint8_t)channel);
        if (err != ESP_OK) {
            char errmsg[32];
            snprintf(errmsg, sizeof(errmsg), "ERR:deauth_%d", err);
            send_line(errmsg);
            return;
        }

        send_line(RESP_ACK);
        s_attack_active = true;
        xTaskCreate(status_tx_task, "status_tx", 2048, NULL, 4, NULL);
        return;
    }

    /* BEACON:RANDOM / BEACON:CUSTOM */
    if (strcmp(cmd, CMD_BEACON_RANDOM) == 0 ||
        strcmp(cmd, CMD_BEACON_CUSTOM) == 0) {

        bool random_mode = (strcmp(cmd, CMD_BEACON_RANDOM) == 0);
        esp_err_t err = wifi_ops_start_beacon_flood(random_mode);
        if (err != ESP_OK) {
            send_line("ERR:beacon_failed");
            return;
        }

        send_line(RESP_ACK);
        s_attack_active = true;
        xTaskCreate(status_tx_task, "status_tx", 2048, NULL, 4, NULL);
        return;
    }

    /* Unknown command */
    char errmsg[PROTO_MAX_LINE];
    snprintf(errmsg, sizeof(errmsg), "ERR:unknown_cmd");
    send_line(errmsg);
}

/* ---- Main ----------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "BitRot WROOM Radio Board booting");

    /* UART setup */
    uart_config_t cfg = {
        .baud_rate  = PROTO_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, PIN_TX, PIN_RX,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF, UART_BUF, 0, NULL, 0));

    /* Wi-Fi init */
    ESP_ERROR_CHECK(wifi_ops_init());

    ESP_LOGI(TAG, "WROOM ready, waiting for commands on UART2 (RX:16, TX:17)");

    /* Main command loop */
    char line[PROTO_MAX_LINE];
    while (1) {
        if (read_line(line, sizeof(line), 100)) {
            handle_command(line);
        }
    }
}
