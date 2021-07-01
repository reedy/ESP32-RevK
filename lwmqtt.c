// Light weight MQTT client
// QoS 0 only, no queuing or resending (using TCP to do that for us)
// Live sending to TCP for outgoing messages
// Simple callback for incoming messages
// Automatic reconnect

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

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_tls.h"
#ifdef  CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#else
#include "lecert.h"
#endif

#include "lwmqtt.h"

struct lwmqtt_handle_s {
   char *host;                  // malloc'd host name
   int port;
   lwmqtt_callback_t *callback;
   void *arg;
   size_t connectlen;
   unsigned char *connect;      // malloc'd connect message
   // Mutex TODO
   // TLS settings TODO
};

static void task(void *pvParameters);

// Create a connection
lwmqtt_handle_t lwmqtt_init(lwmqtt_config_t * config)
{
   if (!config || !config->host)
      return NULL;
   lwmqtt_handle_t handle = malloc(sizeof(*handle));
   if (!handle)
      return NULL;
   memset(handle, 0, sizeof(*handle));
   handle->host = strdup(config->host);
   if (!handle->host)
   {
      free(handle);
      return NULL;
   }
   handle->port = (config->port ? : 1883);
   // Make connection message
   size_t len = 6 + 1 + 1 + 2 + strlen(config->client ? : "");
   if (config->topic)
      len += 2 + strlen(config->topic) + 2 + config->len;
   if (config->username)
      len += 2 + strlen(config->username);
   if (config->password)
      len += 2 + strlen(config->password);
   if (len >= 128 * 128)
   {                            // Nope
      free(handle->host);
      free(handle);
      return NULL;
   }
   if (len >= 128)
      len++;                    // two byte len
   len += 2;                    // header and one byte len
   handle->connect = malloc(len);
   if (!handle->connect)
   {
      free(handle->host);
      free(handle);
      return NULL;
   }
   unsigned char *p = handle->connect;
   void str(int l, const char *s) {
      if (l < 0)
         l = strlen(s ? : "");
      *p++ = l >> 8;
      *p++ = l;
      if (l && s)
         memcpy(p, s, l);
      p += l;
   }
   *p++ = 0x10;                 // connect
   if (len > 129)
   {                            // Two byte len
      *p++ = (((len - 3) & 0x7F) | 0x80);
      *p++ = ((len - 3) >> 7);
   } else
      *p++ = len - 2;           // 1 byte len
   *p++ = 0;                    // protocol name
   str(4, "MQTT");
   *p++ = 4;                    // protocol level
   *p = 0x02;                   // connect flags (clean)
   if (config->username)
      *p |= 0x80;               // Username
   if (config->password)
      *p |= 0x40;               // Password
   if (config->topic)
   {
      *p |= 0x04;               // Will
      if (config->retain)
         *p |= 0x40;            // Will retain
   }
   p++;
   str(-1, config->client);     // Client ID
   if (config->topic)
   {                            // Will
      str(-1, config->topic);   // Topic
      str(config->len, (void *) config->payload);       // Payload
   }
   if (config->username)
      str(-1, config->username);
   if (config->password)
      str(-1, config->password);
   TaskHandle_t task_id = NULL;
   xTaskCreate(task, "mqtt", 8 * 1024, (void *) handle, 2, &task_id);
   return NULL;
}

// End connection - actually freed later as part of task. Will do a callback when closed if was connected
// NULLs the passed handle - do not use handle after this call
void lwmqtt_end(lwmqtt_handle_t * handle)
{
   if (!handle || !*handle)
      return;
   (*handle)->port = 0;         // Tells server closed
}

// Subscribe (return is non null error message if failed)
const char *lwmqtt_subscribe(lwmqtt_handle_t handle, const char *topic)
{
   return "TODO";
}

const char *lwmqtt_unsubscribe(lwmqtt_handle_t handle, const char *topic)
{
   return "TODO";
}

// Send (return is non null error message if failed)
const char *lwmqtt_send(lwmqtt_handle_t handle, const char *topic, size_t len, const unsigned char *payload, char retain)
{
   return "TODO";
}

static void task(void *pvParameters)
{
   lwmqtt_handle_t handle = pvParameters;
   int port;                    // port gets cleared to indicate close wanted
   while ((port = handle->port))
   {
      // Connect

      // TODO

      // Handle rx messages

      // TODO

      sleep(1);                 // Wait a mo
   }
   free(handle->host);
   free(handle->connect);
   free(handle);
   vTaskDelete(NULL);
}
