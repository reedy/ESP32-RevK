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
typedef const char *app_callback_t(const char *tag,const char *value); // Return NULL=OK, empty-string=Unknown tag, string=error

// Data
extern const char *revk_app;	// App name
extern char revk_version[20]; // App version

// Calls
void revk_init(const char *file, const char *date, const char *time, app_callback_t *app_setting, app_callback_t *app_command);

// MQTT reporting
void revk_status(const char *tag,const char *fmt,...); // Send status
void revk_event(const char *tag,const char *fmt,...); // Send event
void revk_error(const char *tag,const char *fmt,...); // Send error
void revk_info(const char *tag,const char *fmt,...); // Send info

// Settings
void revk_setting(const char *tag,const char *value); // Store a value (calls setting, same as if via MQTT)
void revk_reboot(void); // Restart cleanly
void revk_ota(void); // OTA and restart cleanly

#endif

