#ifndef CLOCK_H
#define CLOCK_H

/* TEMPORARY stand-in for Samee's clock driver: HSI -> PLL, 64 MHz. */
#define SYSCLK_HZ 64000000UL
#define PCLK2_HZ  64000000UL   /* APB2 /1: feeds USART1 */

void clock_init(void);

#endif
