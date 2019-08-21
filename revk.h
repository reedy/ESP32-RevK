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
extern const char *revk_version;        // App version
extern char revk_id[7];         // Chip ID hex

// Calls
void revk_init (app_command_t * app_command);
// Register a setting, call from init (i.e. this is not expecting to be thread safe) - sets the value when called and on revk_setting/MQTT changes
void revk_register (const char *name,   // Setting name (note max 15 characters inc any number suffix)
                    unsigned char array,        // If non zero then settings are suffixed numerically 1 to array
                    signed char size,   // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                    void *data, // The setting itself (for string this points to a char* pointer)
                    const char *defval, // default value (default for each byte for BINARY fixed size)
                    unsigned char flags);       // Setting flags
#define	SETTING_REBOOT		1       // Reboot after changing setting (after a short delay to allow multiple settings)
#define	SETTING_BINARY		2       // Binary data, size (if non 0) is exact size of data expected at memory pointed to by data/
                                        // If size is 0 this is a string, malloced and stored at pointer at data, with first byte being length of binary data
#define	SETTING_POLARITY	4       // A leading "-" causes top bit set (so only makes sense with unsigned values)
#define	SETTING_BOOLEAN		8       // Boolean value (array sets bits in value)
#define	SETTING_MALLOC		128     // Internally used - marks if current dynamic setting is malloc'd and so needs freeing if changed

// MQTT reporting
void revk_status (const char *tag, const char *fmt, ...);       // Send status
void revk_event (const char *tag, const char *fmt, ...);        // Send event
void revk_error (const char *tag, const char *fmt, ...);        // Send error
void revk_info (const char *tag, const char *fmt, ...); // Send info

// Settings
const char *revk_setting (const char *tag, unsigned int len, const unsigned char *value);       // Store a setting (same as MQTT, so calls app_setting)
const char *revk_command (const char *tag, unsigned int len, const unsigned char *value);       // Do a command (same as MQTT, so calls app_command)
const char *revk_restart (const char *reason, int delay);       // Restart cleanly
const char *revk_ota (const char *host);        // OTA and restart cleanly

#endif
