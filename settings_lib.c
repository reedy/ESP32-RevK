// (new) settings library
#include "revk.h"
#ifndef  CONFIG_REVK_OLD_SETTINGS
extern revk_settings_t revk_settings[];

void
revk_settings_load (const char *tag, const char *appname)
{                               // Scan NVS to load values to settings
   // Default strings bodge - TODO
   for (revk_settings_t * r = revk_settings; r->len1; r++)
   {
      if (r->ptr && !r->size)
         // TODO Bodges for now...
      {                         // String - set some defaults
         int a = r->array;
         char **p = (char **) r->ptr;
         if (r->blob)
         {
            *p = calloc (1, 2); // Bodge
            if (a)
               while (--a)
                  *++p = calloc (1, 2);
         } else
         {
            if (!r->def)
               *p = strdup ("");
            else if (!r->dq || *r->def != '"' || !r->def[1])
               *p = strdup (r->def);
            else
               *p = strndup (r->def + 1, strlen (r->def) - 2);  // Strip quotes from CONFIG_...
            ESP_LOGE (tag, "%s=%s", r->name, *p);
            if (a)
               while (--a)
                  *++p = strdup ("");
         }
      }
   }
   // Scan
   for (int revk = 0; revk < 2; revk++)
   {
      // TODO Make list of deletions...
      // TODO loading values
      nvs_iterator_t i = NULL;
      if (!nvs_entry_find (revk ? tag : "nvs", revk ? tag : appname, NVS_TYPE_ANY, &i))
      {
         do
         {
            nvs_entry_info_t info = { 0 };
            if (!nvs_entry_info (i, &info))
            {
               ESP_LOGE (tag, "Found %s %s type %d", info.namespace_name, info.key, info.type);
               // TODO
            }
         }
         while (!nvs_entry_next (&i));
      }
      nvs_release_iterator (i);
   }
}

const char *
revk_setting_dump (void)
{
   return "TODO";
}

const char *
revk_setting (jo_t j)
{
   return "TODO";
}

void
revk_settings_commit (void)
{
   // TODO;
}

#endif
