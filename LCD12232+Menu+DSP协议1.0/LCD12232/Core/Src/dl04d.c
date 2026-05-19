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
    return _send_udp((uint8_t*)LOCAL_IP, 8999, json);
}

/* ── Receive response (polling, with DMA handoff) ─── */
static void _recv_response(void)
{
    memset(_rx_buf, 0, sizeof(_rx_buf));
    _last_ok = 0;

    /* Stop DMA to free UART RX for polling */
    HAL_UART_AbortReceive(DL04D_UART);

    uint16_t pos = 0;
    uint32_t tick = HAL_GetTick();

    while ((HAL_GetTick() - tick) < 300)
    {
        uint8_t byte;
        if (HAL_UART_Receive(DL04D_UART, &byte, 1, 10) == HAL_OK)
        {
            if (pos < DL04D_RX_BUFLEN - 1)
                _rx_buf[pos++] = byte;

            if (strstr((char*)_rx_buf, "\"result\""))
            {
                _last_ok = (strstr((char*)_rx_buf, "\"succ\"") != NULL);
                break;
            }
        }
    }

    /* Re-arm DMA idle-line receive for host commands */
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
