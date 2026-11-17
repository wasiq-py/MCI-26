/* led.c - see led.h. CMSIS register-level only. Owner: Samee */
#include "stm32f303xc.h"
#include "led.h"

void led_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOEEN;              /* GPIOEEN: clock port E */
    GPIOE->MODER = (GPIOE->MODER & ~((3UL << (LED_RED * 2)) | (3UL << (LED_GREEN * 2))))
                 | (1UL << (LED_RED * 2))           /* MODER9  = 01: PE9 output */
                 | (1UL << (LED_GREEN * 2));        /* MODER15 = 01: PE15 output */
}

void led_on(unsigned pin)     { GPIOE->BSRR = 1UL << pin; }           /* BSx: set pin   */
void led_off(unsigned pin)    { GPIOE->BSRR = 1UL << (pin + 16U); }   /* BRx: reset pin */
void led_toggle(unsigned pin) { GPIOE->ODR ^= 1UL << pin; }           /* ODRx: flip pin */
