/* clock.c - see clock.h. CMSIS register-level only. Owner: Samee */
#include "stm32f303xc.h"
#include "clock.h"

static volatile uint32_t s_ms;
static volatile uint32_t s_ms_in_second;
static void (*volatile s_second_cb)(void);

void clock_init(void)
{
    /* Flash needs 2 wait states for 48 MHz < HCLK <= 72 MHz; prefetch on for speed */
    FLASH->ACR = FLASH_ACR_PRFTBE                    /* PRFTBE: prefetch buffer enable */
               | FLASH_ACR_LATENCY_1;                /* LATENCY = 010: two wait states */

    RCC->CR |= RCC_CR_HSION;                         /* HSION: make sure 8 MHz HSI is running */
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }           /* HSIRDY: wait until HSI stable */

    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSI_DIV2            /* PLLSRC = 0: PLL input = HSI/2 = 4 MHz */
               | RCC_CFGR_PLLMUL16                   /* PLLMUL = 1110: x16 -> 64 MHz */
               | RCC_CFGR_HPRE_DIV1                  /* HPRE  = 0xxx: AHB  = SYSCLK/1 = 64 MHz */
               | RCC_CFGR_PPRE1_DIV2                 /* PPRE1 = 100 : APB1 = 32 MHz (max 36 MHz) */
               | RCC_CFGR_PPRE2_DIV1;                /* PPRE2 = 0xx : APB2 = 64 MHz */

    RCC->CR |= RCC_CR_PLLON;                         /* PLLON: start the PLL */
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }           /* PLLRDY: wait for lock */

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;      /* SW = 10: PLL as SYSCLK */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }     /* SWS = 10: switch done */

    SystemCoreClock = CLOCK_SYSCLK_HZ;               /* keep CMSIS global in sync */

    /* SysTick: 1 kHz tick from HCLK (CMSIS core helper programs LOAD/VAL/CTRL) */
    SysTick_Config(CLOCK_SYSCLK_HZ / 1000U);
    NVIC_SetPriority(SysTick_IRQn, 2);               /* lower priority than the ADC ISR (0) */
}

uint32_t clock_millis(void) { return s_ms; }

void clock_set_second_callback(void (*cb)(void)) { s_second_cb = cb; }

void SysTick_Handler(void)
{
    s_ms++;
    if (++s_ms_in_second >= 1000U) {
        s_ms_in_second = 0;
        if (s_second_cb) s_second_cb();
    }
}
