// (new) settings library
#include "revk.h"
#ifndef  CONFIG_REVK_OLD_SETTINGS
extern revk_settings_t revk_settings[];

static nvs_handle nvs[2] = { -1, -1 };

static void
load_default (revk_settings_t * s)
{
   int a = s->array;
   const char *d = s->def;
   const char *e = NULL;
   if (d)
   {
      e = d + strlen (d);
      if (s->dq && e > d + 1 && *d == '"' && e[-1] == '"')
      {
         d++;
         e--;
      }
      if (d == e)
         d = e = NULL;
   }
   if (s->ptr)
   {                            // value
      if (s->size)
      {                         // Value
         void *p = s->ptr;
         if (a && s->size == 1 && a && (s->hex || s->base64))
         {                      // Fixed string
            if (d)
            {
               // TODO
            } else
               memset (p, 0, a);
         } else
         {                      // Value(s)
            if (d)
            {
               // TODO
            } else
               memset (p, 0, s->size * (a ? : 1));
         }
      } else
      {                         // Pointer
         void **p = (void **) s->ptr;
         if (s->blob)
         {                      // Blob
            if (d)
            {
               // TODO
            } else
            {                   // Empty blob
               free (*p);
               *p = calloc (1, 2);
               if (a)
                  while (--a)
                  {
                     free (*++p);
                     *p = calloc (1, 2);
                  }
            }
         } else
         {                      // String
            free (*p);
            if (d)
               *p = strdnup (d, (int) (e - d));
            else
               *p = strdup ("");
            if (a)
               while (--a)
               {
                  free (*++p);
                  *p = strdup ("");
               }
         }
      }
   }

   else
   {                            // bitfield
      if (d && (*d == '1' || *d == 't'))
         ((uint8_t *) & revk_settings_bitfield)[s->bit / 8] |= (1 << (s->bit & 7));
      else
         ((uint8_t *) & revk_settings_bitfield)[s->bit / 8] &= ~(1 << (s->bit & 7));
   }
}

void
revk_settings_load (const char *tag, const char *appname)
{                               // Scan NVS to load values to settings
   for (revk_settings_t * r = revk_settings; r->len1; r++)
      load_default (r);
   // Scan
   for (int revk = 0; revk < 2; revk++)
   {
      nvs_open_from_partition (revk ? tag : "nvs", revk ? tag : appname, NVS_READWRITE, &nvs[revk]);
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
   REVK_ERR_CHECK (nvs_commit (nvs[0]));
   REVK_ERR_CHECK (nvs_commit (nvs[1]));
}

#endif
