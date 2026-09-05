# Distributed Smart Greenhouse Monitoring & Control System

This repository contains the source code for my Master's Thesis project: an advanced, distributed IoT system designed for monitoring and controlling a smart greenhouse environment. 

The system leverages the ESP32 microcontroller's capabilities and utilizes the **ESP-NOW** protocol to create a robust, low-latency wireless sensor network without relying on a traditional router architecture.

## System Architecture
The project is divided into two main operational modes to ensure maximum reliability and stability:

### 1. Online System (Web-Server & Sync)
* **Master Node:** Operates as an Asynchronous Web Server utilizing LittleFS for web file management. It handles WiFi connections dynamically (via a custom WiFi Manager), manages channel synchronization for ESP-NOW, and logs data.
* **Online RTC Node:** Communicates with the Master Node to sync time via NTP (Network Time Protocol) and ensures the timestamp data remains accurate across the network.

### 2. Offline Distributed System
A fully autonomous local network ensuring continuous operation regardless of internet connectivity.
* **Client Node (Data Logger):** Acts as the central hub. It polls data from sensor nodes every 10 seconds, calculates environmental states (e.g., Day/Night mood based on RTC), and securely logs sessions and data to a Micro SD card in CSV format.
* **Sensor Node:** Reads environmental data including Temperature & Ralative Air Humidity (DHT11), Light Intensity (BH1750 / GY-30), and Soil Moisture (Capacitive Analog Sensor V1.2).
* **Relay (Actuator) Node:** Controls the cooling fan and the water pump of the humidity pads. It features an advanced control algorithm implementing **Hysteresis** (to prevent relay toggling noise) and **Coupled Logic** (e.g., activating the humidity pad for additional cooling if the fan is running but temperature remains high).
* **Local RTC Node:** Utilizes a DS3231 I2C module to provide highly accurate, offline timestamps to the Client Node for reliable data logging.

## Hardware Requirements
* **Microcontrollers:** 6x ESP32 Development Boards
* **Sensors:** 
  * DHT11 (Temperature & Ralative Air Humidity)
  * BH1750 / GY-30 (Light / Lux)
  * Capacitive Soil Moisture Sensor V1.2
* **Modules:** DS3231 RTC Module, Micro SD Card Module (SPI), 2-Channel Relay Module.
* **Actuators:** Ventilation Fan, Water Pump (for cooling pads).

## Key Software Libraries
* `ESPAsyncWebServer` & `AsyncTCP` (For the Master web interface)
* `esp_now.h` & `esp_wifi.h` (For the core distributed network)
* `RTClib` (For DS3231 management)
* `LittleFS` & `SD` (For local file storage and data logging)

## Academic Context
This project was developed as part of a Master's thesis focusing on embedded systems in smart agriculture. It demonstrates proficiency in Embedded C/C++, distributed network architectures, RTOS-like non-blocking operations, and hardware-software integration.
