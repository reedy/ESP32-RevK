// Main control code, working with WiFi, MQTT, and managing settings and OTA
static const char *TAG = "RevK";

#include "revk.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "lecert.h"
#include "esp_sntp.h"
#include "esp_phy_init.h"

#define	settings	\
		s(otahost,CONFIG_REVK_OTAHOST);		\
		s(otacert,NULL);			\
		s(ntphost,CONFIG_REVK_NTPHOST);		\
		u32(wifireset,300);			\
		sa(wifissid,3,CONFIG_REVK_WIFISSID);	\
		f(wifibssid,3,6);			\
		u8(wifichan,3,0);			\
		sa(wifipass,3,CONFIG_REVK_WIFIPASS);	\
		u32(mqttreset,0);			\
		sa(mqtthost,3,CONFIG_REVK_MQTTHOST);	\
		sa(mqttuser,3,NULL);			\
		sa(mqttpass,3,NULL);			\
		u16(mqttport,3,0);			\
		sa(mqttcert,3,NULL);			\
		p(command);				\
		p(setting);				\
		p(state);				\
		p(event);				\
		p(info);				\
		p(error);				\

#define s(n,d)		char *n;
#define sa(n,a,d)	char *n[a];
#define f(n,a,s)	char n[a][s];
#define	u32(n,d)	uint32_t n;
#define	u16(n,a,d)	uint16_t n[a];
#define	u8(n,a,d)	uint8_t n[a];
#define p(n)		char *prefix##n;
settings
#undef s
#undef sa
#undef f
#undef u32
#undef u16
#undef u8
#undef p
// Local types
typedef struct setting_s setting_t;
struct setting_s
{
   setting_t *next;
   const char *name;
   const char *defval;
   void *data;
   unsigned char size;
   unsigned char array;
   unsigned char flags;
};

// Public
const char *revk_app = "";
const char *revk_version = "";  // ISO date version
char revk_id[7];                // Chip ID as hex (derived from MAC)
uint32_t revk_binid = 0;        // Binary chip ID

// Local
static EventGroupHandle_t revk_group;
const static int GROUP_WIFI = BIT0;
const static int GROUP_MQTT = BIT1;
static TaskHandle_t revk_task_id = NULL;
static TaskHandle_t ota_task_id = NULL;
static app_command_t *app_command = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static int64_t restart_time = 0;
static int64_t nvs_time = 0;
static int64_t slow_dhcp = 0;
static const char *restart_reason = "Unknown";
static nvs_handle nvs = -1;
static setting_t *setting = NULL;
static int wifi_count = 0;
static int wifi_index = -1;
static int mqtt_count = 0;
static int mqtt_index = -1;

// Local functions
static void mqtt_next (void);
static void
wifi_next (void)
{
   int last = wifi_index;
   wifi_index++;
   if (wifi_index >= sizeof (wifissid) / sizeof (*wifissid) || !*wifissid[wifi_index])
      wifi_index = 0;
   if (last == wifi_index)
      return;                   // No change
   wifi_config_t wifi_config = { };
   if (wifibssid[wifi_index][0] || wifibssid[wifi_index][1] || wifibssid[wifi_index][2])
   {
      memcpy (wifi_config.sta.bssid, wifibssid[wifi_index], sizeof (wifi_config.sta.bssid));
      wifi_config.sta.bssid_set = 1;
   }
   wifi_config.sta.channel = wifichan[wifi_index];
   strncpy ((char *) wifi_config.sta.ssid, wifissid[wifi_index], sizeof (wifi_config.sta.ssid));
   strncpy ((char *) wifi_config.sta.password, wifipass[wifi_index], sizeof (wifi_config.sta.password));
   ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_STA, &wifi_config));
   ESP_LOGI (TAG, "Start the WIFi SSID:[%s]", wifissid[wifi_index]);
}

static esp_err_t
mqtt_event_handler (esp_mqtt_event_handle_t event)
{
   esp_mqtt_client_handle_t mqtt_client = event->client;
   // your_context_t *context = event->context;
   switch (event->event_id)
   {
   case MQTT_EVENT_CONNECTED:
      if (mqttreset)
         revk_restart (NULL, -1);
      xEventGroupSetBits (revk_group, GROUP_MQTT);
      void sub (const char *prefix)
      {
         char *topic;
         if (asprintf (&topic, "%s/%s/%s/#", prefix, revk_app, revk_id) < 0)
            return;
         esp_mqtt_client_subscribe (mqtt_client, topic, 0);
         free (topic);
         if (asprintf (&topic, "%s/%s/*/#", prefix, revk_app) < 0)
            return;
         esp_mqtt_client_subscribe (mqtt_client, topic, 0);
         free (topic);
      }
      sub (prefixcommand);
      sub (prefixsetting);
      // Version, up
      revk_state (NULL, "1 ESP32 %s", revk_version);    // Up
      // Info
      const esp_partition_t *p = esp_ota_get_running_partition ();
      wifi_ap_record_t ap = { };
      esp_wifi_sta_get_ap_info (&ap);
      revk_info (NULL, "MQTT%d(%d) %s mem=%d %dms L%d", mqtt_index + 1, mqtt_count, p->label,
                 esp_get_free_heap_size (), portTICK_PERIOD_MS, CONFIG_LOG_DEFAULT_LEVEL);
      if (app_command)
         app_command ("connect", strlen (mqtthost[mqtt_index]), (unsigned char *) mqtthost[mqtt_index]);
      break;
   case MQTT_EVENT_DISCONNECTED:
      mqtt_next ();
      if (mqttreset)
         revk_restart ("MQTT lost", mqttreset);
      mqtt_count++;
      xEventGroupClearBits (revk_group, GROUP_MQTT);
      if (app_command)
         app_command ("disconnect", strlen (mqtthost[mqtt_index]), (unsigned char *) mqtthost[mqtt_index]);
      esp_mqtt_client_start (mqtt_client);
      break;
   case MQTT_EVENT_DATA:
      {
         const char *e = NULL;
         int p;
         for (p = event->topic_len; p && event->topic[p - 1] != '/'; p--);
         char *tag = malloc (event->topic_len + 1 - p);
         memcpy (tag, event->topic + p, event->topic_len - p);
         tag[event->topic_len - p] = 0;
         char *value = malloc (event->data_len + 1);
         if (event->data_len)
            memcpy (value, event->data, event->data_len);
         value[event->data_len] = 0;    // Safe
         for (p = 0; p < event->topic_len && event->topic[p] != '/'; p++);
         if (p == 7 && !memcmp (event->topic, prefixcommand, p))
            e = revk_command (tag, event->data_len, (const unsigned char *) value);
         else if (p == 7 && !memcmp (event->topic, "setting", p))       // TODo configurable
            e = (revk_setting (tag, event->data_len, (const unsigned char *) value) ? : "");    // Returns NULL if OK
         else
            e = "";
         if (!e || *e)
            revk_error (tag, "Failed %s", e ? : "Unknown");
         free (tag);
         free (value);
      }
      break;
   case MQTT_EVENT_ERROR:
      break;
   default:
      break;
   }
   return ESP_OK;
}

static void
mqtt_next (void)
{
   int last = mqtt_index;
   mqtt_index++;
   if (mqtt_index >= sizeof (mqtthost) / sizeof (*mqtthost) || !*mqtthost[mqtt_index])
      mqtt_index = 0;
   if (last == mqtt_index)
      return;                   // No change
   char *topic;
   if (asprintf (&topic, "%s/%s/%s", prefixstate, revk_app, revk_id) < 0)
      return;
   char *url;
   if (asprintf (&url, "%s://%s/", *mqttcert[mqtt_index] ? "mqtts" : "mqtt", mqtthost[mqtt_index]) < 0)
   {
      free (topic);
      return;
   }
   esp_mqtt_client_config_t config = {
      .uri = url,
      .event_handle = mqtt_event_handler,
      .lwt_topic = topic,
      .lwt_qos = 1,
      .lwt_retain = 1,
      .lwt_msg_len = 8,
      .lwt_msg = "0 Failed",
//      .disable_auto_reconnect = true,
   };
   if (*mqttcert[mqtt_index])
      config.cert_pem = mqttcert[mqtt_index];
   if (*mqttuser[mqtt_index])
      config.username = mqttuser[mqtt_index];
   if (*mqttpass[mqtt_index])
      config.password = mqttpass[mqtt_index];
   ESP_LOGI (TAG, "Start the MQTT [%s]", mqtthost[mqtt_index]);
   if (mqtt_client)
      esp_mqtt_client_destroy (mqtt_client);
   mqtt_client = esp_mqtt_client_init (&config);
   free (topic);
   free (url);
}

static esp_err_t
wifi_event_handler (void *ctx, system_event_t * event)
{
   switch (event->event_id)
   {
   case SYSTEM_EVENT_STA_START:
      esp_wifi_connect ();
      break;
   case SYSTEM_EVENT_STA_CONNECTED:
      slow_dhcp = esp_timer_get_time () + 10000000;     // If no DHCP we disconnect WiFi
      if (wifireset)
         esp_phy_erase_cal_data_in_nvs ();      // Lets calibrate on boot
      break;
   case SYSTEM_EVENT_STA_LOST_IP:
      esp_wifi_disconnect ();
      break;
   case SYSTEM_EVENT_STA_GOT_IP:
      slow_dhcp = 0;            // Got IP
      if (wifireset)
         revk_restart (NULL, -1);
      xEventGroupSetBits (revk_group, GROUP_WIFI);
      if (app_command)
         app_command ("wifi", strlen (wifissid[wifi_index]), (unsigned char *) wifissid[wifi_index]);
      sntp_stop ();
      sntp_init ();
      break;
   case SYSTEM_EVENT_STA_DISCONNECTED:
      if (wifireset)
         revk_restart ("WiFi lost", wifireset);
      wifi_next ();
      wifi_count++;
      xEventGroupClearBits (revk_group, GROUP_WIFI);
      esp_wifi_connect ();
      break;
   default:
      break;
   }
   return ESP_OK;
}

static void
revk_task (void *pvParameters)
{                               // Main RevK task
   pvParameters = pvParameters;
   xEventGroupWaitBits (revk_group, GROUP_WIFI, false, true, portMAX_DELAY);
// Start MQTT
   esp_mqtt_client_start (mqtt_client);
// Idle
   while (1)
   {
      sleep (1);
      int64_t now = esp_timer_get_time ();
      if (slow_dhcp && slow_dhcp < now)
      {
         ESP_LOGI (TAG, "Slow DHCP, disconnecting");
         slow_dhcp = 0;
         esp_wifi_disconnect ();
      }
      if (restart_time && restart_time < now && !ota_task_id)
      {                         // Restart
         if (!restart_reason)
            restart_reason = "Unknown";
         revk_state (NULL, "0 %s", restart_reason);
         if (app_command)
            app_command ("restart", strlen (restart_reason), (unsigned char *) restart_reason);
         esp_mqtt_client_stop (mqtt_client);
         ESP_ERROR_CHECK (nvs_commit (nvs));
         sleep (2);             // Wait for MQTT to close cleanly
         esp_restart ();
         restart_time = 0;
      }
      if (nvs_time && nvs_time < now)
      {
         ESP_ERROR_CHECK (nvs_commit (nvs));
         nvs_time = 0;
      }
      {                         // No event for channel change, etc
         static int lastch = 0;
         static uint8_t lastbssid[6];
         wifi_ap_record_t ap = {
         };
         esp_wifi_sta_get_ap_info (&ap);
         if (lastch != ap.primary || memcmp (lastbssid, ap.bssid, 6))
         {
            revk_info (NULL, "WiFi%d(%d) %02X%02X%02X:%02X%02X%02X %s (%ddB) ch%d%s", wifi_index + 1,
                       wifi_count, ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
                       ap.bssid[5], ap.ssid, ap.rssi, ap.primary, ap.country.cc);
            lastch = ap.primary;
            memcpy (lastbssid, ap.bssid, 6);
         }
      }
   }
}


// External functions
void
revk_init (app_command_t * app_command_cb)
{                               // Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID
   ESP_ERROR_CHECK (esp_tls_set_global_ca_store (LECert, sizeof (LECert)));
   const esp_app_desc_t *app = esp_ota_get_app_description ();
   revk_app = app->project_name;
   {
      revk_version = app->version;
      char *d = strstr (revk_version, "dirty");
      if (d)
         asprintf ((char **) &revk_version, "%.*s%s", d - revk_version, app->version, app->time);
   }
   // TODO secure NVS option
   nvs_flash_init ();
   ESP_ERROR_CHECK (nvs_open (revk_app, NVS_READWRITE, &nvs));  // TODO should we open/close on use?
#define s(n,d)		revk_register(#n,0,0,&n,d,0)
#define sa(n,a,d)	revk_register(#n,a,0,&n,d,0)
#define f(n,a,s)	revk_register(#n,a,s,&n,0,SETTING_BINARY)
#define	u32(n,d)	revk_register(#n,0,4,&n,#d,0)
#define	u16(n,a,d)	revk_register(#n,a,2,&n,#d,0)
#define	u8(n,a,d)	revk_register(#n,a,1,&n,#d,0)
#define p(n)	revk_register("prefix"#n,0,0,&prefix##n,#n,0)
   settings;
#undef s
#undef sa
#undef f
#undef u32
#undef u16
#undef u8
#undef p
   restart_time = 0;            // If settings change at start up we can ignore.
   tcpip_adapter_init ();
   sntp_setoperatingmode (SNTP_OPMODE_POLL);
   sntp_setservername (0, ntphost);
   app_command = app_command_cb;
   {                            // Chip ID from MAC
      unsigned char mac[6];
      ESP_ERROR_CHECK (esp_efuse_mac_get_default (mac));
      revk_binid = ((mac[0] << 16) + (mac[1] << 8) + mac[2]) ^ ((mac[3] << 16) + (mac[4] << 8) + mac[5]);
      snprintf (revk_id, sizeof (revk_id), "%06X", revk_binid);
   }
   // MQTT
   mqtt_next ();
   // WiFi
   revk_group = xEventGroupCreate ();
   ESP_ERROR_CHECK (esp_event_loop_init (wifi_event_handler, NULL));
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT ();
   ESP_ERROR_CHECK (esp_wifi_init (&cfg));
   ESP_ERROR_CHECK (esp_wifi_set_storage (WIFI_STORAGE_RAM));
   ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));
   wifi_next ();
   ESP_ERROR_CHECK (esp_wifi_start ());
   char *hostname;
   asprintf (&hostname, "%s-%s", revk_app, revk_id);
   tcpip_adapter_set_hostname (TCPIP_ADAPTER_IF_STA, hostname);
   free (hostname);
   // Start task
   xTaskCreatePinnedToCore (revk_task, "RevK", 16 * 1024, NULL, 1, &revk_task_id, tskNO_AFFINITY);      // TODO stack, priority, affinity check?
}

// MQTT reporting
void
revk_mqtt_ap (const char *prefix, int retain, const char *tag, const char *fmt, va_list ap)
{                               // Send status
   char *topic;
   if (asprintf (&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, revk_app, revk_id, tag) < 0)
      return;
   char *buf;
   int l;
   if ((l = vasprintf (&buf, fmt, ap)) < 0)
   {
      free (topic);
      return;
   }
   ESP_LOGD (TAG, "MQTT publish %s %s", topic ? : "-", buf);
   if (xEventGroupGetBits (revk_group) & GROUP_MQTT)
      esp_mqtt_client_publish (mqtt_client, topic, buf, l, 1, retain);
   free (buf);
   free (topic);
}

void
revk_raw (const char *prefix, const char *tag, int len, uint8_t * data, int retain)
{
   char *topic;
   if (asprintf (&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, revk_app, revk_id, tag) < 0)
      return;
   ESP_LOGD (TAG, "MQTT publish %s (%d)", topic ? : "-", len);
   if (xEventGroupGetBits (revk_group) & GROUP_MQTT)
      esp_mqtt_client_publish (mqtt_client, topic, (const char *) data, len, 1, retain);
   free (topic);
}

void
revk_state (const char *tag, const char *fmt, ...)
{                               // Send status
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt_ap (prefixstate, 1, tag, fmt, ap); // TODo configurable
   va_end (ap);
}

void
revk_event (const char *tag, const char *fmt, ...)
{                               // Send event
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt_ap (prefixevent, 0, tag, fmt, ap); // TODo configurable
   va_end (ap);
}

void
revk_error (const char *tag, const char *fmt, ...)
{                               // Send error
   xEventGroupWaitBits (revk_group, GROUP_WIFI | GROUP_MQTT, false, true, 10000 / portTICK_PERIOD_MS);  // Chance of reporting issues
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt_ap (prefixerror, 0, tag, fmt, ap); // TODo configurable
   va_end (ap);
}

void
revk_info (const char *tag, const char *fmt, ...)
{                               // Send info
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt_ap (prefixinfo, 0, tag, fmt, ap);  // TODo configurable
   va_end (ap);
}

const char *
revk_restart (const char *reason, int delay)
{                               // Restart cleanly
   restart_reason = reason;
   if (delay < 0)
      restart_time = 0;         // Cancelled
   else
      restart_time = esp_timer_get_time () + 1000000LL * delay; // Reboot now
   return "";                   // Done
}

static esp_err_t
ota_handler (esp_http_client_event_t * evt)
{
   static int ota_size = 0;
   static int ota_running = 0;
   static int ota_progress = 0;
   static esp_ota_handle_t ota_handle;
   static const esp_partition_t *ota_partition = NULL;
   switch (evt->event_id)
   {
   case HTTP_EVENT_ERROR:
      break;
   case HTTP_EVENT_ON_CONNECTED:
      ota_size = 0;
      if (ota_running)
         esp_ota_end (ota_handle);
      ota_running = 0;
      break;
   case HTTP_EVENT_HEADER_SENT:
      break;
   case HTTP_EVENT_ON_HEADER:
      if (!strcmp (evt->header_key, "Content-Length"))
         ota_size = atoi (evt->header_value);
      break;
   case HTTP_EVENT_ON_DATA:
      if (ota_size)
      {
         int64_t now = esp_timer_get_time ();
         static int64_t next = 0;
         if (esp_http_client_get_status_code (evt->client) / 100 != 2)
            ota_size = 0;       // Failed
         if (!ota_running && ota_size)
         {                      // Start
            ota_progress = 0;
            if (!ota_partition)
               ota_partition = esp_ota_get_running_partition ();
            ota_partition = esp_ota_get_next_update_partition (ota_partition);
            if (!ota_partition)
            {
               revk_error ("upgrade", "No OTA parition available");     // TODO if running in OTA, boot to factory to allow OTA
               ota_size = 0;
            } else
            {
               if (REVK_ERR_CHECK (esp_ota_begin (ota_partition, ota_size, &ota_handle)))
               {
                  ota_size = 0;
                  ota_partition = NULL;
               } else
               {
                  revk_info ("upgrade", "Loading %d", ota_size);
                  ota_running = 1;
                  next = now + 5000000;
               }
            }
         }
         if (ota_running && ota_size)
         {
            if (REVK_ERR_CHECK (esp_ota_write (ota_handle, evt->data, evt->data_len)))
            {
               ota_size = 0;
            } else
            {
               ota_running += evt->data_len;
               int percent = ota_running * 100 / ota_size;
               if (percent != ota_progress && (percent == 100 || next < now || percent / 10 != ota_progress / 10))
               {
                  revk_info ("upgrade", "%3d%%", ota_progress = percent);
                  next = now + 5000000;
               }
            }
         }
      }
      break;
   case HTTP_EVENT_ON_FINISH:
      if (!ota_running && esp_http_client_get_status_code (evt->client) / 100 > 3)
         revk_error ("Upgrade", "Failed to start %d (%d)", esp_http_client_get_status_code (evt->client), ota_size);
      if (ota_running)
      {
         if (!REVK_ERR_CHECK (esp_ota_end (ota_handle)))
         {
            revk_info ("upgrade", "Updated %s %d", ota_partition->label, ota_running - 1);
            esp_ota_set_boot_partition (ota_partition);
            revk_restart ("OTA", 0);
         }
      }
      ota_running = 0;
      break;
   case HTTP_EVENT_DISCONNECTED:
      break;
   }
   return ESP_OK;
}

static void
ota_task (void *pvParameters)
{
   char *url = pvParameters;
   revk_info ("upgrade", "%s", url);
   esp_http_client_config_t config = {
      .url = url,.event_handler = ota_handler,
   };
   if (*otacert)
      config.cert_pem = otacert;        // Pinned cert
   else
      config.use_global_ca_store = true;        // Global cert
   esp_http_client_handle_t client = esp_http_client_init (&config);
   if (!client)
      revk_error ("upgrade", "HTTP client failed");
   else
   {
      esp_err_t err = REVK_ERR_CHECK (esp_http_client_perform (client));
      int status = esp_http_client_get_status_code (client);
      esp_http_client_cleanup (client);
      free (url);
      if (!err && status / 100 != 2)
         revk_error ("upgrade", "HTTP code %d", status);
   }
   ota_task_id = NULL;
   vTaskDelete (NULL);
}

const char *
revk_ota (const char *url)
{                               // OTA and restart cleanly
   if (ota_task_id)
      return "OTA running";
   xTaskCreatePinnedToCore (ota_task, "OTA", 16 * 1024, (char *) url, 1, &ota_task_id, tskNO_AFFINITY); // TODO stack, priority, affinity check?
   return "";
}

static int
nvs_get (setting_t * s, const char *tag, void *data, size_t len)
{                               // Low level get logic, returns <0 if error. Calls the right nvs get function for type of setting
   esp_err_t err;
   if (s->flags & SETTING_BINARY)
   {
      if ((err = nvs_get_blob (nvs, tag, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   if (s->size == 0)
   {                            // String
      if ((err = nvs_get_str (nvs, tag, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   uint64_t temp;
   if (!data)
      data = &temp;
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
      {                         // int64
         if ((err = nvs_get_i64 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         // int32
         if ((err = nvs_get_i32 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         // int32
         if ((err = nvs_get_i16 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         // int8
         if ((err = nvs_get_i8 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   } else
   {
      if (s->size == 8)
      {                         // uint64
         if ((err = nvs_get_u64 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         // uint32
         if ((err = nvs_get_u32 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         // uint32
         if ((err = nvs_get_u16 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         // uint8
         if ((err = nvs_get_u8 (nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   }
   return -999;
}

static esp_err_t
nvs_set (setting_t * s, const char *tag, void *data)
{                               // Low level get logic, returns <0 if error. Calls the right nvs get function for type of setting
   if (s->flags & SETTING_BINARY)
   {
      if (s->size)
         return nvs_set_blob (nvs, tag, data, s->size); // Fixed
      return nvs_set_blob (nvs, tag, data, 1 + *((unsigned char *) data));      // Variable
   }
   if (s->size == 0)
   {
      ESP_LOGD (TAG, "Written %s=%s", tag, (char *) data);
      return nvs_set_str (nvs, tag, data);
   }
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
         return nvs_set_i64 (nvs, tag, *((int64_t *) data));
      if (s->size == 4)
         return nvs_set_i32 (nvs, tag, *((int32_t *) data));
      if (s->size == 2)
         return nvs_set_i16 (nvs, tag, *((int16_t *) data));
      if (s->size == 1)
         return nvs_set_i8 (nvs, tag, *((int8_t *) data));
   } else
   {
      if (s->size == 8)
         return nvs_set_u64 (nvs, tag, *((uint64_t *) data));
      if (s->size == 4)
         return nvs_set_u32 (nvs, tag, *((uint32_t *) data));
      if (s->size == 2)
         return nvs_set_u16 (nvs, tag, *((uint16_t *) data));
      if (s->size == 1)
         return nvs_set_u8 (nvs, tag, *((uint8_t *) data));
   }
   return -1;
}

static const char *
revk_setting_internal (setting_t * s, unsigned int len, const unsigned char *value, unsigned char index, unsigned char flags)
{
   flags |= s->flags;
   void *data = s->data;
   if (s->array)
   {
      if (index > s->array)
         return "Bad index";
      if (s->array && index > 1 && !(flags & SETTING_BOOLEAN))
         data += (index - 1) * (s->size ? : sizeof (void *));
   }
   if (!value)
      value = (const unsigned char *) "";
   char tag[16];                // Max NVS name size
   if (snprintf (tag, sizeof (tag), s->array ? "%s%u" : "%s", s->name, index ? : 1) >= sizeof (tag))
      return "Setting name too long";
   ESP_LOGD (TAG, "MQTT setting %s (%d)", tag, len);
   char erase = 0;              // Using default, so remove from flash (as defaults may change later, don't store the default in flash)
   if (!len && s->defval && !(flags & SETTING_BITFIELD) && index <= 1)
   {                            // Use default value
      len = strlen (s->defval);
      value = (const unsigned char *) s->defval;
      if (flags & SETTING_BINARY)
         flags |= SETTING_HEX;
      erase = 1;
   }
   // Parse new setting
   unsigned char *n = NULL;
   int l = 0;
   if (flags & SETTING_HEX)
   {                            // Count length
      int p = 0;
      while (p < len)
      {                         // get hex length
         if (!isxdigit (value[p]))
            break;
         p++;
         if (p < len && isxdigit (value[p]))
            p++;                // Second hex digit in byte
         if (p < len && !isalnum (value[p]))
            p++;                // Separator
         l++;
      }
   } else
      l = len;
   if (flags & SETTING_BINARY)
   {                            // Blob
      unsigned char *o;
      if (!s->size)
      {                         // Dynamic
         if (l > 255)
            return "Data too long";
         o = n = malloc (l + 1);        // One byte for length
         *o++ = l;
         l++;
      } else if (l && l != s->size)
         return "Wrong size";
      else
      {
         o = n = malloc (s->size);
         if (!l)
            memset (n, 0, s->size);     // Default
      }
      if (l)
      {
         if (flags & SETTING_HEX)
         {                      // hex
            int p = 0;
            while (p < len)
            {                   // store hex length
               int v = (isalpha (value[p]) ? 9 : 0) + (value[p] & 15);
               p++;
               if (p < len && isxdigit (value[p]))
               {
                  v = v * 16 + (isalpha (value[p]) ? 9 : 0) + (value[p] & 15);
                  p++;          // Second hex digit in byte
               }
               *o++ = v;
               if (p < len && !isalnum (value[p]))
                  p++;          // Separator
            }
         } else
            memcpy (o, value, len);     // Binary
      }
   } else if (!s->size)
   {                            // String
      n = malloc (len + 1);     // One byte for null termination
      if (len)
         memcpy (n, value, len);
      n[len] = 0;
      l = len + 1;
   } else
   {                            // Numeric
      uint64_t v = 0;
      if (flags & SETTING_BOOLEAN)
      {                         // Boolean
         if (s->size == 1)
            v = *(uint8_t *) data;
         else if (s->size == 2)
            v = *(uint16_t *) data;
         else if (s->size == 4)
            v = *(uint32_t *) data;
         else if (s->size == 8)
            v = *(uint64_t *) data;
         if (len && strchr ("YytT1", *value))
         {
            if (s->array && index)
               v |= (1ULL << (index - 1));
            else
               v |= 1;
         } else
         {
            if (s->array && index)
               v &= ~(1ULL << (index - 1));
            else
               v &= ~1;
         }
      } else
      {
         char neg = 0;
         int bits = s->size * 8;
         uint64_t bitfield = 0;
         if (flags & SETTING_SET)
         {                      // Set top bit if a value is present
            bits--;
            if (len && value != (const unsigned char *) s->defval)
               bitfield |= (1ULL << bits);      // Value is set (not so if using default value)
         }
         if (flags & SETTING_BITFIELD && s->defval)
         {                      // Bit fields
            while (len)
            {
               char *c = strchr (s->defval, *value);
               if (!c)
                  break;
               uint64_t m = (1ULL << (bits - 1 - (c - s->defval)));
               if (bitfield & m)
                  break;
               bitfield |= m;
               len--;
               value++;
            }
            bits -= strlen (s->defval);
         }
         if (len && bits <= 0)
            return "Extra data on end";
         if (len > 2 && *value == '0' && value[1] == 'x')
         {
            flags |= SETTING_HEX;
            len -= 2;
            value += 2;
         }
         if (len && *value == '-' && (flags & SETTING_SIGNED))
         {                      // Decimal
            len--;
            value++;
            neg = 1;
         }
         if (flags & SETTING_HEX)
            while (len && isxdigit (*value))
            {                   // Hex
               uint64_t n = v * 16 + (isalpha (*value) ? 9 : 0) + (*value & 15);
               if (n < v)
                  return "Silly number";
               v = n;
               value++;
               len--;
         } else
            while (len && isdigit (*value))
            {
               uint64_t n = v * 10 + (*value++ - '0');
               if (n < v)
                  return "Silly number";
               v = n;
               len--;
            }
         if (len)
            return "Bad number";
         if (flags & SETTING_SIGNED)
            bits--;
         if (bits < 0 || (bits < 64 && ((v - (v && neg ? 1 : 0)) >> bits)))
            return "Number too big";
         if (neg)
            v = -v;
         if (flags & SETTING_SIGNED)
            bits++;
         if (bits < 64)
            v &= (1ULL << bits) - 1;
         v |= bitfield;
      }
      if (flags & SETTING_SIGNED)
      {
         if (s->size == 8)
            *((int64_t *) (n = malloc (l = 8))) = v;
         else if (s->size == 4)
            *((int32_t *) (n = malloc (l = 4))) = v;
         else if (s->size == 2)
            *((int16_t *) (n = malloc (l = 2))) = v;
         else if (s->size == 1)
            *((int8_t *) (n = malloc (l = 1))) = v;
      } else
      {
         if (s->size == 8)
            *((int64_t *) (n = malloc (l = 8))) = v;
         else if (s->size == 4)
            *((int32_t *) (n = malloc (l = 4))) = v;
         else if (s->size == 2)
            *((int16_t *) (n = malloc (l = 2))) = v;
         else if (s->size == 1)
            *((int8_t *) (n = malloc (l = 1))) = v;
      }
   }
   if (!n)
      return "Bad setting type";
   // See if setting has changed
   int o = nvs_get (s, tag, NULL, 0);
   if (o != l)
      o = -1;                   // Different size
   if (o > 0)
   {
      void *d = malloc (l);
      if (nvs_get (s, tag, d, l) != o)
      {
         free (n);
         free (d);
         return "Bad setting get";
      }
      if (memcmp (n, d, o))
         o = -1;                // Different content
      free (d);
   }
   if (o < 0)
   {                            // Flash changed
      if (erase)
         nvs_erase_key (nvs, tag);
      else if (nvs_set (s, tag, n) != ERR_OK && (nvs_erase_key (nvs, tag) != ERR_OK || nvs_set (s, tag, n) != ERR_OK))
      {
         free (n);
         return "Unable to store";
      }
      if (flags & SETTING_BINARY)
         ESP_LOGD (TAG, "Setting %s changed (%d)", tag, len);
      else
         ESP_LOGD (TAG, "Setting %s changed %.*s", tag, len, value);
      nvs_time = esp_timer_get_time () + 60000000;
   }
   if (flags & SETTING_LIVE)
   {                            // Store changed value in memory live
      if (!s->size)
      {                         // Dynamic
         void *o = *((void **) data);
         // See if different?
         if (!o || ((flags & SETTING_BINARY) ? memcmp (o, n, 1 + *(uint8_t *) o) : strcmp (o, (char *) n)))
         {
            *((void **) data) = n;
            if (o)
               free (o);
         } else
            free (n);           // No change
      } else
      {                         // Static (try and make update atomic)
         if (s->size == 1)
            *(uint8_t *) data = *(uint8_t *) n;
         else if (s->size == 2)
            *(uint16_t *) data = *(uint16_t *) n;
         else if (s->size == 4)
            *(uint32_t *) data = *(uint32_t *) n;
         else if (s->size == 8)
            *(uint64_t *) data = *(uint64_t *) n;
         else
            memcpy (data, n, s->size);
         free (n);
      }
   } else if (o < 0)
      revk_restart ("Settings changed", 5);
   return NULL;                 // OK
}

const char *
revk_setting (const char *tag, unsigned int len, const unsigned char *value)
{
   unsigned char flags = 0;
   if (*tag == '0' && tag[1] == 'x')
   {                            // Store hex
      flags |= SETTING_HEX;
      tag += 2;
   }
   int index = 0;
   int match (setting_t * s)
   {
      const char *a = s->name;
      const char *b = tag;
      while (*a && *a == *b)
      {
         a++;
         b++;
      }
      if (*a)
         return 1;              // not matched whole name, no match
      if (!*b)
         return 0;              // Match, no index
      if (!s->array && *b)
         return 2;              // not array, and more characters, no match
      int v = 0;
      while (isdigit ((int) (*b)))
         v = v * 10 + (*b++) - '0';
      if (*b)
         return 3;              // More on end after any digits, no match
      if (!v || v > s->array)
         return 4;              // Invalid index, no match
      index = v;
      return 0;                 // Match, index
   }
   setting_t *s;
   for (s = setting; s && match (s); s = s->next);
   if (!s)
      return "Unknown setting";
   return revk_setting_internal (s, len, value, index, flags);
}

const char *
revk_command (const char *tag, unsigned int len, const unsigned char *value)
{
   ESP_LOGD (TAG, "MQTT command [%s]", tag);
   const char *e = NULL;
   // My commands
   if (!e && !strcmp (tag, "upgrade"))
   {
      char *url;                // TODO, yeh, not freed, but we are rebooting
      if (len && !strncmp ((char *) value, "https://", 8))
         url = strdup ((char *) value);
      else
         asprintf (&url, "https://%s/%s.bin", len ? (char *) value : otahost, revk_app);
      e = revk_ota (url);
   }
   if (!e && !strcmp (tag, "restart"))
      e = revk_restart ("Restart command", 0);
   if (!e && !strcmp (tag, "factory") && len == strlen (revk_id) + strlen (revk_app)
       && !strncmp ((char *) value, revk_id, strlen (revk_id)) && !strcmp ((char *) value + strlen (revk_id), revk_app))
   {
      setting_t *s;
      for (s = setting; s; s = s->next)
         nvs_erase_key (nvs, s->name);
      revk_restart ("Factory reset", 0);
   }
   // App commands
   if (!e && app_command)
      e = app_command (tag, len, value);
   return e;
}

void
revk_register (const char *name, unsigned char array, unsigned char size, void *data, const char *defval, unsigned char flags)
{                               // Register setting (not expected to be thread safe, should be called from init)
   if (flags & SETTING_BITFIELD && !defval)
      ESP_LOGE (TAG, "%s missing defval on bitfield", name);
   else if (flags & SETTING_BITFIELD && !size)
      ESP_LOGE (TAG, "%s missing size on bitfield", name);
   else if (flags & SETTING_BITFIELD && strlen (defval) > 8 * size)
      ESP_LOGE (TAG, "%s too small for bitfield", name);
   // TODO other checks, maybe as asserts
   setting_t *s;
   for (s = setting; s && strcmp (s->name, name); s = s->next);
   if (s)
      ESP_LOGE (TAG, "%s duplicate", name);
   s = malloc (sizeof (*s));
   s->name = name;
   s->array = array;
   s->size = size;
   s->data = data;
   s->flags = flags;
   s->defval = defval;
   s->next = setting;
   setting = s;
   memset (data, 0, (size ? : sizeof (void *)) * (!(flags & SETTING_BOOLEAN) && array ? array : 1));    // Initialise memory
   // Get value
   int get_val (const char *tag, int index)
   {
      void *data = s->data;
      if (s->array && index > 1 && !(flags & SETTING_BOOLEAN))
         data += (s->size ? : sizeof (void *)) * (index - 1);
      int l = -1;
      if (!s->size)
      {                         // Dynamic
         void *d = NULL;
         l = nvs_get (s, tag, NULL, 0);
         if (l > 1)
         {                      // 1 byte means zero len or zero terminated so use default
            d = malloc (l);
            l = nvs_get (s, tag, d, l);
            if (l > 0)
               *((void **) data) = d;
            else
               free (d);        // Should not happen
         } else
            l = -1;             // default
      } else
         l = nvs_get (s, tag, data, s->size);   // Stored static
      return l;
   }
   const char *e;
   if (array)
   {                            // Work through tags
      int i;
      for (i = 1; i <= array; i++)
      {
         char tag[16];          // NVS tag size
         if (snprintf (tag, sizeof (tag), "%s%u", s->name, i) < sizeof (tag) && get_val (tag, i) < 0)
         {
            e = revk_setting_internal (s, 0, NULL, i, SETTING_LIVE);    // Defaulting logic
            if (e && *e)
               ESP_LOGE (TAG, "Setting %s failed %s", tag, e);
            else
               ESP_LOGD (TAG, "Setting %s created", tag);
         }
      }
   } else                       // Simple setting, not array
   if (get_val (s->name, 0) < 0)
   {
      e = revk_setting_internal (s, 0, NULL, 0, SETTING_LIVE);  // Defaulting logic
      if (e && *e)
         ESP_LOGE (TAG, "Setting %s failed %s", s->name, e);
      else
         ESP_LOGD (TAG, "Setting %s created", s->name);
   }
}

esp_err_t
revk_err_check (esp_err_t e, const char *file, int line)
{
   if (e != ERR_OK)
      revk_error ("error", "Error at line %s in %s (%s)", line, file, esp_err_to_name (e));
   return e;
}
