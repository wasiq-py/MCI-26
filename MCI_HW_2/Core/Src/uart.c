/* uart.c - sends bytes from the board to the PC over USART1.
 *
 * The wire to the PC is the ST-LINK USB cable (it acts as a virtual COM port).
 * USART1's transmit pin is PC4. Format: 8 data bits, no parity, 1 stop bit.
 */
#include "uart.h"
#include "clock.h"            /* gives PCLK2_HZ = the clock that feeds USART1 */
#include "stm32f303xc.h"

void uart_init(uint32_t baud)
{
    /* STEP 1: Peripherals are switched off to save power, so turn on their clocks first. */
    RCC->AHBENR  |= RCC_AHBENR_GPIOCEN;            /* clock for GPIOC (the port that holds pin PC4) */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;          /* clock for USART1 */

    /* STEP 2: Connect pin PC4 to USART1 (instead of being a normal on/off pin). */
    GPIOC->MODER &= ~GPIO_MODER_MODER4;            /* clear the 2 mode bits of pin 4 */
    GPIOC->MODER |= GPIO_MODER_MODER4_1;           /* mode = 10 -> "alternate function" (hand the pin to a peripheral) */
    GPIOC->AFR[0] &= ~(0xFUL << 16);               /* clear the 4 alternate-function bits of pin 4 */
    GPIOC->AFR[0] |= (7UL << 16);                  /* alternate function 7 = USART1_TX */
    GPIOC->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR4;      /* fast pin edges, so the signal stays clean */

    /* STEP 3: Set up USART1 while it is still switched off. */
    USART1->CR1 = 0;                               /* everything off/default: 8 data bits, no parity */
    USART1->CR2 = 0;                               /* default: 1 stop bit */

    /* BRR sets the speed. Formula: BRR = USART clock / baud rate.
       Example: 64,000,000 / 230,400 = 277.78, rounded to 278 (error is only 0.08 %). */
    USART1->BRR = (PCLK2_HZ + (baud / 2)) / baud;  /* "+ baud/2" makes it round instead of cut off */

    /* STEP 4: Switch it on. */
    USART1->CR1 |= USART_CR1_TE;                   /* TE = transmitter enable */
    USART1->CR1 |= USART_CR1_UE;                   /* UE = USART enable (always done last) */
}

/* Sends 'len' bytes, one after another. */
void uart_write(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        /* TXE flag = 1 means the transmit register is empty, so we can give it the next byte. */
        while ((USART1->ISR & USART_ISR_TXE) == 0) { }   /* wait until it is empty */

        USART1->TDR = data[i];                     /* writing here starts sending the byte */
    }
}
