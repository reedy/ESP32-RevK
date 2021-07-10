/*
 * Main control code, working with WiFi, MQTT, and managing settings and OTA Copyright Â ©2019 Adrian Kennard Andrews & Arnold Ltd
 */
static const char
    __attribute__((unused)) * TAG = "RevK";

//#define       SETTING_DEBUG
#define		SETTING_CHANGED

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
#ifdef	CONFIG_REVK_MESH
#include <esp_mesh.h>
#endif

#ifndef	CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS
#warning CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS recommended
#endif

#ifndef	CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER
#warning CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER recommended
#endif

//#ifndef       CONFIG_MBEDTLS_DYNAMIC_BUFFER
//#warning CONFIG_MBEDTLS_DYNAMIC_BUFFER recommended
//#endif
#ifdef	CONFIG_MBEDTLS_DYNAMIC_BUFFER
#warning CONFIG_MBEDTLS_DYNAMIC_BUFFER is buggy, sadly
#endif

#if CONFIG_FREERTOS_HZ != 1000
#warning CONFIG_FREERTOS_HZ recommend set to 1000
#endif

#ifndef CONFIG_TASK_WDT_PANIC
#warning CONFIG_TASK_WDT_PANIC recommended
#endif

#ifndef CONFIG_MQTT_BUFFER_SIZE
#define	CONFIG_MQTT_BUFFER_SIZE 2048
#endif

#ifdef	CONFIG_REVK_MESH
#define	MQTT_MAX MESH_MPS
#else
#define	MQTT_MAX CONFIG_MQTT_BUFFER_SIZE
#endif

#define	MQTT_CLIENTS	2
#define	settings	\
		s(otahost,CONFIG_REVK_OTAHOST);		\
		bd(otacert,CONFIG_REVK_OTACERT);		\
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
		ioa(blink,3);				\
    		bdp(clientkey,NULL);			\
    		bd(clientcert,NULL);			\

#define	apconfigsettings	\
		u32(apport,CONFIG_REVK_APPORT);		\
		u32(aptime,CONFIG_REVK_APTIME);		\
		u32(apwait,CONFIG_REVK_APWAIT);		\
		io(apgpio);		\

#define	mqttsettings	\
		sa(mqtthost,MQTT_CLIENTS,CONFIG_REVK_MQTTHOST);	\
		sa(mqttuser,MQTT_CLIENTS,CONFIG_REVK_MQTTUSER);	\
		sap(mqttpass,MQTT_CLIENTS,CONFIG_REVK_MQTTPASS);	\
		u16a(mqttport,MQTT_CLIENTS,CONFIG_REVK_MQTTPORT);	\
		bad(mqttcert,MQTT_CLIENTS,CONFIG_REVK_MQTTCERT);	\

#define	wifisettings	\
		u16(wifireset,CONFIG_REVK_WIFIRESET);	\
		s(wifissid,CONFIG_REVK_WIFISSID);	\
		s(wifiip,CONFIG_REVK_WIFIIP);		\
		s(wifigw,CONFIG_REVK_WIFIGW);		\
		sa(wifidns,3,CONFIG_REVK_WIFIDNS);		\
		h(wifibssid,6,CONFIG_REVK_WIFIBSSID);	\
		u8(wifichan,CONFIG_REVK_WIFICHAN);	\
		sp(wifipass,CONFIG_REVK_WIFIPASS);	\

#define wifimqttsettings		\
    		u16(wifiretry,CONFIG_REVK_WIFIRETRY);			\
 		s(wifimqtt, NULL);	\

#define	apsettings	\
		s(apssid,CONFIG_REVK_APSSID);		\
		sp(appass,CONFIG_REVK_APPASS);		\
    		u8(apmax,CONFIG_REVK_APMAX);	\
		s(apip,CONFIG_REVK_APIP);		\
		b(aplr,CONFIG_REVK_APLR);		\
		b(aphide,CONFIG_REVK_APHIDE);		\

#define	meshsettings	\
		h(meshid,6,CONFIG_REVK_MESHID);		\
		hs(meshkey,16,NULL);		\
    		u16(meshwidth,CONFIG_REVK_MESHWIDTH);	\
    		u16(meshmax,CONFIG_REVK_MESHMAX);	\
		sp(meshpass,CONFIG_REVK_MESHPASS);	\
		b(meshlr,CONFIG_REVK_MESHLR);		\

#define s(n,d)		char *n;
#define sp(n,d)		char *n;
#define snl(n,d)	char *n;
#define sa(n,a,d)	char *n[a];
#define sap(n,a,d)	char *n[a];
#define fh(n,a,s,d)	char n[a][s];
#define	u32(n,d)	uint32_t n;
#define	u16(n,d)	uint16_t n;
#define	u16a(n,a,d)	uint16_t n[a];
#define	i16(n)		int16_t n;
#define	u8a(n,a,d)	uint8_t n[a];
#define	u8(n,d)		uint8_t n;
#define	b(n,d)		uint8_t n;
#define	s8(n,d)		int8_t n;
#define	io(n)		uint8_t n;
#define	ioa(n,a)	uint8_t n[a];
#define p(n)		char *prefix##n;
#define h(n,l,d)	char n[l];
#define hs(n,l,d)	char n[l];
#define bd(n,d)		revk_bindata_t *n;
#define bad(n,a,d)	revk_bindata_t *n[a];
#define bdp(n,d)	revk_bindata_t *n;
settings
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
    wifisettings
#ifdef	CONFIG_REVK_MQTT_SERVER
    wifimqttsettings
#endif
#ifdef	CONFIG_REVK_MESH
    meshsettings
#else
    apsettings
#endif
#endif
#ifdef	CONFIG_REVK_MQTT
    mqttsettings
#endif
#ifdef	CONFIG_REVK_APCONFIG
    apconfigsettings
#endif
#undef s
#undef sp
#undef snl
#undef sa
#undef sap
#undef fh
#undef u32
#undef u16
#undef u16a
#undef i16
#undef u8
#undef b
#undef u8a
#undef s8
#undef io
#undef ioa
#undef p
#undef h
#undef hs
#undef bd
#undef bad
#undef bdp
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
static volatile uint64_t offline = 1;   // When we first went off line
static volatile uint64_t offline_try = 1;       // When we last tried to get on line
#ifdef	CONFIG_REVK_MQTT_SERVER
static char wifimqttbackup = 0;
#endif
const static int GROUP_OFFLINE = BIT0;  // We are off line (IP not set)
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
const static int GROUP_WIFI = BIT1;     // We are WiFi connected
const static int GROUP_IP = BIT2;       // We have IP address
#endif
#ifdef	CONFIG_REVK_APCONFIG
const static int GROUP_APCONFIG = BIT3; // We are running AP config
const static int GROUP_APCONFIG_DONE = BIT4;    // Config done
const static int GROUP_APCONFIG_NONE = BIT5;    // No stations connected
#endif
#ifdef	CONFIG_REVK_MQTT
const static int GROUP_MQTT = BIT6 /*7... */ ;  // We are MQTT connected - and MORE BITS (MQTT_CLIENTS)
const static int GROUP_MQTT_DOWN = (GROUP_MQTT << MQTT_CLIENTS);        /*... */
#endif
static TaskHandle_t ota_task_id = NULL;
#ifdef	CONFIG_REVK_APCONFIG
static TaskHandle_t ap_task_id = NULL;
#endif
static app_callback_t *app_callback = NULL;
lwmqtt_t mqtt_client[MQTT_CLIENTS] = { };

static int64_t restart_time = 0;
static int64_t nvs_time = 0;
static uint8_t setting_dump_requested = 0;
static const char *restart_reason = "Unknown";
static nvs_handle nvs = -1;
static setting_t *setting = NULL;
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
#endif
static char wdt_test = 0;
static uint8_t blink_on = 0,
    blink_off = 0;
static const char *blink_colours = "RYGCBM";
static const char *revk_setting_dump(void);

/* Local functions */
#ifdef	CONFIG_REVK_APCONFIG
static void ap_task(void *pvParameters);
#endif
#ifdef	CONFIG_REVK_MQTT
static void mqtt_init(void);
#endif

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void mqtt_rx(void *arg, char *topic, unsigned short plen, unsigned char *payload);
void make_mesh_mqtt(mesh_data_t * data, int client, int tlen, const char *topic, int plen, const unsigned char *payload, char retain);

static void revk_report_state(int copies)
{                               // Report state
   const esp_app_desc_t *app = esp_ota_get_app_description();
   uint64_t t = esp_timer_get_time();
   jo_t j = jo_object_alloc();
   jo_litf(j, "up", "%d.%06d", (uint32_t) (t / 1000000LL), (uint32_t) (t % 1000000LL));
#ifdef	CONFIG_SECURE_BOOT
   jo_bool(j, "secureboot", 1);
#endif
#ifdef	CONFIG_NVS_ENCRYPTION
   jo_bool(j, "nvsecryption", 1);
#endif
   jo_string(j, "id", revk_id);
   jo_string(j, "app", appname);
   jo_string(j, "version", revk_version);
   const char *v = app->date;
   if (v && strlen(v) == 11)
   {                            // Stupid format Jul 10 2021
      char date[11];
      sprintf(date, "%s-xx-%.2s", v + 7, v + 4);
      const char mname[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
      for (int m = 0; m < 12; m++)
         if (!strncmp(mname + m * 3, v, 3))
         {
            date[5] = '0' + (m + 1) / 10;
            date[6] = '0' + (m + 1) % 10;
            break;
         }
      jo_stringf(j, "build", "%sT%s", date, app->time);
   }
   jo_int(j, "mem", esp_get_free_heap_size());
   jo_int(j, "flash", spi_flash_get_chip_size());
   time_t now = time(0);
   if (now > 1000000000)
      jo_int(j, "time", now);
   jo_int(j, "rst", esp_reset_reason());
   wifi_ap_record_t ap = { };
   if (!esp_wifi_sta_get_ap_info(&ap) && ap.primary)
   {
      jo_string(j, "ssid", (char *) ap.ssid);
      jo_stringf(j, "bssid", "%02X%02X%02X:%02X%02X%02X", (uint8_t) ap.bssid[1], (uint8_t) ap.bssid[2], (uint8_t) ap.bssid[3], (uint8_t) ap.bssid[4], (uint8_t) ap.bssid[5]);
      jo_int(j, "rssi", ap.rssi);
      jo_int(j, "chan", ap.primary);
      if (ap.phy_lr)
         jo_bool(j, "lr", 1);
   }
   revk_state_copy(NULL, &j, copies);
}

#if defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
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
   freez(i);
}
#endif

#ifdef CONFIG_REVK_MESH
static void child_init(void)
{                               // We are a child, send login details, etc.
   revk_report_state(MQTT_CLIENTS - 1); // Send status
}
#endif

#ifdef CONFIG_REVK_MESH
void mesh_decode(mesh_data_t * data)
{                               // Security - decode mesh message
   // TODO - mesh decode
}
#endif

#ifdef CONFIG_REVK_MESH
void mesh_encode(mesh_data_t * data)
{                               // Security - encode mesh message
   // TODO - mesh encode
}
#endif

#ifdef CONFIG_REVK_MESH
static void mesh_task(void *pvParameters)
{                               // Mesh root
   pvParameters = pvParameters;
   mesh_data_t data = { };
   data.data = malloc(MESH_MPS);
   char isroot = 0;
   while (1)
   {
      if (esp_mesh_is_root())
      {
         if (!isroot)
         {                      // We have become root
            isroot = 1;
            mqtt_init();
            // Any other init
         }
      } else
      {
         if (isroot)
         {                      // We are no longer root
            isroot = 0;
            revk_mqtt_close("Not root");
            // Any other cleanup
         }
      }
      if (!esp_mesh_is_device_active())
      {
         sleep(1);
         continue;
      }
      mesh_addr_t from = { };
      data.size = MESH_MPS;
      int flag = 0;
      if (!esp_mesh_recv(&from, &data, 1000, &flag, NULL, 0))
      {
         ESP_LOGD(TAG, "Mesh rx size=%d proto=%d tos=%d flag=%d %02X%02X%02X%02X%02X%02X", data.size, data.proto, data.tos, flag, from.addr[0], from.addr[1], from.addr[2], from.addr[3], from.addr[4], from.addr[5]);
         mesh_decode(&data);
         // We use MESH_PROTO_MQTT to relay
         // We use MESH_PROTO_JSON for messages internally
         if (data.proto == MESH_PROTO_MQTT)
         {
            // Extract topic and payload
            // Topic prefix digit for client
            // Topic then prefix + for retain
            char retain = 0;
            int client = 0;
            char *e = (char *) data.data + data.size;
            char *topic = (char *) data.data;
            if (*topic >= '0' && *topic < '0' + MQTT_CLIENTS)
               client = *topic++ - '0';
            if (*topic == '+')
               retain = (*topic++ == '+');
            char *payload = topic;
            while (payload < e && *payload)
               payload++;
            if (payload == e)
               continue;        // We expect topic ending in NULL
            payload++;
            char *target = topic;
            while (*target && *target != '/')
               target++;        // clear the command
            if (!*target)
               continue;        // Uh
            target++;
            while (*target && *target != '/')
               target++;        // clear the appname
            if (!*target)
               continue;        // Uh
            target++;
            char *suffix = target;
            while (*suffix && *suffix != '/')
               suffix++;
            if (*suffix)
               suffix++;
            else
               suffix = NULL;
            jo_t j = NULL;
            if (e > payload)
               j = jo_parse_mem(payload, e - payload);
            ESP_LOGD(TAG, "client=%d retain=%d topic=%s target=%s suffix=%s", client, retain, topic, target ? : "", suffix ? : "");
            if (isroot)
               lwmqtt_send_full(mqtt_client[client], -1, topic, e - payload, (void *) payload, retain); // Out
            else
               mqtt_rx((void *) client, topic, e - payload, (void *) payload);  // In
            jo_free(&j);
         } else if (data.proto == MESH_PROTO_JSON)
         {                      // Internal message
            if (isroot)
            {
               // TODO - Mesh control to root

            } else
            {
               // TODO - Mesh control to child

            }
         }
      }
   }
   vTaskDelete(NULL);
}
#endif

#if defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
static void setup_ip(void)
{                               // Set up DHCPC / fixed IP
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
      freez(i);
   }
   if (*wifiip)
   {
      esp_netif_dhcpc_stop(sta_netif);
      esp_netif_ip_info_t info = { 0, };
      makeip(&info, wifiip, wifigw);
      REVK_ERR_CHECK(esp_netif_set_ip_info(sta_netif, &info));
      ESP_LOGI(TAG, "Fixed IP %s GW %s", wifiip, wifigw);
      if (!*wifidns[0])
         dns(wifiip, ESP_NETIF_DNS_MAIN);       /* Fallback to using gateway for DNS */
   } else
      esp_netif_dhcpc_start(sta_netif); /* Dynamic IP */
   dns(wifidns[0], ESP_NETIF_DNS_MAIN);
   dns(wifidns[1], ESP_NETIF_DNS_BACKUP);
   dns(wifidns[2], ESP_NETIF_DNS_FALLBACK);
#ifndef	CONFIG_REVK_MESH
#ifdef  CONFIG_REVK_MQTT
   if (*wifiip)
      mqtt_init();              // Won't start on GOT_IP
#endif
#endif
}
#endif

#if defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
static void stop_ip(void)
{
   esp_netif_dhcpc_stop(sta_netif);
}
#endif

#if defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
static void sta_init(void)
{
   REVK_ERR_CHECK(esp_event_loop_create_default());
   REVK_ERR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   REVK_ERR_CHECK(esp_wifi_init(&cfg));
   REVK_ERR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
   REVK_ERR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
   sta_netif = esp_netif_create_default_wifi_sta();
   ap_netif = esp_netif_create_default_wifi_ap();
}
#endif

#ifdef	CONFIG_REVK_WIFI
static void wifi_init(void)
{
   if (!sta_netif)
      sta_init();
   else
      REVK_ERR_CHECK(esp_wifi_stop());
   // Mode
   esp_wifi_set_mode(*apssid ? WIFI_MODE_APSTA : WIFI_MODE_STA);
   // Client
   REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
   const char *ssid = wifissid;
#ifdef	CONFIG_REVK_MQTT_SERVER
   if (*wifimqtt && !wifimqttbackup)
      ssid = wifimqtt;
#endif
   ESP_LOGI(TAG, "WIFi [%s]", ssid);
   wifi_config_t wifi_config = { 0, };
   if (wifibssid[0] || wifibssid[1] || wifibssid[2])
   {
      memcpy(wifi_config.sta.bssid, wifibssid, sizeof(wifi_config.sta.bssid));
      wifi_config.sta.bssid_set = 1;
   }
   wifi_config.sta.channel = wifichan;
   wifi_config.sta.scan_method = ((esp_reset_reason() == ESP_RST_DEEPSLEEP) ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
   strncpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
   strncpy((char *) wifi_config.sta.password, wifipass, sizeof(wifi_config.sta.password));
   REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   setup_ip();
   // Doing AP mode after STA mode - seems to fail is not
   if (*apssid)
   {                            // AP config
      wifi_config_t wifi_config = { 0, };
      wifi_config.ap.channel = wifichan;
      wifi_config.ap.ssid_len = strlen(apssid);
      if (wifi_config.ap.ssid_len > sizeof(wifi_config.ap.ssid))
         wifi_config.ap.ssid_len = sizeof(wifi_config.ap.ssid);
      memcpy((char *) wifi_config.ap.ssid, apssid, wifi_config.ap.ssid_len);
      if (*appass)
      {
         strncpy((char *) wifi_config.ap.password, appass, sizeof(wifi_config.ap.password));
         wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
      }
      wifi_config.ap.ssid_hidden = aphide;
      wifi_config.ap.max_connection = apmax;
      esp_netif_ip_info_t info = { 0, };
      makeip(&info, *apip ? apip : "10.0.0.1/24", NULL);
      REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, aplr ? WIFI_PROTOCOL_LR : (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)));
      REVK_ERR_CHECK(esp_netif_dhcps_stop(ap_netif));
      REVK_ERR_CHECK(esp_netif_set_ip_info(ap_netif, &info));
      REVK_ERR_CHECK(esp_netif_dhcps_start(ap_netif));
      REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
      ESP_LOGI(TAG, "WIFiAP [%s]%s%s", apssid, aphide ? " (hidden)" : "", aplr ? " (LR)" : "");
   }
   REVK_ERR_CHECK(esp_wifi_start());
   REVK_ERR_CHECK(esp_wifi_connect());
}
#endif

#ifdef	CONFIG_REVK_MESH
static void mesh_init(void)
{
   // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_mesh.html
   if (!sta_netif)
   {
      sta_init();
      REVK_ERR_CHECK(esp_netif_dhcps_stop(ap_netif));
      //REVK_ERR_CHECK(esp_netif_dhcpc_stop(sta_netif)); // We stop when child connects
      REVK_ERR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
      REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, meshlr ? WIFI_PROTOCOL_LR : (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)));
      REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
      REVK_ERR_CHECK(esp_wifi_start());
      REVK_ERR_CHECK(esp_mesh_init());
      REVK_ERR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));
      mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
      memcpy((uint8_t *) & cfg.mesh_id, meshid, 6);
      cfg.channel = wifichan;
      if (!wifichan)
         cfg.allow_channel_switch = 1;
      cfg.router.ssid_len = strlen(wifissid);
      strncpy((char *) cfg.router.ssid, wifissid, sizeof(cfg.router.ssid));
      if (*wifipass)
         strncpy((char *) &cfg.router.password, wifipass, strlen(wifipass));
      cfg.mesh_ap.max_connection = meshwidth;
      strncpy((char *) &cfg.mesh_ap.password, meshpass, sizeof(cfg.mesh_ap.password));
      REVK_ERR_CHECK(esp_mesh_set_config(&cfg));
      if (meshmax)
         REVK_ERR_CHECK(esp_mesh_set_capacity_num(meshmax));
      REVK_ERR_CHECK(esp_mesh_disable_ps());
      revk_task("Mesh", mesh_task, NULL);
   }
   REVK_ERR_CHECK(esp_mesh_start());
}
#endif

#ifdef	CONFIG_REVK_MQTT
static void mqtt_rx(void *arg, char *topic, unsigned short plen, unsigned char *payload)
{                               // Expects to be able to write over topic
   int client = (int) arg;
   if (client < 0 || client >= MQTT_CLIENTS)
      return;
   if (topic)
   {
      const char *err = NULL;
      // Break up topic
      char *prefix = topic;
      char *target = "?";
      char *suffix = NULL;
      char *appname = NULL;
      char *p = topic;
      while (*p && *p != '/')
         p++;
      if (*p)
      {                         // Expect app name next
         appname = ++p;
         while (*p && *p != '/')
            p++;
      }
      if (*p)
      {
         target = ++p;
         while (*p && *p != '/')
            p++;
      }
      if (*p)
         suffix = ++p;
      jo_t j = NULL;
      if (plen)
      {
         if (*payload != '"' && *payload != '{' && *payload != '[')
         {                      // Looks like non JSON
            if (prefix && suffix && !strcmp(prefix, prefixsetting))
            {                   // Special case for settings, the suffix is the setting
               j = jo_object_alloc();
               jo_stringf(j, suffix, "%.*s", plen, payload);
            } else
            {                   // Just JSON the argument
               j = jo_create_alloc();
               jo_stringf(j, NULL, "%.*s", plen, payload);
            }
         } else
         {                      // Parse JSON argument
            j = jo_parse_mem(payload, plen);
            jo_skip(j);         // Check whole JSON
            int pos;
            err = jo_error(j, &pos);
            if (err)
               ESP_LOGE(TAG, "Fail at pos %d, %s: %s", pos, err, jo_debug(j));
         }
         jo_rewind(j);
      }
#ifdef	CONFIG_REVK_MESH
      if (*target == '*' || strncmp(target, revk_id, strlen(revk_id)))
      {                         // pass on to clients
         mesh_data_t data = {.proto = MESH_PROTO_MQTT };
         make_mesh_mqtt(&data, client, -1, topic, plen, payload, 0);
         mesh_addr_t addr = {.addr = { 255, 255, 255, 255, 255, 255 }
         };
         if (*target != '*')
            for (int n = 0; n < sizeof(addr); n++)
               addr.addr[n] = (((target[n * 2] & 0xF) + (target[n * 2] > '9' ? 9 : 0)) << 4) + ((target[1 + n * 2] & 0xF) + (target[1 + n * 2] > '9' ? 9 : 0));
         esp_mesh_send(&addr, &data, MESH_DATA_P2P, NULL, 0);   // TODO - re-entrant issue?
         free(data.data);
      }
#endif
      // Break up topic
      if (appname)
         appname[-1] = 0;
      if (target)
         target[-1] = 0;
      if (suffix)
         suffix[-1] = 0;
      if (!strcmp(target, "*") || !strcmp(target, revk_id))
         target = NULL;         // Mark as us for simple testing by app_command, etc
      if (!client && !target)
      {                         // For us (could otherwise be for app callback)
         if (prefix && !strcmp(prefix, prefixcommand))
            err = (err ? : revk_command(suffix, j));
         else if (prefix && !strcmp(prefix, prefixsetting))
         {
            if (!suffix && !plen)
            {
               setting_dump_requested = 1;
               err = "";
            } else
               err = ((err ? : revk_setting(j)) ? : "Unknown setting");
         } else
            err = (err ? : ""); // Ignore
      }
      if ((!err || !*err) && app_callback)
      {                         /* Pass to app, even if we handled with no error */
         jo_rewind(j);
         const char *e2 = app_callback(client, prefix, target, suffix, j);
         if (e2 && (*e2 || !err))
            err = e2;           /* Overwrite error if we did not have one */
      }
      jo_free(&j);
      if (!err)
         err = "Unknown";
      if (*err)
      {
         jo_t j = jo_object_alloc();
         jo_string(j, "description", err);
         if (prefix)
            jo_string(j, "prefix", prefix);
         if (target)
            jo_string(j, "target", target);
         if (suffix)
            jo_string(j, "suffix", suffix);
         if (plen)
            jo_stringf(j, "payload", "%s", payload);
         revk_error(suffix, &j);
      }
   } else if (payload)
   {
      ESP_LOGI(TAG, "MQTT%d connected %s", client, (char *) payload);
      xEventGroupSetBits(revk_group, (GROUP_MQTT << client));
      xEventGroupClearBits(revk_group, (GROUP_MQTT_DOWN << client));
      if (!client)
      {                         // main MQTT - subscribes
         void sub(const char *prefix) {
            char *topic = NULL;
            if (asprintf(&topic, "%s/%s/%s/#", prefix, appname, revk_id) < 0)
               return;
            lwmqtt_subscribe(mqtt_client[client], topic);
            freez(topic);
            if (asprintf(&topic, "%s/%s/*/#", prefix, appname) < 0)
               return;
            lwmqtt_subscribe(mqtt_client[client], topic);
            freez(topic);
            if (*hostname && strcmp(hostname, revk_id))
            {
               if (asprintf(&topic, "%s/%s/%s/#", prefix, appname, hostname) < 0)
                  return;
               lwmqtt_subscribe(mqtt_client[client], topic);
               freez(topic);
            }
         }
         sub(prefixcommand);
         sub(prefixsetting);
         // TODO We need subscribes for all mesh connected devices as well...
      }
      revk_report_state(-client);
      if (app_callback)
      {
         jo_t j = jo_create_alloc();
         jo_string(j, NULL, (char *) payload);
         app_callback(client, prefixcommand, NULL, "connect", j);
         jo_free(&j);
      }
   } else
   {
      if (xEventGroupGetBits(revk_group) & (GROUP_MQTT << client))
      {
         xEventGroupSetBits(revk_group, (GROUP_MQTT_DOWN << client));
         xEventGroupClearBits(revk_group, (GROUP_MQTT << client));
         ESP_LOGI(TAG, "MQTT%d disconnected (mem:%d)", client, esp_get_free_heap_size());
         if (app_callback)
            app_callback(client, prefixcommand, NULL, "disconnect", NULL);
         // Can we flush TCP TLS stuff somehow?
      } else
         ESP_LOGI(TAG, "MQTT%d failed (mem:%d)", client, esp_get_free_heap_size());
   }
}
#endif

#ifdef	CONFIG_REVK_MQTT
static void mqtt_init(void)
{
   if (mqtt_client[0])
      return;                   // Already set up
   if (!*mqtthost[0]
#ifdef	CONFIG_REVK_MQTT_SERVER
       && !*wifimqtt
#endif
       )                        /* No MQTT */
      return;
   for (int client = 0; client < MQTT_CLIENTS; client++)
   {
      xEventGroupSetBits(revk_group, (GROUP_MQTT_DOWN << client));
      if (*mqtthost[client])
      {
#ifdef	CONFIG_REVK_MQTT_SERVER
         if (!client)
         {
            esp_netif_ip_info_t info = { };
            static char gw[16] = "";
            if (*wifimqtt && !wifimqttbackup && (!sta_netif || esp_netif_get_ip_info(sta_netif, &info) || !info.gw.addr))
               return;
         }
#endif
         char *topic = NULL;
         if (asprintf(&topic, "%s/%s/%s", prefixstate, appname, *hostname ? hostname : revk_id) < 0)
            return;
         lwmqtt_client_config_t config = {
            .arg = (void *) client,
            .hostname = mqtthost[client],
            .topic = topic,
            .retain = 1,
            .payload = (void *) "{\"up\":false}",
            .plen = -1,
            .keepalive = 30,
            .callback = &mqtt_rx,
         };
#ifdef	CONFIG_REVK_MQTT_SERVER
         if (!client && *wifimqtt && !wifimqttbackup)
         {                      // Special case - server is gateway IP
            config.tlsname = wifimqtt;  // The device name of the host if using TLS
            config.tlsname_ref = 1;     // No need to duplicate
            sprintf(gw, "%d.%d.%d.%d", info.gw.addr & 255, (info.gw.addr >> 8) & 255, (info.gw.addr >> 16) & 255, info.gw.addr >> 24);
            config.hostname = gw;       // safe on stack as lwmqtt_client copies it
            sntp_setservername(0, gw);
         } else
            sntp_setservername(0, ntphost);
#endif
         ESP_LOGI(TAG, "MQTT%d %s", client, config.hostname);
         if (mqttcert[client]->len)
         {
            config.ca_cert_ref = 1;     // No need to duplicate
            config.ca_cert_buf = (void *) mqttcert[client]->data;
            config.ca_cert_bytes = mqttcert[client]->len;
         } else if (mqttport[client] == 8883)
            config.crt_bundle_attach = esp_crt_bundle_attach;
         if (clientkey->len && clientcert->len)
         {
            config.client_cert_ref = 1; // No need to duplicate
            config.client_cert_buf = (void *) clientcert->data;
            config.client_cert_bytes = clientcert->len;
            config.client_key_ref = 1;  // No need to duplicate
            config.client_key_buf = (void *) clientkey->data;
            config.client_key_bytes = clientkey->len;
         }
         if (*mqttuser[client])
            config.username = mqttuser[client];
         if (*mqttpass[client])
            config.password = mqttpass[client];
         config.port = mqttport[client];
         mqtt_client[client] = lwmqtt_client(&config);
         freez(topic);
      }
   }
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
         if (app_callback)
         {
            jo_t j = jo_create_alloc();
            jo_string(j, "ssid", apssid);
            app_callback(0, prefixcommand, NULL, "ap", j);
            jo_free(&j);
         }
         break;
      case WIFI_EVENT_STA_START:
         ESP_LOGI(TAG, "STA Start");
         break;
      case WIFI_EVENT_STA_STOP:
         ESP_LOGI(TAG, "STA Stop");
         xEventGroupClearBits(revk_group, GROUP_WIFI | GROUP_IP);
         xEventGroupSetBits(revk_group, GROUP_OFFLINE);
         if (!offline)
            offline_try = offline = esp_timer_get_time();
         break;
      case WIFI_EVENT_STA_CONNECTED:
         ESP_LOGI(TAG, "STA Connected");
         xEventGroupSetBits(revk_group, GROUP_WIFI);
         xEventGroupClearBits(revk_group, GROUP_OFFLINE);
         break;
      case WIFI_EVENT_STA_DISCONNECTED:
         ESP_LOGI(TAG, "STA Disconnect");
         xEventGroupClearBits(revk_group, GROUP_WIFI | GROUP_IP);
         xEventGroupSetBits(revk_group, GROUP_OFFLINE);
         if (!offline)
            offline_try = offline = esp_timer_get_time();
         esp_wifi_connect();
         break;
         // AP
      case WIFI_EVENT_AP_STOP:
         ESP_LOGI(TAG, "AP Stop");
         break;
      case WIFI_EVENT_AP_STACONNECTED:
#ifdef CONFIG_REVK_APCONFIG
         xEventGroupClearBits(revk_group, GROUP_APCONFIG_NONE);
#endif
         ESP_LOGI(TAG, "AP STA Connect");
         break;
      case WIFI_EVENT_AP_STADISCONNECTED:
#ifdef CONFIG_REVK_APCONFIG
         xEventGroupSetBits(revk_group, GROUP_APCONFIG_DONE | GROUP_APCONFIG_NONE);
#endif
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
         if (!offline)
            offline_try = offline = esp_timer_get_time();
         break;
      case IP_EVENT_STA_GOT_IP:
         {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            wifi_ap_record_t ap = { };
            REVK_ERR_CHECK(esp_wifi_sta_get_ap_info(&ap));
            ESP_LOGI(TAG, "Got IP " IPSTR " from %s", IP2STR(&event->ip_info.ip), (char *) ap.ssid);
            xEventGroupSetBits(revk_group, GROUP_IP);
            offline = 0;
            sntp_stop();
            sntp_init();
#ifdef	CONFIG_REVK_MQTT
            mqtt_init();
#endif
#ifdef  CONFIG_REVK_WIFI
            xEventGroupSetBits(revk_group, GROUP_WIFI);
            if (app_callback)
            {
               jo_t j = jo_object_alloc();
               jo_string(j, "ssid", (char *) ap.ssid);
               if (ap.phy_lr)
                  jo_bool(j, "lr", 1);
               jo_stringf(j, "ip", IPSTR, IP2STR(&event->ip_info.ip));
               jo_stringf(j, "gw", IPSTR, IP2STR(&event->ip_info.gw));
#ifdef	CONFIG_REVK_MQTT_SERVER
               if (*wifimqtt && !wifimqttbackup)
                  jo_bool(j, "slave", 1);
#endif
               jo_rewind(j);
               app_callback(0, prefixcommand, NULL, "wifi", j);
               jo_free(&j);
            }
#endif
         }
         break;
      case IP_EVENT_GOT_IP6:
         ESP_LOGI(TAG, "Got IPv6");
         break;
      }
   }
#ifdef	CONFIG_REVK_MESH
   if (event_base == MESH_EVENT)
   {
      ESP_LOGI(TAG, "Mesh event %d", event_id);
      switch (event_id)
      {                         // TODO debug level a lot of these
      case MESH_EVENT_STARTED:
         break;
      case MESH_EVENT_STOPPED:
         ESP_LOGI(TAG, "STA Stop");
         xEventGroupClearBits(revk_group, GROUP_WIFI | GROUP_IP);
         xEventGroupSetBits(revk_group, GROUP_OFFLINE);
         if (!offline)
            offline_try = offline = esp_timer_get_time();
         break;
      case MESH_EVENT_CHANNEL_SWITCH:
         break;
      case MESH_EVENT_CHILD_CONNECTED: // A child connected to us
         ESP_LOGI(TAG, "Child connected");
         break;
      case MESH_EVENT_CHILD_DISCONNECTED:      // A child disconnected from us
         ESP_LOGI(TAG, "Child disconnected");
         break;
      case MESH_EVENT_ROUTING_TABLE_ADD:
         ESP_LOGI(TAG, "Routing update");
         // TODO subscribe if root
         break;
      case MESH_EVENT_ROUTING_TABLE_REMOVE:
         // TODO unsubscribe if root
         ESP_LOGI(TAG, "Routing remove");
         break;
      case MESH_EVENT_PARENT_CONNECTED:
         {
            if (esp_mesh_is_root())
            {
               ESP_LOGI(TAG, "Mesh root");
               setup_ip();
            } else
            {
               ESP_LOGI(TAG, "Mesh child");
               stop_ip();
            }
         }
         break;
      case MESH_EVENT_PARENT_DISCONNECTED:
         ESP_LOGI(TAG, "Mesh disconnected");
         stop_ip();
         break;
      case MESH_EVENT_NO_PARENT_FOUND:
         ESP_LOGI(TAG, "No mesh found");
         break;
      case MESH_EVENT_LAYER_CHANGE:
         break;
      case MESH_EVENT_TODS_STATE:
         ESP_LOGI(TAG, "toDS state");
         break;
      case MESH_EVENT_VOTE_STARTED:
         break;
      case MESH_EVENT_VOTE_STOPPED:
         break;
      case MESH_EVENT_ROOT_ADDRESS:
         ESP_LOGI(TAG, "Root has IP");
         // TODO a root mqtt connect message?
         child_init();
         break;
      case MESH_EVENT_ROOT_SWITCH_REQ:
         break;
      case MESH_EVENT_ROOT_SWITCH_ACK:
         break;
      case MESH_EVENT_ROOT_ASKED_YIELD:
         break;
      case MESH_EVENT_ROOT_FIXED:
         break;
      case MESH_EVENT_SCAN_DONE:
         break;
      case MESH_EVENT_NETWORK_STATE:
         break;
      case MESH_EVENT_STOP_RECONNECTION:
         break;
      case MESH_EVENT_FIND_NETWORK:
         break;
      case MESH_EVENT_ROUTER_SWITCH:
         break;
      case MESH_EVENT_PS_PARENT_DUTY:
         break;
      case MESH_EVENT_PS_CHILD_DUTY:
         break;
      case MESH_EVENT_PS_DEVICE_DUTY:
         break;
      default:
         ESP_LOGI(TAG, "Unknown mesh event %d", event_id);
      }
   }
#endif
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
      if (blink[0])
      {                         // LED blinking
         if (blink[1] && blink_colours)
         {                      // Colours
            static const char *c = "";
            if (!*c)
               c = blink_colours;
            gpio_set_level(blink[0] & 0x3F, (*c == 'R' || *c == 'Y' || *c == 'M' || *c == 'W') ^ ((blink[0] & 0x40) ? 1 : 0));  // Red LED
            gpio_set_level(blink[1] & 0x3F, (*c == 'G' || *c == 'Y' || *c == 'C' || *c == 'W') ^ ((blink[1] & 0x40) ? 1 : 0));  // Green LED
            gpio_set_level(blink[2] & 0x3F, (*c == 'B' || *c == 'C' || *c == 'M' || *c == 'W') ^ ((blink[2] & 0x40) ? 1 : 0));  // Blue LED
            c++;
         } else
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
                  gpio_set_level(blink[0] & 0x3F, lit ^ ((blink[0] & 0x40) ? 1 : 0));   // Single LED
            }
         }
      }
      if (setting_dump_requested)
      {                         // Done here so not reporting from MQTT
         setting_dump_requested = 0;
         revk_setting_dump();
      }
      if (restart_time && restart_time < now && !ota_task_id)
      {                         /* Restart */
         if (!restart_reason)
            restart_reason = "Unknown";
         if (app_callback)
         {
            jo_t j = jo_create_alloc();
            jo_string(j, NULL, restart_reason);
            jo_rewind(j);
            app_callback(0, prefixcommand, NULL, "shutdown", j);
            jo_free(&j);
         }
         revk_mqtt_close(restart_reason);
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
         revk_wifi_close();
#endif
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
      {                         // Report even if not on-line as mesh works anyway
         static int lastch = 0;
         static uint8_t lastbssid[6];
         static uint32_t lastheap = 0;
         static uint32_t was = 0;       // Yes time_t but ESP has an odd idea of time_t
         uint32_t heap = esp_get_free_heap_size();
         wifi_ap_record_t ap = {
         };
         esp_wifi_sta_get_ap_info(&ap);
         uint32_t now = time(0);
         if (lastch != ap.primary || memcmp(lastbssid, ap.bssid, 6) || lastheap / 10240 != heap / 10240 || now > was + 300)
         {
            if (now > 1000000000 && was <= 1000000000)
               ESP_LOGD(TAG, "Clock set %u", now);
            was = now;
            lastheap = heap;
            lastch = ap.primary;
            memcpy(lastbssid, ap.bssid, 6);
            revk_report_state(0);
         }
      }
#endif
#ifdef	CONFIG_REVK_WIFI
      if (wifireset && offline && (now - offline) > 1000000LL * wifireset)
         revk_restart("Offline too long", 1);
#endif
#ifdef	CONFIG_REVK_MQTT_SERVER
      if (*wifimqtt && wifiretry && offline && (now - offline_try) > 1000000LL * wifiretry)
      {
         offline_try = esp_timer_get_time();
         wifimqttbackup = 1 - wifimqttbackup;
         revk_mqtt_close("backup flip");
         mqtt_init();
         wifi_init();           // Must be wifi not mesh
      }
#endif
#ifdef	CONFIG_REVK_APCONFIG
      if (!ap_task_id && ((apgpio && (gpio_get_level(apgpio & 0x3F) ^ (apgpio & 0x40 ? 1 : 0)))
#if     defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
                          || (apwait && (now - offline_try) > 1000000LL * apwait)
#endif
#ifdef	CONFIG_REVK_WIFI
                          || !*wifissid
#endif
          ))
         ap_task_id = revk_task("AP", ap_task, NULL);   /* Start AP mode */
#endif
   }
}

/* External functions */
void revk_init(app_callback_t * app_callback_cb)
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
   freez(mem);
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
#define fh(n,a,s,d)	revk_register(#n,a,s,&n,d,SETTING_BINDATA|SETTING_HEX)
#define	u32(n,d)	revk_register(#n,0,4,&n,str(d),0)
#define	u16(n,d)	revk_register(#n,0,2,&n,str(d),0)
#define	u16a(n,a,d)	revk_register(#n,a,2,&n,str(d),0)
#define	i16(n)		revk_register(#n,0,2,&n,0,SETTING_SIGNED)
#define	u8a(n,a,d)	revk_register(#n,a,1,&n,str(d),0)
#define	u8(n,d)		revk_register(#n,0,1,&n,str(d),0)
#define	b(n,d)		revk_register(#n,0,1,&n,str(d),SETTING_BOOLEAN)
#define	s8(n,d)		revk_register(#n,0,1,&n,str(d),SETTING_SIGNED)
#define io(n)		revk_register(#n,0,sizeof(n),&n,"-",SETTING_SET|SETTING_BITFIELD)
#define ioa(n,a)	revk_register(#n,a,sizeof(*n),&n,"-",SETTING_SET|SETTING_BITFIELD)
#define p(n)		revk_register("prefix"#n,0,0,&prefix##n,#n,0)
#define h(n,l,d)	revk_register(#n,0,l,&n,d,SETTING_BINDATA|SETTING_HEX)
#define hs(n,l,d)	revk_register(#n,0,l,&n,d,SETTING_BINDATA|SETTING_HEX|SETTING_SECRET)
#define bd(n,d)		revk_register(#n,0,0,&n,d,SETTING_BINDATA)
#define bad(n,a,d)	revk_register(#n,a,0,&n,d,SETTING_BINDATA)
#define bdp(n,d)	revk_register(#n,0,0,&n,d,SETTING_BINDATA|SETTING_SECRET)
   settings;
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
   revk_register("wifi", 0, 0, &wifissid, CONFIG_REVK_WIFISSID, SETTING_SECRET);        // Parent
   wifisettings;
#ifdef	CONFIG_REVK_MQTT_SERVER
   wifimqttsettings;
#endif
#ifdef	CONFIG_REVK_MESH
   revk_register("mesh", 0, 6, &meshid, CONFIG_REVK_MESHID, SETTING_BINDATA | SETTING_HEX | SETTING_SECRET);    // Parent
   meshsettings;
#else
#ifdef	CONFIG_REVK_WIFI
   revk_register("ap", 0, 0, &apssid, CONFIG_REVK_APSSID, SETTING_SECRET);      // Parent
   apsettings;
#endif
#endif
#endif
#ifdef	CONFIG_REVK_MQTT
   revk_register("mqtt", 0, 0, &mqtthost, CONFIG_REVK_MQTTHOST, SETTING_SECRET);        // Parent
   mqttsettings;
#endif
#ifdef	CONFIG_REVK_APCONFIG
   apconfigsettings;
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
#undef ioa
#undef p
#undef str
#undef h
#undef hs
#undef bd
#undef bad
#undef bdp
   if (watchdogtime)
      esp_task_wdt_init(watchdogtime, true);
   REVK_ERR_CHECK(nvs_open(app->project_name, NVS_READWRITE, &nvs));
   /* Application specific settings */
   if (!*appname)
      appname = strdup(app->project_name);
   /* Default is from build */
   for (int b = 0; b < sizeof(blink) / sizeof(*blink); b++)
      if (blink[b])
      {
         gpio_reset_pin(blink[b] & 0x3F);
         gpio_set_level(blink[b] & 0x3F, (blink[b] & 0x40) ? 0 : 1);    /* on */
         gpio_set_direction(blink[b] & 0x3F, GPIO_MODE_OUTPUT); /* Blinking LED */
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
   setenv("TZ", tz, 1);
   tzset();
   sntp_setservername(0, ntphost);
   app_callback = app_callback_cb;
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
   revk_group = xEventGroupCreate();
   xEventGroupSetBits(revk_group, GROUP_OFFLINE);
#ifdef	CONFIG_REVK_WIFI
   wifi_init();
#endif
#ifdef	CONFIG_REVK_MESH
   mesh_init();
#endif
   /* DHCP */
   char *id = NULL;
   if (*hostname)
      asprintf(&id, "%s-%s", appname, hostname);
   else
      asprintf(&id, "%s-%06llX", appname, revk_binid & 0xFFFFFF);
   esp_netif_set_hostname(sta_netif, id);
   esp_netif_create_ip6_linklocal(sta_netif);
   freez(id);
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
void make_mesh_mqtt(mesh_data_t * data, int client, int tlen, const char *topic, int plen, const unsigned char *payload, char retain)
{
   memset(data, 0, sizeof(*data));
   data->proto = MESH_PROTO_MQTT;
   if (plen < 0)
      plen = strlen((char *) payload);
   if (tlen < 0)
      tlen = strlen(topic);
   data->size = tlen + 1 + plen;
   if (client)
      data->size++;
   if (retain)
      data->size++;
   data->data = malloc(data->size);
   char *p = (char *) data->data;
   if (client)
      *p++ = '0' + client;
   if (retain)
      *p++ = '+';
   strcpy(p, topic);
   p += tlen + 1;
   if (plen)
      memcpy(p, payload, plen);
   p += plen;
}
#endif

#ifdef	CONFIG_REVK_MQTT
const char *revk_mqtt_out(int client, int tlen, const char *topic, int plen, const unsigned char *payload, char retain)
{
#ifdef	CONFIG_REVK_MESH
   if (!mqtt_client[client] && esp_mesh_is_device_active() && !esp_mesh_is_root())
   {                            // Send via mesh
      mesh_data_t data = {.proto = MESH_PROTO_MQTT };
      make_mesh_mqtt(&data, client, tlen, topic, plen, payload, retain);
      ESP_LOGD(TAG, "Sending MQTT via mesh");
      esp_mesh_send(NULL, &data, 0, NULL, 0);   // TODO - re-entrant issue?
      free(data.data);
      return NULL;
   }
#endif
   return lwmqtt_send_full(mqtt_client[client], tlen, topic, plen, payload, retain);
}
#endif

void revk_mqtt_send_raw(const char *topic, int retain, const char *payload, int copies)
{
#ifdef	CONFIG_REVK_MQTT
   int from = 0,
       to = 0;
   if (copies > 0)
      to = copies;
   else
      from = to = -copies;
   if (to >= MQTT_CLIENTS)
      to = MQTT_CLIENTS - 1;
   for (int client = from; client <= to; client++)
   {
      ESP_LOGD(TAG, "MQTT%d publish %s (%s)", client, topic ? : "-", payload);
      revk_mqtt_out(client, -1, topic, -1, (void *) payload, retain);
   }
#endif
}

void revk_mqtt_send_str_copy(const char *str, int retain, int copies)
{
#ifdef	CONFIG_REVK_MQTT
   if (!str)
      return;
   int from = 0,
       to = 0;
   if (copies > 0)
      to = copies;
   else
      from = to = -copies;
   if (to >= MQTT_CLIENTS)
      to = MQTT_CLIENTS - 1;
   const char *e = str;
   while (*e && *e != ' ');
   const char *p = e;
   if (*p)
      p++;
   for (int client = from; client <= to; client++)
   {
      ESP_LOGD(TAG, "MQTT%d publish %.*s (%s)", client, e - str, str, p);
      revk_mqtt_out(client, e - str, str, -1, (void *) p, retain);
   }
#endif
}


void revk_mqtt_send_copy(const char *prefix, int retain, const char *tag, jo_t * jp, int copies)
{                               // Send to main, and N additional MQTT servers, or only to extra server N if copies -ve
#ifdef	CONFIG_REVK_MQTT
   char *payload = NULL;
   if (jp)
   {
      int pos = 0;
      const char *err = jo_error(*jp, &pos);
      payload = jo_finisha(jp);
      if (!payload && err)
         ESP_LOGE(TAG, "JSON error sending %s/%s (%s) at %d", prefix ? : "", tag ? : "", err, pos);
   }
   char *topic = NULL;
   if (!prefix)
      topic = (char *) tag;     /* Set fixed topic */
   else if (asprintf(&topic, tag ? "%s/%s/%s/%s" : "%s/%s/%s", prefix, appname, *hostname ? hostname : revk_id, tag) < 0)
      topic = NULL;
   if (!topic)
   {
      freez(payload);
      return;
   }
   revk_mqtt_send_raw(topic, retain, payload, copies);
   if (topic != tag)
      freez(topic);
   freez(payload);
#endif
}

void revk_state_copy(const char *tag, jo_t * jp, int copies)
{                               // State message (retained)
   revk_mqtt_send_copy(prefixstate, 1, tag, jp, copies);
}

void revk_event_copy(const char *tag, jo_t * jp, int copies)
{                               // Event message (may one day create log entries)
   revk_mqtt_send_copy(prefixevent, 0, tag, jp, copies);
}

void revk_error_copy(const char *tag, jo_t * jp, int copies)
{                               // Error message, waits a while for connection if possible before sending
   xEventGroupWaitBits(revk_group,
#ifdef	CONFIG_REVK_WIFI
                       GROUP_WIFI |
#endif
                       GROUP_MQTT, false, true, 20000 / portTICK_PERIOD_MS);
   revk_mqtt_send_copy(prefixerror, 0, tag, jp, copies);
}

void revk_info_copy(const char *tag, jo_t * jp, int copies)
{                               // Info message, nothing special
   revk_mqtt_send_copy(prefixinfo, 0, tag, jp, copies);
}

const char *revk_restart(const char *reason, int delay)
{                               /* Restart cleanly */
   if (restart_reason != reason)
      ESP_LOGI(TAG, "Restart %d %s", delay, reason);
   restart_reason = reason;
   if (delay < 0)
      restart_time = 0;         /* Cancelled */
   else
   {
      restart_time = esp_timer_get_time() + 1000000LL * (int64_t) delay;        /* Reboot now */
      if (app_callback)
      {
         jo_t j = jo_create_alloc();
         jo_string(j, NULL, reason);
         jo_rewind(j);
         app_callback(0, prefixcommand, NULL, "restart", j);
         jo_free(&j);
      }
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
               jo_t j = jo_object_alloc();
               jo_string(j, "description", "No OTA partition available");
               revk_error("upgrade", &j);
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
                  revk_info("upgrade", &j);
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
                  revk_info("upgrade", &j);
                  next = now + 5000000LL;
               }
            }
         }
      }
      break;
   case HTTP_EVENT_ON_FINISH:
      if (!ota_running && esp_http_client_get_status_code(evt->client) / 100 > 3)
      {
         jo_t j = jo_object_alloc();
         jo_string(j, "description", "Failed to start");
         jo_int(j, "code", esp_http_client_get_status_code(evt->client));
         jo_int(j, "size", ota_size);
         revk_error("Upgrade", &j);
      }
      if (ota_running)
      {
         if (!REVK_ERR_CHECK(esp_ota_end(ota_handle)))
         {
            jo_t j = jo_object_alloc();
            jo_string(j, "complete", ota_partition->label);
            jo_int(j, "size", ota_size);
            revk_info("upgrade", &j);
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
               jo_t j = jo_object_alloc();
               jo_object(j, "wifi");
               jo_string(j, "ssid", ssid);
               jo_string(j, "pass", pass);
               revk_setting(j);
               jo_free(&j);
            }
         }
         {
            char host[129];
            if (!httpd_query_key_value(query, "host", host, sizeof(host)) && *host)
            {
               jo_t j = jo_object_alloc();
               jo_object(j, "mqtt");
               jo_string(j, "host", host);
               revk_setting(j);
               jo_free(&j);
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
   xEventGroupClearBits(revk_group, GROUP_APCONFIG_DONE);
   xEventGroupSetBits(revk_group, GROUP_APCONFIG | GROUP_APCONFIG_NONE);
   if (!*apssid)
   {                            // If we are not running an AP already
      if (xEventGroupGetBits(revk_group) & GROUP_WIFI)
      {
         esp_wifi_disconnect();
         xEventGroupWaitBits(revk_group, GROUP_OFFLINE, false, true, 1000 / portTICK_PERIOD_MS);
      }
      REVK_ERR_CHECK(esp_wifi_stop());
      {                         /* IP */
         esp_netif_ip_info_t info = {
            0,
         };
         IP4_ADDR(&info.ip, 10, revk_binid >> 8, revk_binid, 1);
         info.gw = info.ip;     /* We are the gateway */
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
      if (xEventGroupGetBits(revk_group) & GROUP_WIFI)
      {
         REVK_ERR_CHECK(esp_wifi_disconnect());
         xEventGroupWaitBits(revk_group, GROUP_OFFLINE, false, true, 1000 / portTICK_PERIOD_MS);
      }
#endif
      REVK_ERR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
      REVK_ERR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
      REVK_ERR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
      REVK_ERR_CHECK(esp_wifi_start());
   }
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
      if (!(xEventGroupWaitBits(revk_group, GROUP_APCONFIG_DONE, true, true, (aptime ? : 3600) * 1000LL / portTICK_PERIOD_MS) & GROUP_APCONFIG_DONE))
         xEventGroupWaitBits(revk_group, GROUP_APCONFIG_NONE, true, true, 60 * 1000LL / portTICK_PERIOD_MS);    // Wait for disconnect if not done yet
      else
         sleep(2);              // Allow http close cleanly
      //Send reply maybe...
      REVK_ERR_CHECK(httpd_stop(server));
   }
   offline_try = esp_timer_get_time();  // Don't retry instantly
   xEventGroupClearBits(revk_group, GROUP_APCONFIG | GROUP_APCONFIG_DONE);
   if (!*apssid)
   {
      REVK_ERR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      REVK_ERR_CHECK(esp_wifi_connect());
   }
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
   revk_info("upgrade", &j);
   esp_http_client_config_t config = {
      .url = url,.event_handler = ota_handler,
   };
   /* Set the TLS in case redirect to TLS even if http */
   if (otacert->len)
   {
      config.cert_pem = (void *) otacert->data;
      config.cert_len = otacert->len;
   } else
#ifdef	CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
      config.crt_bundle_attach = esp_crt_bundle_attach;
#else
      config.use_global_ca_store = true;        /* Global cert */
#endif
   if (clientcert->len && clientkey->len)
   {
      config.client_cert_pem = (void *) clientcert->data;
      config.client_cert_len = clientcert->len;
      config.client_key_pem = (void *) clientkey->data;
      config.client_key_len = clientkey->len;
   }
   esp_http_client_handle_t client = esp_http_client_init(&config);
   if (!client)
   {
      jo_t j = jo_object_alloc();
      jo_string(j, "description", "HTTP client failed");
      revk_error("upgrade", &j);
   } else
   {
      esp_err_t err = REVK_ERR_CHECK(esp_http_client_perform(client));
      int status = esp_http_client_get_status_code(client);
      esp_http_client_cleanup(client);
      freez(url);
      if (!err && status / 100 != 2)
      {
         jo_t j = jo_object_alloc();
         jo_string(j, "description", "HTTP failed");
         jo_int(j, "code", status);
         revk_error("upgrade", &j);
      }
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
   if (s->flags & SETTING_BINDATA)
   {
      if (s->size || !data)
      {                         // Fixed size, or getting len
         if ((err = nvs_get_blob(s->nvs, tag, data, &len)) != ERR_OK)
            return -err;
         if (!s->size)
            len += sizeof(revk_bindata_t);
         return len;
      }
      len -= sizeof(revk_bindata_t);
      revk_bindata_t *d = data;
      d->len = len;
      if ((err = nvs_get_blob(s->nvs, tag, d->data, &len)) != ERR_OK)
         return -err;
      return len + sizeof(revk_bindata_t);
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
   if (s->flags & SETTING_BINDATA)
   {
      if (s->size)
         return nvs_set_blob(s->nvs, tag, data, s->size);       // Fixed size - just store
      // Variable size, store the size it is
      revk_bindata_t *d = data;
      return nvs_set_blob(s->nvs, tag, d->data, d->len);        // Variable
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
   unsigned char *temp = NULL;  // Malloced space to be freed
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
      if (s->flags & SETTING_BINDATA)
      {                         // Convert to binary
         jo_t j = jo_create_alloc();
         jo_string(j, NULL, defval);
         jo_rewind(j);
         int l;
         if (s->flags & SETTING_HEX)
         {
            l = jo_strncpy16(j, NULL, 0);
            if (l > 0)
               jo_strncpy16(j, temp = malloc(l), l);
         } else
         {
            l = jo_strncpy64(j, NULL, 0);
            if (l > 0)
               jo_strncpy64(j, temp = malloc(l), l);
         }
         value = temp;          // temp gets freed at end
         len = l;
         jo_free(&j);
      } else
      {
         len = strlen(defval);
         value = (const unsigned char *) defval;
      }
      erase = 1;
   }
   if (!value)
   {
      value = (const unsigned char *) "";
      erase = 1;
   } else
      s->set = 1;
#ifdef SETTING_DEBUG
   if (s->flags & SETTING_BINDATA)
      ESP_LOGI(TAG, "%s=(%d bytes)", (char *) tag, len);
   else
      ESP_LOGI(TAG, "%s=%.*s", (char *) tag, len, (char *) value);
#endif
   const char *parse(void) {
      /* Parse new setting */
      unsigned char *n = NULL;
      int l = len;
      if (flags & SETTING_BINDATA)
      {                         /* Blob */
         unsigned char *o;
         if (!s->size)
         {                      /* Dynamic */
            l += sizeof(revk_bindata_t);
            revk_bindata_t *d = malloc(l);
            o = n = (void *) d;
            if (o)
            {
               d->len = len;
               if (len)
                  memcpy(d->data, value, len);
            }
         } else
         {                      // Fixed size binary
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
      {                         /* String */
         l++;
         n = malloc(l);         /* One byte for null termination */
         if (len)
            memcpy(n, value, len);
         n[len] = 0;
      } else
      {                         /* Numeric */
         uint64_t v = 0;
         if (flags & SETTING_BOOLEAN)
         {                      /* Boolean */
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
            {                   /* Set top bit if a value is present */
               bits--;
               if (len && value != (const unsigned char *) defval)
                  bitfield |= (1ULL << bits);   /* Value is set (not so if using default value) */
            }
            if (flags & SETTING_BITFIELD && s->defval)
            {                   /* Bit fields */
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
            {                   /* Decimal */
               len--;
               value++;
               neg = 1;
            }
            if (flags & SETTING_HEX)
               while (len && isxdigit(*value))
               {                /* Hex */
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
      int o = nvs_get(s, tag, NULL, 0); // Get length
#ifdef SETTING_DEBUG
      if (o < 0 && o != -ESP_ERR_NVS_NOT_FOUND)
         ESP_LOGI(TAG, "Setting %s nvs read fail %s", tag, esp_err_to_name(-o));
#endif
      if (o != l)
      {
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
         if (o >= 0)
            ESP_LOGI(TAG, "Setting %s different len %d/%d", tag, o, l);
#endif
         o = -1;                /* Different size */
      }
      if (o > 0)
      {
         unsigned char *d = malloc(l);
         if (nvs_get(s, tag, d, l) != o)
         {
            freez(n);
            freez(d);
            return "Bad setting get";
         }
         if (memcmp(n, d, o))
         {
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            ESP_LOGI(TAG, "Setting %s different content %d (%02X%02X%02X%02X/%02X%02X%02X%02X)", tag, o, d[0], d[1], d[2], d[3], n[0], n[1], n[2], n[3]);
#endif
            o = -1;             /* Different content */
         }
         freez(d);
      }
      if (o < 0)
      {                         /* Flash changed */
         if (erase)
         {
            esp_err_t __attribute__((unused)) err = nvs_erase_key(s->nvs, tag);
            if (err == ESP_ERR_NVS_NOT_FOUND)
               o = 0;
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            else
               ESP_LOGI(TAG, "Setting %s erased", tag);
#endif
         } else
         {
            if (nvs_set(s, tag, n) != ERR_OK && (nvs_erase_key(s->nvs, tag) != ERR_OK || nvs_set(s, tag, n) != ERR_OK))
            {
               freez(n);
               return "Unable to store";
            }
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            if (flags & SETTING_BINDATA)
               ESP_LOGI(TAG, "Setting %s stored (%d)", tag, len);
            else
               ESP_LOGI(TAG, "Setting %s stored %.*s", tag, len, value);
#endif
         }
         nvs_time = esp_timer_get_time() + 60000000LL;
      }
      if (flags & SETTING_LIVE)
      {                         /* Store changed value in memory live */
         if (!s->size)
         {                      /* Dynamic */
            void *o = *((void **) data);
            /* See if different */
            if (!o || ((flags & SETTING_BINDATA) ? memcmp(o, n, len) : strcmp(o, (char *) n)))
            {
               *((void **) data) = n;
               freez(o);
            } else
               freez(n);        /* No change */
         } else
         {                      /* Static (try and make update atomic) */
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
            freez(n);
         }
      } else if (o < 0)
         revk_restart("Settings changed", 5);
      return NULL;
   }
   const char *fail = parse();
   freez(temp);
   return fail;                 /* OK */
}

static const char *revk_setting_dump(void)
{                               // Dump settings (in JSON)
   const char *err = NULL;
   jo_t j = NULL;
   void send(void) {
      if (!j)
         return;
      revk_mqtt_send(prefixsetting, 0, NULL, &j);
   }
   char buf[MQTT_MAX - 100];
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
            if (s->flags & SETTING_BINDATA)
            {                   // Binary data
               int len = s->size;
               if (!len)
               {                // alloc'd with len at start
                  revk_bindata_t *d = *(void **) data;
                  len = d->len;
                  data = d->data;
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
                  {
                     jo_t j = jo_object_alloc();
                     jo_string(j, "description", "Setting did not fit");
                     jo_string(j, "setting", tag);
                     if (err)
                        jo_string(j, "reason", err);
                     revk_error(TAG, &j);
                  }
                  freez(tag);
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
         {
            jo_t j = jo_object_alloc();
            jo_string(j, "description", "Setting did not fit");
            jo_string(j, "setting", s->name);
            if (err)
               jo_string(j, "reason", err);
            revk_error(TAG, &j);
         }
      }
   }
   send();
   return NULL;
}

const char *revk_setting(jo_t j)
{
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
   const char *er = NULL;
   jo_type_t t = jo_next(j);    // Start object
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
            ESP_LOGI(TAG, "Unknown %s %.20s", tag, jo_debug(j));
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
                  if (t == JO_STRING && (s->flags & SETTING_BINDATA))
                  {
                     if (s->flags & SETTING_HEX)
                     {
                        l = jo_strncpy16(j, NULL, 0);
                        if (l)
                           jo_strncpy16(j, val = malloc(l), l);
                     } else
                     {
                        l = jo_strncpy64(j, NULL, 0);
                        if (l)
                           jo_strncpy64(j, val = malloc(l), l);
                     }
                  } else
                  {
                     l = jo_strlen(j);
                     if (l >= 0)
                        jo_strncpy(j, val = malloc(l + 1), l + 1);
                  }
                  er = revk_setting_internal(s, l, (const unsigned char *) (val ? : ""), index, 0);
               } else if (t == JO_NULL)
                  er = revk_setting_internal(s, 0, NULL, index, 0);     // Factory
               else
                  er = "Bad data type";
               freez(val);
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
                        {
                           ESP_LOGI(TAG, "Unknown %s %.20s", tag2, jo_debug(j));
                           er = "Unknown setting";
                        } else
                        {
                           q->used = 1;
                           store(q);
                        }
                        freez(tag2);
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
         freez(tag);
         t = jo_next(j);
      }
   }

   return er ? : "";
}

const char *revk_command(const char *tag, jo_t j)
{
   if (!tag || !*tag)
      return "No command";
   ESP_LOGD(TAG, "MQTT command [%s]", tag);
   const char *e = NULL;
   /* My commands */
   if (!e && !strcmp(tag, "status"))
   {
      revk_report_state(0);
      e = "";
   }
   if (!e && !strcmp(tag, "upgrade"))
   {
      char val[256];
      if (jo_strncpy(j, val, sizeof(val)) < 0)
         *val = 0;
      char *url;                /* Yeh, not freed, but we are rebooting */
      if (!strncmp((char *) val, "https://", 8) || !strncmp((char *) val, "http://", 7))
         url = strdup(val);
      else
         asprintf(&url, "%s://%s/%s.bin",
#ifdef CONFIG_SECURE_SIGNED_ON_UPDATE
                  otacert->len ? "https" : "http",
#else
                  "http",       /* If not signed, use http as code should be signed and this uses way less memory  */
#endif
                  *val ? val : otahost, appname);
      e = revk_ota(url);
   }
   if (!e && watchdogtime && !strcmp(tag, "watchdog"))
   {                            /* Test watchdog */
      wdt_test = 1;
      return "";
   }
   if (!e && !strcmp(tag, "restart"))
      e = revk_restart("Restart command", 5);
   if (!e && !strcmp(tag, "factory"))
   {
      char val[256];
      if (jo_strncpy(j, val, sizeof(val)) < 0)
         *val = 0;
      if (strncmp(val, revk_id, strlen(revk_id)))
         return "Bad ID";
      if (strcmp(val + strlen(revk_id), appname))
         return "Bad appname";
      esp_err_t e = nvs_flash_erase();
      if (!e)
         e = nvs_flash_erase_partition(TAG);
      if (!e)
         revk_restart("Factory reset", 5);
      return "";
   }
#ifdef	CONFIG_REVK_APCONFIG
   if (!e && !strcmp(tag, "apconfig") && !ap_task_id)
   {
      ap_task_id = revk_task("AP", ap_task, NULL);
      return "";
   }
#endif
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
         if (l >= 0)
         {                      // Has data
            d = malloc(l);
            l = nvs_get(s, tag, d, l);
            *((void **) data) = d;
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

#if CONFIG_LOG_DEFAULT_LEVEL > 2
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
      revk_error(NULL, &j);
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
      revk_error(NULL, &j);
   }
   return e;
}
#endif

#ifdef	CONFIG_REVK_MQTT
lwmqtt_t revk_mqtt(int client)
{
   if (client >= MQTT_CLIENTS)
      return NULL;
   return mqtt_client[client];
}
#endif

#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
const char *revk_wifi(void)
{
   return wifissid;
}
#endif

void revk_blink(uint8_t on, uint8_t off, const char *colours)
{
   blink_on = on;
   blink_off = off;
   blink_colours = colours;
}

#if     defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MQTT)
uint32_t revk_offline(void)
{
   if (!offline)
      return 0;                 // On line
   return (esp_timer_get_time() - offline) / 1000000ULL ? : 1;
}
#endif

#ifdef	CONFIG_REVK_MQTT
void revk_mqtt_close(const char *reason)
{
   for (int client = 0; client < MQTT_CLIENTS; client++)
   {
      jo_t j = jo_object_alloc();
      jo_bool(j, "up", 0);
      jo_string(j, "id", revk_id);
      jo_string(j, "reason", reason);
      revk_state_copy(NULL, &j, -client);
      if (mqtt_client[client])
      {
         lwmqtt_end(&mqtt_client[client]);
         ESP_LOGI(TAG, "MQTT%d Closed", client);
         xEventGroupWaitBits(revk_group, GROUP_MQTT_DOWN << client, false, true, 2 * 1000 / portTICK_PERIOD_MS);
      }
   }
}
#endif

#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
void revk_wifi_close(void)
{
   ESP_LOGI(TAG, "WIFi Close");
#ifdef	CONFIG_REVK_MESH
   if (esp_mesh_is_root())
   {
      esp_mesh_waive_root(NULL, MESH_VOTE_REASON_ROOT_INITIATED);
      sleep(1);
   }
   esp_mesh_stop();
   esp_mesh_deinit();
#endif
   esp_wifi_set_mode(WIFI_MODE_NULL);
   esp_wifi_deinit();
   ESP_LOGI(TAG, "WIFi Closed");
}
#endif

#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
int revk_wait_wifi(int seconds)
{
   ESP_LOGD(TAG, "Wait WiFi %d", seconds);
   return xEventGroupWaitBits(revk_group, GROUP_IP, false, true, seconds * 1000 / portTICK_PERIOD_MS) & GROUP_IP;
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
