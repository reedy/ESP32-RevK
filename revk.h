// Include file for revk.c

#ifndef	REVK_H
#define	REVK_H

void revk_init(const char *file, const char *date, const char *time
 // TODO callbacks
); // Start the revk task, use __FILE__ and __DATE__ and __TIME__ to set task name and version ID

// MQTT reporting
void revk_status(const char *tag,const char *fmt,...); // Send status
void revk_event(const char *tag,const char *fmt,...); // Send event
void revk_error(const char *tag,const char *fmt,...); // Send error
void revk_info(const char *tag,const char *fmt,...); // Send info

// Settings
void revk_setting(const char *tag,const char *value); // Store a value (calls setting, same as if via MQTT)
void revk_reboot(void); // Restart cleanly
void revk_ota(void); // OTA and restart cleanly

#endif

