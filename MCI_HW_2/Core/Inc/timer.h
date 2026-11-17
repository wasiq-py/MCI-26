/* timer.h - TIM6 sample clock (TRGO -> ADC) and TIM3 PWM test source
 * Owner: Samee (Part A)
 *
 * Sample clock math (TIM6 on APB1, timer clock f_tim = 64 MHz):
 *   f_TRGO = f_tim / ((PSC + 1) * (ARR + 1))
 *   PSC = 63  -> counter tick = 64 MHz / 64 = 1 MHz
 *   ARR = 99  -> update rate  = 1 MHz / 100  = 10 kHz exactly
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_sample_clock_init(uint32_t sample_rate_hz); /* TIM6, TRGO on update; not started */
void timer_sample_clock_start(void);
void timer_sample_clock_stop(void);

/* Self-test signal: 0-3.3 V square wave on PA6 (TIM3_CH1). Jumper PA6 -> PA0 to test
 * the scope without a function generator. freq_hz 16..1000000, duty 0..100 %. */
void timer_pwm_test_init(uint32_t freq_hz, uint32_t duty_percent);

#endif
