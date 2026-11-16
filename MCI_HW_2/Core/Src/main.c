/* main.c - BRING-UP TEST (temporary).
 *
 * Goal: check that the UART works, before the real ADC code exists.
 * It sends a fake signal (a ramp that counts 0, 1, 2 ... 4095, then starts again)
 * in the same format the real oscilloscope will use. Look at it on the PC (COM port).
 *
 * FORMAT ON THE WIRE
 *   A sample is a 12-bit number (0..4095) sent as 2 bytes:
 *       byte 1 = 1 + the upper 6 bits   (starts with a 1, so the PC can find the start)
 *       byte 2 = 0 + the lower 6 bits   (starts with a 0)
 *   A status report is 3 bytes: a type (0xC0 or 0xC1), then the value in two 7-bit pieces.
 */
#include "stm32f303xc.h"
#include "clock.h"
#include "uart.h"

/* Sends one sample (a number from 0 to 4095). */
static void send_sample(uint16_t sample)
{
    uint8_t bytes[2];

    /* sample >> 6 means "throw away the lower 6 bits", which leaves the upper 6 bits.
       0x80 is binary 10000000, so OR-ing it sets the first bit to 1. */
    bytes[0] = 0x80 | (sample >> 6);

    /* sample & 0x3F means "keep only the lower 6 bits" (0x3F = binary 00111111). */
    bytes[1] = sample & 0x3F;

    uart_write(bytes, 2);
}

/* Sends one status report. type = 0xC0 (samples counted) or 0xC1 (samples lost). */
static void send_status(uint8_t type, uint16_t value)
{
    uint8_t bytes[3];

    bytes[0] = type;
    bytes[1] = (value >> 7) & 0x7F;      /* upper part of the value (7 bits) */
    bytes[2] = value & 0x7F;             /* lower part of the value (7 bits) */

    uart_write(bytes, 3);
}

int main(void)
{
    uint16_t sample = 0;                 /* the fake signal value, goes 0..4095 */
    uint32_t count = 0;                  /* how many samples we have sent so far */

    clock_init();                        /* run the chip at 64 MHz */
    uart_init(230400);                   /* start the UART at 230400 baud */

    while (1) {
        send_sample(sample);             /* send the current value */

        sample = sample + 1;             /* next value of the ramp */
        if (sample > 4095) {
            sample = 0;                  /* after 4095, start again from 0 */
        }

        count = count + 1;
        if (count == 10000) {            /* every 10,000 samples (stands in for "once a second") */
            send_status(0xC0, 10000);    /* report: 10000 samples counted */
            send_status(0xC1, 0);        /* report: 0 samples lost */
            count = 0;
        }
    }
}
