// Include file for revk.c

#ifndef	REVK_H
#define	REVK_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

// Types
typedef const char *app_command_t (const char *tag, unsigned int len, const unsigned char *value);      // Return NULL=OK, empty-string=Unknown tag, string=error

// Data
extern const char *revk_app;    // App name
extern char revk_version[20];   // App version
extern char revk_id[7];         // Chip ID hex

// Calls
void revk_init (const char *file, const char *date, const char *time, app_command_t * app_command);
// Register a setting, call from init (i.e. this is not expecting to be thread safe)
void revk_register (const char *name,   // Setting name (note max 15 characters inc any number suffix)
                    int array,  // If non zero then settings are suffixed numerically 1 to array
                    int size,   // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                    void *data, // The setting itself (for string this points to a char* pointer)
                    int flags); // Setting flags
#define	SETTING_REBOOT	1       // Reboot after changing setting (after a short delay to allow multiple settings)
#define	SETTING_FIXED	2       // Fixed binary data, size is size of byte array pointed to by data, and value must be this many bytes
#define	SETTING_INPUT	4       // Setting is an input port, store port number, but allow - prefix to say inverted input and set INV
#define	SETTING_OUTPUT	8       // Setting is an output port, store port number, but allow - prefix to say inverted output and set INV

// MQTT reporting
void revk_status (const char *tag, const char *fmt, ...);       // Send status
void revk_event (const char *tag, const char *fmt, ...);        // Send event
void revk_error (const char *tag, const char *fmt, ...);        // Send error
void revk_info (const char *tag, const char *fmt, ...); // Send info

// Settings
const char *revk_setting (const char *tag, unsigned int len, const unsigned char *value);       // Store a setting (same as MQTT, so calls app_setting)
const char *revk_command (const char *tag, unsigned int len, const unsigned char *value);       // Do a command (same as MQTT, so calls app_command)
const char *revk_restart (const char *reason);  // Restart cleanly
const char *revk_ota (const char *host);        // OTA and restart cleanly

#endif
