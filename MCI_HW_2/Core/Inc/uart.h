/* uart.h - USART1 driver interface. Owner: Wasiq (Part B), code in uart.c
 *
 * Hardware: USART1 TX = PC4, AF7. On the Discovery board PC4/PC5 go to the
 * ST-LINK, which shows up on the PC as a virtual COM port (no extra wires).
 * Kernel clock = PCLK2 = 64 MHz (see clock.h). For 230400 baud:
 * BRR = 64e6 / 230400 = 277.8 -> 278, actual 230216 baud, error 0.08%.
 *
 * main.c only relies on these two functions.
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baud);
void uart_write(const uint8_t *data, uint32_t len);   /* blocks (polls TXE) until all bytes are handed over */

#endif
