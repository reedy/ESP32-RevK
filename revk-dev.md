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

## LWMQTT

## JO
