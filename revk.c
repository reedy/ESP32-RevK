// Main control code, working with WiFi, MQTT, and managing settinsg and OTA

#include "revk.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"

#define CONFIG_WIFI_SSID "WineDark"
#define CONFIG_WIFI_PASSWORD "old-town"
#define CONFIG_BROKER_URI "mqtt://mqtt.revk.uk/"
#define CONFIG_OTA_HOST "ota.revk.uk"

// Public
const char *revk_app = "";
char revk_version[20];          // ISO date version
char revk_id[7];                // Chip ID as hex

// Local
static TaskHandle_t revk_task_id = NULL;
static TaskHandle_t ota_task_id = NULL;
static app_callback_t *app_setting = NULL;
static app_callback_t *app_command = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;

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
      sub ("command");          // TODO configurable
      sub ("setting");
      revk_status (NULL, "1 %s", revk_version); // Up
      break;
      // TODO trim
   case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI (TAG, "MQTT_EVENT_DISCONNECTED");
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
         printf ("TOPIC=%.*s\r\n", event->topic_len, event->topic);
         printf ("DATA=%.*s\r\n", event->data_len, event->data);
         int p;
         for (p = event->topic_len; p && event->topic[p - 1] != '/'; p--);
         char *tag = malloc (event->topic_len + 1 - p);
         memcpy (tag, event->topic + p, event->topic_len - p);
         tag[event->topic_len - p] = 0;
         for (p = 0; p < event->topic_len && event->topic[p] != '/'; p++);
         char *value = malloc (event->data_len + 1);
         memcpy (value, event->data, event->data_len + 1);
         value[event->data_len] = 0;
         if (p == 7 && !memcmp (event->topic, "command", p))    // TODo configurable
            e = revk_command (tag, event->data_len, (const unsigned char *) event->data);
         else if (p == 7 && !memcmp (event->topic, "setting", p))       // TODo configurable
            e = revk_setting (tag, event->data_len, (const unsigned char *) event->data);
         else
            e = "";
         ESP_LOGI (TAG, "MQTT err %s", e ? : "(null)");
         if (!e||*e)
            revk_error (tag, "Failed %s (%.*s)", *e ? e : "Unknown", event->data_len, event->data);
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
   wifi_config_t wifi_config = {
      .sta = {
              .ssid = CONFIG_WIFI_SSID,
              .password = CONFIG_WIFI_PASSWORD,
              },
   };
   ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));
   ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_STA, &wifi_config));
   ESP_LOGI (TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
   ESP_ERROR_CHECK (esp_wifi_start ());
   xEventGroupWaitBits (wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
   // Start MQTT
   esp_mqtt_client_start (mqtt_client);
   // Idle
   while (1)
   {
      // TODO unsure what we need here to be honest as most stuff can be run on events, and maybe waiting on group things
      printf ("RevK task\n");
      sleep (10);
   }
}


// External functions
void
revk_init (const char *file, const char *date, const char *time, app_callback_t * app_setting_cb, app_callback_t * app_command_cb)
{                               // Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID
   nvs_flash_init ();
   tcpip_adapter_init ();
   app_setting = app_setting_cb;
   app_command = app_command_cb;
   {                            // Chip ID from MAC
      unsigned char mac[6];
      ESP_ERROR_CHECK (esp_efuse_mac_get_default (mac));
      snprintf (revk_id, sizeof (revk_id), "%02X%02X%02X", mac[3], mac[4], mac[5]);
   }
   if (date && strlen (date) == 11 && time && strlen (time) == 8)
   {                            // date expected as "May 13 2019", time as "07:35:27"
      int m = 0,
         d = atoi (date + 4);
      if (date[0] == 'J')
      {
         if (date[1] == 'a')
            m = 1;
         else if (date[1] == 'u')
         {
            if (date[2] == 'n')
               m = 6;
            else if (date[2] == 'l')
               m = 7;
         }
      } else if (date[0] == 'F')
         m = 2;
      else if (date[0] == 'M')
      {
         if (date[1] == 'a')
         {
            if (date[2] == 'r')
               m = 3;
            else if (date[2] == 'y')
               m = 5;
         }
      } else if (date[0] == 'A')
      {
         if (date[1] == 'p')
            m = 4;
         else if (date[1] == 'u')
            m = 8;
      } else if (date[0] == 'S')
         m = 9;
      else if (date[0] == 'O')
         m = 10;
      else if (date[0] == 'N')
         m = 11;
      else if (date[0] == 'D')
         m = 12;
      snprintf (revk_version, sizeof (revk_version), "%.4s-%02d-%02d %.8s", date + 7, m, d, time);
   } else
      strcpy (revk_version, "?");
   if (file)
   {                            // App name extract from file
      const char *p = strrchr (file, '/');
      if (p)
         p++;
      else
         p = file;
      const char *d = strrchr (p, '.');
      if (d)
      {
         revk_app = strncpy (malloc (d + 1 - p), p, d - p);
         ((char *) revk_app)[d - p] = 0;
      } else
         revk_app = p;
   }
   // Work out version string
   // TODO
   // MQTT
   char *topic;
   if (asprintf (&topic, "status/%s/%s", revk_app, revk_id) < 0)
      return;
   const esp_mqtt_client_config_t mqtt_cfg = {
      .uri = CONFIG_BROKER_URI,
      .event_handle = mqtt_event_handler,
      .lwt_topic=topic,
      .lwt_qos=1,
      .lwt_retain=1,
      .lwt_msg_len=8,
      .lwt_msg="0 Failed",
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
revk_restart (const char *reason)
{                               // Restart cleanly
   // TODO app_command to advise restart
   revk_status (NULL, "0 %s", reason);
   esp_mqtt_client_stop(mqtt_client);
   sleep(1);
   esp_restart ();
   return "Restart failed";
}

static esp_err_t ota_handler(esp_http_client_event_t *evt)
{
static int ota_size=0;
static int ota_running=0;
static esp_ota_handle_t ota_handle;
static const esp_partition_t *ota_partition;
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
	    ota_size=0;
	    if(ota_running)esp_ota_end(ota_handle);
	    ota_running=0;
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG,"HTTP_HEADER %s: %s",evt->header_key,evt->header_value);
	    if(!strcmp(evt->header_key,"Content-Length"))ota_size=atoi(evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
	    if(!ota_running&&ota_size&&esp_http_client_get_status_code(evt->client)/100==2)
	    { // Start
		    esp_err_t err=esp_ota_begin(ota_partition=esp_ota_get_next_update_partition(NULL), ota_size,&ota_handle);
		    if(err!=ERR_OK)
			    revk_error("upgrade","Error %s",esp_err_to_name(err));
		    else ota_running=1;
	    }
	    if(ota_running)esp_ota_write(ota_handle,evt->data,evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
	    if(!ota_running)revk_error("Upgrade","Failed");
	    else
	    {
		    if(esp_ota_end(ota_handle)==ERR_OK)
			    esp_ota_set_boot_partition(ota_partition);
	    }
            ota_running=0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void
ota_task (void *pvParameters)
{
	const char *host=pvParameters;
	char *url;
	if(asprintf(&url,"http://%s/%s.bin",host,revk_app)<0)
	{ // Should not happen
	ota_task_id=NULL;
	vTaskDelete(NULL);
	return;
	}
   // TODO LE cert golbal and using HTTPS
   // TODO cert pinning
esp_http_client_config_t config = {
   .url = url,
   .event_handler=ota_handler,
};
esp_http_client_handle_t client = esp_http_client_init(&config);
esp_err_t err = esp_http_client_perform(client);
	int status=esp_http_client_get_status_code(client);
esp_http_client_cleanup(client);
	free(url);
	ota_task_id=NULL;
	if(err!=ERR_OK)revk_error("upgrade","Error %s",esp_err_to_name(err));
	else if(status/100!=2)revk_error("upgrade","Failed %d",status);
	else revk_restart("OTA");
	vTaskDelete(NULL);
}

const char *
revk_ota (const char *host)
{                               // OTA and restart cleanly
	if(ota_task_id)return "OTA running";
   xTaskCreatePinnedToCore (ota_task, "OTA", 16 * 1024, (char*)host, 1, &ota_task_id, tskNO_AFFINITY);      // TODO stack, priority, affinity check?
   return "";
}

const char *
revk_setting (const char *tag, unsigned int len, const unsigned char *value)
{
   ESP_LOGI (TAG, "MQTT setting %s", tag);
   // TODO Check setting has changed?
   const char *e = NULL;
   // TODO my settings
   // App settings
   if (!e && app_setting)
      e = app_setting (tag, len, value);
   // TODO if not error, store setting
   return e;
}

const char *
revk_command (const char *tag, unsigned int len, const unsigned char *value)
{
   ESP_LOGI (TAG, "MQTT command [%s]", tag);
   const char *e = NULL;
   // My commands
   if (!e && !strcmp (tag, "upgrade"))
      e = revk_ota (CONFIG_OTA_HOST);
   if (!e && !strcmp (tag, "restart"))
      e = revk_restart ("Restart command");
   // App commands
   if (!e && app_command)
      e = app_command (tag, len, value);
   return e;
}
