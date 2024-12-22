# ESP32RevK

Library of tools used for ESP32 development.

This includes functions for management of settins in NVS using MQTT controls.

This includes Kconfig settings to enable/disable key functions, such as AP mode config, Multiple WiFi handling, MQTT handling, etc.

User guide: [Details of using devices that use this library](revk-user.md)

Dev guide: [Details for apps using this library](revk-dev.md)

## Flashing code

My various code typically has a `release` and `betarelease` directory. In this are several `.bin` files.

You can use `esptool` to flash, but there is a simpler way for most people using a web page and Chrome browser.

[https://adafruit.github.io/Adafruit_WebSerial_ESPTool/](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)

The code to load, lets call the app `MyApp`, and assume we want to flash for the ESP32-S3-MINI-N4-R2 chip...

|Offset|File|
|----|----|
|0x0|MyApp-S3-MINI-N4-R4-bootloader.bin|
|0x8000|MyApp-S3-MINI-N4-R4-partition-table.bin|
|0xD000|MyApp-S3-MINI-N4-R4-ota_data_initial.bin|
|0x10000|MyApp-S3-MINI-N4-R4.bin|

