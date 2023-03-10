# ESP32 RevK support library

The `ESP32-RevK` support library is used in almost all of the ESP32 projects on this repository. It provides basic management of WiFi connections, settings, MQTT, and JSON.

This manual covers the user view of this library in terms of interacting with any of the projects using it over MQTT.

It also covers using the libraries themself in the code.

## App name

The application will have a name. A good example is the Daikin appi, e.g. see https://github.com/revk/ESP32-Daikin which is, as you may guess `Daikin`.

## WiFi Settings

One of the key features is allowing WiFi to be configured on a new unit. This is a config option so does not have to be enabled, but if it is then when a unit is unable to connect to local WiFI, it changes to be an access point.
The WiFI name you see starts with the app name.

![IMG_2446](https://user-images.githubusercontent.com/996983/218394248-b409626b-2614-439d-95f4-71527c00aaa7.PNG)

If you select this on an iPhone, it auto loads the config page. On other devices you have to check the router IP for the subnet and enter that in to a browser. Either way you get the WiFi settings page that will look something like this (other fields may be added over time).

![IMG_2448](https://user-images.githubusercontent.com/996983/218394254-e03537f7-09a0-490d-aa38-b6964a5cd77f.PNG)




## MQTT 

### Topics

### Commands

### Settings

## RevK

## LWMQTT

## JO
