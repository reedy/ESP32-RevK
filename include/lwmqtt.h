#ifndef	LWMQTT_H
#define	LWMQTT_H
// Light weight MQTT client
// QoS 0 only, no queuing or resending (using TCP to do that for us)
// Live sending to TCP for outgoing messages
// Simple callback for incoming messages
// Automatic reconnect

// Callback function for a connection (client or server)
// Called for incoming message
// - Topic is NULL terminated string, even if zero length topic has been used
// - Payload is also NULL terminated at len, for convenience
// Called for connect
// - Topic is NULL
// - Payload is server/client name
// Called for disconnect
// - Topic is NULL
// - Payload is NULL
typedef void lwmqtt_callback_t(void *arg, const char *topic, unsigned short len, const unsigned char *payload);

// For server operation
// TODO server config
// TODO server handle or global?
// TODO server callback or use connect callback with arg being the mqtt_t for the connection?

typedef struct lwmqtt_client_config_s lwmqtt_client_config_t;

// Config for connection
struct lwmqtt_client_config_s {
   lwmqtt_callback_t *callback;
   void *arg;
   const char *client;
   const char *hostname;        // Name or IP
   const char *username;
   const char *password;
   const char *tlsname;         // Name of cert if not host name
   unsigned short port;         // Port 0=auto
   unsigned short keepalive;    // 0=default
   // Will
   const char *topic;           // Will topic
   int plen;                    // Will payload len (-1 does strlen)
   const unsigned char *payload;        // Will payload
   uint8_t retain:1;            // Will retain
   // TLS
   void *cert_pem;              // For checking server
   int cert_len;
   void *client_cert_pem;       // For client auth
   int client_cert_len;
   void *client_key_pem;        // For client auth
   int client_key_len;
    esp_err_t(*crt_bundle_attach) (void *conf);
};

typedef struct lwmqtt_server_config_s lwmqtt_server_config_t;

// Config for connection
struct lwmqtt_server_config_s {
   lwmqtt_callback_t *callback;
   unsigned short port;         // Port 0=auto
   // TLS
   void *cert_pem;              // For checking server
   int cert_len;
   void *server_cert_pem;       // For server auth
   int server_cert_len;
   void *server_key_pem;        // For server auth
   int server_key_len;
};

// Handle for connection
typedef struct lwmqtt_s *lwmqtt_t;

// Create a client connection (NULL if failed)
lwmqtt_t lwmqtt_client(lwmqtt_client_config_t *);

// Start a server
void lwmqtt_server(lwmqtt_server_config_t *);
// TODO how to stop server?
// TODO how to close or reject connections?

// End connection - actually freed later as part of task. Will do a callback when closed if was connected
// NULLs the passed handle - do not use handle after this call
void lwmqtt_end(lwmqtt_t *);

// Subscribe (return is non null error message if failed)
const char *lwmqtt_subscribeub(lwmqtt_t, const char *topic, char unsubscribe);
#define lwmqtt_subscribe(h,t) lwmqtt_subscribeub(h,t,0);
#define lwmqtt_unsubscribe(h,t) lwmqtt_subscribeub(h,t,0);

// Send (return is non null error message if failed) (-1 tlen or plen do strlen)
const char *lwmqtt_send_full(lwmqtt_t, int tlen, const char *topic, int plen, const unsigned char *payload, char retain, char nowait);
// Simpler
#define lwmqtt_send(h,t,l,p) lwmqtt_send_full(h,-1,t,l,p,0,0);

// Simple send - non retained no wait topic ends on space then payload
const char *lwmqtt_send_str(lwmqtt_t, const char *msg);
#endif
