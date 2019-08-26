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
extern char revk_id[7];         // Chip ID hex (derived from MAC)
extern uint32_t revk_binid;     // Chip ID binary
extern char *prefixstate;
extern char *prefixevent;
extern char *prefixinfo;
extern char *prefixerror;

// Calls
void revk_init (app_command_t * app_command);
// Register a setting, call from init (i.e. this is not expecting to be thread safe) - sets the value when called and on revk_setting/MQTT changes
void revk_register (const char *name,   // Setting name (note max 15 characters inc any number suffix)
                    unsigned char array,        // If non zero then settings are suffixed numerically 1 to array
                    unsigned char size, // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                    void *data, // The setting itself (for string this points to a char* pointer)
                    const char *defval, // default value (default for each byte for BINARY fixed size)
                    unsigned char flags);       // Setting flags
#define	SETTING_LIVE		1       // Setting update live (else reboots shortly after any change)
#define	SETTING_BINARY		2       // Binary block rather than numeric
#define	SETTING_SIGNED		4       // Numeric is signed
#define	SETTING_BOOLEAN		8       // Boolean value (array sets bits)
#define	SETTING_BITFIELD	16      // Numeric value has bit field prefix (from defval string)
#define	SETTING_HEX		32      // Source string is hex coded
#define	SETTING_SET		64      // Set top bit of numeric if a value is present at all
esp_err_t revk_err_check (esp_err_t, const char *file, int line);       // Log if error
#define	REVK_ERR_CHECK(x) revk_err_check(x,__FILE__,__LINE__)

// MQTT reporting
void revk_state (const char *tag, const char *fmt, ...);        // Send status
void revk_event (const char *tag, const char *fmt, ...);        // Send event
void revk_error (const char *tag, const char *fmt, ...);        // Send error
void revk_info (const char *tag, const char *fmt, ...); // Send info
void revk_raw (const char *prefix, const char *tag, int len, uint8_t * data, int retain);

// Settings
const char *revk_setting (const char *tag, unsigned int len, const unsigned char *value);       // Store a setting (same as MQTT, so calls app_setting)
const char *revk_command (const char *tag, unsigned int len, const unsigned char *value);       // Do a command (same as MQTT, so calls app_command)
const char *revk_restart (const char *reason, int delay);       // Restart cleanly
const char *revk_ota (const char *host);        // OTA and restart cleanly

#endif
