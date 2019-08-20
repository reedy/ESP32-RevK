// Main control code, working with WiFi, MQTT, and managing settings and OTA

#include "revk.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "lecert.h"

#define	settings	\
		s(otahost);             \
		f(otasha1,20);          \
		n(wifireset,300);       \
		s(wifissid);            \
		f(wifibssid,6);         \
		n(wifichan,0);          \
		s(wifipass);            \
		n(mqttreset,0);         \
		s(mqtthost);            \
		f(mqttsha1,20);         \
		s(mqttuser);            \
		s(mqttpass);            \
		s(mqttport);            \
		s(ntphost);             \
		s(prefixcommand);       \
		s(prefixsetting);       \
		s(prefixstate);         \
		s(prefixevent);         \
		s(prefixinfo);          \
		s(prefixerror);         \

#define s(n)	char *n=NULL;
#define f(n,s)	char n[s]={};
#define	n(n,d)	int n=d;
settings
#undef s
#undef f
#undef n
// Local types
typedef struct setting_s setting_t;
struct setting_s
{
   setting_t *next;
   const char *name;
   int64_t defval;
   void *data;
   signed char size;
   unsigned char array;
   unsigned char flags;
};

// Public
const char *revk_app = "";
const char *revk_version = "";  // ISO date version
char revk_id[7];                // Chip ID as hex

// Local
static TaskHandle_t revk_task_id = NULL;
static TaskHandle_t ota_task_id = NULL;
static app_command_t *app_command = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static int64_t restart_time = 0;
static int64_t nvs_time = 0;
static const char *restart_reason = "Unknown";
static nvs_handle nvs = -1;
static setting_t *setting = NULL;

// Local functions
static const char *TAG = "RevK";
static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;
static esp_err_t
wifi_event_handler (void *ctx, system_event_t * event)
{
   switch (event->event_id)
   {
   case SYSTEM_EVENT_STA_START:
      esp_wifi_connect ();
      break;
   case SYSTEM_EVENT_STA_GOT_IP:
      xEventGroupSetBits (wifi_event_group, CONNECTED_BIT);
      break;
   case SYSTEM_EVENT_STA_DISCONNECTED:
      esp_wifi_connect ();
      xEventGroupClearBits (wifi_event_group, CONNECTED_BIT);
      break;
   default:
      break;
   }
   return ESP_OK;
}

static esp_err_t
mqtt_event_handler (esp_mqtt_event_handle_t event)
{
   esp_mqtt_client_handle_t mqtt_client = event->client;
   // your_context_t *context = event->context;
   switch (event->event_id)
   {
   case MQTT_EVENT_CONNECTED:
      ESP_LOGI (TAG, "MQTT_EVENT_CONNECTED");
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
      revk_status (NULL, "1 %s", revk_version); // Up
      // Info
      const esp_partition_t *p = esp_ota_get_running_partition ();
      wifi_ap_record_t ap = { };
      esp_wifi_sta_get_ap_info (&ap);
      revk_info (NULL, "Running %s WiFi %02X%02X%02X:%02X%02X%02X %s (%ddB) ch%d", p->label, ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.ssid, ap.rssi, ap.primary);
      // TODO app command
      break;
      // TODO trim
   case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI (TAG, "MQTT_EVENT_DISCONNECTED");
      // TODO app command
      break;
   case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI (TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      break;
   case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGI (TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
      break;
   case MQTT_EVENT_PUBLISHED:
      ESP_LOGI (TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
      break;
   case MQTT_EVENT_DATA:
      {
         const char *e = NULL;
         ESP_LOGI (TAG, "MQTT_EVENT_DATA");
         int p;
         for (p = event->topic_len; p && event->topic[p - 1] != '/'; p--);
         char *tag = malloc (event->topic_len + 1 - p);
         memcpy (tag, event->topic + p, event->topic_len - p);
         tag[event->topic_len - p] = 0;
         for (p = 0; p < event->topic_len && event->topic[p] != '/'; p++);
         char *value = malloc (event->data_len + 1);
         memcpy (value, event->data, event->data_len + 1);
         value[event->data_len] = 0;
         if (p == 7 && !memcmp (event->topic, prefixcommand, p))
            e = revk_command (tag, event->data_len, (const unsigned char *) event->data);
         else if (p == 7 && !memcmp (event->topic, "setting", p))       // TODo configurable
            e = (revk_setting (tag, event->data_len, (const unsigned char *) event->data) ? : "");      // Returns NULL if OK
         else
            e = "";
         if (!e || *e)
            revk_error (tag, "Failed %s (%.*s)", e ? : "Unknown", event->data_len, event->data);
         free (tag);
         free (value);
      }
      break;
   case MQTT_EVENT_ERROR:
      ESP_LOGI (TAG, "MQTT_EVENT_ERROR");
      break;
   default:
      ESP_LOGI (TAG, "Other event id:%d", event->event_id);
      break;
   }
   return ESP_OK;
}

static void
revk_task (void *pvParameters)
{                               // Main RevK task
   pvParameters = pvParameters;
   // WiFi
   wifi_event_group = xEventGroupCreate ();
   ESP_ERROR_CHECK (esp_event_loop_init (wifi_event_handler, NULL));
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT ();
   ESP_ERROR_CHECK (esp_wifi_init (&cfg));
   ESP_ERROR_CHECK (esp_wifi_set_storage (WIFI_STORAGE_RAM));
   wifi_config_t wifi_config = { };
   strncpy ((char *) wifi_config.sta.ssid, wifissid, sizeof (wifi_config.sta.ssid));
   strncpy ((char *) wifi_config.sta.password, wifipass, sizeof (wifi_config.sta.password));
   ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));
   ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_STA, &wifi_config));
   ESP_LOGI (TAG, "start the WIFI SSID:[%s]", wifissid);
   ESP_ERROR_CHECK (esp_wifi_start ());
   xEventGroupWaitBits (wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
   // Start MQTT
   esp_mqtt_client_start (mqtt_client);
   // Idle
   while (1)
   {
      sleep (1);
      int64_t now = esp_timer_get_time ();
      if (restart_time && restart_time < now)
      {                         // Restart
         revk_status (NULL, "0 %s", restart_reason);
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
   }
}


// External functions
void
revk_init (app_command_t * app_command_cb)
{                               // Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID
	ESP_ERROR_CHECK(esp_tls_set_global_ca_store(LECert,sizeof(LECert)));
   const esp_app_desc_t *app = esp_ota_get_app_description ();
   revk_app = app->project_name;
   {
      revk_version = app->version;
      char *d = strstr (revk_version, "dirty");
      if (d)
         asprintf ((char **) &revk_version, "%.*s%s", d - revk_version, app->version, app->time);
   }
   // TODO maybe compile date/time would be better?
   // TODO secure NVS option
   nvs_flash_init ();
   ESP_ERROR_CHECK (nvs_open (revk_app, NVS_READWRITE, &nvs));
#define s(n)	revk_register(#n,0,0,&n,0,SETTING_REBOOT)
#define f(n,s)	revk_register(#n,0,s,&n,0,SETTING_REBOOT|SETTING_BINARY)
#define	n(n,d)	revk_register(#n,0,4,&n,d,SETTING_REBOOT)
   settings;
#undef s
#undef f
#undef n
   // some default settings
   if (!*prefixcommand)
      prefixcommand = "command";
   if (!*prefixsetting)
      prefixsetting = "setting";
   if (!*prefixerror)
      prefixerror = "error";
   if (!*prefixinfo)
      prefixinfo = "info";
   if (!*prefixevent)
      prefixevent = "event";
   if (!*prefixstate)
      prefixstate = "state";
   if (!*otahost)
      otahost = "ota.revk.uk";
   if (!*mqtthost)
      mqtthost = "mqtt.iot";
   if (!*wifissid && !wifipass)
      wifipass = "security";
   if (!*wifissid)
      wifissid = "IoT";
   restart_time = 0;            // If settings change at start up we can ignore.
   tcpip_adapter_init ();
   app_command = app_command_cb;
   {                            // Chip ID from MAC
      unsigned char mac[6];
      ESP_ERROR_CHECK (esp_efuse_mac_get_default (mac));
      snprintf (revk_id, sizeof (revk_id), "%02X%02X%02X", mac[0] ^ mac[3], mac[1] ^ mac[4], mac[2] ^ mac[5]);
   }
   // MQTT
   char *topic;
   if (asprintf (&topic, "status/%s/%s", revk_app, revk_id) < 0)
      return;
   char *url;
   if (asprintf (&url, "mqtt://%s/", mqtthost) < 0)
   {
      free (topic);
      return;
   }
   const esp_mqtt_client_config_t mqtt_cfg = {
      .uri = url,
      .event_handle = mqtt_event_handler,
      .lwt_topic = topic,
      .lwt_qos = 1,
      .lwt_retain = 1,
      .lwt_msg_len = 8,
      .lwt_msg = "0 Failed",
   };
   ESP_LOGI (TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size ());
   mqtt_client = esp_mqtt_client_init (&mqtt_cfg);
   // TODO cert pinning
   // Start task
   xTaskCreatePinnedToCore (revk_task, "RevK", 16 * 1024, NULL, 1, &revk_task_id, tskNO_AFFINITY);      // TODO stack, priority, affinity check?
}

// MQTT reporting
void
revk_mqtt (const char *prefix, int retain, const char *tag, const char *fmt, va_list ap)
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
   ESP_LOGI (TAG, "MQTT publish %s %s", topic ? : "-", buf);
   esp_mqtt_client_publish (mqtt_client, topic, buf, l, 1, retain);
   free (buf);
   free (topic);
}

void
revk_status (const char *tag, const char *fmt, ...)
{                               // Send status
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt ("status", 1, tag, fmt, ap);       // TODo configurable
   va_end (ap);
}

void
revk_event (const char *tag, const char *fmt, ...)
{                               // Send event
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt ("event", 0, tag, fmt, ap);        // TODo configurable
   va_end (ap);
}

void
revk_error (const char *tag, const char *fmt, ...)
{                               // Send error
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt ("error", 0, tag, fmt, ap);        // TODo configurable
   va_end (ap);
}

void
revk_info (const char *tag, const char *fmt, ...)
{                               // Send info
   va_list ap;
   va_start (ap, fmt);
   revk_mqtt ("info", 0, tag, fmt, ap); // TODo configurable
   va_end (ap);
}

const char *
revk_restart (const char *reason, int delay)
{                               // Restart cleanly
   restart_reason = reason;
   restart_time = esp_timer_get_time () + 1000000LL * delay;    // Reboot now
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
      ESP_LOGI (TAG, "HTTP_EVENT_ERROR");
      break;
   case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGI (TAG, "HTTP_EVENT_ON_CONNECTED");
      ota_size = 0;
      if (ota_running)
         esp_ota_end (ota_handle);
      ota_running = 0;
      break;
   case HTTP_EVENT_HEADER_SENT:
      ESP_LOGI (TAG, "HTTP_EVENT_HEADER_SENT");
      break;
   case HTTP_EVENT_ON_HEADER:
      ESP_LOGI (TAG, "HTTP_HEADER %s: %s", evt->header_key, evt->header_value);
      if (!strcmp (evt->header_key, "Content-Length"))
         ota_size = atoi (evt->header_value);
      break;
   case HTTP_EVENT_ON_DATA:
      //ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      if (!ota_running && ota_size && esp_http_client_get_status_code (evt->client) / 100 == 2)
      {                         // Start
         ota_progress = 0;
         if (!ota_partition)
            ota_partition = esp_ota_get_running_partition ();
         ota_partition = esp_ota_get_next_update_partition (ota_partition);
         esp_err_t err = esp_ota_begin (ota_partition, ota_size, &ota_handle);
         if (err != ERR_OK)
            revk_error ("upgrade", "Error %s", esp_err_to_name (err));
         else
         {
            revk_info ("upgrade", "Loading %d", ota_size);
            ota_running = 1;
         }
      }
      if (ota_running)
      {
         esp_ota_write (ota_handle, evt->data, evt->data_len);
         ota_running += evt->data_len;
         int percent = ota_running * 100 / ota_size;
         if (percent / 10 != ota_progress / 10)
            revk_info ("upgrade", "%3d%%", ota_progress = percent);
      }
      break;
   case HTTP_EVENT_ON_FINISH:
      ESP_LOGI (TAG, "HTTP_EVENT_ON_FINISH");
      if (!ota_running && esp_http_client_get_status_code (evt->client) / 100 > 3)
         revk_error ("Upgrade", "Failed to start %d (%d)", esp_http_client_get_status_code (evt->client), ota_size);
      if (ota_running)
      {
         esp_err_t err = esp_ota_end (ota_handle);
         if (err == ERR_OK)
         {
            revk_info ("upgrade", "Updated %s %d", ota_partition->label, ota_running - 1);
            esp_ota_set_boot_partition (ota_partition);
         } else
            revk_error ("upgrade", "Error %s", esp_err_to_name (err));
      }
      ota_running = 0;
      break;
   case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI (TAG, "HTTP_EVENT_DISCONNECTED");
      break;
   }
   return ESP_OK;
}

static void
ota_task (void *pvParameters)
{
   const char *host = pvParameters;
   char *url;
   if (asprintf (&url, "https://%s/%s.bin", host, revk_app) < 0)
   {                            // Should not happen
      ota_task_id = NULL;
      vTaskDelete (NULL);
      return;
   }
   revk_info ("upgrade", "%s", url);
   // TODO LE cert golbal and using HTTPS
   // TODO cert pinning
   esp_http_client_config_t config = {
      .url = url,
      .event_handler = ota_handler,
   };
   esp_http_client_handle_t client = esp_http_client_init (&config);
   esp_err_t err = esp_http_client_perform (client);
   int status = esp_http_client_get_status_code (client);
   esp_http_client_cleanup (client);
   free (url);
   if (err != ERR_OK)
      revk_error ("upgrade", "Error %s", esp_err_to_name (err));
   else if (status / 100 != 2)
      revk_error ("upgrade", "Failed %d", status);
   else
      revk_restart ("OTA", 0);
   ota_task_id = NULL;
   vTaskDelete (NULL);
}

const char *
revk_ota (const char *host)
{                               // OTA and restart cleanly
   if (ota_task_id)
      return "OTA running";
   xTaskCreatePinnedToCore (ota_task, "OTA", 16 * 1024, (char *) host, 1, &ota_task_id, tskNO_AFFINITY);        // TODO stack, priority, affinity check?
   return "";
}

static int
nvs_get (setting_t * s, void *data, size_t len)
{                               // Low level get logic, returns <0 if error. Calls the right nvs get function for type of setting
   esp_err_t err;
   if (s->flags & SETTING_BINARY)
   {
      if ((err = nvs_get_blob (nvs, s->name, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   if (s->size == 0)
   {                            // String
      if ((err = nvs_get_str (nvs, s->name, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   uint64_t temp;
   if (!data)
      data = &temp;
   if (s->size == -8)
   {                            // int64
      if ((err = nvs_get_i64 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 8;
   }
   if (s->size == -4)
   {                            // int32
      if ((err = nvs_get_i32 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 4;
   }
   if (s->size == -2)
   {                            // int32
      if ((err = nvs_get_i16 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 2;
   }
   if (s->size == -1)
   {                            // int8
      if ((err = nvs_get_i8 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 1;
   }
   if (s->size == 8)
   {                            // uint64
      if ((err = nvs_get_u64 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 8;
   }
   if (s->size == 4)
   {                            // uint32
      if ((err = nvs_get_u32 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 4;
   }
   if (s->size == 2)
   {                            // uint32
      if ((err = nvs_get_u16 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 2;
   }
   if (s->size == 1)
   {                            // uint8
      if ((err = nvs_get_u8 (nvs, s->name, data)) != ERR_OK)
         return -err;
      return 1;
   }
   return -999;
}

static esp_err_t
nvs_set (setting_t * s, void *data)
{                               // Low level get logic, returns <0 if error. Calls the right nvs get function for type of setting
   if (s->flags & SETTING_BINARY)
   {
      if (s->size)
         return nvs_set_blob (nvs, s->name, data, s->size);     // Fixed
      return nvs_set_blob (nvs, s->name, data, 1 + *((unsigned char *) data));  // Variable
   }
   if (s->size == 0)
   {
      ESP_LOGI (TAG, "Written %s=%s", s->name, (char *) data);
      return nvs_set_str (nvs, s->name, data);
   }
   if (s->size == -8)
      return nvs_set_i64 (nvs, s->name, *((int64_t *) data));
   if (s->size == -4)
      return nvs_set_i32 (nvs, s->name, *((int32_t *) data));
   if (s->size == -2)
      return nvs_set_i16 (nvs, s->name, *((int16_t *) data));
   if (s->size == -1)
      return nvs_set_i8 (nvs, s->name, *((int8_t *) data));
   if (s->size == 8)
      return nvs_set_u64 (nvs, s->name, *((uint64_t *) data));
   if (s->size == 4)
      return nvs_set_u32 (nvs, s->name, *((uint32_t *) data));
   if (s->size == 2)
      return nvs_set_u16 (nvs, s->name, *((uint16_t *) data));
   if (s->size == 1)
      return nvs_set_u8 (nvs, s->name, *((uint8_t *) data));
   return -1;
}

const char *
revk_setting (const char *tag, unsigned int len, const unsigned char *value)
{
   if (!value)
      value = (unsigned char *) "";
   ESP_LOGI (TAG, "MQTT setting %s (%d)", tag, len);
   // Find setting in registered settings
   setting_t *s;
   for (s = setting; s && strcmp (s->name, tag); s = s->next);
   if (!s)
      return "No such setting";
   // Parse new setting
   unsigned char *n = NULL;
   int l = 0;
   if (s->flags & SETTING_BINARY)
   {                            // Blog
      if (s->size && len && len != s->size)
         return "Wrong size";
      if (s->size)
      {
         n = malloc (s->size);
         if (value)
            memcpy (n, value, l = s->size);
         memset (n, s->defval, s->size);        // Assumed byte
         l = len;
      } else
      {                         // Dynamic size
         n = malloc (len + 1);
         if (len)
            memcpy (n + 1, value, len);
         *n = len;
         l = len + 1;
      }
   } else if (!s->size)
   {                            // String
      n = malloc (len + 1);
      if (len)
         memcpy (n, value, len);
      n[len] = 0;
      l = len + 1;
   } else
   {                            // Numeric
      uint64_t v = 0;
      char neg = 0;
      if (len && *value == '-')
      {
         len--;
         value++;
         neg = 1;
      }
      if (!len)
         v = s->defval;
      else
         while (len && isdigit (*value))
         {
            v = v * 10 + (*value++ - '0');
            len--;
         }
      if (s->size > 0 && (s->flags & SETTING_POLARITY))
         v |= (1ULL << (s->size * 8 - 1));      // Set top bit
      else if (s->size < 0 && neg)
         v = -v;
      if (s->size == -8)
         *((int64_t *) (n = malloc (l = 8))) = v;
      else if (s->size == -4)
         *((int32_t *) (n = malloc (l = 4))) = v;
      else if (s->size == -2)
         *((int16_t *) (n = malloc (l = 2))) = v;
      else if (s->size == -1)
         *((int8_t *) (n = malloc (l = 1))) = v;
      else if (s->size == 8)
         *((int64_t *) (n = malloc (l = 8))) = v;
      else if (s->size == 4)
         *((int32_t *) (n = malloc (l = 4))) = v;
      else if (s->size == 2)
         *((int32_t *) (n = malloc (l = 2))) = v;
      else if (s->size == 1)
         *((int8_t *) (n = malloc (l = 1))) = v;
   }
   if (!n)
      return "Bad setting type";
   // See if setting has changed
   int o = nvs_get (s, NULL, 0);
   if (o != l)
      o = -1;                   // Different size
   if (o > 0)
   {
      void *d = malloc (l);
      if (nvs_get (s, d, l) < 0)
      {
         free (d);
         return "Bad setting get";
      }
      if (memcmp (n, d, o))
         o = -1;                // Different content
      free (d);
   }
   if (o >= 0)
      return NULL;              // No change
   // Save in flash
   if (nvs_set (s, n) != ERR_OK && (nvs_erase_key (nvs, s->name) != ERR_OK || nvs_set (s, n) != ERR_OK))
   {
      free (n);
      return "Unable to store";
   }
   if (s->flags & SETTING_BINARY)
      ESP_LOGI (TAG, "Setting %s changed (%d)", tag, len);
   else
      ESP_LOGI (TAG, "Setting %s changed %.*s", tag, len, value);
   // Store changed value
   if (!s->size)
   {                            // Dynamic
      void *o = *((void **) s->data);
      if (l == 1 && !*n)
      {                         // Empty string/data
         free (n);
         *((void **) s->data) = "";
         if (s->flags & SETTING_MALLOC)
            free (o);
         s->flags &= ~SETTING_MALLOC;
      } else
      {
         *((void **) s->data) = n;
         if (s->flags & SETTING_MALLOC)
            free (o);
         s->flags |= SETTING_MALLOC;
      }
   } else
   {                            // Static
      memcpy (s->data, n, l);
      free (n);
   }
   if (s->flags & SETTING_REBOOT)
      revk_restart ("Settings changed", 5);
   nvs_time = esp_timer_get_time () + 60000000;
   return NULL;                 // OK
}

const char *
revk_command (const char *tag, unsigned int len, const unsigned char *value)
{
   ESP_LOGI (TAG, "MQTT command [%s]", tag);
   const char *e = NULL;
   // My commands
   if (!e && !strcmp (tag, "upgrade"))
      e = revk_ota (otahost);
   if (!e && !strcmp (tag, "restart"))
      e = revk_restart ("Restart command", 0);
   // App commands
   if (!e && app_command)
      e = app_command (tag, len, value);
   return e;
}

void
revk_register (const char *name, unsigned char array, signed char size, void *data, int64_t defval, unsigned char flags)
{                               // Register setting (not expected to be thread safe, should be called from init)
   setting_t *s = malloc (sizeof (*s));
   s->name = name;
   s->array = array;
   s->size = size;
   s->data = data;
   s->flags = flags;
   s->defval = defval;
   s->next = setting;
   setting = s;
   // Get value
   int l = -1;
   if (!s->size)
   {                            // Dynamic
      void *d = NULL;
      l = nvs_get (s, NULL, 0);
      if (l > 0)
      {
         d = malloc (l);
         l = nvs_get (s, d, l);
         if (l >= 0)
            *((void **) data) = d;
         else
            free (d);
      }
   } else
      l = nvs_get (s, data, s->size);   // Stored static
   if (l < 0)
   {
      const char *e = revk_setting (name, 0, NULL);     // Defaulting logic
      if (e && *e)
         ESP_LOGE (TAG, "Setting %s failed %s", s->name, e);
      else
         ESP_LOGI (TAG, "Setting %s created", s->name);
   }
}
