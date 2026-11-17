# HW2 - Real-time digital oscilloscope (STM32F3 Discovery)

Samee + Wasiq, MCI Fall 2026. CMSIS register-level code only (no HAL / LL).

TIM6 TRGO (10 kHz) -> ADC1 on PA0 -> ADC ISR -> 1024-sample ring buffer
-> main loop -> USART1 (PC4, 230400 baud, ST-LINK virtual COM port) -> `pc/scope.py`

| File | Owner | Job |
|------|-------|-----|
| `Core/Src/main.c` | Samee | glue: start everything, send samples + status once per second |
| `Core/Src/clock.c` | Samee | 64 MHz clock, 1 ms SysTick, once-per-second callback |
| `Core/Src/timer.c` | Samee | TIM6 10 kHz trigger, TIM3 1 kHz test square on PA6 |
| `Core/Src/adc.c` | Samee | ADC1 PA0, EOC/OVR interrupt, ring buffer |
| `Core/Src/led.c` | Samee | green PE15 = OK, red PE9 = sample lost |
| `Core/Src/uart.c` | Wasiq | USART1 TX on PC4, polling |
| `Core/Inc/protocol.h` | both | 2-byte sample frames, 3-byte status frames |
| `pc/scope.py` | Wasiq | live plotter with readouts, trigger, FFT, CSV |

## Build and flash
Open this folder in VS Code (STM32 extension), configure the **Debug** preset, Build, then Flash.

## Run
Jumper PA6 -> PA0 for the built-in 1 kHz test square, then:

    cd pc
    python scope.py COM7

`python scope.py fake` runs without a board, `python scope.py selftest` runs the self-test.
