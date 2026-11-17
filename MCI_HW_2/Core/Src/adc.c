/* adc.c - see adc.h. CMSIS register-level only. Owner: Samee */
#include "stm32f303xc.h"
#include "adc.h"

#define BUF_MASK (ADC_BUFFER_SIZE - 1U)

static volatile uint16_t s_buf[ADC_BUFFER_SIZE];
static volatile uint32_t s_head;      /* written only by ISR  */
static volatile uint32_t s_tail;      /* written only by main */
static volatile uint32_t s_total, s_dropped, s_overruns;

static void delay_cycles(volatile uint32_t n) { while (n--) { } }

void adc_init(void)
{
    /* --- clocks and pin --- */
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN               /* GPIOAEN: clock port A */
                |  RCC_AHBENR_ADC12EN;              /* ADC12EN: clock ADC1/ADC2 */
    GPIOA->MODER |= GPIO_MODER_MODER0;              /* MODER0 = 11: PA0 analog mode */
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR0;             /* PUPDR0 = 00: no pull-up/down on PA0 */

    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE)
                      | ADC_CCR_CKMODE_0;           /* CKMODE = 01: ADC clock = HCLK/1 = 64 MHz */

    /* --- voltage regulator: ADVREGEN must go 10 -> 00 -> 01, then wait >= 10 us --- */
    ADC1->CR &= ~ADC_CR_ADVREGEN;                   /* ADVREGEN = 00: intermediate state */
    ADC1->CR |=  ADC_CR_ADVREGEN_0;                 /* ADVREGEN = 01: regulator on */
    delay_cycles(1000);                             /* ~15 us at 64 MHz */

    /* --- calibration (ADC must be disabled) --- */
    ADC1->CR &= ~ADC_CR_ADCALDIF;                   /* ADCALDIF = 0: single-ended calibration */
    ADC1->CR |=  ADC_CR_ADCAL;                      /* ADCAL = 1: start calibration */
    while (ADC1->CR & ADC_CR_ADCAL) { }             /* hardware clears ADCAL when done */
    delay_cycles(100);                              /* a few ADC clocks before ADEN */

    /* --- conversion setup (only while ADSTART = 0) --- */
    ADC1->CFGR = (ADC1->CFGR & ~(ADC_CFGR_RES | ADC_CFGR_ALIGN | ADC_CFGR_CONT |
                                 ADC_CFGR_EXTSEL | ADC_CFGR_EXTEN | ADC_CFGR_DMAEN))
               /* RES = 00: 12-bit, ALIGN = 0: right aligned, CONT = 0: one conversion per trigger */
               | (13UL << ADC_CFGR_EXTSEL_Pos)      /* EXTSEL = 1101: EXT13 = TIM6_TRGO (RM0316 ADC1/2 regular triggers) */
               | ADC_CFGR_EXTEN_0;                  /* EXTEN = 01: trigger on rising edge */

    ADC1->SQR1 = (0UL << ADC_SQR1_L_Pos)            /* L = 0: sequence length 1 conversion */
               | (1UL << ADC_SQR1_SQ1_Pos);         /* SQ1 = 1: first conversion = channel 1 (PA0) */

    ADC1->SMPR1 = (ADC1->SMPR1 & ~ADC_SMPR1_SMP1)
                | (5UL << ADC_SMPR1_SMP1_Pos);      /* SMP1 = 101: 61.5 cycles sample time
                                                       -> (61.5 + 12.5) / 64 MHz = 1.16 us per conversion,
                                                       far below the 100 us sample period */

    /* --- enable --- */
    ADC1->ISR = ADC_ISR_ADRDY;                      /* ADRDY: clear (write 1) */
    ADC1->CR |= ADC_CR_ADEN;                        /* ADEN = 1: enable ADC */
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) { }        /* ADRDY: wait until ready */

    /* --- interrupts --- */
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_OVR;          /* EOC, OVR: clear stale flags */
    ADC1->IER = ADC_IER_EOCIE                       /* EOCIE: interrupt per conversion */
              | ADC_IER_OVRIE;                      /* OVRIE: interrupt if a result was lost */
    NVIC_SetPriority(ADC1_2_IRQn, 0);               /* highest priority: never miss a sample */
    NVIC_EnableIRQ(ADC1_2_IRQn);

    ADC1->CR |= ADC_CR_ADSTART;                     /* ADSTART = 1: armed, converts on each TRGO */
}

/* Kept as short as possible: read, store, count. No printing or maths here. */
void ADC1_2_IRQHandler(void)
{
    uint32_t isr = ADC1->ISR;

    if (isr & ADC_ISR_OVR) {                        /* OVR: previous result was not read in time */
        ADC1->ISR = ADC_ISR_OVR;                    /* clear by writing 1 */
        s_overruns++;
    }
    if (isr & ADC_ISR_EOC) {
        uint16_t sample = (uint16_t)ADC1->DR;       /* reading DR also clears EOC */
        uint32_t next = (s_head + 1U) & BUF_MASK;
        if (next != s_tail) {
            s_buf[s_head] = sample;
            s_head = next;
        } else {
            s_dropped++;                            /* buffer full: consumer is too slow */
        }
        s_total++;
    }
}

uint32_t adc_buffer_pop(uint16_t *out)
{
    uint32_t tail = s_tail;
    if (tail == s_head) return 0;                   /* empty */
    *out = s_buf[tail];
    s_tail = (tail + 1U) & BUF_MASK;
    return 1;
}

uint32_t adc_buffer_level(void)    { return (s_head - s_tail) & BUF_MASK; }
uint32_t adc_total_samples(void)   { return s_total; }
uint32_t adc_dropped_samples(void) { return s_dropped; }
uint32_t adc_overruns(void)        { return s_overruns; }
