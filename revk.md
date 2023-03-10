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

You can click the buttons for existing SSID, or enter manually, and enter password. It then tries to connect and shows the IP so you can connect to any web interface on the device. Not all devices have a web interface.

However, the MQTT settings allow control and settings to be configured over MQTT.

## MQTT 

The system will connect to an MQTT server and provide information via MQTT, allow commands over MQTT, and allow settings over MQTT.

### Topics

There are two styles, which depend on a config option. One style has a *prefix*, then *app name*, then *device id*, then an optional suffix. The other format omits the *app name*.

#### Messages to the device

|Prefix|Meaning|
|------|-------|
|command|Does a command. This does not change settings but could change some live status of some sort, or do an action. In some cases commands can talk to external devices, such as the SDC41 in the Environmental monitor.|
|setting|Changes a setting value, or gets settings. See below|

#### Messages from the device


|Prefix|Meaning|
|------|-------|
|state|This is sent with *retain* and relates to the state of some aspect of the device. With no suffix, this is a top level state for the device, in JSON, including either `"up":false` or `"up":`*time*.|

### Commands

### Settings

## RevK

## LWMQTT

## JO
