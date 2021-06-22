// Include file for revk.c

#ifndef	REVK_H
#define	REVK_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_timer.h"

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
#include "jo.h"

// Types
typedef const char *app_command_t (const char *tag, unsigned int len, const unsigned char *value);      // Return NULL=OK, empty-string=Unknown tag, string=error

// Data
extern const char *revk_app;    // App name
extern const char *revk_version;        // App version
extern char revk_id[13];         // Chip ID hex (from MAC)
extern uint64_t revk_binid;     // Chip ID binary
extern char *prefixstate;
extern char *prefixevent;
extern char *prefixinfo;
extern char *prefixerror;

// Calls
void revk_init (app_command_t * app_command);
// Register a setting, call from init (i.e. this is not expecting to be thread safe) - sets the value when called and on revk_setting/MQTT changes
// Note, a setting that is SECRET that is a root name followed by sub names creates parent/child. Only shown if parent has value or default value (usually overlap a key child)
void revk_register (const char *name,   // Setting name (note max 15 characters inc any number suffix)
                    uint8_t array,        // If non zero then settings are suffixed numerically 1 to array
                    uint16_t size, // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                    void *data, // The setting itself (for string this points to a char* pointer)
                    const char *defval, // default value (default value text, or bitmask[space]default)
                    uint8_t flags);       // Setting flags
#define	SETTING_LIVE		1       // Setting update live (else reboots shortly after any change)
#define	SETTING_BINARY		2       // Binary block rather than numeric
#define	SETTING_SIGNED		4       // Numeric is signed
#define	SETTING_BOOLEAN		8       // Boolean value (array sets bits)
#define	SETTING_BITFIELD	16      // Numeric value has bit field prefix (from defval string)
#define	SETTING_HEX		32      // Source string is hex coded
#define	SETTING_SET		64      // Set top bit of numeric if a value is present at all
#define	SETTING_SECRET		128     // Don't dump setting

#if CONFIG_LOG_DEFAULT_LEVEL > 2
esp_err_t revk_err_check (esp_err_t, const char *file, int line,const char *func,const char *cmd);       // Log if error
#define	REVK_ERR_CHECK(x) revk_err_check(x,__FILE__,__LINE__,__FUNCTION__,#x)
#else
esp_err_t revk_err_check(esp_err_t e);
#define	REVK_ERR_CHECK(x) revk_err_check(x)
#endif

const char *revk_appname(void);
const char *revk_hostname(void);

// Make a task
TaskHandle_t revk_task (const char *tag, TaskFunction_t t, const void *param);

// reporting (normally MQTT)
void revk_state (const char *tag, const char *fmt, ...);        // Send status
void revk_statej (const char *tag,jo_t *);
void revk_event (const char *tag, const char *fmt, ...);        // Send event
void revk_eventj (const char *tag,jo_t *);
void revk_error (const char *tag, const char *fmt, ...);        // Send error
void revk_errorj (const char *tag,jo_t *);
void revk_info (const char *tag, const char *fmt, ...); // Send info
void revk_infoj (const char *tag,jo_t *);
#ifdef	CONFIG_REVK_MQTT
void revk_raw (const char *prefix, const char *tag, int len, void * data, int retain);
#endif

const char *revk_setting (const char *tag, unsigned int len, const void *value);       // Store a setting (same as MQTT, so calls app_setting)
const char *revk_command (const char *tag, unsigned int len, const void *value);       // Do a command (same as MQTT, so calls app_command)
const char *revk_restart (const char *reason, int delay);       // Restart cleanly
const char *revk_ota (const char *host);        // OTA and restart cleanly

#ifdef	CONFIG_REVK_MQTT
const char *revk_mqtt (void);
void revk_mqtt_close(const char *reason); // Clean close MQTT
int revk_wait_mqtt(int seconds);
#endif
#ifdef	CONFIG_REVK_WIFI
const char *revk_wifi (void);
void revk_wifi_close(void);
int revk_wait_wifi(int seconds);
#endif
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
uint32_t revk_offline (void);   // How long we have been offline (seconds), or 0 if online
#endif
void revk_blink(uint8_t on,uint8_t off); // Set LED blink rate (0,0) for default

#endif


