/* dl04d.c - DIGISYN-AES67 Serial Port Control Protocol
 *
 * Serial frame format (binary mode / UDP tunnel):
 *   [Header 4B: 0x55,0x55,0x55,0x5A]
 *   [Type   1B: 0x31 = UDP]
 *   [Length 2B: LL (big-endian)]    LL = IP(4) + Port(2) + JSON(N) = 6 + N
 *   [IP     4B]
 *   [Port   2B: big-endian]
 *   [JSON   N bytes]
 *
 * Text mode (internal):
 *   [Header 4B][Type 1B: 0x30][JSON][\n]
 */
#include "dl04d.h"

/* ── Internal state ─────────────────────────────── */
static uint8_t  _rx_buf[DL04D_RX_BUFLEN];
static uint8_t  _last_ok = 0;

/* DMA receive buffer (defined in main.c) */
extern uint8_t g_rxBufferDMA[256];
#define DMA_RX_BUF_SIZE 256

/* ── Frame header ───────────────────────────────── */
static const uint8_t FRAME_HEADER[4] = {0x55, 0x55, 0x55, 0x5A};

/* ── Send internal text command ──────────────────── */
static HAL_StatusTypeDef _send_internal(const char *json)
{
    HAL_StatusTypeDef ret;
    ret = HAL_UART_Transmit(DL04D_UART, (uint8_t*)FRAME_HEADER, 4, 100);
    if (ret != HAL_OK) return ret;
    uint8_t type = TYPE_INTERNAL;
    ret = HAL_UART_Transmit(DL04D_UART, &type, 1, 100);
    if (ret != HAL_OK) return ret;
    ret = HAL_UART_Transmit(DL04D_UART, (uint8_t*)json, strlen(json), 200);
    if (ret != HAL_OK) return ret;
    uint8_t term = '\n';
    return HAL_UART_Transmit(DL04D_UART, &term, 1, 100);
}

/* ── Send UDP tunnel command (binary mode) ────────── */
static HAL_StatusTypeDef _send_udp(uint8_t ip[4], uint16_t port,
                                   const char *json)
{
    uint16_t json_len = (uint16_t)strlen(json);
    /* LL = IP(4) + Port(2) + JSON(N) */
    uint16_t LL = 6 + json_len;
    uint8_t  len_be[2] = {(LL >> 8) & 0xFF, LL & 0xFF};
    uint8_t  port_be[2] = {(port >> 8) & 0xFF, port & 0xFF};
    HAL_StatusTypeDef ret;

    ret = HAL_UART_Transmit(DL04D_UART, (uint8_t*)FRAME_HEADER, 4, 100);
    if (ret != HAL_OK) return ret;
    uint8_t type = TYPE_UDP;
    ret = HAL_UART_Transmit(DL04D_UART, &type, 1, 100);
    if (ret != HAL_OK) return ret;
    ret = HAL_UART_Transmit(DL04D_UART, len_be, 2, 100);
    if (ret != HAL_OK) return ret;
    ret = HAL_UART_Transmit(DL04D_UART, ip, 4, 100);
    if (ret != HAL_OK) return ret;
    ret = HAL_UART_Transmit(DL04D_UART, port_be, 2, 100);
    if (ret != HAL_OK) return ret;
    return HAL_UART_Transmit(DL04D_UART, (uint8_t*)json, json_len, 300);
}

/* ── Convenience: send UDP to 127.0.0.1:8999 ────── */
static const uint8_t LOCAL_IP[4] = {127, 0, 0, 1};

static HAL_StatusTypeDef _send_to_dsp(const char *json)
{
    /* Flush RX before sending: abort DMA + drain shift register,
     * so the DSP's response arrives while we are polling, not into DMA. */
    HAL_UART_AbortReceive(DL04D_UART);
    {
        uint8_t dummy;
        while (HAL_UART_Receive(DL04D_UART, &dummy, 1, 1) == HAL_OK) {}
    }
    return _send_udp((uint8_t*)LOCAL_IP, 8999, json);
}

/* ── Receive response (polling) ──
 *   DMA is already aborted by _send_to_dsp; we poll and re-arm it here.
 *   Uses memchr to skip binary frame header (avoids 0x00 breaking strstr). */
static void _recv_response(void)
{
    memset(_rx_buf, 0, sizeof(_rx_buf));
    _last_ok = 0;

    uint16_t pos = 0;
    uint32_t tick = HAL_GetTick();

    while ((HAL_GetTick() - tick) < 300)
    {
        uint8_t byte;
        if (HAL_UART_Receive(DL04D_UART, &byte, 1, 10) == HAL_OK)
        {
            if (pos < DL04D_RX_BUFLEN - 1)
                _rx_buf[pos++] = byte;

            /* Search past binary header: find '{' first, then look for "succ" */
            char *json = memchr(_rx_buf, '{', pos);
            if (json) {
                if (strstr(json, "\"result\"")) {
                    _last_ok = (strstr(json, "\"succ\"") != NULL);
                    break;
                }
            }
        }
    }

    /* Re-arm DMA idle-line receive for next command */
    HAL_UARTEx_ReceiveToIdle_DMA(DL04D_UART, g_rxBufferDMA, DMA_RX_BUF_SIZE);
}

/* ================================================================
   Public API
   ================================================================ */

/** Set mute for all 4 inputs + 4 outputs simultaneously
 *  mute: 1=mute  0=unmute
 */
HAL_StatusTypeDef DL04D_SendMuteAll(uint8_t mute)
{
    char v = mute ? '1' : '0';
    char json[256];

    snprintf(json, sizeof(json),
        "{\"ops\":\"setMute\","
         "\"inArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "],"
         "\"outArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "]}",
        v,v,v,v, v,v,v,v);

    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Set mute for single input channel (ch: 0~3) */
HAL_StatusTypeDef DL04D_SendMuteIn(uint8_t ch, uint8_t mute)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"ops\":\"setMute\","
         "\"inArr\":[{\"index\":\"%d\",\"val\":\"%d\"}],"
         "\"outArr\":[]}",
        ch, mute ? 1 : 0);

    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Set mute for single output channel (ch: 0~3) */
HAL_StatusTypeDef DL04D_SendMuteOut(uint8_t ch, uint8_t mute)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"ops\":\"setMute\","
         "\"inArr\":[],"
         "\"outArr\":[{\"index\":\"%d\",\"val\":\"%d\"}]}",
        ch, mute ? 1 : 0);

    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Get current mute status for all channels */
HAL_StatusTypeDef DL04D_GetMute(void)
{
    const char *json = "{\"ops\":\"getMute\"}";
    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Return whether last command returned "succ" */
uint8_t DL04D_LastResultOK(void)
{
    return _last_ok;
}

/* ── Physical channel mute (setPhyMuteRx / setPhyMuteTx) ── */

/** Set physical output channel mute */
HAL_StatusTypeDef DL04D_SetPhyOutputMute(uint8_t ch, uint8_t mute)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"1\",\"ops\":\"setPhyMuteTx\","
        "\"id\":\"%d\",\"val\":\"%d\"}", ch, mute ? 1 : 0);
    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Set physical input channel mute */
HAL_StatusTypeDef DL04D_SetPhyInputMute(uint8_t ch, uint8_t mute)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"1\",\"ops\":\"setPhyMuteRx\","
        "\"id\":\"%d\",\"val\":\"%d\"}", ch, mute ? 1 : 0);
    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}

/** Set all 4 input channel gains to the same dB value
 *  db: gain in dB, range -60 ~ +12
 *  Note: fire-and-forget (no response wait) for smooth encoder rotation
 */
HAL_StatusTypeDef DL04D_SetAllInputGain(int8_t db)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"1\",\"ops\":\"setGainDb\","
         "\"inArr\":["
           "{\"index\":\"0\",\"val\":\"%d\"},"
           "{\"index\":\"1\",\"val\":\"%d\"},"
           "{\"index\":\"2\",\"val\":\"%d\"},"
           "{\"index\":\"3\",\"val\":\"%d\"}"
         "],"
         "\"outArr\":[]}",
        db, db, db, db);
    return _send_to_dsp(json);
}

/* ── Output gain (fire-and-forget for smooth encoder) ── */
HAL_StatusTypeDef DL04D_SetAllOutputGain(int8_t db)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"1\",\"ops\":\"setGainDb\","
         "\"inArr\":[],"
         "\"outArr\":["
           "{\"index\":\"0\",\"val\":\"%d\"},"
           "{\"index\":\"1\",\"val\":\"%d\"},"
           "{\"index\":\"2\",\"val\":\"%d\"},"
           "{\"index\":\"3\",\"val\":\"%d\"}"
         "]}",
        db, db, db, db);
    return _send_to_dsp(json);
}

/* ── All-input mute ── */
HAL_StatusTypeDef DL04D_SetAllInputMute(uint8_t mute)
{
    char v = mute ? '1' : '0';
    char json[256];
    snprintf(json, sizeof(json),
        "{\"ops\":\"setMute\","
         "\"inArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "],"
         "\"outArr\":[]}",
        v, v, v, v);
    return _send_to_dsp(json);
}

/* ── All-output mute ── */
HAL_StatusTypeDef DL04D_SetAllOutputMute(uint8_t mute)
{
    char v = mute ? '1' : '0';
    char json[256];
    snprintf(json, sizeof(json),
        "{\"ops\":\"setMute\","
         "\"inArr\":[],"
         "\"outArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "]}",
        v, v, v, v);
    return _send_to_dsp(json);
}

/* ── All-input inverse ──
 *   inv=1: cookie="71" 全部反相
 *   inv=0: cookie="72" 全部正相  */
HAL_StatusTypeDef DL04D_SetAllInputInverse(uint8_t inv)
{
    char v = inv ? '1' : '0';
    const char *cookie = inv ? "71" : "72";
    char json[256];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"%s\",\"ops\":\"setInverse\","
         "\"inArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "],"
         "\"outArr\":[]}",
        cookie, v, v, v, v);
    return _send_to_dsp(json);
}

/* ── All-output inverse ──
 *   inv=1: cookie="77" 全部反相
 *   inv=0: cookie="78" 全部正相  */
HAL_StatusTypeDef DL04D_SetAllOutputInverse(uint8_t inv)
{
    char v = inv ? '1' : '0';
    const char *cookie = inv ? "77" : "78";
    char json[256];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"%s\",\"ops\":\"setInverse\","
         "\"inArr\":[],"
         "\"outArr\":["
           "{\"index\":\"0\",\"val\":\"%c\"},"
           "{\"index\":\"1\",\"val\":\"%c\"},"
           "{\"index\":\"2\",\"val\":\"%c\"},"
           "{\"index\":\"3\",\"val\":\"%c\"}"
         "]}",
        cookie, v, v, v, v);
    return _send_to_dsp(json);
}

/* ── Internal communication ──────────────────────── */

/** Query device IP (internal text mode) */
void DL04D_GetIP(void)
{
    _send_internal("{\"ops\":\"getIp\"}");
}

/* ══════════════════════════════════════════════════════════════
   Preset Save / Load
   ══════════════════════════════════════════════════════════════ */

static char s_preset_list[PRESET_MAX][PRESET_NAME_LEN];
static uint8_t s_preset_count = 0;

/** ① Get preset name list from DL-04D (getConfigNameArr)
 *  Uses TEXT MODE (type 0x30) like _send_internal — the cookie:"0" config
 *  commands talk to the DSP's internal command parser, not the UDP tunnel. */
uint8_t DL04D_GetPresetList(void)
{
    const char *json = "{\"cookie\":\"0\",\"ops\":\"getConfigNameArr\"}";

    /* 1. Flush RX: abort DMA + drain shift register before sending */
    HAL_UART_AbortReceive(DL04D_UART);
    memset(_rx_buf, 0, sizeof(_rx_buf));
    _last_ok = 0;
    {
        uint8_t dummy;
        while (HAL_UART_Receive(DL04D_UART, &dummy, 1, 1) == HAL_OK) {}
    }

    /* 2. Send via TEXT MODE (type 0x30) — same as _send_internal */
    HAL_UART_Transmit(DL04D_UART, (uint8_t*)FRAME_HEADER, 4, 100);
    uint8_t type = TYPE_INTERNAL;  /* 0x30 */
    HAL_UART_Transmit(DL04D_UART, &type, 1, 100);
    HAL_UART_Transmit(DL04D_UART, (uint8_t*)json, strlen(json), 200);
    uint8_t term = '\n';
    HAL_UART_Transmit(DL04D_UART, &term, 1, 100);

    /* 3. Poll for text-mode response (no binary header → strstr is safe) */
    uint16_t pos = 0;
    uint8_t  hasResult = 0;
    uint32_t tick = HAL_GetTick();

    while ((HAL_GetTick() - tick) < 500)
    {
        uint8_t byte;
        if (HAL_UART_Receive(DL04D_UART, &byte, 1, 10) == HAL_OK)
        {
            if (pos < DL04D_RX_BUFLEN - 1)
                _rx_buf[pos++] = byte;

            if (!hasResult && strstr((char*)_rx_buf, "\"result\"")) {
                hasResult = 1;
                _last_ok = (strstr((char*)_rx_buf, "\"succ\"") != NULL);
            }
            if (hasResult && strchr((char*)_rx_buf, ']')) {
                break;
            }
        }
    }

    /* 4. Re-arm DMA idle-line receive */
    HAL_UARTEx_ReceiveToIdle_DMA(DL04D_UART, g_rxBufferDMA, DMA_RX_BUF_SIZE);

    /* 5. Parse nameArr — text mode: find JSON start after header */
    s_preset_count = 0;
    memset(s_preset_list, 0, sizeof(s_preset_list));

    char *p = strstr((char*)_rx_buf, "\"nameArr\"");
    if (!p) return 0;

    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    while (*p && *p != ']' && s_preset_count < PRESET_MAX)
    {
        char *start = strchr(p, '"');
        if (!start) break;
        start++;

        char *end = strchr(start, '"');
        if (!end) break;

        uint8_t len = end - start;
        if (len == 0) { p = end + 1; continue; }
        if (len >= PRESET_NAME_LEN) len = PRESET_NAME_LEN - 1;
        strncpy(s_preset_list[s_preset_count], start, len);
        s_preset_list[s_preset_count][len] = '\0';
        s_preset_count++;

        p = end + 1;
    }

    return s_preset_count;
}

/** ② Check whether a preset name already exists in the cached list */
static uint8_t _PresetExists(const char *name)
{
    for (uint8_t i = 0; i < s_preset_count; i++) {
        if (strcmp(s_preset_list[i], name) == 0)
            return 1;
    }
    return 0;
}

/** ③ Delete a preset by name */
uint8_t DL04D_DeletePreset(const char *name)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"0\",\"ops\":\"deleteFile\",\"name\":\"%s\"}",
        name);
    _send_to_dsp(json);
    _recv_response();
    return DL04D_LastResultOK();
}

/** ④ Save current DSP settings as a named preset */
uint8_t DL04D_SavePreset(const char *name)
{
    DL04D_GetPresetList();

    if (_PresetExists(name)) {
        if (!DL04D_DeletePreset(name)) {
            return 0;
        }
        HAL_Delay(50);
    }

    char json[128];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"0\",\"ops\":\"saveToFile\",\"name\":\"%s\"}",
        name);
    _send_to_dsp(json);
    _recv_response();

    return DL04D_LastResultOK();
}

/** ⑤ Load a named preset from DL-04D and refresh local state */
uint8_t DL04D_LoadPreset(const char *name)
{
    DL04D_GetPresetList();

    if (!_PresetExists(name)) {
        return 0;
    }

    char json[128];
    snprintf(json, sizeof(json),
        "{\"cookie\":\"0\",\"ops\":\"loadFromFile\",\"name\":\"%s\"}",
        name);
    _send_to_dsp(json);
    _recv_response();

    if (DL04D_LastResultOK()) {
        HAL_Delay(200);
        DL04D_GetMute();
        DL04D_GetGain();
    }

    return DL04D_LastResultOK();
}

/** ⑥ Get cached preset count (for LCD menu display) */
uint8_t DL04D_GetPresetCount(void)
{
    return s_preset_count;
}

/** ⑦ Get cached preset name by index */
const char* DL04D_GetPresetName(uint8_t index)
{
    if (index >= s_preset_count) return "";
    return s_preset_list[index];
}

/** ⑧ Query current gain from DSP (refresh after loading preset) */
HAL_StatusTypeDef DL04D_GetGain(void)
{
    const char *json = "{\"cookie\":\"1\",\"ops\":\"getGainDb\"}";
    HAL_StatusTypeDef ret = _send_to_dsp(json);
    _recv_response();
    return ret;
}
