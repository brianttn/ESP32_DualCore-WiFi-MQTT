# Dual core for WiFi and MQTT operation

## ESP32 brief Instroduction

The ESP32 has 2 microprocessors：core 0 and core 1. So, it is dual core.

Normally, ESP32 runs on core 1 when we run code on Arduino IDE.

## Implementation

This program will run codes simultaneously on both cores, and make ESP32 multitasking.

### Core0 Task : Reconnect WiFi & MQTT

Core0 is mainly responsible for the connection tasks of WiFi and MQTT.

### Core1 Task : Main Loop

Core1 is mainly responsible for all the control logic of the main program.
