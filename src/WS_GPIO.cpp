#include "../include/WS_GPIO.h"
#include <Arduino.h>

/*************************************************************  I/O  *************************************************************/
// Toggles the digital state of a pin.
void digitalToggle(int pin)
{
  digitalWrite(pin, !digitalRead(pin));
}

// Sets the color of the RGB LED.
// Note: The neopixelWrite function expects colors in Green, Red, Blue order for some hardware.
void RGB_Light(uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
  neopixelWrite(GPIO_PIN_RGB, green_val, red_val, blue_val);
}

// Activates the buzzer for a specified duration using PWM.
// Time: duration in milliseconds to keep the buzzer on.
void Buzzer_PWM(uint16_t Time) 
{
  ledcWrite(PWM_Channel, Dutyfactor); // Turn buzzer on with configured duty cycle
  delay(Time);                        // Keep buzzer on for specified time
  ledcWrite(PWM_Channel, 0);          // Turn buzzer off
}

// Initializes all GPIO pins used by the system.
void GPIO_Init()
{
  // Initialize Relay control pins as OUTPUT
  pinMode(GPIO_PIN_CH1, OUTPUT);
  pinMode(GPIO_PIN_CH2, OUTPUT);
  pinMode(GPIO_PIN_CH3, OUTPUT);
  pinMode(GPIO_PIN_CH4, OUTPUT);
  pinMode(GPIO_PIN_CH5, OUTPUT);
  pinMode(GPIO_PIN_CH6, OUTPUT);
  
  // Initialize RGB LED pin as OUTPUT
  pinMode(GPIO_PIN_RGB, OUTPUT);
  
  // Initialize Buzzer pin as OUTPUT and configure PWM channel for it
  pinMode(GPIO_PIN_Buzzer, OUTPUT);
  ledcSetup(PWM_Channel, Frequency, Resolution); // Configure PWM channel parameters
  ledcAttachPin(GPIO_PIN_Buzzer, PWM_Channel);   // Attach buzzer pin to the PWM channel
}