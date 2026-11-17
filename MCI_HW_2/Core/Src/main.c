/* main.c - HW2 real-time oscilloscope, STM32F3 Discovery, CMSIS register level only.
 *
 * Data path: TIM6 TRGO (10 kHz) -> ADC1 PA0 -> EOC ISR -> ring buffer -> main loop -> USART1 -> PC
 * Status:    green LED (PE15) blinks once per second while exactly 10,000 samples/s are taken
 *            and nothing is lost; red LED (PE9) latches on at the first missed/dropped sample.
 * Part A task 4: analyze() measures Vpp and frequency on the board; read them in Live Watch.
 */
#include "stm32f303xc.h"
#include "clock.h"
#include "timer.h"
#include "adc.h"
#include "uart.h"
#include "led.h"
#include "protocol.h"

#define SAMPLE_RATE_HZ   10000U
#define UART_BAUD        230400U
#define WINDOW           1000U      /* analyze 1000 samples = 0.1 s at a time */

/* Visible in the debugger (Live Watch) */
volatile uint32_t samples_last_second;
volatile uint32_t lost_samples;
volatile uint16_t last_sample;
volatile uint32_t g_vpp_mV;         /* peak-to-peak voltage in mV */
volatile uint32_t g_freq_dHz;       /* frequency in 0.1 Hz (5003 = 500.3 Hz) */

static volatile uint32_t s_second_flag;
static uint32_t s_prev_total;

/* Runs in the SysTick ISR exactly every 1000 ms */
static void on_second(void)
{
    uint32_t total = adc_total_samples();
    samples_last_second = total - s_prev_total;
    s_prev_total = total;
    s_second_flag = 1;
}

/* Collects WINDOW samples, then works out Vpp and frequency.
 * Vpp:  (max - min) * 3300 / 4095.
 * Freq: count upward crossings of the midpoint; f = (crossings - 1) / (time first->last crossing).
 *       Each sample is 0.1 ms apart, so time is measured in samples. */
static void analyze(uint16_t s)
{
    static uint16_t buf[WINDOW];
    static uint32_t n;

    buf[n++] = s;
    if (n < WINDOW) return;
    n = 0;

    uint16_t min = 4095, max = 0;
    for (uint32_t i = 0; i < WINDOW; i++) {
        if (buf[i] < min) min = buf[i];
        if (buf[i] > max) max = buf[i];
    }
    g_vpp_mV = ((uint32_t)(max - min) * 3300U) / 4095U;

    uint16_t mid  = (uint16_t)((min + max) / 2U);
    uint16_t hyst = (uint16_t)((max - min) / 8U);   /* ignore noise near the midpoint */
    uint32_t crossings = 0, first = 0, last = 0, high = (buf[0] > mid);

    for (uint32_t i = 1; i < WINDOW; i++) {
        if (!high && buf[i] > mid + hyst) {          /* upward crossing */
            high = 1;
            if (crossings == 0) first = i;
            last = i;
            crossings++;
        } else if (high && buf[i] < mid - hyst) {
            high = 0;
        }
    }

    if (crossings >= 2 && (uint32_t)(max - min) > 40U)
        g_freq_dHz = ((crossings - 1U) * SAMPLE_RATE_HZ * 10U) / (last - first);
    else
        g_freq_dHz = 0;                              /* flat signal, no frequency */
}

int main(void)
{
    clock_init();
    led_init();
    uart_init(UART_BAUD);
    adc_init();                                 /* armed, waits for TIM6 trigger */
    timer_sample_clock_init(SAMPLE_RATE_HZ);
    timer_pwm_test_init(1000U, 50U);            /* optional self-test: jumper PA6 -> PA0 */

    clock_set_second_callback(on_second);
    timer_sample_clock_start();                 /* sampling starts here */

    for (;;) {
        uint16_t s;
        uint8_t frame[3];

        if (adc_buffer_pop(&s)) {
            last_sample = s;
            analyze(s);
            proto_encode_sample(s, frame);
            uart_write(frame, 2);
        }

        if (s_second_flag) {
            s_second_flag = 0;
            lost_samples = adc_overruns() + adc_dropped_samples();

            proto_encode_status(PROTO_STATUS_RATE, samples_last_second, frame);
            uart_write(frame, 3);
            proto_encode_status(PROTO_STATUS_LOST, lost_samples, frame);
            uart_write(frame, 3);

            if (samples_last_second == SAMPLE_RATE_HZ && lost_samples == 0U) {
                led_toggle(LED_GREEN);
            } else {
                led_off(LED_GREEN);
            }
            if (lost_samples != 0U) led_on(LED_RED);
        }
    }
}
