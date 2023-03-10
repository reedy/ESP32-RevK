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

There are two styles, which depend on a config option. One style has (`/` separated) *prefix*, then *app name*, then *device id*, then an optional suffix. The other format omits the *app name*.

The *device id* is either a hex device ID, its MAC address, or the `hostname` setting value if set.

Where the topic has an *app name* you can command all of the device with that *app name* by using a *device name* of `*`.
Where the topic does not have the *app name* you can command all of the devices with the same *app name* by using the *app name* as the *device name*. 

#### Messages to the device

In most cases the payload, if any, is JSON. This could however by a JSON data type such as a number, or string, rather than an actual object.

|Prefix|Meaning|
|------|-------|
|`command`|Does a command. This does not change settings but could change some live status of some sort, or do an action. In some cases commands can talk to external devices, such as the SDC41 in the Environmental monitor.|
|`setting`|Changes a setting value, or gets settings. See below|

#### Messages from the device

In most cases the payload is JSON, usually as a JSON object.

|Prefix|Meaning|
|------|-------|
|`state`|This is sent with *retain* and relates to the state of some aspect of the device. With no suffix, this is a top level state for the device, in JSON, including either `"up":false` or `"up":`*time*. With a suffix this is the state of some aspect of the device.|
|`event`|This is for an event, rather than a state change. A good example might be a key press on a device.|
|`info`|This is information of some sort|
|`error`|This is an error message|
|`setting`|This is the current settings, as a JSON object, if requested|

### Commands

The device may have any number of commands documents, but there are some commands provided directly by the library for all devices.

|Command|Meaning|
|-------|-------|
|`upgrade`|This does an *over the air* upgrade from the setting defined `otahost`. You can include a URL as the argument (`http://` only, not `https`). Usually the device will be build with code signing to ensure the file is genuine.|
|`restart`|This does a restart of the device|
|`factory`|This does a factory reset of all settings, the argument has to be a string of the MAC address and the app name, e.g. `112233445566TestApp`|

### Settings

## RevK

## LWMQTT

## JO
