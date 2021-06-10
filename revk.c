/*
 * Main control code, working with WiFi, MQTT, and managing settings and OTA Copyright Â ©2019 Adrian Kennard Andrews & Arnold Ltd
 */
static const char
    __attribute__((unused)) * TAG = "RevK";

//#define       SETTING_DEBUG

#include "revk.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#ifdef	CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#else
#include "lecert.h"
#endif
#include "esp_int_wdt.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "esp_phy_init.h"
#ifdef	CONFIG_REVK_APCONFIG
#include "esp_http_server.h"
#endif
#include <driver/gpio.h>

#if CONFIG_FREERTOS_HZ != 1000
#warn Reccomend CONFIG_FREERTOS_HZ set to 1000
#endif

#ifndef CONFIG_TASK_WDT_PANIC
#warning Set CONFIG_TASK_WDT_PANIC
#endif

#ifndef CONFIG_MQTT_BUFFER_SIZE
#define	CONFIG_MQTT_BUFFER_SIZE 1024
#endif

#define	WIFIMAX	3
#define	MQTTMAX	3

#define	settings	\
		s(otahost,CONFIG_REVK_OTAHOST);		\
		s(otacert,CONFIG_REVK_OTACERT);		\
		s(ntphost,CONFIG_REVK_NTPHOST);		\
		s(tz,CONFIG_REVK_TZ);			\
		u32(watchdogtime,10);			\
		s(appname,CONFIG_REVK_APPNAME);		\
		snl(hostname,NULL);			\
		p(command);				\
		p(setting);				\
		p(state);				\
		p(event);				\
		p(info);				\
		p(error);				\
		io(blink);				\

#define	apconfigsettings	\
		u32(apport,CONFIG_REVK_APPORT);		\
		u32(aptime,CONFIG_REVK_APTIME);		\
		u32(apwait,CONFIG_REVK_APWAIT);		\
		io(apgpio);		\

#define	mqttsettings	\
		u32(mqttreset,0);			\
		sa(mqtthost,MQTTMAX,CONFIG_REVK_MQTTHOST);	\
		sa(mqttuser,MQTTMAX,CONFIG_REVK_MQTTUSER);	\
		sap(mqttpass,MQTTMAX,CONFIG_REVK_MQTTPASS);	\
		u16(mqttport,MQTTMAX,CONFIG_REVK_MQTTPORT);	\
		u32(mqttsize,CONFIG_REVK_MQTTSIZE);	\
		sa(mqttcert,MQTTMAX,CONFIG_REVK_MQTTCERT);	\

#define	wifisettings	\
		u32(wifireset,0);			\
		sa(wifissid,WIFIMAX,CONFIG_REVK_WIFISSID);	\
		sa(wifiip,WIFIMAX,CONFIG_REVK_WIFIIP);	\
		sa(wifigw,WIFIMAX,CONFIG_REVK_WIFIGW);	\
		sa(wifidns,WIFIMAX,CONFIG_REVK_WIFIDNS);	\
		fh(wifibssid,WIFIMAX,6,CONFIG_REVK_WIFIBSSID);	\
		u8a(wifichan,WIFIMAX,CONFIG_REVK_WIFICHAN);	\
		sap(wifipass,WIFIMAX,CONFIG_REVK_WIFIPASS);	\
		s(apssid,CONFIG_REVK_APSSID);		\
		sp(appass,CONFIG_REVK_APPASS);		\
		s(apip,CONFIG_REVK_APIP);		\
		b(aplr,CONFIG_REVK_APLR);		\
		b(aphide,CONFIG_REVK_APHIDE);		\

#define	meshsettings	\
		s(wifissid,CONFIG_REVK_WIFISSID);	\
		s(wifiip,CONFIG_REVK_WIFIIP);		\
		s(wifigw,CONFIG_REVK_WIFIGW);		\
		s(wifidns,CONFIG_REVK_WIFIDNS);		\
		h(wifibssid,6,CONFIG_REVK_WIFIBSSID);	\
		u8(wifichan,CONFIG_REVK_WIFICHAN);	\
		sp(wifipass,CONFIG_REVK_WIFIPASS);	\
		h(meshid,6,CONFIG_REVK_MESHID);		\
		sp(meshpass,CONFIG_REVK_MESHPASS);	\

#define s(n,d)		static char *n;
#define sp(n,d)		static char *n;
#define snl(n,d)	static char *n;
#define sa(n,a,d)	static char *n[a];
#define sap(n,a,d)	static char *n[a];
#define fh(n,a,s,d)	static char n[a][s];
#define	u32(n,d)	static uint32_t n;
#define	u16(n,a,d)	static uint16_t n[a];
#define	i16(n)		static int16_t n;
#define	u8a(n,a,d)	static uint8_t n[a];
#define	u8(n,d)		static uint8_t n;
#define	b(n,d)		static uint8_t n;
#define	s8(n,d)		static int8_t n;
#define	io(n)		static uint8_t n;
#define p(n)		char *prefix##n;
#define h(n,l,d)	static char n[l];
settings
#ifdef	CONFIG_REVK_WIFI
    wifisettings
#endif
#ifdef	CONFIG_REVK_MQTT
    mqttsettings
#endif
#ifdef	CONFIG_REVK_APCONFIG
    apconfigsettings
#endif
#ifdef	CONFIG_REVK_MESH
    meshsettings
#endif
#undef s
#undef sp
#undef snl
#undef sa
#undef sap
#undef fh
#undef u32
#undef u16
#undef i16
#undef u8
#undef b
#undef u8a
#undef s8
#undef io
#undef p
#undef h
/* Local types */
typedef struct setting_s setting_t;
struct setting_s {
   nvs_handle nvs;              // Where stored
   setting_t *next;             // Next setting
   const char *name;            // Setting name
   const char *defval;          // Default value, or bitfield{[space]default}
   void *data;                  // Stored data
   uint16_t size;               // Size of data, 0=dynamic
   uint8_t array;               // array size
   uint8_t flags;               // flags 
   uint8_t namelen;             // Length of name
   uint8_t set:1;               // Has been set
   uint8_t parent:1;            // Parent setting
   uint8_t child:1;             // Child setting
   uint8_t dup:1;               // Set in parent if it is a duplicate of a child
   uint8_t used:1;              // Used in settings as temp
};
/* Public */
const char *revk_version = "";  /* Git version */
const char *revk_app = "";      /* App name */
char revk_id[13];               /* Chip ID as hex (from MAC) */
uint64_t revk_binid = 0;        /* Binary chip ID */

/* Local */
static EventGroupHandle_t revk_group;
#ifdef	CONFIG_REVK_WIFI
const static int GROUP_WIFI = BIT0;
const static int GROUP_WIFI_DONE = BIT1;
const static int GROUP_WIFI_TRY = BIT2;
#endif
#ifdef	CONFIG_REVK_MQTT
const static int GROUP_MQTT = BIT3;
const static int GROUP_MQTT_DONE = BIT4;
const static int GROUP_MQTT_TRY = BIT5;
#endif
#ifdef	CONFIG_REVK_APCONFIG
const static int GROUP_APCONFIG = BIT6;
const static int GROUP_APCONFIG_DONE = BIT7;
#endif
static TaskHandle_t ota_task_id = NULL;
#ifdef	CONFIG_REVK_APCONFIG
static TaskHandle_t ap_task_id = NULL;
#endif
static app_command_t *app_command = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static int64_t restart_time = 0;
static int64_t nvs_time = 0;
static int64_t slow_connect = 0;
static uint8_t revk_dump = 0;
static const char *restart_reason = "Unknown";
static nvs_handle nvs = -1;
static setting_t *setting = NULL;
#ifdef	CONFIG_REVK_WIFI
static int wifi_count = 0;
static int wifi_fails = 0;
static int wifi_index = -1;
static esp_netif_t *sta_netif = NULL;
#endif
#ifdef	CONFIG_REVK_MQTT
static int mqtt_count = 0;
static int mqtt_index = -1;
#endif
static int64_t lastonline = 0;
static char wdt_test = 0;
#ifdef  CONFIG_REVK_WIFI
static esp_netif_t *ap_netif = NULL;
#endif
static uint8_t blink_on = 0,
    blink_off = 0;
static const char *revk_setting_dump(void);

/* Local functions */
#ifdef	CONFIG_REVK_APCONFIG
static void ap_task(void *pvParameters);
#endif

#ifdef	CONFIG_REVK_MQTT
static void mqtt_next(void);
#endif

#ifdef	CONFIG_REVK_WIFI
static void makeip(esp_netif_ip_info_t * info, const char *ip, const char *gw)
{
   char *i = strdup(ip);
   int cidr = 24;
   char *n = strrchr(i, '/');
   if (n)
   {
      *n++ = 0;
      cidr = atoi(n);
   }
   esp_netif_set_ip4_addr(&info->netmask, (0xFFFFFFFF << (32 - cidr)) >> 24, (0xFFFFFFFF << (32 - cidr)) >> 16, (0xFFFFFFFF << (32 - cidr)) >> 8, (0xFFFFFFFF << (32 - cidr)));
   REVK_ERR_CHECK(esp_netif_str_to_ip4(i, &info->ip));
   if (!gw || !*gw)
      info->gw = info->ip;
   else
      REVK_ERR_CHECK(esp_netif_str_to_ip4(gw, &info->gw));
   free(i);
}
#endif

#ifdef	CONFIG_REVK_WIFI
static void wifi_next(const char *reason)
{
   if (wifi_index < -1)
      return;
   ESP_LOGI(TAG, "WiFi next %s", reason);
#ifdef	CONFIG_REVK_APCONFIG
   if (xEventGroupGetBits(revk_group) & GROUP_APCONFIG)
      return;
#endif
   if (wifi_index == -1)
   {                            /* init */
      if (*apssid)
      {
         wifi_config_t wifi_config = { 0, };
         if (strlen(apssid) >= sizeof(wifi_config.ap.ssid))
         {
            memcpy((char *) wifi_config.ap.ssid, apssid, sizeof(wifi_config.ap.ssid));
            wifi_config.ap.ssid_len = sizeof(wifi_config.ap.ssid);
         } else
         {
            strcpy((char *) wifi_config.ap.ssid, apssid);
            wifi_config.ap.ssid_len = strlen(apssid);
         }
         if (*appass)
         {
            strncpy((char *) wifi_config.ap.password, appass, sizeof(wifi_config.ap.password));
            wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
         }
         wifi_config.ap.ssid_hidden = aphide;
         wifi_config.ap.max_connection = 255;
         esp_netif_ip_info_t info = { 0, };
         makeip(&info, *apip ? apip : "10.0.0.1/24", NULL);
         REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, aplr ? WIFI_PROTOCOL_LR : (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)));
         REVK_ERR_CHECK(esp_netif_dhcps_stop(ap_netif));
         REVK_ERR_CHECK(esp_netif_set_ip_info(ap_netif, &info));
         REVK_ERR_CHECK(esp_netif_dhcps_start(ap_netif));
         REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
         esp_wifi_set_mode(WIFI_MODE_APSTA);
      } else
      {                         /* station only */
         esp_wifi_set_mode(WIFI_MODE_STA);
      }
      REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
      REVK_ERR_CHECK(esp_wifi_start());
   }
   int last = wifi_index;
   wifi_index++;
   if (wifi_index >= sizeof(wifissid) / sizeof(*wifissid) || !*wifissid[wifi_index])
      wifi_index = 0;           /* loop */
   if (last != wifi_index && last >= 0 && app_command)
      app_command("change", 0, NULL);
   ESP_LOGI(TAG, "WIFi [%s]", wifissid[wifi_index]);
   wifi_config_t wifi_config = { 0, };
   if (wifibssid[wifi_index][0] || wifibssid[wifi_index][1] || wifibssid[wifi_index][2])
   {
      memcpy(wifi_config.sta.bssid, wifibssid[wifi_index], sizeof(wifi_config.sta.bssid));
      wifi_config.sta.bssid_set = 1;
   }
   xEventGroupSetBits(revk_group, GROUP_WIFI_TRY);
   wifi_config.sta.channel = wifichan[wifi_index];
   wifi_config.sta.scan_method = ((esp_reset_reason() == ESP_RST_DEEPSLEEP) ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
   strncpy((char *) wifi_config.sta.ssid, wifissid[wifi_index], sizeof(wifi_config.sta.ssid));
   strncpy((char *) wifi_config.sta.password, wifipass[wifi_index], sizeof(wifi_config.sta.password));
   REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   /* DNS(not per wifi_index, but main, backup and fallback) */
   void dns(const char *ip, esp_netif_dns_type_t type) {
      if (!ip || !*ip)
         return;
      char *i = strdup(ip);
      char *c = strrchr(i, '/');
      if (c)
         *c = 0;
      esp_netif_dns_info_t dns = { };
      if (!esp_netif_str_to_ip4(i, &dns.ip.u_addr.ip4))
         dns.ip.type = AF_INET;
      else if (!esp_netif_str_to_ip6(i, &dns.ip.u_addr.ip6))
         dns.ip.type = AF_INET6;
      else
      {
         ESP_LOGE(TAG, "Bad DNS IP %s", i);
         return;
      }
      if (esp_netif_set_dns_info(sta_netif, type, &dns))
         ESP_LOGE(TAG, "Bad DNS %s", i);
      else
         ESP_LOGI(TAG, "Set DNS IP %s", i);
      free(i);
   }
   /* Static IP(per wifi_index) */
   if (*wifiip[wifi_index])
   {
      esp_netif_dhcpc_stop(sta_netif);
      esp_netif_ip_info_t info = { 0, };
      makeip(&info, wifiip[wifi_index], wifigw[wifi_index]);
      REVK_ERR_CHECK(esp_netif_set_ip_info(sta_netif, &info));
      ESP_LOGI(TAG, "Fixed IP %s GW %s", wifiip[wifi_index], wifigw[wifi_index]);
      if (!*wifidns[0])
         dns(wifiip[wifi_index], ESP_NETIF_DNS_MAIN);   /* Fallback to using gateway for DNS */
   } else
      esp_netif_dhcpc_start(sta_netif); /* Dynamic IP */
   dns(wifidns[0], ESP_NETIF_DNS_MAIN);
   dns(wifidns[1], ESP_NETIF_DNS_BACKUP);
   dns(wifidns[2], ESP_NETIF_DNS_FALLBACK);
   esp_wifi_connect();
}
#endif

#ifdef	CONFIG_REVK_MQTT
static esp_err_t mqtt_event_handler(esp_mqtt_event_t * event)
{
   switch (event->event_id)
   {
   case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "MQTT connect");
      lastonline = esp_timer_get_time() + 3000000LL;
      slow_connect = 0;
      if (mqttreset)
         revk_restart(NULL, -1);
      xEventGroupSetBits(revk_group, GROUP_MQTT);
      xEventGroupClearBits(revk_group, GROUP_MQTT_TRY | GROUP_MQTT_DONE);
      void sub(const char *prefix) {
         char *topic;
         if (asprintf(&topic, "%s/%s/%s/#", prefix, appname, revk_id) < 0)
            return;
         esp_mqtt_client_subscribe(mqtt_client, topic, 0);
         free(topic);
         if (asprintf(&topic, "%s/%s/*/#", prefix, appname) < 0)
            return;
         esp_mqtt_client_subscribe(mqtt_client, topic, 0);
         if (*hostname)
         {
            if (asprintf(&topic, "%s/%s/%s/#", prefix, appname, hostname) < 0)
               return;
            esp_mqtt_client_subscribe(mqtt_client, topic, 0);
         }
         free(topic);
      }
      sub(prefixcommand);
      sub(prefixsetting);

      /* Info */
      wifi_ap_record_t ap = { };
      esp_wifi_sta_get_ap_info(&ap);
      uint64_t t = esp_timer_get_time();
      jo_t j = jo_object_alloc();
      jo_litf(j, "up", "%d.%06d", (uint32_t) (t / 1000000LL), (uint32_t) (t % 1000000LL));
      jo_string(j, "id", revk_id);
      jo_string(j, "app", appname);
      jo_string(j, "version", revk_version);
      jo_int(j, "mem", esp_get_free_heap_size());
      jo_int(j, "rst", esp_reset_reason());
      jo_string(j, "ssid", (char *) ap.ssid);
      jo_stringf(j, "bssid", "%02X%02X%02X:%02X%02X%02X", ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
      jo_int(j, "rssi", ap.rssi);
      jo_int(j, "chan", ap.primary);
      revk_statej(NULL, &j);
      if (app_command)
         app_command("connect", strlen(mqtthost[mqtt_index]), (unsigned char *) mqtthost[mqtt_index]);
      break;
   case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "MQTT disconnect");
      if (mqttreset)
         revk_restart("MQTT lost", mqttreset);
      mqtt_count++;
      xEventGroupSetBits(revk_group, GROUP_MQTT_DONE);
      xEventGroupClearBits(revk_group, GROUP_MQTT | GROUP_MQTT_TRY);
      if (app_command)
         app_command("disconnect", strlen(mqtthost[mqtt_index]), (unsigned char *) mqtthost[mqtt_index]);
      break;
   case MQTT_EVENT_DATA:
      {                         // topic is expected to be a prefix/appname/id/tag where tag could be omitted
         const char *t = event->topic,
             *e = t + event->topic_len;
         const char *err = NULL;
         const char *p = t;
         char *tag = NULL;
         while (p < e && *p != '/')
            p++;                // prefix
         if (p >= e)
            break;              // odd
         int plen = p - t;
         p++;
         while (p < e && *p != '/')
            p++;                // appname (ignore)
         if (p >= e)
            break;              // odd
         p++;
         while (p < e && *p != '/')
            p++;                // id (ignore)
         if (p < e)
         {                      // tag
            p++;
            tag = malloc(e + 1 - p);
            if (tag)
            {
               memcpy(tag, p, e - p);
               tag[e - p] = 0;
            }
         }
         char *value = malloc(event->data_len + 1);
         if (event->data_len)
            memcpy(value, event->data, event->data_len);
         value[event->data_len] = 0;    /* Safe */
         if (plen == strlen(prefixcommand) && !memcmp(event->topic, prefixcommand, plen))
            err = revk_command(tag, event->data_len, (const unsigned char *) value);
         else if (plen == strlen(prefixsetting) && !memcmp(event->topic, prefixsetting, plen))
            err = (revk_setting(tag, event->data_len, (const unsigned char *) value) ? : "");   /* Returns NULL if OK */
         else
            err = "";
         if (!err || *err)
            revk_error(tag, "Failed %s", err ? : "Unknown");
         if (tag)
            free(tag);
         free(value);
      }
      break;
   case MQTT_EVENT_ERROR:
      break;
   default:
      break;
   }
   return ESP_OK;
}
#endif

#ifdef	CONFIG_REVK_MQTT
static void mqtt_next(void)
{
   if (mqtt_index < -1)
      return;
   int last = mqtt_index;
   mqtt_index++;
   if (mqtt_index >= sizeof(mqtthost) / sizeof(*mqtthost) || !*mqtthost[mqtt_index])
      mqtt_index = 0;
   ESP_LOGI(TAG, "MQTT [%s]", mqtthost[mqtt_index]);
   if (last == mqtt_index && mqtt_client)
   {
      esp_mqtt_client_reconnect(mqtt_client);
      return;                   /* No change */
   }
   if (last != mqtt_index && last >= 0 && app_command)
      app_command("change", 0, NULL);
   if (!*mqtthost[mqtt_index] || *mqtthost[mqtt_index] == '-')  /* No MQTT */
      return;
   char *topic;
   if (asprintf(&topic, "%s/%s/%s", prefixstate, appname, *hostname ? hostname : revk_id) < 0)
      return;
   char *url;
   if (asprintf(&url, "%s://%s/", *mqttcert[mqtt_index] ? "mqtts" : "mqtt", mqtthost[mqtt_index]) < 0)
   {
      free(topic);
      return;
   }
   esp_mqtt_client_config_t config = {
      .uri = url,
      .lwt_topic = topic,
      .lwt_qos = 1,
      .lwt_retain = 1,
      .lwt_msg = "{\"up\":false}",
      .lwt_msg_len = 12,
      .event_handle = mqtt_event_handler,
      .buffer_size = mqttsize,
      /* .disable_auto_reconnect = true, */
   };
   if (*mqttcert[mqtt_index])
   {
#if 0                           /* When MQTT supports this! */
#ifdef  CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
      if (!strcmp(mqttcert[mqtt_index], "*"))
         config.crt_bundle_attach = esp_crt_bundle_attach;
      else
#endif
#endif
         config.cert_pem = mqttcert[mqtt_index];
   }
   if (*mqttuser[mqtt_index])
      config.username = mqttuser[mqtt_index];
   if (*mqttpass[mqtt_index])
      config.password = mqttpass[mqtt_index];
   if (mqttport[mqtt_index])
      config.port = mqttport[mqtt_index];
   if (!mqtt_client)
      mqtt_client = esp_mqtt_client_init(&config);
   else
   {
      esp_mqtt_client_stop(mqtt_client);
      xEventGroupWaitBits(revk_group, GROUP_MQTT_DONE, false, true, 1000 / portTICK_PERIOD_MS);
      esp_mqtt_set_config(mqtt_client, &config);
   }
   xEventGroupSetBits(revk_group, GROUP_MQTT_TRY);
   esp_mqtt_client_start(mqtt_client);
   free(topic);
   free(url);
}
#endif

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
#ifdef	CONFIG_REVK_WIFI
   if (event_base == WIFI_EVENT)
   {
      switch (event_id)
      {
      case WIFI_EVENT_AP_START:
         ESP_LOGI(TAG, "AP Start");
         break;
      case WIFI_EVENT_STA_START:
         ESP_LOGI(TAG, "STA Start");
         esp_wifi_connect();
         break;
      case WIFI_EVENT_AP_STOP:
         ESP_LOGI(TAG, "AP Stop");
         break;
      case WIFI_EVENT_STA_STOP:
         ESP_LOGI(TAG, "STA Stop");
         break;
      case WIFI_EVENT_STA_CONNECTED:
         ESP_LOGI(TAG, "STA Connect");
         slow_connect = esp_timer_get_time() + 300000000LL;     /* If no DHCP && MQTT we disconnect WiFi */
         if (wifireset)
            esp_phy_erase_cal_data_in_nvs();    /* Lets calibrate on boot */
         break;
      case WIFI_EVENT_AP_STACONNECTED:
         ESP_LOGI(TAG, "AP STA Connect");
         break;
      case WIFI_EVENT_STA_DISCONNECTED:
         ESP_LOGI(TAG, "STA Disconnect");
         if (wifireset)
            revk_restart("WiFi lost", wifireset);
         xEventGroupClearBits(revk_group, GROUP_WIFI | GROUP_WIFI_TRY);
         xEventGroupSetBits(revk_group, GROUP_WIFI_DONE);
         wifi_count++;
         wifi_fails++;
         break;
      case WIFI_EVENT_AP_STADISCONNECTED:
         ESP_LOGI(TAG, "AP STA Disconnect");
         break;
      case WIFI_EVENT_AP_PROBEREQRECVED:
         ESP_LOGE(TAG, "AP PROBEREQRECVED");
         break;
      default:
         break;
      }
   }
#endif
   if (event_base == IP_EVENT)
   {
      switch (event_id)
      {
      case IP_EVENT_STA_LOST_IP:
         ESP_LOGI(TAG, "Lost IP");
#ifdef	CONFIG_REVK_WIFI
         wifi_next("Lost IP");
#endif
         break;
      case IP_EVENT_STA_GOT_IP:
         ESP_LOGI(TAG, "Got IP");
         wifi_fails = 0;
#ifdef	CONFIG_REVK_MQTT
         if (mqtt_index >= 0 && (!*mqtthost[mqtt_index] || *mqtthost[mqtt_index] == '-'))
            slow_connect = 0;
#endif
#ifdef  CONFIG_REVK_WIFI
         if (wifireset)
            revk_restart(NULL, -1);
#endif
         sntp_stop();
         sntp_init();
#ifdef	CONFIG_REVK_MQTT
         if (mqtt_client)
            esp_mqtt_client_reconnect(mqtt_client);
#endif
#ifdef  CONFIG_REVK_WIFI
         xEventGroupSetBits(revk_group, GROUP_WIFI);
         xEventGroupClearBits(revk_group, GROUP_WIFI_TRY | GROUP_WIFI_DONE);
         if (app_command)
            app_command("wifi", strlen(wifissid[wifi_index]), (unsigned char *) wifissid[wifi_index]);
#endif
         break;
      case IP_EVENT_GOT_IP6:
         ESP_LOGI(TAG, "Got IPv6");
         break;
      }
   }
}

static void task(void *pvParameters)
{                               /* Main RevK task */
   if (watchdogtime)
      esp_task_wdt_add(NULL);
   pvParameters = pvParameters;
   /* Log if unexpected restart */
   int64_t tick = 0;
   while (1)
   {                            /* Idle */
      int64_t now = esp_timer_get_time();
      if (now < tick)
      {                         /* wait for next 10th, so idle task runs */
         usleep(tick - now);
         now = tick;
      }
      tick += 100000ULL;        /* 10th second */
      if (!wdt_test && watchdogtime)
         esp_task_wdt_reset();
      if (blink)
      {
         static uint8_t lit = 0,
             count = 0;
         if (count)
            count--;
         else
         {
            uint8_t on = blink_on,
                off = blink_off;
#if     defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
            if (!on && !off)
               on = off = (revk_offline()? 6 : 3);
#endif
            lit = 1 - lit;
            count = (lit ? on : off);
            if (count)
               gpio_set_level(blink & 0x3F, lit ^ ((blink & 0x40) ? 1 : 0));
         }
      }
      if (slow_connect && slow_connect < now)
      {
         ESP_LOGI(TAG, "Slow connect, disconnecting");
         slow_connect = 0;
         if (xEventGroupGetBits(revk_group) & (GROUP_WIFI | GROUP_WIFI_TRY))
         {
            esp_wifi_disconnect();
            xEventGroupWaitBits(revk_group, GROUP_WIFI_DONE, false, true, 1000 / portTICK_PERIOD_MS);
         }
      }
      if (revk_dump)
      {                         // So not from mqtt
         revk_dump = 0;
         revk_setting_dump();
      }
      if (restart_time && restart_time < now && !ota_task_id)
      {                         /* Restart */
         if (!restart_reason)
            restart_reason = "Unknown";
         if (app_command)
            app_command("shutdown", strlen(restart_reason), (unsigned char *) restart_reason);
         revk_mqtt_close(restart_reason);
         revk_wifi_close();
         REVK_ERR_CHECK(nvs_commit(nvs));
         esp_restart();
         restart_time = 0;
      }
      if (nvs_time && nvs_time < now)
      {
         REVK_ERR_CHECK(nvs_commit(nvs));
         nvs_time = 0;
      }
#ifdef	CONFIG_REVK_MQTT
      if (xEventGroupGetBits(revk_group) & GROUP_MQTT)
      {                         /* on line */
         lastonline = esp_timer_get_time() + 3000000LL;
#ifdef	CONFIG_REVK_WIFI
         static int lastch = 0;
         static uint8_t lastbssid[6];
         static int lastindex = 0;
         wifi_ap_record_t ap = {
         };
         esp_wifi_sta_get_ap_info(&ap);
         if (lastch != ap.primary || memcmp(lastbssid, ap.bssid, 6) || lastindex != wifi_index)
         {
            lastindex = wifi_index;
            lastch = ap.primary;
            memcpy(lastbssid, ap.bssid, 6);
         }
#endif
      }
#endif
#ifdef	CONFIG_REVK_WIFI
      if ((xEventGroupGetBits(revk_group) & (GROUP_WIFI
#ifdef	CONFIG_REVK_MQTT
                                             | GROUP_MQTT | GROUP_MQTT_TRY
#endif
#ifdef	CONFIG_REVK_APCONFIG
                                             | GROUP_APCONFIG
#endif
           )) == (GROUP_WIFI))
#endif
         mqtt_next();           /* reconnect */
#ifdef	CONFIG_REVK_WIFI
      if (!(xEventGroupGetBits(revk_group) & (GROUP_WIFI | GROUP_WIFI_TRY)))
         wifi_next("Not running WiFi");
#endif
#ifdef	CONFIG_REVK_APCONFIG
      if (!ap_task_id && ((apgpio && (gpio_get_level(apgpio & 0x3F) ^ (apgpio & 0x40 ? 1 : 0)))
#if     defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
                          || (apwait && (wifi_fails >= 6 || revk_offline() > apwait))
#endif
#ifdef	CONFIG_REVK_WIFI
                          || !*wifissid[0]
#endif
          ))
         ap_task_id = revk_task("AP", ap_task, NULL);   /* Start AP mode */
#endif
   }
}

/* External functions */
void revk_init(app_command_t * app_command_cb)
{                               /* Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID */
   /* Watchdog */
#ifdef	CONFIG_REVK_PARTITION_CHECK
   extern const uint8_t part_start[] asm("_binary_partitions_4m_bin_start");
   extern const uint8_t part_end[] asm("_binary_partitions_4m_bin_end");
   /* Check and update partition table - expects some code to stay where it can run, i.e.0x10000, but may clear all settings */
   if ((part_end - part_start) > SPI_FLASH_SEC_SIZE)
   {
      ESP_LOGE(TAG, "Block size error (%d>%d)", part_end - part_start, SPI_FLASH_SEC_SIZE);
      return;
   }
   uint8_t *mem = malloc(SPI_FLASH_SEC_SIZE);
   if (!mem)
   {
      ESP_LOGE(TAG, "Malloc fail: %d", SPI_FLASH_SEC_SIZE);
      return;
   }
   REVK_ERR_CHECK(spi_flash_read(CONFIG_PARTITION_TABLE_OFFSET, mem, SPI_FLASH_SEC_SIZE));
   if (memcmp(mem, part_start, part_end - part_start))
   {
#ifndef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
#error Set CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
#endif
      ESP_LOGI(TAG, "Updating partition table");
      memset(mem, 0, SPI_FLASH_SEC_SIZE);
      memcpy(mem, part_start, part_end - part_start);
      REVK_ERR_CHECK(spi_flash_erase_range(CONFIG_PARTITION_TABLE_OFFSET, SPI_FLASH_SEC_SIZE));
      REVK_ERR_CHECK(spi_flash_write(CONFIG_PARTITION_TABLE_OFFSET, mem, SPI_FLASH_SEC_SIZE));
      esp_restart();
   }
   free(mem);
#endif
   nvs_flash_init();
   nvs_flash_init_partition(TAG);
   const esp_app_desc_t *app = esp_ota_get_app_description();
   if (nvs_open_from_partition(TAG, TAG, NVS_READWRITE, &nvs))
      REVK_ERR_CHECK(nvs_open(TAG, NVS_READWRITE, &nvs));
   revk_register("prefix", 0, 0, &prefixcommand, "command", SETTING_SECRET);    // Parent
   /* Fallback if no dedicated partition */
#define str(x) #x
#define snl(n,d)	revk_register(#n,0,0,&n,d,0)
#define s(n,d)		revk_register(#n,0,0,&n,d,0)
#define sp(n,d)		revk_register(#n,0,0,&n,d,SETTING_SECRET)
#define sa(n,a,d)	revk_register(#n,a,0,&n,d,0)
#define sap(n,a,d)	revk_register(#n,a,0,&n,d,SETTING_SECRET)
#define fh(n,a,s,d)	revk_register(#n,a,s,&n,d,SETTING_BINARY|SETTING_HEX)
#define	u32(n,d)	revk_register(#n,0,4,&n,str(d),0)
#define	u16(n,a,d)	revk_register(#n,a,2,&n,str(d),0)
#define	i16(n)		revk_register(#n,0,2,&n,0,SETTING_SIGNED)
#define	u8a(n,a,d)	revk_register(#n,a,1,&n,str(d),0)
#define	u8(n,d)		revk_register(#n,0,1,&n,str(d),0)
#define	b(n,d)		revk_register(#n,0,1,&n,str(d),SETTING_BOOLEAN)
#define	s8(n,d)		revk_register(#n,0,1,&n,str(d),SETTING_SIGNED)
#define io(n)		revk_register(#n,0,sizeof(n),&n,"-",SETTING_SET|SETTING_BITFIELD)
#define p(n)		revk_register("prefix"#n,0,0,&prefix##n,#n,0)
#define h(n,l,d)	revk_register(#n,0,l,&n,d,SETTING_BINARY|SETTING_HEX)
   settings;
#ifdef	CONFIG_REVK_WIFI
   revk_register("wifi", WIFIMAX, 0, &wifissid, CONFIG_REVK_WIFISSID, SETTING_SECRET);  // Parent
   revk_register("ap", 0, 0, &apssid, CONFIG_REVK_APSSID, SETTING_SECRET);      // Parent
   wifisettings;
#endif
#ifdef	CONFIG_REVK_MQTT
   revk_register("mqtt", MQTTMAX, 0, &mqtthost, CONFIG_REVK_MQTTHOST, SETTING_SECRET);  // Parent
   mqttsettings;
#endif
#ifdef	CONFIG_REVK_APCONFIG
   apconfigsettings;
#endif
#ifdef	CONFIG_REVK_MESH
   revk_register("wifi", 0, 0, &wifissid, CONFIG_REVK_WIFISSID, SETTING_SECRET);        // Parent
   revk_register("mesh", 0, 6, &meshid, CONFIG_REVK_MESHID, SETTING_BINARY | SETTING_HEX | SETTING_SECRET);     // Parent
   meshsettings;
#endif
#undef s
#undef snl
#undef sa
#undef fh
#undef u32
#undef u16
#undef i16
#undef u8a
#undef u8
#undef b
#undef s8
#undef io
#undef p
#undef str
#undef h
   if (watchdogtime)
      esp_task_wdt_init(watchdogtime, true);
   REVK_ERR_CHECK(nvs_open(app->project_name, NVS_READWRITE, &nvs));
   /* Application specific settings */
   if (!*appname)
      appname = strdup(app->project_name);
   /* Default is from build */
   if (blink)
   {
      gpio_reset_pin(blink & 0x3F);
      gpio_set_level(blink & 0x3F, (blink & 0x40) ? 0 : 1);     /* on */
      gpio_set_direction(blink & 0x3F, GPIO_MODE_OUTPUT);       /* Blinking LED */
   }
#ifdef	CONFIG_REVK_APCONFIG
   if (apgpio)
   {
      gpio_reset_pin(apgpio & 0x3F);
      gpio_set_direction(apgpio & 0x3F, GPIO_MODE_INPUT);       /* AP mode button */
   }
#endif
   restart_time = 0;
   /* If settings change at start up we can ignore. */
   esp_netif_init();
#ifndef	CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
   REVK_ERR_CHECK(esp_tls_set_global_ca_store(LECert, sizeof(LECert)));
#endif
   revk_version = app->version;
   revk_app = appname;
   char *d = strstr(revk_version, "dirty");
   if (d)
      asprintf((char **) &revk_version, "%.*s%s", d - revk_version, app->version, app->time);
   sntp_setoperatingmode(SNTP_OPMODE_POLL);
   sntp_setservername(0, ntphost);
   setenv("TZ", tz, 1);
   tzset();
   app_command = app_command_cb;
   {                            /* Chip ID from MAC */
      unsigned char mac[6];
      REVK_ERR_CHECK(esp_efuse_mac_get_default(mac));
#ifdef	CONFIG_REVK_SHORT_ID
      revk_binid = ((mac[0] << 16) + (mac[1] << 8) + mac[2]) ^ ((mac[3] << 16) + (mac[4] << 8) + mac[5]);
      snprintf(revk_id, sizeof(revk_id), "%06llX", revk_binid);
#else
      revk_binid = ((uint64_t) mac[0] << 40) + ((uint64_t) mac[1] << 32) + ((uint64_t) mac[2] << 24) + ((uint64_t) mac[3] << 16) + ((uint64_t) mac[4] << 8) + ((uint64_t) mac[5]);
      snprintf(revk_id, sizeof(revk_id), "%012llX", revk_binid);
#endif
   }
   /* WiFi */
   revk_group = xEventGroupCreate();
#ifdef	CONFIG_REVK_WIFI
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
#endif
   REVK_ERR_CHECK(esp_event_loop_create_default());
#ifdef	CONFIG_REVK_WIFI
   REVK_ERR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));
#endif
   REVK_ERR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
   REVK_ERR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &ip_event_handler, NULL));
   REVK_ERR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &ip_event_handler, NULL));
#ifdef	CONFIG_REVK_WIFI
   REVK_ERR_CHECK(esp_wifi_init(&cfg));
   REVK_ERR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
   REVK_ERR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
   sta_netif = esp_netif_create_default_wifi_sta();
   ap_netif = esp_netif_create_default_wifi_ap();
   /* DHCP */
   char *id;
   if (*hostname)
      asprintf(&id, "%s-%s", appname, hostname);
   else
      asprintf(&id, "%s-%06llX", appname, revk_binid & 0xFFFFFF);
   esp_netif_set_hostname(sta_netif, id);
   esp_netif_create_ip6_linklocal(sta_netif);
   free(id);
   wifi_next("Start");
#endif
   revk_task(TAG, task, NULL);
}

TaskHandle_t revk_task(const char *tag, TaskFunction_t t, const void *param)
{                               /* General user task make */
   TaskHandle_t task_id = NULL;
#ifdef REVK_LOCK_CPU1
   xTaskCreatePinnedToCore(t, tag, 8 * 1024, (void *) param, 2, &task_id, 1);
#else
   xTaskCreate(t, tag, 8 * 1024, (void *) param, 2, &task_id);
#endif
   return task_id;
}

#ifdef	CONFIG_REVK_MQTT
/* MQTT reporting */
void revk_mqtt_ap(const char *prefix, int qos, int retain, const char *tag, const char *fmt, va_list ap)
{                               /* Send formatted mqtt message */
   if (!mqtt_client)
      return;
   char *topic = NULL;
   if (!prefix)
      topic = (char *) tag;     /* Set fixed topic */
   if (asprintf(&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, appname, *hostname ? hostname : revk_id, tag) < 0)
      topic = NULL;
   if (!topic)
      return;
   char *buf;
   int l;
   if ((l = vasprintf(&buf, fmt, ap)) < 0)
   {
      free(topic);
      return;
   }
   ESP_LOGD(TAG, "MQTT publish %s %s", topic ? : "-", buf);
   if (xEventGroupGetBits(revk_group) & GROUP_MQTT)
      esp_mqtt_client_publish(mqtt_client, topic, buf, l, qos, retain);
   free(buf);
   if (topic != tag)
      free(topic);
}
#endif

#ifdef	CONFIG_REVK_MQTT
void revk_mqtt_apj(const char *prefix, int qos, int retain, const char *tag, jo_t * jp)
{
   if (!jp)
      return;
   const char *err = jo_error(*jp, NULL);
   char *res = jo_finisha(jp);
   if (!res)
   {
      ESP_LOGE(TAG, "JSON not sent: %s", err);
      return;
   }
   if (mqtt_client)
   {
      char *topic = NULL;
      if (!prefix)
         topic = (char *) tag;  /* Set fixed topic */
      else if (asprintf(&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, appname, *hostname ? hostname : revk_id, tag) < 0)
         topic = NULL;
      if (!topic)
      {
         free(res);
         return;
      }
      ESP_LOGD(TAG, "MQTT publish %s (%s)", topic ? : "-", res);
      if (xEventGroupGetBits(revk_group) & GROUP_MQTT)
         esp_mqtt_client_publish(mqtt_client, topic, res, strlen(res), qos, retain);
      if (topic != tag)
         free(topic);
   }
   free(res);
}
#endif

#ifdef	CONFIG_REVK_MQTT
void revk_raw(const char *prefix, const char *tag, int len, void *data, int retain)
{                               /* Send raw MQTT message */
   if (!mqtt_client)
      return;
   char *topic;
   if (!prefix)
      topic = (char *) tag;     /* Set fixed topic */
   else if (asprintf(&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, appname, *hostname ? hostname : revk_id, tag) < 0)
      topic = NULL;
   if (!topic)
      return;
   ESP_LOGD(TAG, "MQTT publish %s (%d)", topic ? : "-", len);
   if (xEventGroupGetBits(revk_group) & GROUP_MQTT)
      esp_mqtt_client_publish(mqtt_client, topic, data, len, 2, retain);
   if (topic != tag)
      free(topic);
}
#endif

void revk_state(const char *tag, const char *fmt, ...)
{                               /* Send status */
#ifdef	CONFIG_REVK_MQTT
   va_list ap;
   va_start(ap, fmt);
   revk_mqtt_ap(prefixstate, 1, 1, tag, fmt, ap);
   va_end(ap);
#endif
}

void revk_statej(const char *tag, jo_t * jp)
{
#ifdef	CONFIG_REVK_MQTT
   revk_mqtt_apj(prefixstate, 1, 1, tag, jp);
#endif
}

void revk_event(const char *tag, const char *fmt, ...)
{                               /* Send event */
#ifdef	CONFIG_REVK_MQTT
   va_list ap;
   va_start(ap, fmt);
   revk_mqtt_ap(prefixevent, 0, 0, tag, fmt, ap);
   va_end(ap);
#endif
}

void revk_eventj(const char *tag, jo_t * jp)
{
#ifdef	CONFIG_REVK_MQTT
   revk_mqtt_apj(prefixevent, 0, 0, tag, jp);
#endif
}

void revk_error(const char *tag, const char *fmt, ...)
{                               /* Send error */
#ifdef	CONFIG_REVK_MQTT
   xEventGroupWaitBits(revk_group,
#ifdef	CONFIG_REVK_WIFI
                       GROUP_WIFI |
#endif
                       GROUP_MQTT, false, true, 20000 / portTICK_PERIOD_MS);
   /* Chance of reporting issues */
   va_list ap;
   va_start(ap, fmt);
   revk_mqtt_ap(prefixerror, 0, 0, tag, fmt, ap);
   va_end(ap);
#endif
}

void revk_errorj(const char *tag, jo_t * jp)
{
#ifdef	CONFIG_REVK_MQTT
   revk_mqtt_apj(prefixerror, 0, 0, tag, jp);
#endif
}

void revk_info(const char *tag, const char *fmt, ...)
{                               /* Send info */
#ifdef	CONFIG_REVK_MQTT
   va_list ap;
   va_start(ap, fmt);
   revk_mqtt_ap(prefixinfo, 0, 0, tag, fmt, ap);
   va_end(ap);
#endif
}

void revk_infoj(const char *tag, jo_t * jp)
{
#ifdef	CONFIG_REVK_MQTT
   revk_mqtt_apj(prefixinfo, 0, 0, tag, jp);
#endif
}

const char *revk_restart(const char *reason, int delay)
{                               /* Restart cleanly */
   ESP_LOGI(TAG, "Restart %d %s", delay, reason);
   restart_reason = reason;
   if (delay < 0)
      restart_time = 0;         /* Cancelled */
   else
   {
      restart_time = esp_timer_get_time() + 1000000LL * (int64_t) delay;        /* Reboot now */
      if (app_command)
         app_command("restart", strlen(reason ? : ""), (void *) reason);        /* Warn of reset */
   }
   return "";                   /* Done */
}

static esp_err_t ota_handler(esp_http_client_event_t * evt)
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
         esp_ota_end(ota_handle);
      ota_running = 0;
      break;
   case HTTP_EVENT_HEADER_SENT:
      break;
   case HTTP_EVENT_ON_HEADER:
      if (!strcmp(evt->header_key, "Content-Length"))
         ota_size = atoi(evt->header_value);
      break;
   case HTTP_EVENT_ON_DATA:
      if (ota_size)
      {
         int64_t now = esp_timer_get_time();
         static int64_t next = 0;
         if (esp_http_client_get_status_code(evt->client) / 100 != 2)
            ota_size = 0;       /* Failed */
         if (!ota_running && ota_size)
         {                      /* Start */
            ota_progress = 0;
            if (!ota_partition)
               ota_partition = esp_ota_get_running_partition();
            ota_partition = esp_ota_get_next_update_partition(ota_partition);
            if (!ota_partition)
            {
               revk_error("upgrade", "No OTA partition available");     /* If running in OTA, boot to factory to allow OTA */
               ota_size = 0;
            } else
            {
               if (REVK_ERR_CHECK(esp_ota_begin(ota_partition, ota_size, &ota_handle)))
               {
                  ota_size = 0;
                  ota_partition = NULL;
               } else
               {
                  jo_t j = jo_object_alloc();
                  jo_int(j, "size", ota_size);
                  revk_infoj("upgrade", &j);
                  ota_running = 1;
                  next = now + 5000000LL;
               }
            }
         }
         if (ota_running && ota_size)
         {
            if (REVK_ERR_CHECK(esp_ota_write(ota_handle, evt->data, evt->data_len)))
            {
               ota_size = 0;
            } else
            {
               ota_running += evt->data_len;
               int percent = ota_running * 100 / ota_size;
               if (percent != ota_progress && (percent == 100 || next < now || percent / 10 != ota_progress / 10))
               {
                  jo_t j = jo_object_alloc();
                  jo_int(j, "progress", ota_progress = percent);
                  jo_int(j, "loaded", ota_running - 1);
                  jo_int(j, "size", ota_size);
                  revk_infoj("upgrade", &j);
                  next = now + 5000000LL;
               }
            }
         }
      }
      break;
   case HTTP_EVENT_ON_FINISH:
      if (!ota_running && esp_http_client_get_status_code(evt->client) / 100 > 3)
         revk_error("Upgrade", "Failed to start %d (%d)", esp_http_client_get_status_code(evt->client), ota_size);
      if (ota_running)
      {
         if (!REVK_ERR_CHECK(esp_ota_end(ota_handle)))
         {
            jo_t j = jo_object_alloc();
            jo_string(j, "complete", ota_partition->label);
            jo_int(j, "size", ota_size);
            revk_infoj("upgrade", &j);
            esp_ota_set_boot_partition(ota_partition);
            revk_restart("OTA", 5);
         }
      }
      ota_running = 0;
      break;
   case HTTP_EVENT_DISCONNECTED:
      break;
   }
   return ESP_OK;
}

#ifdef	CONFIG_REVK_APCONFIG
static esp_err_t ap_get(httpd_req_t * req)
{
   if (httpd_req_get_url_query_len(req))
   {
      char query[200];
      if (!httpd_req_get_url_query_str(req, query, sizeof(query)))
      {
         {
            char ssid[33],
             pass[33];
            if (!httpd_query_key_value(query, "ssid", ssid, sizeof(ssid)) && *ssid && !httpd_query_key_value(query, "pass", pass, sizeof(pass)))
            {
               revk_setting("wifissid", strlen(ssid), ssid);
               revk_setting("wifipass", strlen(pass), pass);
            }
         }
         {
            char host[129];
            if (!httpd_query_key_value(query, "host", host, sizeof(host)) && *host)
            {
               revk_setting("mqtthost", strlen(host), host);
               revk_setting("mqttuser", 0, NULL);
               revk_setting("mqttpass", 0, NULL);
               revk_setting("mqttcert", 0, NULL);
               revk_setting("mqttport", 0, NULL);
            }
         }
         const char resp[] = "Done";
         httpd_resp_send(req, resp, strlen(resp));
         xEventGroupSetBits(revk_group, GROUP_APCONFIG_DONE);
         return ESP_OK;
      }
   }
   /* httpd_resp_sendstr_chunk */
   const char resp[] = "<form><input name=ssid placeholder='SSID'><br/><input name=pass placeholder='Password'></br><input name=host placeholder='MQTT host'></br><input type=submit value='Set'></form>";
   httpd_resp_send(req, resp, strlen(resp));
   return ESP_OK;
}
#endif

#ifdef	CONFIG_REVK_APCONFIG
static void ap_task(void *pvParameters)
{
   xEventGroupSetBits(revk_group, GROUP_APCONFIG);
   wifi_fails = 0;
   lastonline = esp_timer_get_time();
   if (xEventGroupGetBits(revk_group) & GROUP_MQTT)
      revk_mqtt_close("AP mode start");
   if (xEventGroupGetBits(revk_group) & GROUP_WIFI)
   {
      esp_wifi_disconnect();
      xEventGroupWaitBits(revk_group, GROUP_WIFI_DONE, false, true, 1000 / portTICK_PERIOD_MS);
   }
   esp_wifi_stop();
   {                            /* IP */
      esp_netif_ip_info_t info = {
         0,
      };
      IP4_ADDR(&info.ip, 10, revk_binid >> 8, revk_binid, 1);
      info.gw = info.ip;        /* We are the gateway */
      IP4_ADDR(&info.netmask, 255, 255, 255, 0);
      REVK_ERR_CHECK(esp_netif_dhcps_stop(ap_netif));
      REVK_ERR_CHECK(esp_netif_set_ip_info(ap_netif, &info));
      REVK_ERR_CHECK(esp_netif_dhcps_start(ap_netif));
   }
   wifi_config_t wifi_config = { 0, };
   wifi_config.ap.ssid_len = snprintf((char *) wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s-10.%d.%d.1", appname, (uint8_t) (revk_binid >> 8), (uint8_t) (revk_binid & 255));
   if (wifi_config.ap.ssid_len > sizeof(wifi_config.ap.ssid))
      wifi_config.ap.ssid_len = sizeof(wifi_config.ap.ssid);
   ESP_LOGI(TAG, "AP mode start %.*s", wifi_config.ap.ssid_len, wifi_config.ap.ssid);
   wifi_config.ap.max_connection = 255;
#ifdef	CONFIG_REVK_WIFI
   if (xEventGroupGetBits(revk_group) & (GROUP_WIFI | GROUP_WIFI_TRY))
   {
      esp_wifi_disconnect();
      xEventGroupWaitBits(revk_group, GROUP_WIFI_DONE, false, true, 1000 / portTICK_PERIOD_MS);
   }
#endif
   REVK_ERR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
   REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
   REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
   esp_wifi_start();
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();
   if (apport)
      config.server_port = apport;
   /* Empty handle to esp_http_server */
   httpd_handle_t server = NULL;
   if (!httpd_start(&server, &config))
   {
      httpd_uri_t uri = {
         .uri = "/",
         .method = HTTP_GET,
         .handler = ap_get,
         .user_ctx = NULL
      };
      REVK_ERR_CHECK(httpd_register_uri_handler(server, &uri));
      xEventGroupWaitBits(revk_group, GROUP_APCONFIG_DONE, true, true, (aptime ? : 3600) * 1000LL / portTICK_PERIOD_MS);
      sleep(2);
      //Send reply maybe...
      httpd_stop(server);
   }
   esp_wifi_set_mode(WIFI_MODE_NULL);
   esp_wifi_stop();
   wifi_index = -1;             /* needs restarting */
   xEventGroupClearBits(revk_group, GROUP_APCONFIG | GROUP_APCONFIG_DONE | GROUP_WIFI | GROUP_WIFI_TRY);
   ESP_LOGI(TAG, "AP mode end");
   ap_task_id = NULL;
   vTaskDelete(NULL);
}
#endif

static void ota_task(void *pvParameters)
{
   char *url = pvParameters;
   jo_t j = jo_object_alloc();
   jo_string(j, "url", url);
   revk_infoj("upgrade", &j);
   esp_http_client_config_t config = {
      .url = url,.event_handler = ota_handler,
   };
   /* Set the TLS in case redirect to TLS even if http */
   if (*otacert)
      config.cert_pem = otacert;        /* Pinned cert */
   else
#ifdef	CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
      config.crt_bundle_attach = esp_crt_bundle_attach;
#else
      config.use_global_ca_store = true;        /* Global cert */
#endif
   esp_http_client_handle_t client = esp_http_client_init(&config);
   if (!client)
      revk_error("upgrade", "HTTP client failed");
   else
   {
      esp_err_t err = REVK_ERR_CHECK(esp_http_client_perform(client));
      int status = esp_http_client_get_status_code(client);
      esp_http_client_cleanup(client);
      free(url);
      if (!err && status / 100 != 2)
         revk_error("upgrade", "HTTP code %d", status);
   }
   ota_task_id = NULL;
   vTaskDelete(NULL);
}

const char *revk_ota(const char *url)
{                               /* OTA and restart cleanly */
   if (ota_task_id)
      return "OTA running";
   ota_task_id = revk_task("OTA", ota_task, url);
   return "";
}

static int nvs_get(setting_t * s, const char *tag, void *data, size_t len)
{                               /* Low level get logic, returns < 0 if error.Calls the right nvs get function for type of setting */
   esp_err_t err;
   if (s->flags & SETTING_BINARY)
   {
      if ((err = nvs_get_blob(s->nvs, tag, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   if (s->size == 0)
   {                            /* String */
      if ((err = nvs_get_str(s->nvs, tag, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   uint64_t temp;
   if (!data)
      data = &temp;
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
      {                         /* int64 */
         if ((err = nvs_get_i64(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         /* int32 */
         if ((err = nvs_get_i32(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         /* int16 */
         if ((err = nvs_get_i16(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         /* int8 */
         if ((err = nvs_get_i8(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   } else
   {
      if (s->size == 8)
      {                         /* uint64 */
         if ((err = nvs_get_u64(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         /* uint32 */
         if ((err = nvs_get_u32(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         /* uint16 */
         if ((err = nvs_get_u16(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         /* uint8 */
         if ((err = nvs_get_u8(s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   }
   return -999;
}

static esp_err_t nvs_set(setting_t * s, const char *tag, void *data)
{                               /* Low level set logic, returns < 0 if error. Calls the right nvs set function for type of setting */
   if (s->flags & SETTING_BINARY)
   {
      if (s->size)
         return nvs_set_blob(s->nvs, tag, data, s->size);       /* Fixed */
      return nvs_set_blob(s->nvs, tag, data, 1 + *((unsigned char *) data));    /* Variable */
   }
   if (s->size == 0)
      return nvs_set_str(s->nvs, tag, data);
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
         return nvs_set_i64(s->nvs, tag, *((int64_t *) data));
      if (s->size == 4)
         return nvs_set_i32(s->nvs, tag, *((int32_t *) data));
      if (s->size == 2)
         return nvs_set_i16(s->nvs, tag, *((int16_t *) data));
      if (s->size == 1)
         return nvs_set_i8(s->nvs, tag, *((int8_t *) data));
   } else
   {
      if (s->size == 8)
         return nvs_set_u64(s->nvs, tag, *((uint64_t *) data));
      if (s->size == 4)
         return nvs_set_u32(s->nvs, tag, *((uint32_t *) data));
      if (s->size == 2)
         return nvs_set_u16(s->nvs, tag, *((uint16_t *) data));
      if (s->size == 1)
         return nvs_set_u8(s->nvs, tag, *((uint8_t *) data));
   }
   ESP_LOGE(TAG, "Not saved setting %s", tag);
   return -1;
}

static const char *revk_setting_internal(setting_t * s, unsigned int len, const unsigned char *value, unsigned char index, unsigned char flags)
{                               // Value is expected to already be binary if using binary
   flags |= s->flags;
   {                            // Overlap check
      setting_t *q;
      for (q = setting; q && q->data != s->data; q = q->next);
      if (q)
         s = q;
   }
   void *data = s->data;
   if (s->array)
   {
      if (index >= s->array)
         return "Bad index";
      if (s->array && index && !(flags & SETTING_BOOLEAN))
         data += index * (s->size ? : sizeof(void *));
   }
   char tag[16];                /* Max NVS name size */
   if (snprintf(tag, sizeof(tag), s->array ? "%s%u" : "%s", s->name, index + 1) >= sizeof(tag))
      return "Setting name too long";
   ESP_LOGD(TAG, "MQTT setting %s (%d)", tag, len);
   char erase = 0;
   /* Using default, so remove from flash(as defaults may change later, don 't store the default in flash) */
   const char *defval = s->defval;
   if (defval && (flags & SETTING_BITFIELD))
   {                            /* default is after bitfields and a space */
      while (*defval && *defval != ' ')
         defval++;
      if (*defval == ' ')
         defval++;
   }
   if (!len && defval && !index && !value)
   {                            /* Use default value */
      len = strlen(defval);
      value = (const unsigned char *) defval;
      erase = 1;
   }
   if (!value)
      value = (const unsigned char *) "";
   else
      s->set = 1;
#ifdef SETTING_DEBUG
   if (s->flags & SETTING_BINARY)
      ESP_LOGI(TAG, "%s=(%d bytes)", (char *) tag, len);
   else
      ESP_LOGI(TAG, "%s=%.*s", (char *) tag, len, (char *) value);
#endif
   /* Parse new setting */
   unsigned char *n = NULL;
   int l = len;
   l = len;
   if (flags & SETTING_BINARY)
   {                            /* Blob */
      unsigned char *o;
      if (!s->size)
      {                         /* Dynamic */
         if (l > 255)
            return "Data too long";
         o = n = malloc(l + 1); /* One byte for length */
         if (o)
         {
            *o = l++;
            memcpy(o + 1, value, len);  /* Binary */
         }
      } else
      {                         // Fixed size binary
         if (l && l != s->size)
            return "Wrong size";
         o = n = malloc(s->size);
         if (o)
         {
            if (l)
               memcpy(o, value, l);
            else
               memset(o, 0, l = s->size);
         }
      }
   } else if (!s->size)
   {                            /* String */
      n = malloc(len + 1);      /* One byte for null termination */
      if (len)
         memcpy(n, value, len);
      n[len] = 0;
      l = len + 1;
   } else
   {                            /* Numeric */
      uint64_t v = 0;
      if (flags & SETTING_BOOLEAN)
      {                         /* Boolean */
         if (s->size == 1)
            v = *(uint8_t *) data;
         else if (s->size == 2)
            v = *(uint16_t *) data;
         else if (s->size == 4)
            v = *(uint32_t *) data;
         else if (s->size == 8)
            v = *(uint64_t *) data;
         if (len && strchr("YytT1", *value))
            v |= (1ULL << index);
         else
            v &= ~(1ULL << index);
      } else
      {
         char neg = 0;
         int bits = s->size * 8;
         uint64_t bitfield = 0;
         if (flags & SETTING_SET)
         {                      /* Set top bit if a value is present */
            bits--;
            if (len && value != (const unsigned char *) defval)
               bitfield |= (1ULL << bits);      /* Value is set (not so if using default value) */
         }
         if (flags & SETTING_BITFIELD && s->defval)
         {                      /* Bit fields */
            while (len)
            {
               const char *c = s->defval;
               while (*c && *c != ' ' && *c != *value)
                  c++;
               if (*c != *value)
                  break;
               uint64_t m = (1ULL << (bits - 1 - (c - s->defval)));
               if (bitfield & m)
                  break;
               bitfield |= m;
               len--;
               value++;
            }
            const char *c = s->defval;
            while (*c && *c != ' ')
               c++;
            bits -= (c - s->defval);
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
         {                      /* Decimal */
            len--;
            value++;
            neg = 1;
         }
         if (flags & SETTING_HEX)
            while (len && isxdigit(*value))
            {                   /* Hex */
               uint64_t n = v * 16 + (isalpha(*value) ? 9 : 0) + (*value & 15);
               if (n < v)
                  return "Silly number";
               v = n;
               value++;
               len--;
         } else
            while (len && isdigit(*value))
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
            *((int64_t *) (n = malloc(l = 8))) = v;
         else if (s->size == 4)
            *((int32_t *) (n = malloc(l = 4))) = v;
         else if (s->size == 2)
            *((int16_t *) (n = malloc(l = 2))) = v;
         else if (s->size == 1)
            *((int8_t *) (n = malloc(l = 1))) = v;
      } else
      {
         if (s->size == 8)
            *((int64_t *) (n = malloc(l = 8))) = v;
         else if (s->size == 4)
            *((int32_t *) (n = malloc(l = 4))) = v;
         else if (s->size == 2)
            *((int16_t *) (n = malloc(l = 2))) = v;
         else if (s->size == 1)
            *((int8_t *) (n = malloc(l = 1))) = v;
      }
   }
   if (!n)
      return "Bad setting type";
   /* See if setting has changed */
   int o = nvs_get(s, tag, NULL, 0);    // Get length
#ifdef SETTING_DEBUG
   if (o < 0 && o != -ESP_ERR_NVS_NOT_FOUND)
      ESP_LOGI(TAG, "Setting %s nvs read fail %s", tag, esp_err_to_name(-o));
#endif
   if (o < 0 && erase)
      o = 0;
   else if (o != l)
   {
#ifdef SETTING_DEBUG
      if (o >= 0)
         ESP_LOGI(TAG, "Setting %s different len %d/%d", tag, o, l);
#endif
      o = -1;                   /* Different size */
   }
   if (o > 0)
   {
      void *d = malloc(l);
      if (nvs_get(s, tag, d, l) != o)
      {
         free(n);
         free(d);
         return "Bad setting get";
      }
      if (memcmp(n, d, o))
      {
#ifdef SETTING_DEBUG
         ESP_LOGI(TAG, "Setting %s different content %d (%02X/%02X)", tag, o, *(uint8_t *) d, *(uint8_t *) n);
#endif
         o = -1;                /* Different content */
      }
      free(d);
   }
   if (o < 0)
   {                            /* Flash changed */
      if (erase)
         nvs_erase_key(s->nvs, tag);
      else if (nvs_set(s, tag, n) != ERR_OK && (nvs_erase_key(s->nvs, tag) != ERR_OK || nvs_set(s, tag, n) != ERR_OK))
      {
         free(n);
         return "Unable to store";
      }
#ifdef SETTING_DEBUG
      if (flags & SETTING_BINARY)
         ESP_LOGI(TAG, "Setting %s changed (%d)", tag, len);
      else
         ESP_LOGI(TAG, "Setting %s changed %.*s", tag, len, value);
#endif
      nvs_time = esp_timer_get_time() + 60000000LL;
   }
   if (flags & SETTING_LIVE)
   {                            /* Store changed value in memory live */
      if (!s->size)
      {                         /* Dynamic */
         void *o = *((void **) data);
         /* See if different */
         if (!o || ((flags & SETTING_BINARY) ? memcmp(o, n, 1 + *(uint8_t *) o) : strcmp(o, (char *) n)))
         {
            *((void **) data) = n;
            if (o)
               free(o);
         } else
            free(n);            /* No change */
      } else
      {                         /* Static (try and make update atomic) */
         if (s->size == 1)
            *(uint8_t *) data = *(uint8_t *) n;
         else if (s->size == 2)
            *(uint16_t *) data = *(uint16_t *) n;
         else if (s->size == 4)
            *(uint32_t *) data = *(uint32_t *) n;
         else if (s->size == 8)
            *(uint64_t *) data = *(uint64_t *) n;
         else
            memcpy(data, n, s->size);
         free(n);
      }
   } else if (o < 0)
      revk_restart("Settings changed", 5);
   return NULL;                 /* OK */
}

static const char *revk_setting_dump(void)
{                               // Dump settings (in JSON)
   const char *err = NULL;
   jo_t j = NULL;
   void send(void) {
      if (!j)
         return;
      char *v = jo_finish(&j);
      if (v)
         revk_raw(prefixsetting, NULL, strlen(v), v, 0);
   }
   char buf[CONFIG_MQTT_BUFFER_SIZE - 100];
   const char *hasdef(setting_t * s) {
      const char *d = s->defval;
      if (!d)
         return NULL;
      if (s->flags & SETTING_BITFIELD)
      {
         while (*d && *d != ' ')
            d++;
         if (*d == ' ')
            d++;
      }
      if (!*d)
         return NULL;
      if ((s->flags & SETTING_BOOLEAN) && !strchr("YytT1", *d))
         return NULL;
      if (s->size && !strcmp(d, "0"))
         return NULL;
      return d;
   }
   int isempty(setting_t * s, int n) {  // Check empty
      if (s->flags & SETTING_BOOLEAN)
      {                         // This is basically testing it is false
         uint64_t v = 0;
         if (s->size == 1)
            v = *(uint8_t *) s->data;
         else if (s->size == 2)
            v = *(uint16_t *) s->data;
         else if (s->size == 4)
            v = *(uint32_t *) s->data;
         else if (s->size == 8)
            v = *(uint64_t *) s->data;
         if (v & (1ULL << n))
            return 0;
         return 1;              // Empty bool
      }
      void *data = s->data + (s->size ? : sizeof(void *)) * n;
      int q = s->size;
      if (!q)
      {
         char *p = *(char **) data;
         if (!p || !*p)
            return 2;           // Empty string
         return 0;
      }
      while (q && !*(char *) data)
      {
         q--;
         data++;
      }
      if (!q)
         return 3;              // Empty value
      return 0;
   }
   setting_t *s;
   for (s = setting; s; s = s->next)
   {
      if ((!(s->flags & SETTING_SECRET) || s->parent) && !s->child)
      {
         int max = 0;
         if (s->array)
         {                      // Work out m - for now, parent items in arrays have to be set for rest to be output - this is the rule...
            max = s->array;
            if (!(s->flags & SETTING_BOOLEAN))
               while (max && isempty(s, max - 1))
                  max--;
         }
         jo_t p = NULL;
         void start(void) {
            if (!p)
            {
               if (j)
                  p = jo_copy(j);
               else
               {
                  p = jo_create_mem(buf, sizeof(buf));
                  jo_object(p, NULL);
               }
            }
         }
         const char *failed(void) {
            err = NULL;
            if (p && (err = jo_error(p, NULL)))
               jo_free(&p);     // Did not fit
            return err;
         }
         void addvalue(setting_t * s, const char *tag, int n) { // Add a value
            start();
            void *data = s->data;
            const char *defval = s->defval ? : "";
            if (!(s->flags & SETTING_BOOLEAN))
               data += (s->size ? : sizeof(void *)) * n;
            if (s->flags & SETTING_BINARY)
            {                   // Binary data
               int len = s->size;
               if (!len)
               {                // alloc'd with len at start
                  data = *(void **) data;
                  len = *(uint8_t *) data++;
               }
               if (s->flags & SETTING_HEX)
                  jo_base16(p, tag, data, len);
               else
                  jo_base64(p, tag, data, len);
            } else if (!s->size)
            {
               char *v = *(char **) data;
               if (v)
               {
                  jo_string(p, tag, v); // String
               } else
                  jo_null(p, tag);      // Null string - should not happen
            } else
            {
               uint64_t v = 0;
               if (s->size == 1)
                  v = *(uint8_t *) data;
               else if (s->size == 2)
                  v = *(uint16_t *) data;
               else if (s->size == 4)
                  v = *(uint32_t *) data;
               else if (s->size == 8)
                  v = *(uint64_t *) data;
               if (s->flags & SETTING_BOOLEAN)
               {
                  jo_bool(p, tag, (v >> n) & 1);
               } else
               {                // numeric
                  char temp[100],
                  *t = temp;
                  uint8_t bits = s->size * 8;
                  if (s->flags & SETTING_SET)
                     bits--;
                  if (!(s->flags & SETTING_SET) || ((v >> bits) & 1))
                  {
                     if (s->flags & SETTING_BITFIELD)
                     {
                        while (*defval && *defval != ' ')
                        {
                           bits--;
                           if ((v >> bits) & 1)
                              *t++ = *defval;
                           defval++;
                        }
                        if (*defval == ' ')
                           defval++;
                     }
                     if (s->flags & SETTING_SIGNED)
                     {
                        bits--;
                        if ((v >> bits) & 1)
                        {
                           *t++ = '-';
                           v = (v ^ ((1ULL << bits) - 1)) + 1;
                        }
                     }
                     v &= ((1ULL << bits) - 1);
                     if (s->flags & SETTING_HEX)
                        t += sprintf(t, "%llX", v);
                     else if (bits)
                        t += sprintf(t, "%llu", v);
                  }
                  *t = 0;
                  t = temp;
                  if (*t == '-')
                     t++;
                  if (*t == '0')
                     t++;
                  else
                     while (*t >= '0' && *t <= '9')
                        t++;
                  if (t == temp || *t || (s->flags & SETTING_HEX))
                     jo_string(p, tag, temp);
                  else
                     jo_lit(p, tag, temp);
               }
            }
         }
         void addsub(setting_t * s, const char *tag, int n) {   // n is 0 based
            if (s->parent)
            {
               if (!tag || (!n && hasdef(s)) || !isempty(s, n))
               {
                  start();
                  jo_object(p, tag);
                  setting_t *q;
                  for (q = setting; q; q = q->next)
                     if (q->child && !strncmp(q->name, s->name, s->namelen))
                        if ((!n && hasdef(q)) || !isempty(q, n))
                           addvalue(q, q->name + s->namelen, n);
                  jo_close(p);
               }
            } else
               addvalue(s, tag, n);
         }
         void addsetting(void) {        // Add a whole setting
            if (s->parent)
            {
               if (s->array)
               {                // Array above
                  if (max || hasdef(s))
                  {
                     start();
                     jo_array(p, s->name);
                     for (int n = 0; n < max; n++)
                        addsub(s, NULL, n);
                     jo_close(p);
                  }
               } else
                  addsub(s, s->name, 0);
            } else if (s->array)
            {
               if (max || hasdef(s))
               {
                  start();
                  jo_array(p, s->name);
                  for (int n = 0; n < max; n++)
                     addvalue(s, NULL, n);
                  jo_close(p);
               }
            } else if (hasdef(s) || !isempty(s, 0))
               addvalue(s, s->name, 0);
         }
         addsetting();
         if (failed() && j)
         {
            send();             // Failed, clear what we were sending and try again
            addsetting();
         }
         if (failed() && s->array)
         {                      // Failed, but is an array, so try each setting individually
            for (int n = 0; n < max; n++)
            {
               char *tag;
               asprintf(&tag, "%s%d", s->name, n + 1);
               if (tag)
               {
                  addsub(s, tag, n);
                  if (failed() && j)
                  {
                     send();    // Failed, clear what we were sending and try again
                     addsub(s, tag, n);
                  }
                  if (!failed())
                  {             // Fitted, move forward
                     jo_free(&j);
                     j = p;
                  } else
                     revk_error(TAG, "Setting did not fit %s (%s)", tag, err ? : "?");
                  free(tag);
               }
            }
         }
         if (!failed())
         {                      // Fitted, move forward
            if (p)
            {
               jo_free(&j);
               j = p;
            }
         } else
            revk_error(TAG, "Setting did not fit %s (%s)", s->name, err ? : "?");
      }
   }
   send();
   return NULL;
}

const char *revk_setting(const char *tag, unsigned int len, const void *value)
{
   if (!tag && !len)
   {
      revk_dump = 1;
      return NULL;
   }
   jo_t j;
   jo_type_t t;
   if (tag)
   {                            // Legacy
      j = jo_object_alloc();
      jo_stringf(j, tag, "%.*s", len, value);
      jo_rewind(j);
   } else
      j = jo_parse_mem(value, len);
   if (jo_here(j) != JO_OBJECT)
   {
      jo_free(&j);
      return "Pass JSON object";
   }
   int index = 0;
   int match(setting_t * s, const char *tag) {
      const char *a = s->name;
      const char *b = tag;
      while (*a && *a == *b)
      {
         a++;
         b++;
      }
      if (*a)
         return 1;              /* not matched whole name, no match */
      if (!*b)
      {
         index = 0;
         return 0;              /* Match, no index */
      }
      if (!s->array && *b)
         return 2;              /* not array, and more characters, no match */
      int v = 0;
      while (isdigit((int) (*b)))
         v = v * 10 + (*b++) - '0';
      if (*b)
         return 3;              /* More on end after any digits, no match */
      if (!v || v > s->array)
         return 4;              /* Invalid index, no match */
      index = v - 1;
      return 0;                 /* Match, index */
   }
   jo_skip(j);                  // Check whole JSON
   int pos;
   const char *er = jo_error(j, &pos);
   if (er)
   {
      ESP_LOGE(TAG, "Fail at pos %d, %s: %s", pos, er, jo_debug(j));
      jo_free(&j);
      return er;
   }
   jo_rewind(j);
   t = jo_next(j);              // Start object
   while (t == JO_TAG && !er)
   {
#ifdef SETTING_DEBUG
      ESP_LOGI(TAG, "Setting: %.10s", jo_debug(j));
#endif
      int l = jo_strlen(j);
      if (l < 0)
         break;
      char *tag = malloc(l + 1);
      if (!tag)
         er = "Malloc";
      else
      {
         l = jo_strncpy(j, (char *) tag, l + 1);
         t = jo_next(j);        // the value
         setting_t *s;
         for (s = setting; s && match(s, tag); s = s->next);
         if (!s)
         {
#ifdef SETTING_DEBUG
            ESP_LOGI(TAG, "Unknown %s len=%d: %.10s", tag, l, jo_debug(j));
#endif
            er = "Unknown setting";
         } else
         {
            void store(setting_t * s) {
               if (s->dup)
                  for (setting_t * q = setting; q; q = q->next)
                     if (!q->dup && q->data == s->data)
                     {
                        s = q;
                        break;
                     }
#ifdef SETTING_DEBUG
               if (s->array)
                  ESP_LOGI(TAG, "Store %s[%d] (type %d): %.20s", s->name, index, t, jo_debug(j));
               else
                  ESP_LOGI(TAG, "Store %s (type %d): %.20s", s->name, t, jo_debug(j));
#endif
               int l = 0;
               char *val = NULL;
               if (t == JO_NUMBER || t == JO_STRING || t >= JO_TRUE)
               {
                  if (t == JO_STRING && (s->flags & SETTING_BINARY))
                  {
                     if (s->flags & SETTING_HEX)
                     {
                        l = jo_strncpy16(j, NULL, 0);
                        if (l >= 0)
                           jo_strncpy16(j, val = malloc(l), l);
                     } else
                     {
                        l = jo_strncpy64(j, NULL, 0);
                        if (l >= 0)
                           jo_strncpy64(j, val = malloc(l), l);
                     }
                  } else
                  {
                     l = jo_strlen(j);
                     if (l >= 0)
                        jo_strncpy(j, val = malloc(l + 1), l + 1);
                  }
                  er = revk_setting_internal(s, l, (const unsigned char *) val, index, 0);
               } else
                  er = "Bad data type";
               if (val)
                  free(val);
            }
            void zap(setting_t * s) {   // Erasing
               if (s->dup)
                  return;
#ifdef SETTING_DEBUG
               ESP_LOGI(TAG, "Zap %s[%d]", s->name, index);
#endif
               er = revk_setting_internal(s, 0, NULL, index, 0);        // Factory default
            }
            void storesub(void) {
               setting_t *q;
               for (q = setting; q; q = q->next)
                  if (q->child && q->namelen > s->namelen && !strncmp(s->name, q->name, s->namelen))
                     q->used = 0;
               t = jo_next(j);  // In to object
               while (t && t != JO_CLOSE && !er)
               {
                  if (t == JO_TAG)
                  {
                     int l2 = jo_strlen(j);
                     char *tag2 = malloc(s->namelen + l2 + 1);
                     if (tag2)
                     {
                        strcpy(tag2, s->name);
                        jo_strncpy(j, (char *) tag2 + s->namelen, l2 + 1);
                        t = jo_next(j); // To value
                        for (q = setting; q && (!q->child || strcmp(q->name, tag2)); q = q->next);
                        if (!q)
                           er = "Unknown setting";
                        else
                        {
                           q->used = 1;
                           store(q);
                        }
                        free(tag2);
                     }
                  }
                  t = jo_skip(j);
               }
               for (q = setting; q; q = q->next)
                  if (!q->used && q->child && q->namelen > s->namelen && !strncmp(s->name, q->name, s->namelen))
                     zap(q);
            }
            if (t == JO_OBJECT)
            {
               if (!s->parent)
                  er = "Unexpected object";
               else
                  storesub();
            } else if (t == JO_ARRAY)
            {
               if (!s->array)
                  er = "Not an array";
               else
               {
                  t = jo_next(j);       // In to array
                  while (index < s->array && t != JO_CLOSE && !er)
                  {
                     if (t == JO_OBJECT)
                        storesub();
                     else if (t == JO_ARRAY)
                        er = "Unexpected array";
                     else
                        store(s);
                     t = jo_next(j);
                     index++;
                  }
                  while (index < s->array)
                  {
                     zap(s);
                     if (s->parent)
                        for (setting_t * q = setting; q; q = q->next)
                           if (q->child && q->namelen > s->namelen && !strncmp(s->name, q->name, s->namelen))
                              zap(q);
                     index++;
                  }
               }
            } else
               store(s);
         }
         free(tag);
         t = jo_next(j);
      }
   }
   jo_free(&j);
   return er;
}

const char *revk_command(const char *tag, unsigned int len, const void *value)
{
   if (!tag || !*tag)
      return "No command";
   ESP_LOGD(TAG, "MQTT command [%s]", tag);
   const char *e = NULL;
   /* My commands */
   if (!e && !strcmp(tag, "upgrade"))
   {
      char *url;                /* Yeh, not freed, but we are rebooting */
      if (len && (!strncmp((char *) value, "https://", 8) || !strncmp((char *) value, "http://", 7)))   /* Yeh allowing http as
                                                                                                         * code is signed anyway */
         url = strdup((char *) value);
      else
         asprintf(&url, "%s://%s/%s.bin",
#ifdef CONFIG_SECURE_SIGNED_ON_UPDATE
                  *otacert ? "https" : "http",
#else
                  "https",      /* If not signed, use https even if no cert pinned */
#endif
                  len ? (char *) value : otahost, appname);
      e = revk_ota(url);
   }
   if (!e && watchdogtime && !strcmp(tag, "watchdog"))
   {                            /* Test watchdog */
      wdt_test = 1;
      return "";
   }
   if (!e && !strcmp(tag, "restart"))
      e = revk_restart("Restart command", 5);
   if (!e && !strcmp(tag, "factory") && len == strlen(revk_id) + strlen(appname) && !strncmp((char *) value, revk_id, strlen(revk_id)) && !strcmp((char *) value + strlen(revk_id), appname))
   {
      esp_err_t e = nvs_flash_erase();
      if (!e)
         e = nvs_flash_erase_partition(TAG);
      if (e)
         return "Erase failed";
      revk_restart("Factory reset", 5);
      return "";
   }
   if (!e && !strcmp(tag, "uptime"))
   {
      uint64_t t = esp_timer_get_time();
      revk_info(tag, "%d.%06d", (uint32_t) (t / 1000000LL), (uint32_t) (t % 1000000LL));
      return "";
   }
#ifdef	CONFIG_REVK_APCONFIG
   if (!e && !strcmp(tag, "apconfig") && !ap_task_id)
   {
      ap_task_id = revk_task("AP", ap_task, NULL);
      return "";
   }
#endif
   /* App commands */
   if ((!e || !*e) && app_command)
   {                            /* Pass to app, even if we handled with no error */
      const char *e2 = app_command(tag, len, value);
      if (e2 && (*e2 || !e))
         e = e2;                /* Overwrite error if we did not have one */
   }
   return e;
}

void revk_register(const char *name, uint8_t array, uint16_t size, void *data, const char *defval, uint8_t flags)
{                               /* Register setting (not expected to be thread safe, should be called from init) */
   if (flags & SETTING_BITFIELD)
   {
      if (!defval)
         ESP_LOGE(TAG, "%s missing defval on bitfield", name);
      else if (!size)
         ESP_LOGE(TAG, "%s missing size on bitfield", name);
      else
      {
         const char *p = defval;
         while (*p && *p != ' ')
            p++;
         if ((p - defval) > 8 * size - ((flags & SETTING_SET) ? 1 : 0))
            ESP_LOGE(TAG, "%s too small for bitfield", name);
      }
   }
   setting_t *s;
   for (s = setting; s && strcmp(s->name, name); s = s->next);
   if (s)
      ESP_LOGE(TAG, "%s duplicate", name);
   s = malloc(sizeof(*s));
   memset(s, 0, sizeof(*s));
   s->nvs = nvs;
   s->name = name;
   s->namelen = strlen(name);
   s->array = array;
   s->size = size;
   s->data = data;
   s->flags = flags;
   s->defval = defval;
   s->next = setting;
   if (!(flags & SETTING_SECRET))
   {                            // Check if sub setting - parent must be set first, and be secret and same array size
      setting_t *q;
      for (q = setting; q && (q->namelen >= s->namelen || strncmp(q->name, name, q->namelen) || !(q->flags & SETTING_SECRET) || q->array != array); q = q->next);
      if (q)
      {
         s->child = 1;
         q->parent = 1;
         if (s->data == q->data)
            q->dup = 1;
      }
   }
   setting = s;
   memset(data, 0, (size ? : sizeof(void *)) * (!(flags & SETTING_BOOLEAN) && array ? array : 1));      /* Initialise memory */
   /* Get value */
   int get_val(const char *tag, int index) {
      void *data = s->data;
      if (s->array && !(flags & SETTING_BOOLEAN))
         data += (s->size ? : sizeof(void *)) * index;
      int l = -1;
      if (!s->size)
      {                         /* Dynamic */
         void *d = NULL;
         l = nvs_get(s, tag, NULL, 0);
         if (l > 1)
         {                      /* 1 byte means zero len or zero terminated so use default */
            d = malloc(l);
            l = nvs_get(s, tag, d, l);
            if (l > 0)
               *((void **) data) = d;
            else
               free(d);         /* Should not happen */
         } else
            l = -1;             /* default */
      } else
         l = nvs_get(s, tag, data, s->size);    /* Stored static */
      return l;
   }
   const char *e;
   if (array)
   {                            /* Work through tags */
      int i;
      for (i = 0; i < array; i++)
      {
         char tag[16];          /* NVS tag size */
         if (snprintf(tag, sizeof(tag), "%s%u", s->name, i + 1) < sizeof(tag) && get_val(tag, i) < 0)
         {
            e = revk_setting_internal(s, 0, NULL, i, SETTING_LIVE);     /* Defaulting logic */
            if (e && *e)
               ESP_LOGE(TAG, "Setting %s failed %s", tag, e);
            else
               ESP_LOGD(TAG, "Setting %s created", tag);
         }
      }
   } else if (get_val(s->name, 0) < 0)
   {                            /* Simple setting, not array */
      e = revk_setting_internal(s, 0, NULL, 0, SETTING_LIVE);   /* Defaulting logic */
      if (e && *e)
         ESP_LOGE(TAG, "Setting %s failed %s", s->name, e);
      else
         ESP_LOGD(TAG, "Setting %s created", s->name);
   }
}

#if CONFIG_LOG_DEFAULT_LEVEL > 3
esp_err_t revk_err_check(esp_err_t e, const char *file, int line, const char *func, const char *cmd)
{
   if (e != ERR_OK)
   {
      const char *fn = strrchr(file, '/');
      if (fn)
         fn++;
      else
         fn = file;
      ESP_LOGE(TAG, "Error %s at line %d in %s (%s)", esp_err_to_name(e), line, fn, cmd);
      jo_t j = jo_object_alloc();
      jo_int(j, "code", e);
      jo_string(j, "description", esp_err_to_name(e));
      jo_string(j, "file", fn);
      jo_int(j, "line", line);
      jo_string(j, "function", func);
      jo_string(j, "command", cmd);
      revk_errorj(NULL, &j);
   }
   return e;
}
#else
esp_err_t revk_err_check(esp_err_t e)
{
   if (e != ERR_OK)
   {
      ESP_LOGE(TAG, "Error %s", esp_err_to_name(e));
      jo_t j = jo_object_alloc();
      jo_int(j, "code", e);
      jo_string(j, "description", esp_err_to_name(e));
      revk_errorj(NULL, &j);
   }
   return e;
}
#endif

#ifdef	CONFIG_REVK_MQTT
const char *revk_mqtt(void)
{
   if (mqtt_index < 0)
      return "";
   return mqtthost[mqtt_index];
}
#endif

#ifdef	CONFIG_REVK_WIFI
const char *revk_wifi(void)
{
   if (wifi_index < 0)
      return "";
   return wifissid[wifi_index];
}
#endif

void revk_blink(uint8_t on, uint8_t off)
{
   blink_on = on;
   blink_off = off;
}

#if     defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
uint32_t revk_offline(void)
{                               /* How long off  line */
   if (!lastonline)
      return 1;                 /* Not online yet */
   int64_t now = esp_timer_get_time();
   if (now < lastonline)
      return 0;
   return ((now - lastonline) / 1000000LL) ? : 1;
}
#endif

#ifdef	CONFIG_REVK_MQTT
void revk_mqtt_close(const char *reason)
{
   if (!mqtt_client)
      return;
   ESP_LOGI(TAG, "MQTT Close");
   jo_t j = jo_object_alloc();
   jo_bool(j, "up", 0);
   jo_string(j, "reason", reason);
   revk_statej(NULL, &j);
   mqtt_index = -2;             /* Don't reconnect */
   esp_mqtt_client_stop(mqtt_client);
   usleep(10000);               /* we don't get event, but need to allow time */
   ESP_LOGI(TAG, "MQTT Closed");
}
#endif

#ifdef	CONFIG_REVK_WIFI
void revk_wifi_close(void)
{
   ESP_LOGI(TAG, "WIFi Close");
   wifi_index = -2;             /* Do not reconnect */
   esp_wifi_set_mode(WIFI_MODE_NULL);
   esp_wifi_deinit();
   ESP_LOGI(TAG, "WIFi Closed");
}
#endif

#ifdef	CONFIG_REVK_WIFI
int revk_wait_wifi(int seconds)
{
   ESP_LOGD(TAG, "Wait WiFi %d", seconds);
   return xEventGroupWaitBits(revk_group, GROUP_WIFI, false, true, seconds * 1000 / portTICK_PERIOD_MS) & GROUP_WIFI;
}
#endif

#ifdef	CONFIG_REVK_MQTT
int revk_wait_mqtt(int seconds)
{
   ESP_LOGD(TAG, "Wait MQTT %d", seconds);
   return xEventGroupWaitBits(revk_group, GROUP_MQTT, false, true, seconds * 1000 / portTICK_PERIOD_MS) & GROUP_MQTT;
}
#endif

const char *revk_appname(void)
{
   return appname;
}

const char *revk_hostname(void)
{
   return hostname;
}
