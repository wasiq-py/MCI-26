/* led.h - STM32F3 Discovery user LEDs on PE8..PE15 (status only). Owner: Samee */
#ifndef LED_H
#define LED_H

#define LED_RED     9U    /* LD3, PE9  */
#define LED_GREEN  15U    /* LD6, PE15 */

void led_init(void);
void led_on(unsigned pin);
void led_off(unsigned pin);
void led_toggle(unsigned pin);

#endif
