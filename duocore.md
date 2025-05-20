# ESP32-S3 Overview and Dual-Core Task Allocation in `main.cpp`

## Summary

This document provides an overview of the ESP32-S3 microcontroller and details the dual-core task allocation strategy implemented in the `main.cpp` file for an intelligent irrigation system. The ESP32-S3's powerful features, including its dual-core processor, are leveraged to efficiently manage sensor readings, network communication, relay control, and scheduling. Core 0 handles network operations, sensor data processing, status updates, and MQTT communication, operating preemptively. Core 1 is dedicated to event-driven relay timer management, ensuring timely control of irrigation relays.

## ESP32-S3 Overview

The ESP32-S3 is a powerful and versatile series of microcontrollers (MCUs) developed by Espressif Systems. It is designed to meet the demands of IoT (Internet of Things) applications requiring high performance, wireless connectivity, and advanced security features. Below are some key features of the ESP32-S3, based on information from the official datasheet ([ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)):

*   **Dual-Core Processor**: The ESP32-S3 is equipped with a dual-core Xtensa® 32-bit LX7 microprocessor, capable of operating at frequencies up to 240 MHz. This dual-core architecture allows for parallel task processing, enhancing system performance and responsiveness.
*   **Wireless Connectivity**: Supports Wi-Fi (IEEE 802.11 b/g/n) and Bluetooth 5 (LE), providing flexible connectivity options for IoT applications.
*   **Memory**: Integrates SRAM and ROM, along with support for external SPI flash memory (Quad SPI or Octal SPI) with large capacity, enabling efficient program and data storage.
*   **Rich Peripherals**: Offers a wide range of peripheral interfaces, including GPIO, ADC (Analog-to-Digital Converter), DAC (Digital-to-Analog Converter), SPI, I2C, I2S, UART, RMT (Remote Control Transceiver), TWAI® (CAN compatible), USB OTG, and more. Many GPIO pins have touch sensing capabilities.
*   **AI Acceleration Features**: Some versions of the ESP32-S3 include hardware acceleration instructions for AI (Artificial Intelligence) tasks, optimizing performance for machine learning and digital signal processing applications.
*   **Security**: Provides hardware security features such as Secure Boot, Flash Encryption, and cryptographic accelerators (AES, SHA, RSA, ECC) to protect the device and data.
*   **Power Management**: Supports multiple low-power modes, helping to optimize battery life for battery-powered applications.

These features make the ESP32-S3 an ideal choice for complex projects like your smart irrigation system, where concurrent processing of multiple tasks (sensor reading, relay control, network communication) is crucial.

## Dual-Core Task Allocation in `main.cpp`

The `main.cpp` file utilizes the dual-core architecture of the ESP32 to efficiently divide tasks, ensuring the system operates smoothly and responds quickly. Below is a description of how tasks are assigned to each core:

### Core 0 (`core0Task` - Core0TaskCode)

Core 0 is designated to handle primary cyclic tasks, priority operations, and network-related activities. The main responsibilities of Core 0 include:

*   **Network Connection Management**:
    *   Maintaining WiFi connection.
    *   Managing connection to the MQTT broker.
    *   Automatically reconnecting WiFi and MQTT if the connection is lost.
    *   Re-subscribing to necessary MQTT topics after reconnection.
*   **Sensor Data Processing**:
    *   Reading data from sensors (temperature, humidity, soil moisture, etc.) at predefined intervals.
    *   Sending the read sensor data to the MQTT broker.
*   **Status and Schedule Management**:
    *   Sending the current status of relays to the MQTT broker upon change or for a forced periodic report.
    *   Sending the status of irrigation schedules (task scheduler) to the MQTT broker.
    *   Checking and updating irrigation schedules based on their designated times.
*   **MQTT Callback Handling**:
    *   Receiving and processing control commands (e.g., turn relays on/off, update schedules, configure logs) from the MQTT broker.
*   **System Status Indication**:
    *   Controlling an RGB LED to display the system status (e.g., network connected, attempting to connect, connection error).
*   **Logging**:
    *   Recording system activity, errors, and warnings.

This core operates preemptively, meaning that critical tasks can interrupt less critical ones to ensure low latency for primary operations.

### Core 1 (`core1Task` - Core1TaskCode)

Core 1 is dedicated to event-driven control tasks, specifically managing events related to turning off relays after a set period of operation.

*   **Relay Timer Event Handling**:
    *   Waiting for events from a queue (`g_relayEventQueue`). These events are generated when a relay has been turned on and needs to be turned off after a timed duration.
    *   Upon receiving a timer expiration event for a relay, Core 1 will execute the command to turn off that relay.

This separation helps ensure that relay control operations (especially turning off relays at the correct time) are not affected or delayed by other tasks running on Core 0, such as network communication or sensor readings.

## Task Initialization and Assignment

In the `setup()` function, the two tasks `Core0TaskCode` and `Core1TaskCode` are created and pinned to their respective cores (Core 0 and Core 1) using the `xTaskCreatePinnedToCore` function from FreeRTOS. This ensures that each task will always run on its designated core, maximizing the parallel processing capabilities of the ESP32.