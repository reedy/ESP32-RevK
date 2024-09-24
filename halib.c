// Home Assistant support / config librrary
#include "revk.h"
#ifdef	CONFIG_REVK_HALIB
#include "halib.h"

static jo_t
ha_config_make (const char *type, ha_config_t * h)
{
   char *hastatus = revk_topic (topicstate, NULL, NULL);
   jo_t j = jo_object_alloc ();
   // ID
   jo_stringf (j, "unique_id", "%s-%s", hostname, h->id);
   jo_object (j, "dev");
   jo_array (j, "ids");
   jo_string (j, NULL, revk_id);
   jo_close (j);
   jo_string (j, "name", hostname);
   jo_string (j, "mdl", appname);
   jo_string (j, "sw", revk_version);
   jo_string (j, "mf", "www.me.uk");
   jo_close (j);
   if (h->type)
      jo_string (j, "dev_cla", h->type);
   if (h->name)
      jo_string (j, "name", h->name);
   if (h->unit)
   {                            // Status
      jo_stringf (j, "stat_t", h->stat ? : hastatus);
      jo_string (j, "unit_of_meas", h->unit);
      jo_stringf (j, "val_tpl", "{{value_json.%s}}", h->field ? : h->id);
   }
   // Availability
   jo_string (j, "avty_t", hastatus);
   jo_string (j, "avty_tpl", "{{value_json.up}}");
   jo_bool (j, "pl_avail", 1);
   jo_bool (j, "pl_not_avail", 0);
   free (hastatus);
   return j;
}

const char *
ha_config_sensor_opts (ha_config_t h)
{
   if (!h.id)
      return "No name";
   char *topic;
   if (asprintf (&topic, "homeassistant/sensor/%s-%s/config", hostname, h.id) < 0)
      return "malloc fail";
   jo_t j = ha_config_make ("status", &h);

   revk_mqtt_send (NULL, 1, topic, &j);
   free (topic);
   return NULL;
}

#endif
