/* clock.h - system clock (64 MHz from HSI) and 1 ms SysTick time base
 * Owner: Samee (Part A)
 *
 * Clock tree after clock_init():
 *   SYSCLK = HCLK = 64 MHz   (HSI 8 MHz / 2 * 16 via PLL)
 *   PCLK1  = 32 MHz  -> APB1 timer clock (TIM2/3/6) = 64 MHz (x2 because APB1 prescaler != 1)
 *   PCLK2  = 64 MHz  -> USART1 kernel clock = 64 MHz (RCC_CFGR3.USART1SW reset value = PCLK)
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#define CLOCK_SYSCLK_HZ      64000000UL
#define CLOCK_APB1_TIM_HZ    64000000UL
#define CLOCK_APB2_HZ        64000000UL

void     clock_init(void);                          /* switch SYSCLK to 64 MHz PLL, start 1 ms SysTick */
uint32_t clock_millis(void);                        /* ms since clock_init() */
void     clock_set_second_callback(void (*cb)(void)); /* called from SysTick ISR once per second */

#endif
