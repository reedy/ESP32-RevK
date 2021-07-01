// Light weight MQTT client
// QoS 0 only, no queuing or resending (using TCP to do that for us)
// Live sending to TCP for outgoing messages
// Simple callback for incoming messages
// Automatic reconnect

// Callback function.
// Called for incoming message
// - Topic is NULL terminated string, even if zero length topic has been used
// - Payload is also NULL terminated at len, for convenience
// Called for connect
// - Topic is NULL
// - Payload is server name
// Called for disconnect
// - Topic is NULL
// - Payload is NULL
typedef void lwmqtt_callback_t(void*arg,const char *topic,size_t len,const unsigned char*payload);

typedef struct lwmqtt_config_s lwmqtt_config_t;

// Config for connection
struct lwmqtt_config_s
{
	const char *host;
	int port;
	lwmqtt_callback_t*callback;
	void *arg;
	char *client;
	char *username;
	char *password;
	const char *topic;	// Will topic
	size_t len;		// Will payload len
	const unsigned char *payload;	// Will payload
	// TLS settings TODO
	uint8_t retain:1;	// Will retain
};

// Handle for connection
typedef struct lwmqtt_handle_s *lwmqtt_handle_t;

// Create a connection (NULL if failed)
lwmqtt_handle_t lwmqtt_init(lwmqtt_config_t*);

// End connection - actually freed later as part of task. Will do a callback when closed if was connected
// NULLs the passed handle - do not use handle after this call
void lwmqtt_end(lwmqtt_handle_t*);

// Subscribe (return is non null error message if failed)
const char * lwmqtt_subscribe(lwmqtt_handle_t,const char *topic);
const char * lwmqtt_unsubscribe(lwmqtt_handle_t,const char *topic);

// Send (return is non null error message if failed)
const char * lwmqtt_send(lwmqtt_handle_t,const char *topic,size_t len,const unsigned char *payload,char retail);
