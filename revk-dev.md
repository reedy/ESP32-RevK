# ESP32 RevK support library

The `ESP32-RevK` support library is used in almost all of the ESP32 projects on this repository. It provides basic management of WiFi connections, settings, MQTT, and JSON.

This manual covers the use of teh library in an app.

## RevK

This is how an application uses the RevK library.

### Include

To use the libraries simply add the include file
```
#include "revk.h"
```
This includes necessary system includes and `lwmqtt.h` and `jo.h` include files needed.

### CMake

In you `main/CMakeLists.txt ` have
```
set (COMPONENT_REQUIRES "ESP32-RevK)
```

### Init

In `void app_main()` you need, close to the start
```
revk_boot(&app_callback);
```
And once you have done any settings (see below) do
```
revk_start();
```

The `app_callback` is `const char *app_callback(int client, const char *prefix, const char *target, const char *suffix, jo_t j)` which is called for any received MQTT messages. The return value is an empty string for all OK, or an error message. NULL means not handled and not an error (which usually means an error as unknown command).

### Settings

Between `revk_boot` and `revk_start` you should add necessary calls to `revk_register(...)` to add any settings you need.
```
  void revk_register(const char *name,    // Setting name (note max 15 characters inc any number suffix)
                     uint8_t array,       // If non zero then settings are suffixed numerically 1 to array
                     uint16_t size,       // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                     void *data,  // The setting itself (for string this points to a char* pointer)
                     const char *defval,  // default value (default value text, or bitmask[space]default)
                     uint8_t flags);      // Setting flags
```
The way this is typically done is a list of settings in a macro, allowing the definition of the settings and the calling of the `revk_register` all to be done from the same list.

For example, define settings like this
```
#define settings                \
        u8(webcontrol,2)        \
        bl(debug)               \
```

You can then define the values, e.g.
```
#define u8(n,d) uint8_t n;
#define bl(n) uint8_t n;
settings
#undef u8
#undef bl
```

And in `app_main` call the `revk_register` like this.
```
#define bl(n) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BOOLEAN|SETTING_LIVE);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,str(d),0);
   settings
#undef u8
#undef bl
```

Obviously there could be way more types and flags you can use for different types of settings. This example uses `bl()` for "Boolean, live update", and `u8()` for `uint8_t` with a default value.

### Useful functions tracking state

There are a number of functions to keep track of things...
```
uint32_t revk_link_down(void);  // How long link down (no IP or no parent if mesh)
const char *revk_wifi(void);	// Return wifi SSID
void revk_wifi_close(void); // Close WiFi
int revk_wait_wifi(int seconds); // Wait for WiFi to be ready
char *revk_setting(jo_t); // Store settings
const char *revk_command(const char *tag, jo_t);        // Do an internal command
const char *revk_restart(const char *reason, int delay);        // Restart cleanly
const char *revk_ota(const char *host, const char *target);     // OTA and restart cleanly (target NULL for self as root node)
uint32_t revk_shutting_down(void); // If we are shutting down (how many seconds to go)
```

## LWMQTT

The `lwmqtt` library is a *light weight MQTT* server and client library used internally by the RevK library.
```
lwmqtt_t revk_mqtt(int);	// Return the current LWMQTT handle
void revk_mqtt_close(const char *reason);       // Clean close MQTT
int revk_wait_mqtt(int seconds);	// Wait for MQTT to connect
```

Generally you do not need to directly interact with MQTT, but there are some simple functions to generate MQTT messages.

To use these you construct a JSON object then call these to send the message and free the object.
```
revk_state(const char *suffix,jo_t j); // Send a state message
revk_event(const char *suffix,jo_t j); // Send a event message
revk_info(const char *suffix,jo_t j); // Send a info message
revk_error(const char *suffix,jo_t j); // Send a error message
```
Additional lower level functions are defined in `revk.h` and `lwmqtt.h`

### Example
```
jo_t j = jo_object_alloc();
jo_string(j, "field", tag);
jo_string(j, "error", err);
revk_error("control", &j);
```

## JO
