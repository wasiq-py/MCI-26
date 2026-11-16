/* clock.c - TEMPORARY: makes the chip run at 64 MHz (Samee's version replaces this).
 *
 * By default the chip runs on a slow 8 MHz internal clock (called HSI).
 * We use the PLL (a clock multiplier) to speed it up:
 *     HSI 8 MHz  ->  divided by 2 = 4 MHz  ->  PLL multiplies by 16  ->  64 MHz
 */
#include "clock.h"
#include "stm32f303xc.h"

void clock_init(void)
{
    /* STEP 1: Make sure the internal 8 MHz clock is running, then wait until it is ready. */
    RCC->CR |= RCC_CR_HSION;                       /* HSION bit = 1 -> turn HSI on */
    while ((RCC->CR & RCC_CR_HSIRDY) == 0) { }     /* wait here until HSIRDY bit = 1 */

    /* STEP 2: Tell the flash memory to be slower than the CPU.
       At 64 MHz the flash needs 2 "wait states", otherwise the chip crashes. */
    FLASH->ACR &= ~FLASH_ACR_LATENCY;              /* first clear the old wait-state setting */
    FLASH->ACR |= FLASH_ACR_LATENCY_1;             /* LATENCY = 2 wait states */
    FLASH->ACR |= FLASH_ACR_PRFTBE;                /* PRFTBE = 1 -> prefetch on (a bit faster) */

    /* STEP 3: Choose the clock settings (the PLL is still off, so this is safe). */
    RCC->CFGR &= ~RCC_CFGR_PLLSRC;                 /* PLLSRC = 0 -> PLL input is HSI divided by 2 */
    RCC->CFGR &= ~RCC_CFGR_PLLMUL;                 /* clear the old multiplier */
    RCC->CFGR |= RCC_CFGR_PLLMUL16;                /* PLLMUL = x16 -> 4 MHz x 16 = 64 MHz */
    RCC->CFGR &= ~RCC_CFGR_HPRE;                   /* HPRE = 0 -> CPU/AHB bus not divided (64 MHz) */
    RCC->CFGR &= ~RCC_CFGR_PPRE2;                  /* PPRE2 = 0 -> APB2 bus not divided (64 MHz) <- USART1 uses this */
    RCC->CFGR &= ~RCC_CFGR_PPRE1;                  /* clear APB1 divider first... */
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;              /* ...then APB1 divided by 2 = 32 MHz (its limit is 36 MHz) */

    /* STEP 4: Turn the PLL on and wait until it is stable. */
    RCC->CR |= RCC_CR_PLLON;                       /* PLLON bit = 1 -> start the PLL */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) { }     /* wait here until PLLRDY bit = 1 */

    /* STEP 5: Switch the whole chip over to the PLL clock. */
    RCC->CFGR &= ~RCC_CFGR_SW;                     /* clear the old clock choice */
    RCC->CFGR |= RCC_CFGR_SW_PLL;                  /* SW = PLL -> use the PLL as the system clock */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }   /* wait until the chip confirms it switched */
}
