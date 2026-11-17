/* protocol.h - SHARED CONTRACT between the board (Samee + Wasiq) and the PC plotter (Wasiq)
 *
 * Every 12-bit sample s (0..4095) is sent as 2 bytes, 8N1:
 *   byte 0 = 1 0 s11 s10 s9 s8 s7 s6     (0x80 | (s >> 6))   -> always 0x80..0xBF
 *   byte 1 = 0 0 s5  s4  s3 s2 s1 s0     (s & 0x3F)          -> always 0x00..0x3F
 * Bit 7 marks the first byte, so after a disconnect the PC skips bytes until bit 7 = 1.
 * PC decode: s = ((b0 & 0x3F) << 6) | (b1 & 0x3F)
 *
 * Status frames (once per second):
 *   0xC0, hi7, lo7  -> samples taken in the last second (expect 10000) = (hi7 << 7) | lo7
 *   0xC1, hi7, lo7  -> total lost samples so far, clipped to 16383
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTO_SAMPLE_FLAG   0x80U
#define PROTO_STATUS_RATE   0xC0U
#define PROTO_STATUS_LOST   0xC1U

static void proto_encode_sample(uint16_t s, uint8_t out[2])
{
    out[0] = (uint8_t)(PROTO_SAMPLE_FLAG | ((s >> 6) & 0x3FU));
    out[1] = (uint8_t)(s & 0x3FU);
}

static void proto_encode_status(uint8_t type, uint32_t value, uint8_t out[3])
{
    if (value > 0x3FFFU) value = 0x3FFFU;
    out[0] = type;
    out[1] = (uint8_t)((value >> 7) & 0x7FU);
    out[2] = (uint8_t)(value & 0x7FU);
}

#endif
