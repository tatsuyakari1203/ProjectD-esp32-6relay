#ifndef _WS_GPIO_H_
#define _WS_GPIO_H_
#include <HardwareSerial.h>     // Required for Serial1 if used, or general ESP32 serial functionalities

// UART1 Pins
#define TXD1 17                 // TXD for UART1
#define RXD1 18                 // RXD for UART1

// Relay Control Pins
#define GPIO_PIN_CH1      1     // Relay Channel 1 Control GPIO
#define GPIO_PIN_CH2      2     // Relay Channel 2 Control GPIO
#define GPIO_PIN_CH3      41    // Relay Channel 3 Control GPIO
#define GPIO_PIN_CH4      42    // Relay Channel 4 Control GPIO
#define GPIO_PIN_CH5      45    // Relay Channel 5 Control GPIO
#define GPIO_PIN_CH6      46    // Relay Channel 6 Control GPIO

// Other Peripheral Pins
#define GPIO_PIN_RGB      38    // RGB LED Control GPIO
#define GPIO_PIN_Buzzer   21    // Buzzer Control GPIO

// Buzzer PWM Configuration
#define PWM_Channel     1       // PWM Channel for Buzzer
#define Frequency       1000    // PWM Frequency (Hz)
#define Resolution      8       // PWM Resolution (bits)
#define Dutyfactor      200     // PWM Duty Factor (0-255 for 8-bit resolution)

// Function Declarations
void digitalToggle(int pin);
void RGB_Light(uint8_t red_val, uint8_t green_val, uint8_t blue_val);
void GPIO_Init();
void Buzzer_PWM(uint16_t Time); // Time in milliseconds

#endif