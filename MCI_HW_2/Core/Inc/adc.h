/* adc.h - ADC1 on PA0 (ADC1_IN1), 12-bit, triggered by TIM6_TRGO, EOC interrupt -> ring buffer
 * Owner: Samee (Part A)
 *
 * SHARED INTERFACE (used by Wasiq's UART side through main.c):
 *   adc_buffer_pop(&s)  -> 1 if a sample was available, 0 if buffer empty.
 *   Samples are raw 12-bit codes 0..4095, V = code * 3.3 / 4095.
 *
 * Buffer size justification:
 *   Producer: ADC ISR at 10,000 samples/s. Consumer: main loop -> UART at 230400 baud
 *   = 23,040 bytes/s = 11,520 samples/s (2 bytes each), i.e. ~15% faster than we produce,
 *   so the buffer only has to absorb jitter/stalls in the main loop, not a steady backlog.
 *   1024 samples = 102.4 ms of signal (~10 periods at 100 Hz, ~100 at 1 kHz) and costs only
 *   2 KB of the 40 KB SRAM. Power of two -> index wrap is a cheap AND mask.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_BUFFER_SIZE  1024U          /* must be a power of two */

void     adc_init(void);                /* PA0 analog, calibrate, enable, arm for TIM6_TRGO */
uint32_t adc_buffer_pop(uint16_t *out); /* main-loop side of the ring buffer */
uint32_t adc_buffer_level(void);        /* samples currently waiting */

/* Diagnostics for the "exactly 10,000 samples per second" proof */
uint32_t adc_total_samples(void);       /* EOC interrupts since start */
uint32_t adc_dropped_samples(void);     /* ring buffer was full (consumer too slow) */
uint32_t adc_overruns(void);            /* ADC OVR flag: ISR too slow, a conversion was lost */

#endif
