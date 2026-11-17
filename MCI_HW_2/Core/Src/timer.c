/* timer.c - see timer.h. CMSIS register-level only. Owner: Samee */
#include "stm32f303xc.h"
#include "clock.h"
#include "timer.h"

#define TICK_HZ 1000000UL   /* both timers count at 1 MHz (PSC = 63) */

void timer_sample_clock_init(uint32_t sample_rate_hz)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;             /* TIM6EN: clock the basic timer */

    TIM6->CR1 = 0;                                  /* CEN = 0: keep stopped while configuring */
    TIM6->PSC = (CLOCK_APB1_TIM_HZ / TICK_HZ) - 1U; /* PSC = 63: 64 MHz / 64 = 1 MHz tick */
    TIM6->ARR = (TICK_HZ / sample_rate_hz) - 1U;    /* ARR = 99 for 10 kHz update */
    TIM6->CR1 |= TIM_CR1_ARPE;                      /* ARPE: ARR is buffered */
    TIM6->CR2 = (TIM6->CR2 & ~TIM_CR2_MMS)
              | TIM_CR2_MMS_1;                      /* MMS = 010: update event -> TRGO */
    TIM6->EGR = TIM_EGR_UG;                         /* UG: load PSC/ARR into shadow registers now */
    TIM6->SR  = 0;                                  /* UIF: clear flag set by UG */
}

void timer_sample_clock_start(void) { TIM6->CR1 |=  TIM_CR1_CEN; }  /* CEN = 1: count */
void timer_sample_clock_stop(void)  { TIM6->CR1 &= ~TIM_CR1_CEN; }  /* CEN = 0: stop  */

void timer_pwm_test_init(uint32_t freq_hz, uint32_t duty_percent)
{
    uint32_t period = TICK_HZ / freq_hz;            /* ticks per PWM period */

    RCC->AHBENR  |= RCC_AHBENR_GPIOAEN;             /* GPIOAEN: clock port A */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;             /* TIM3EN: clock TIM3 */

    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER6)
                 | GPIO_MODER_MODER6_1;             /* MODER6 = 10: PA6 alternate function */
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~GPIO_AFRL_AFRL6)
                  | (2UL << GPIO_AFRL_AFRL6_Pos);   /* AFRL6 = AF2: PA6 = TIM3_CH1 */

    TIM3->CR1  = 0;                                 /* CEN = 0 while configuring */
    TIM3->PSC  = (CLOCK_APB1_TIM_HZ / TICK_HZ) - 1U;/* PSC = 63: 1 MHz tick */
    TIM3->ARR  = period - 1U;                       /* ARR: PWM period */
    TIM3->CCR1 = (period * duty_percent) / 100U;    /* CCR1: high time */
    TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_OC1M | TIM_CCMR1_CC1S))
                | (6UL << TIM_CCMR1_OC1M_Pos)       /* OC1M = 0110: PWM mode 1 */
                | TIM_CCMR1_OC1PE;                  /* OC1PE: CCR1 preload */
    TIM3->CCER |= TIM_CCER_CC1E;                    /* CC1E: drive output pin */
    TIM3->CR1  |= TIM_CR1_ARPE;                     /* ARPE: ARR preload */
    TIM3->EGR   = TIM_EGR_UG;                       /* UG: load preloaded values */
    TIM3->CR1  |= TIM_CR1_CEN;                      /* CEN = 1: start */
}
