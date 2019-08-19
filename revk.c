// Main control code, working with WiFi, MQTT, and managing settinsg and OTA

#include "revk.h"

#define CONFIG_WIFI_SSID "WineDark"
#define CONFIG_WIFI_PASSWORD "old-town"
#define CONFIG_BROKER_URI "mqtt://mqtt.revk.uk/"

// Public
char *app_name;
char revk_version[20];          // ISO date version

// Local
static TaskHandle_t revk_task_id;
static app_callback_t *app_setting = NULL;
static app_callback_t *app_command = NULL;

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
   esp_mqtt_client_handle_t client = event->client;
   int msg_id;
   // your_context_t *context = event->context;
   switch (event->event_id)
   {
   case MQTT_EVENT_CONNECTED:
      ESP_LOGI (TAG, "MQTT_EVENT_CONNECTED");
      msg_id = esp_mqtt_client_subscribe (client, "command/SS/test/#", 0);
      break;
   case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI (TAG, "MQTT_EVENT_DISCONNECTED");
      break;

   case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI (TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      msg_id = esp_mqtt_client_publish (client, "status/SS/test", "1 UP", 0, 0, 0);
      ESP_LOGI (TAG, "sent publish successful, msg_id=%d", msg_id);
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
         char *tag = malloc (p + 1);
         memcpy (tag, event->topic, p);
         tag[p] = 0;
         for (p = 0; p < event->topic_len && event->topic[p] != '/'; p++);
         char *value = malloc (event->data_len + 1);
         memcpy (value, event->data, event->data_len + 1);
         value[event->data_len] = 0;
         if (p == 7 && !memcmp (event->topic, "command", p))
            e = revk_command (tag, event->data_len, (const unsigned char *) event->data);
         else if (p == 7 && !memcmp (event->topic, "setting", p))
            e = revk_setting (tag, event->data_len, (const unsigned char *) event->data);
         else
            e = "";
         if (e)
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
   tcpip_adapter_init ();
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
   ESP_LOGI (TAG, "Waiting for wifi");
   xEventGroupWaitBits (wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
   ESP_LOGI (TAG, "WiFi connected");
   // MQTT
   const esp_mqtt_client_config_t mqtt_cfg = {
      .uri = CONFIG_BROKER_URI,
      .event_handle = mqtt_event_handler,
   };
   ESP_LOGI (TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size ());
   esp_mqtt_client_handle_t client = esp_mqtt_client_init (&mqtt_cfg);
   esp_mqtt_client_start (client);
   ESP_LOGI (TAG, "MQTT started");
   // Idle
   while (1)
   {
      // TODO unsure what we need here to be honest as most stuff can be run on events.
      printf ("RevK task\n");
      sleep (10);
   }
}


// Extrenal functions
void
revk_init (const char *file, const char *date, const char *time, app_callback_t * app_setting_cb, app_callback_t * app_command_cb)
{                               // Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID
   nvs_flash_init ();
   app_setting = app_setting_cb;
   app_command = app_command_cb;
   // App name
   app_name = strdup (file ? : "");     // TODO extract leaf and remove .c, etc
   // Work out version string
   // TODO
   // Start task
   xTaskCreatePinnedToCore (revk_task, "RevK", 16 * 1024, NULL, 1, &revk_task_id, tskNO_AFFINITY);      // TODO stack, priority, affinity check?
}

// MQTT reporting
void
revk_status (const char *tag, const char *fmt, ...)
{                               // Send status
   // TODO
}

void
revk_event (const char *tag, const char *fmt, ...)
{                               // Send event
   // TODO
}

void
revk_error (const char *tag, const char *fmt, ...)
{                               // Send error
   // TODO
}

void
revk_info (const char *tag, const char *fmt, ...)
{                               // Send info
   // TODO
}

const char *
revk_restart (const char *reason)
{                               // Restart cleanly
   // TODO app_command to advise restart
   // mqtt report to advise restart
   revk_status (NULL, "0 %s", reason);
   esp_restart ();
   return "Restart failed";
}

const char *
revk_ota (void)
{                               // OTA and restart cleanly
   // TODO
   return "No OTA yet";
   return "";                   // OK / done
}

const char *
revk_setting (const char *tag, unsigned int len, const unsigned char *value)
{
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
   const char *e = NULL;
   // My commands
   if (!e && !strcmp (tag, "upgrade"))
      e = revk_ota ();
   if (!e && !strcmp (tag, "restart"))
      e = revk_restart ("Restart command");
   // App commands
   if (!e && app_command)
      e = app_command (tag, len, value);
   return e;
}
