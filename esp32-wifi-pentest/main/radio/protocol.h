#pragma once

/* -----------------------------------------------------------------------
 * protocol.h -- BitRot dual-board UART protocol
 *
 * Shared between the S3 (main board) and WROOM (radio board) firmware.
 * Copy this file into both projects -- it must stay identical on both sides.
 *
 * All messages are newline-terminated ASCII strings. The S3 is always
 * the commander; the WROOM is always the responder. The S3 never sends
 * two commands without waiting for ACK or SCAN_DONE first.
 *
 * UART config (both sides must match):
 *   Baud:     115200
 *   Data:     8N1
 *   S3 TX:    GPIO 17  →  WROOM RX: GPIO 16
 *   S3 RX:    GPIO 18  ←  WROOM TX: GPIO 17
 *
 * ---- Commands (S3 → WROOM) ------------------------------------------
 *
 *   PING\n
 *     Check if WROOM is alive. WROOM replies PONG\n immediately.
 *
 *   SCAN\n
 *     Active scan all channels. WROOM replies SCAN_DONE:<count>\n
 *     followed by <count> lines of AP:<ssid>,<bssid>,<ch>,<rssi>\n
 *
 *   DEAUTH:<bssid_hex>,<channel>\n
 *     Start broadcast deauth attack on target. bssid_hex = 12 hex chars,
 *     no colons (e.g. AABBCCDDEEFF). WROOM replies ACK\n then sends
 *     STATUS:<pkts>,<secs>\n every second until STOP.
 *
 *   BEACON:RANDOM\n
 *   BEACON:CUSTOM\n
 *     Start beacon flood (random SSIDs or built-in custom list).
 *     WROOM replies ACK\n then STATUS:<pkts>,<secs>\n every second.
 *
 *   STOP\n
 *     Stop whatever attack is running. WROOM replies ACK\n.
 *
 * ---- Responses (WROOM → S3) -----------------------------------------
 *
 *   PONG\n
 *   ACK\n
 *   ERR:<reason>\n
 *   SCAN_DONE:<count>\n
 *   AP:<ssid>,<bssid>,<ch>,<rssi>\n
 *   STATUS:<packets_sent>,<elapsed_sec>\n
 *
 * -------------------------------------------------------------------- */

#define PROTO_BAUD          115200
#define PROTO_MAX_LINE      128    /* max bytes in any single message */
#define PROTO_STATUS_INTERVAL_MS 1000

/* Command strings */
#define CMD_PING            "PING"
#define CMD_SCAN            "SCAN"
#define CMD_DEAUTH          "DEAUTH"
#define CMD_BEACON_RANDOM   "BEACON:RANDOM"
#define CMD_BEACON_CUSTOM   "BEACON:CUSTOM"
#define CMD_STOP            "STOP"

/* Response strings */
#define RESP_PONG           "PONG"
#define RESP_ACK            "ACK"
#define RESP_ERR            "ERR"
#define RESP_SCAN_DONE      "SCAN_DONE"
#define RESP_AP             "AP"
#define RESP_STATUS         "STATUS"
