# MCI-26 — Microcontroller Interfacing Lab

Lab work for the Microcontroller Interfacing (MCI) course, targeting the
STM32F3-Discovery board (STM32F303VC). Projects are generated with STM32CubeMX
and built with CMake.

## Labs

| Lab | Topic | Peripherals used |
|-----|-------|------------------|
| [Lab01](Lab01_cubemx_usb_setup) | CubeMX base project setup | GPIO (LEDs, user button), USB Device (CDC) |
| [Lab02](Lab02_uart_printf) | UART printf / serial output | USART2 |
| [Lab03](Lab03_seven_segment_gpio) | 7-segment display (counter, buttons, dice) | GPIO (GPIOB/GPIOC outputs, PA0/PC8 inputs) |
| [Lab04](Lab04_timer_input_capture) | Timer input capture — period/frequency | TIM3 CH1 input capture, USART1, USB CDC |
| [Lab05](Lab05_pwm_motor_control) | PWM LED fade and DC motor control | TIM3 PWM, TIM4 CH1/CH2 PWM, GPIO (H-bridge direction) |
| [Lab06](Lab06_exti_freq_measure) | EXTI-based frequency measurement | EXTI (PC7), TIM2, USART1 |
| [Lab08](Lab08_spi_gyroscope) | SPI gyroscope (L3GD20): WHO_AM_I, temperature, X/Y/Z rate | SPI1 (polling + interrupt), GPIO (PE3 chip select), USART1 |
| [Lab09](Lab09_i2c_accelerometer) | I2C accelerometer (LSM303): calibration and tilt angle | I2C1, USART1 |

**Each lab is a component of the final project** — they are kept as
self-contained modules so they can be imported and integrated together in
[`final-project/`](final-project).

## Repository contents

Only source and text files are tracked (`.c`, `.h`, `.txt`): each lab contains
`Core/Inc`, `Core/Src`, `CMakeLists.txt`, `cmake/stm32cubemx/CMakeLists.txt`,
and (Lab01, Lab04) `USB_DEVICE/`. ST vendor code (`Drivers/`, `Middlewares/`),
`.ioc` files, linker scripts, startup files and toolchain files are not
tracked and must be regenerated with STM32CubeMX before building.
