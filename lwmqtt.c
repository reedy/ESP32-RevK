// Light weight MQTT client
// QoS 0 only, no queuing or resending (using TCP to do that for us)
// Live sending to TCP for outgoing messages
// Simple callback for incoming messages
// Automatic reconnect
static const char
    __attribute__((unused)) * TAG = "LWMQTT";

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
#include "esp_log.h"
#include "esp_tls.h"

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

struct lwmqtt_handle_s {        // mallocd copies
   char *host;
   char *port;
   lwmqtt_callback_t *callback;
   void *arg;
   int connectlen;
   unsigned char *connect;
   SemaphoreHandle_t mutex;     // atomic send mutex
   esp_tls_t *tls;              // Connection
   unsigned short keepalive;
   unsigned short seq;
   time_t ka;
   uint8_t running:1;
   void *cert_pem;              // For checking server
   int cert_len;
   void *client_cert_pem;       // For client auth
   int client_cert_len;
   void *client_key_pem;        // For client auth
   int client_key_len;
   // TODO CA Bundle
};

#define freez(x) do{if(x)free(x);}while(0)
static void *handle_free(lwmqtt_handle_t handle)
{
   if (handle)
   {
      freez(handle->host);
      freez(handle->port);
      freez(handle->connect);
      freez(handle->cert_pem);
      freez(handle->client_cert_pem);
      freez(handle->client_key_pem);
      if (handle->mutex)
         vSemaphoreDelete(handle->mutex);
      freez(handle);
   }
   return NULL;
}

static void task(void *pvParameters);

// Create a connection
lwmqtt_handle_t lwmqtt_init(lwmqtt_config_t * config)
{
   if (!config || !config->host)
      return NULL;
   lwmqtt_handle_t handle = malloc(sizeof(*handle));
   if (!handle)
      return handle_free(handle);
   memset(handle, 0, sizeof(*handle));
   handle->callback = config->callback;
   handle->arg = config->arg;
   handle->keepalive = config->keepalive ? : 60;
   if (!(handle->host = strdup(config->host)))
      return handle_free(handle);
   if (!(handle->port = strdup(config->port ? : config->cert_len ? "8883" : "1883")))
      return handle_free(handle);
   // Make connection message
   int mlen = 6 + 1 + 1 + 2 + strlen(config->client ? : "");
   if (config->plen < 0)
      config->plen = strlen((char *) config->payload ? : "");
   if (config->topic)
      mlen += 2 + strlen(config->topic) + 2 + config->plen;
   if (config->username)
      mlen += 2 + strlen(config->username);
   if (config->password)
      mlen += 2 + strlen(config->password);
   if (config->cert_len && config->cert_pem)
   {
      if (!(handle->cert_pem = malloc(config->cert_len)))
         return handle_free(handle);
      memcpy(handle->cert_pem, config->cert_pem, handle->cert_len = config->cert_len);
   }
   if (config->client_cert_len && config->client_cert_pem)
   {
      if (!(handle->client_cert_pem = malloc(config->client_cert_len)))
         return handle_free(handle);
      memcpy(handle->client_cert_pem, config->client_cert_pem, handle->client_cert_len = config->client_cert_len);
   }
   if (config->client_key_len && config->client_key_pem)
   {
      if (!(handle->client_key_pem = malloc(config->client_key_len)))
         return handle_free(handle);
      memcpy(handle->client_key_pem, config->client_key_pem, handle->client_key_len = config->client_key_len);
   }

   if (mlen >= 128 * 128)
      return handle_free(handle);       // Nope
   mlen += 2;                   // keepalive
   if (mlen >= 128)
      mlen++;                   // two byte len
   mlen += 2;                   // header and one byte len
   if (!(handle->connect = malloc(mlen)))
      return handle_free(handle);
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
   if (mlen > 129)
   {                            // Two byte len
      *p++ = (((mlen - 3) & 0x7F) | 0x80);
      *p++ = ((mlen - 3) >> 7);
   } else
      *p++ = mlen - 2;          // 1 byte len
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
         *p |= 0x20;            // Will retain
   }
   p++;
   *p++ = handle->keepalive >> 8;       // keep alive
   *p++ = handle->keepalive;
   str(-1, config->client);     // Client ID
   if (config->topic)
   {                            // Will
      str(-1, config->topic);   // Topic
      str(config->plen, (void *) config->payload);      // Payload
   }
   if (config->username)
      str(-1, config->username);
   if (config->password)
      str(-1, config->password);
   handle->connectlen = mlen;
   handle->mutex = xSemaphoreCreateBinary();
   xSemaphoreGive(handle->mutex);
   TaskHandle_t task_id = NULL;
   handle->running = 1;
   xTaskCreate(task, "mqtt", 8 * 1024, (void *) handle, 2, &task_id);
   return handle;
}

// End connection - actually freed later as part of task. Will do a callback when closed if was connected
// NULLs the passed handle - do not use handle after this call
void lwmqtt_end(lwmqtt_handle_t * handle)
{
   if (!handle || !*handle)
      return;
   (*handle)->running = 0;
   *handle = NULL;
}

// Subscribe (return is non null error message if failed)
const char *lwmqtt_subscribeub(lwmqtt_handle_t handle, const char *topic, char unsubscribe)
{
   const char *ret = NULL;
   if (!handle)
      ret = "No handle";
   else
   {
      int tlen = strlen(topic ? : "");
      int mlen = 2 + 2 + tlen;
      if (!unsubscribe)
         mlen++;                // QoS requested
      if (mlen >= 128 * 128)
         ret = "Too big";
      else
      {
         if (mlen >= 128)
            mlen++;             // two byte len
         mlen += 2;             // header and one byte len
         unsigned char *buf = malloc(mlen);
         if (!buf)
            ret = "Malloc";
         else
         {
            if (!xSemaphoreTake(handle->mutex, portMAX_DELAY))
               ret = "Failed to get lock";
            else
            {
               if (!handle->tls)
                  ret = "Not connected";
               else
               {
                  unsigned char *p = buf;
                  *p++ = (unsubscribe ? 0xA2 : 0x82);   // subscribe/unsubscribe
                  if (mlen > 129)
                  {             // Two byte len
                     *p++ = (((mlen - 3) & 0x7F) | 0x80);
                     *p++ = ((mlen - 3) >> 7);
                  } else
                     *p++ = mlen - 2;   // 1 byte len
                  handle->seq++;
                  *p++ = handle->seq >> 8;
                  *p++ = handle->seq;
                  *p++ = tlen >> 8;
                  *p++ = tlen;
                  if (tlen)
                     memcpy(p, topic, tlen);
                  p += tlen;
                  if (!unsubscribe)
                     *p++ = 0x00;       // QoS requested
                  if (esp_tls_conn_write(handle->tls, buf, mlen) < mlen)
                     ret = "Failed to send";
                  else
                     handle->ka = time(0) + handle->keepalive;
               }
               xSemaphoreGive(handle->mutex);
            }
            free(buf);
         }
      }
   }
   if (ret)
      ESP_LOGI(TAG, "Sub/unsub: %s", ret);
   return ret;
}

// Send (return is non null error message if failed)
const char *lwmqtt_send_full(lwmqtt_handle_t handle, int tlen, const char *topic, int plen, const unsigned char *payload, char retain, char nowait)
{
   // TODO how to nowait with TLS?
   const char *ret = NULL;
   if (!handle)
      ret = "No handle";
   else
   {
      if (tlen < 0)
         tlen = strlen(topic ? : "");
      if (plen < 0)
         plen = strlen((char *) payload ? : "");
      int mlen = 2 + tlen + plen;
      if (mlen >= 128 * 128)
         ret = "Too big";
      else
      {
         if (mlen >= 128)
            mlen++;             // two byte len
         mlen += 2;             // header and one byte len
         unsigned char *buf = malloc(mlen);
         if (!buf)
            ret = "Malloc";
         else
         {
            if (!xSemaphoreTake(handle->mutex, nowait ? 1 : portMAX_DELAY))
               ret = "Failed to get lock";
            else
            {
               if (!handle->tls)
                  ret = "Not connected";
               else
               {
                  unsigned char *p = buf;
                  *p++ = 0x30 + (retain ? 1 : 0);       // message
                  if (mlen > 129)
                  {             // Two byte len
                     *p++ = (((mlen - 3) & 0x7F) | 0x80);
                     *p++ = ((mlen - 3) >> 7);
                  } else
                     *p++ = mlen - 2;   // 1 byte len
                  *p++ = tlen >> 8;
                  *p++ = tlen;
                  if (tlen)
                     memcpy(p, topic, tlen);
                  p += tlen;
                  if (plen && payload)
                     memcpy(p, payload, plen);
                  p += plen;
                  if (esp_tls_conn_write(handle->tls, buf, mlen) < mlen)
                     ret = "Failed to send";
                  else
                     handle->ka = time(0) + handle->keepalive;
               }
               xSemaphoreGive(handle->mutex);
            }
            free(buf);
         }
      }
   }
   if (ret)
      ESP_LOGD(TAG, "Send: %s", ret);
   return ret;
}

static void task(void *pvParameters)
{
   lwmqtt_handle_t handle = pvParameters;
   if (!handle)
   {
      vTaskDelete(NULL);
      return;
   }
   int backoff = 1;
   while (handle->running)
   {
      // Connect
      ESP_LOGD(TAG, "Connecting %s:%s", handle->host, handle->port);
      esp_tls_cfg_t cfg = {
       cacert_buf:handle->cert_pem,
       cacert_bytes:handle->cert_len,
       clientcert_buf:handle->client_cert_pem,
       clientcert_bytes:handle->client_cert_len,
       clientkey_buf:handle->client_key_pem,
       clientkey_bytes:handle->client_key_len,
       is_plain_tcp:handle->cert_len ? 0 : 1,
      };
      esp_tls_t *tls = esp_tls_init();
      if (!tls || esp_tls_conn_new_sync(handle->host, strlen(handle->host), atoi(handle->port), &cfg, tls) != 1)
         ESP_LOGI(TAG, "Cannot connect");
      else
      {
         int len = esp_tls_conn_write(tls, handle->connect, handle->connectlen);
         if (len < 0)
            ESP_LOGE(TAG, "Failed to send connect");
         else
         {
            handle->tls = tls;
            // Handle rx messages
            unsigned char *buf = 0;
            int buflen = 0;
            int pos = 0;
            handle->ka = time(0) + handle->keepalive;
            while (handle->running)
            {
               int need = 0;
               if (pos < 2)
                  need = 2;
               else if (!(buf[1] & 0x80))
                  need = 2 + buf[1];    // One byte len
               else if (pos < 3)
                  need = 3;
               else if (!(buf[2] & 0x80))
                  need = 3 + (buf[2] << 7) + (buf[1] & 0x7F);   // Two byte len
               else
               {
                  ESP_LOGI(TAG, "Silly len %02X %02X %02X", buf[0], buf[1], buf[2]);
                  break;
               }
               if (pos < need)
               {
                  time_t now = time(0);
                  if (now >= handle->ka)
                  {
                     uint8_t b[] = { 0xC0, 0x00 };      // Ping
                     xSemaphoreTake(handle->mutex, portMAX_DELAY);
                     if (esp_tls_conn_write(tls, b, sizeof(b)) == sizeof(b))
                        handle->ka = time(0) + handle->keepalive;
                     xSemaphoreGive(handle->mutex);
                  }
                  if (esp_tls_get_bytes_avail(tls) <= 0)
                  {
                     int sock = -1;
                     if (esp_tls_get_conn_sockfd(tls, &sock))
                        break;
                     fd_set r;
                     FD_ZERO(&r);
                     FD_SET(sock, &r);
                     struct timeval to = { (now < handle->ka) ? (handle->ka - now) : 1, 0 };
                     time_t now = time(0);
                     int sel = select(sock + 1, &r, NULL, NULL, &to);
                     if (sel < 0)
                        break;
                     if (!sel)
                        continue;
                  }
                  if (need > buflen)
                  {
                     buf = realloc(buf, (buflen = need) + 1);   // One more to allow extra null on end in all cases
                     if (!buf)
                     {
                        ESP_LOGE(TAG, "realloc fail %d", need);
                        break;
                     }
                  }
                  int got = esp_tls_conn_read(tls, buf + pos, need - pos);
                  if (got <= 0)
                     break;     // Error or close
                  pos += got;
                  continue;
               }
               unsigned char *p = buf + 1,
                   *e = buf + pos;
               while (p < e && (*p & 0x80))
                  p++;
               p++;
               switch (*buf >> 4)
               {
               case 2:         // conack
                  ESP_LOGD(TAG, "Connected");
                  backoff = 1;
                  if (handle->callback)
                     handle->callback(handle->arg, NULL, strlen(handle->host), (void *) handle->host);
                  break;
               case 3:         // pub
                  {             // Topic
                     int tlen = (p[0] << 8) + p[1];
                     p += 2;
                     char *topic = (char *) p;
                     p += tlen;
                     unsigned short id = 0;
                     if (*buf & 0x06)
                     {
                        id = (p[0] << 8) + p[1];
                        p += 2;
                     }
                     if (p > e)
                     {
                        ESP_LOGE(TAG, "Bad msg");
                        break;
                     }
                     if (*buf & 0x60)
                     {          // reply
                        uint8_t b[4] = { (*buf & 0x4) ? 0x50 : 0x40, 2, id >> 8, id };
                        xSemaphoreTake(handle->mutex, portMAX_DELAY);
                        if (esp_tls_conn_write(tls, b, sizeof(b)) == sizeof(b))
                           handle->ka = time(0) + handle->keepalive;
                        xSemaphoreGive(handle->mutex);
                     }
                     int plen = e - p;
                     if (handle->callback)
                     {
                        if (plen && !(*buf & 0x06))
                        {       // Move back a byte for null termination to be added without hitting payload
                           memmove(topic - 1, topic, tlen);
                           topic--;
                        }
                        topic[tlen] = 0;
                        p[plen] = 0;
                        handle->callback(handle->arg, topic, plen, p);
                     }
                  }
                  break;
               case 4:         // puback - not expected
                  break;
               case 5:         // pubrec - not expected
                  {
                     uint8_t b[4] = { 0x60, p[0], p[1] };
                     xSemaphoreTake(handle->mutex, portMAX_DELAY);
                     if (esp_tls_conn_write(tls, b, sizeof(b)) == sizeof(b))
                        handle->ka = time(0) + handle->keepalive;
                     xSemaphoreGive(handle->mutex);
                  }
                  break;
               case 6:         // pubcomp - not expected
                  break;
               case 9:         // suback - ok
                  break;
               case 11:        // unsuback - ok
                  break;
               case 13:        // pingresp - ok
                  break;
               default:
                  ESP_LOGI(TAG, "Unknown MQTT %02X", *buf);
               }
               pos = 0;
            }
            if (buf)
               free(buf);
            if (handle->callback)
               handle->callback(handle->arg, NULL, 0, NULL);
         }
         // Close connection
         xSemaphoreTake(handle->mutex, portMAX_DELAY);
         if (!handle->running)
         {                      // Closed
            ESP_LOGD(TAG, "Close cleanly");
            uint8_t b[] = { 0xE0, 0x00 };       // Disconnect cleanly
            xSemaphoreTake(handle->mutex, portMAX_DELAY);
            if (esp_tls_conn_write(tls, b, sizeof(b)) == sizeof(b))
               handle->ka = time(0) + handle->keepalive;
            xSemaphoreGive(handle->mutex);
            if (handle->callback)
               handle->callback(handle->arg, NULL, 0, NULL);
         }
         handle->tls = NULL;
         xSemaphoreGive(handle->mutex);
      }
      if (tls)
         esp_tls_conn_destroy(tls);
      if (backoff < 60)
         backoff *= 2;
      ESP_LOGD(TAG, "Waiting %d", backoff);
      sleep(backoff);
   }
   handle_free(handle);
   vTaskDelete(NULL);
}

// Simple send - non retained no wait topic ends on space then payload
const char *lwmqtt_send_str(lwmqtt_handle_t handle, const char *msg)
{
   if (!handle || !*msg)
      return NULL;
   const char *p;
   for (p = msg; *p && *p != '\t'; p++);
   if (!*p)
      for (p = msg; *p && *p != ' '; p++);
   int tlen = p - msg;
   if (*p)
      p++;
   return lwmqtt_send_full(handle, tlen, msg, strlen(p), (void *) p, 0, 1);
}
