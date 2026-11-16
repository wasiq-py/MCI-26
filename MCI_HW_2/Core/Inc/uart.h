#ifndef UART_H
#define UART_H

#include <stdint.h>

/* USART1 TX on PC4 (AF7) -> ST-LINK virtual COM port. 8N1, polling TX. */
void uart_init(uint32_t baud);                        /* e.g. uart_init(230400) */
void uart_write(const uint8_t *data, uint32_t len);   /* blocking send */

#endif
