// (new) settings library
#include "revk.h"
#ifndef  CONFIG_REVK_OLD_SETTINGS
static const char __attribute__((unused)) * TAG = "Settings";

extern revk_settings_t revk_settings[];
extern uint32_t revk_nvs_time;

static nvs_handle nvs[2] = { -1, -1 };

static revk_setting_bits_t nvs_found = { 0 };

char *__malloc_like __result_use_check
strdup (const char *s)
{
   int l = strlen (s);
   char *o = mallocspi (l + 1);
   if (!o)
      return NULL;
   memcpy (o, s, l + 1);
   return o;
}

char *__malloc_like __result_use_check
strndup (const char *s, size_t l)
{
   int l2 = strlen (s);
   if (l2 < l)
      l = l2;
   char *o = mallocspi (l + 1);
   if (!o)
      return NULL;
   memcpy (o, s, l);
   o[l] = 0;
   return o;
}

static const char *
nvs_erase (revk_settings_t * s, const char *tag)
{
   nvs_found[(s - revk_settings) / 8] &= ~(1 << ((s - revk_settings) & 7));
   esp_err_t e = nvs_erase_key (nvs[s->revk], tag);
   if (e && e != ESP_ERR_NVS_NOT_FOUND)
      return "Failed to erase";
   if (!e)
   {
      revk_nvs_time = uptime () + 60;
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
      char taga[20];
      {
         int l = strlen (tag);
         if (tag[l - 1] & 0x80)
            sprintf (taga, "%.*s[%d]", l - 1, tag, tag[l - 1] - 0x80);
         else
            strcpy (taga, tag);
      }
      ESP_LOGE (TAG, "Erase %s", taga);
#endif
   }
   return NULL;
}

static const char *
nvs_put (revk_settings_t * s, const char *prefix, int index, void *ptr)
{                               // Put data, can be from ptr or from settings
   if (s->array && index >= s->array)
      return "Array overflow";
   char tag[17];
   int taglen = strlen (prefix);
   if (taglen + 1 > sizeof (tag))
      return "Tag too long";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
   char taga[20];
   if (s->array)
      sprintf (taga, "%s[%d]", prefix, index);
   else
      strcpy (taga, prefix);
#endif
   strncpy (tag, prefix, sizeof (tag));
   if (s->array)
   {
      tag[taglen++] = 0x80 + index;
      tag[taglen] = 0;
   }
   if (taglen > 15)
      return "Tag too long";
   if (!ptr && s->ptr)
   {                            // Does not apply to bit as ptr not set
      if (s->malloc)
         ptr = ((void **) s->ptr)[index];
      else
         ptr = s->ptr + s->size;
   }
   revk_nvs_time = uptime () + 60;
   nvs_found[(s - revk_settings) / 8] |= (1 << ((s - revk_settings) & 7));
   switch (s->type)
   {
#ifdef	REVK_SETTINGS_HAS_SIGNED
   case REVK_SETTINGS_SIGNED:
      {
         int64_t __attribute__((unused)) v = 0;
         if ((s->size == 8 && nvs_set_i64 (nvs[s->revk], tag, v = *((uint64_t *) ptr))) ||      //
             (s->size == 4 && nvs_set_i32 (nvs[s->revk], tag, v = *((uint32_t *) ptr))) ||      //
             (s->size == 2 && nvs_set_i16 (nvs[s->revk], tag, v = *((uint16_t *) ptr))) ||      //
             (s->size == 1 && nvs_set_i8 (nvs[s->revk], tag, v = *((uint8_t *) ptr))))
            return "Cannot store number (signed)";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s signed %lld 0x%0*llX", taga, v, s->size * 2, v);
#endif
      }
      break;
#endif
#ifdef	REVK_SETTINGS_HAS_UNSIGNED
   case REVK_SETTINGS_UNSIGNED:
      {
         uint64_t __attribute__((unused)) v = 0;
         if ((s->size == 8 && nvs_set_u64 (nvs[s->revk], tag, v = *((uint64_t *) ptr))) ||      //
             (s->size == 4 && nvs_set_u32 (nvs[s->revk], tag, v = *((uint32_t *) ptr))) ||      //
             (s->size == 2 && nvs_set_u16 (nvs[s->revk], tag, v = *((uint16_t *) ptr))) ||      //
             (s->size == 1 && nvs_set_u8 (nvs[s->revk], tag, v = *((uint8_t *) ptr))))
            return "Cannot store number (unsigned)";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s unsigned %llu 0x%0*llX", taga, v, s->size * 2, v);
#endif
      }
      break;
#endif
#ifdef	REVK_SETTINGS_HAS_BIT
   case REVK_SETTINGS_BIT:
      {
         uint8_t bit = 0;
         if (ptr)
            bit = *((uint8_t *) ptr);
         else
            bit = ((((uint8_t *) & revk_settings_bits)[s->bit / 8] & (1 << (s->bit & 7))) ? 1 : 0);
         if (nvs_set_u8 (nvs[s->revk], tag, bit))
            return "Cannot store bit";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s bit %d", taga, bit);
#endif
      }
      break;
#endif
#ifdef	REVK_SETTINGS_HAS_BLOB
   case REVK_SETTINGS_BLOB:
      {
         revk_settings_blob_t *b = ptr;
         if (nvs_set_blob (nvs[s->revk], tag, b->data, b->len))
            return "Cannot store blob";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s fixed %d", taga, b->len);
         ESP_LOG_BUFFER_HEX_LEVEL (TAG, b->data, b->len, ESP_LOG_ERROR);
#endif
      }
      break;
#endif
#ifdef	REVK_SETTINGS_HAS_STRING
   case REVK_SETTINGS_STRING:
      {
         if (nvs_set_str (nvs[s->revk], tag, ptr))
            return "Cannot store string";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s string %s", taga, *((char **) ptr));
#endif
      }
      break;
#endif
#ifdef	REVK_SETTINGS_HAS_OCTET
   case REVK_SETTINGS_OCTET:
      {
         if (nvs_set_blob (nvs[s->revk], tag, ptr, s->size))
            return "Cannot store octets";
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Write %s fixed %d", taga, s->size);
         ESP_LOG_BUFFER_HEX_LEVEL (TAG, ptr, s->array, ESP_LOG_ERROR);
#endif
      }
      break;
#endif
   }
   return NULL;
}

static const char *
nvs_get (revk_settings_t * s, const char *tag, int index)
{                               // Getting NVS
   if (s->array && index >= s->array)
      return "Array overflow";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
   char taga[20];
   {
      int l = strlen (tag);
      if (tag[l - 1] & 0x80)
         sprintf (taga, "%.*s[%d]", l - 1, tag, tag[l - 1] - 0x80);
      else
         strcpy (taga, tag);
   }
#endif
   void *data = NULL;
   size_t len = 0;
   const char *store (void)
   {
      switch (s->type)
      {
#ifdef  REVK_SETTINGS_HAS_SIGNED
      case REVK_SETTINGS_SIGNED:
         {
            data = mallocspi (len = s->size / 8);
            if (!data)
               return "malloc";
            if ((s->size == 8 && nvs_get_i64 (nvs[s->revk], tag, data)) ||      //
                (s->size == 4 && nvs_get_i32 (nvs[s->revk], tag, data)) ||      //
                (s->size == 2 && nvs_get_i16 (nvs[s->revk], tag, data)) ||      //
                (s->size == 1 && nvs_get_i8 (nvs[s->revk], tag, data)))
               return "Cannot load number (signed)";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            int64_t v = (int64_t) (s->size == 1 ? *((int8_t *) data) : s->size == 2 ? *((int16_t *) data) : s->size ==
                                   +4 ? *((int32_t *) data) : *((int64_t *) data));
            ESP_LOGE (TAG, "Read %s signed %lld 0x%0*llX", taga, v, s->size * 2, v);
#endif
         }
         break;
#endif
#ifdef  REVK_SETTINGS_HAS_UNSIGNED
      case REVK_SETTINGS_UNSIGNED:
         {
            data = mallocspi (len = s->size / 8);
            if (!data)
               return "malloc";
            if ((s->size == 8 && nvs_get_u64 (nvs[s->revk], tag, data)) ||      //
                (s->size == 4 && nvs_get_u32 (nvs[s->revk], tag, data)) ||      //
                (s->size == 2 && nvs_get_u16 (nvs[s->revk], tag, data)) ||      //
                (s->size == 1 && nvs_get_u8 (nvs[s->revk], tag, data)))
               return "Cannot load number (unsigned)";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            uint64_t v = (uint64_t) (s->size == 1 ? *((uint8_t *) data) : s->size == 2 ? *((uint16_t *) data) : s->size ==
                                     +4 ? *((uint32_t *) data) : *((uint64_t *) data));
            ESP_LOGE (TAG, "Read %s unsigned %llu 0x%0*llX", taga, v, s->size * 2, v);
#endif
         }
         break;
#endif
#ifdef  REVK_SETTINGS_HAS_BIT
      case REVK_SETTINGS_BIT:
         {
            data = mallocspi (len = 1);
            if (nvs_get_u8 (nvs[s->revk], tag, data))
               return "Cannot load bit";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            ESP_LOGE (TAG, "Read %s bit %d", taga, ((uint8_t *) & revk_settings_bits)[s->bit / 8] & (1 << (s->bit & 7)) ? 1 : 0);
#endif
         }
         break;
#endif
#ifdef  REVK_SETTINGS_HAS_BLOB
      case REVK_SETTINGS_BLOB:
         {
            if (nvs_get_blob (nvs[s->revk], tag, NULL, &len))
               return "Cannot get blob len";
            len += sizeof (revk_settings_blob_t);
            revk_settings_blob_t *b = mallocspi (len);
            if (!b)
               return "malloc";
            if (nvs_get_blob (nvs[s->revk], tag, b->data, &len))
               return "Cannot load blob";
            b->len = len - sizeof (revk_settings_blob_t);
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            ESP_LOGE (TAG, "Read %s blog %d", taga, b->len);
            ESP_LOG_BUFFER_HEX_LEVEL (TAG, b->data, b->len, ESP_LOG_ERROR);
#endif
            data = b;
         }
         break;
#endif
#ifdef  REVK_SETTINGS_HAS_STRING
      case REVK_SETTINGS_STRING:
         {
            if (nvs_get_str (nvs[s->revk], tag, NULL, &len))
               return "Cannot get string len";
            if (!len)
               return "Bad string len";
            data = mallocspi (len);
            if (!data)
               return "malloc";
            if (nvs_get_str (nvs[s->revk], tag, data, &len))
               return "Cannot load string";
            ((char *) data)[len - 1] = 0;       // Just in case
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            ESP_LOGE (TAG, "Read %s string %s", taga, (char *) data);
#endif
         } break;
#endif
#ifdef  REVK_SETTINGS_HAS_OCTET
      case REVK_SETTINGS_OCTET:
         {
            data = mallocspi (len = s->array);
            if (!data)
               return "malloc";
            if (nvs_get_blob (nvs[s->revk], tag, data, &len))
               return "Cannot load fixed block";
            if (len != s->len)
               return "Bad fixed block size";
#ifdef	CONFIG_REVK_SETTINGS_DEBUG
            ESP_LOGE (TAG, "Read %s fixed %d", taga, len);
            ESP_LOG_BUFFER_HEX_LEVEL (TAG, data, len, ESP_LOG_ERROR);
#endif
         }
         break;
#endif
      }
      return NULL;
   }
   const char *err = store ();
   if (err)
   {
      free (data);
      return err;
   }
   if (s->malloc)
   {
      void **p = s->ptr;
      p += index;
      free (*p);
      *p = data;
   }
#ifdef  REVK_SETTINGS_HAS_BIT
   else if (s->type == REVK_SETTINGS_BIT)
   {
      if (*(uint8_t *) data)
         ((uint8_t *) & revk_settings_bits)[s->bit / 8] |= (1 << (s->bit & 7));
      else
         ((uint8_t *) & revk_settings_bits)[s->bit / 8] &= ~(1 << (s->bit & 7));
      free (data);
   }
#endif
   else
   {
      if (len > s->size)
         err = "Bad size";
      else
         memcpy (s->ptr + index * s->size, data, s->size);
      free (data);
   }
   return err;
}

#ifdef	REVK_SETTINGS_HAS_NUMERIC
static const char *
parse_numeric (revk_settings_t * s, void **pp, const char **dp, const char *e)
{                               // Single numeric parse to memory, advance memory and source
   if (!s || !dp || !pp)
      return "NULL";
   if (!s->size)
      return "Not numeric";
   const char *err = NULL;
   const char *d = *dp;
   void *p = *pp;
   while (d && d < e && *d == ' ')
      d++;
   if (!d || d >= e)
   {                            // Empty
      memset (p, 0, s->size);
   } else
   {                            // Value
      uint64_t v = 0,
         f = 0;
      char sign = 0;
      int bits = s->size * 8;
      if (s->set)
         f |= (1ULL << (--bits));
      int top = bits - 1;
      const char *b = s->flags;
      void scan (void)
      {                         // Scan for flags
         while (d < e)
         {
            int l = 1;
            while (d + l < e && (d[l] & 0xC0) == 0x80)
               l++;
            int bit = top;
            const char *q;
            for (q = b; *q; q++)
               if (!(f & (1ULL << bit)) && !memcmp (q, d, l))   // Allows for duplicates in flags
               {
                  f |= (1ULL << bit);
                  d += l;
                  break;
               } else if (*q != ' ' && !((*q & 0xC0) == 0x80))
                  bit--;
            if (!*q)
               break;           // Not found
         }
      }
      if (b)
      {                         // Bit fields
         for (const char *q = b; *q; q++)
            if (*q != ' ' && !((*q & 0xC0) == 0x80))
               bits--;
         if (!err && bits < 0)
            err = "Too many flags";
         scan ();
      }
      // Numeric value
      if (d < e && *d == '-')
         sign = *d++;
      if (!err && bits && (d >= e || !isdigit ((int) *d)))
         err = "No number found";
      void add (char c)
      {
         uint64_t wrap = v;
         v = v * 10 + (c + -'0');
         if (!err && v < wrap)
            err = "Number too large";
      }
      while (!err && d < e && isdigit ((int) *d))
         add (*d++);
      if (!err && s->decimal)
      {
         int q = s->decimal;
         if (d < e && *d == '.')
         {
            d++;
            while (!err && d < e && isdigit ((int) *d) && q && q--)
               add (*d++);
            if (!err && d < e && isdigit ((int) *d))
               err = "Too many decimal places";
         }
         while (!err && q--)
            add ('0');
      }
      if (b)
         scan ();
      if (!err && sign
#ifdef	REVK_SETTINGS_HAS_UNSIGNED
          && s->type == REVK_SETTINGS_UNSIGNED
#endif
         )
         err = "Negative not allowed";
      if (!err && bits < 64 && v >= (1ULL << bits))
         err = "Number too big";
      if (sign)
         v = -v;
      if (bits < 64)
         v = ((v & ((1ULL << bits) - 1)) | f);
      if (!err)
      {
         if (s->size == 1)
            *((uint8_t *) p) = v;
         else if (s->size == 2)
            *((uint16_t *) p) = v;
         else if (s->size == 4)
            *((uint32_t *) p) = v;
         else if (s->size == 8)
            *((uint64_t *) p) = v;
         else
            err = "Bad number size";
      }
   }
   while (d && d < e && *d == ' ' && *d != ',' && *d != '\t')
      d++;
   if (d && d < e && (*d == ',' || *d == '\t'))
      d++;
   while (d && d < e && *d == ' ')
      d++;
   p += (s->size ? : sizeof (void *));
   *pp = p;
   *dp = d;
   return err;
}
#endif

static int
value_cmp (revk_settings_t * s, void *a, void *b)
{                               // Pointer to actual data
#ifdef	REVK_SETTINGS_HAS_BLOB
   if (s->type == REVK_SETTINGS_BLOB)
   {
      int alen = ((revk_settings_blob_t *) a)->len;
      int blen = ((revk_settings_blob_t *) b)->len;
      if (alen > blen)
         return 1;
      if (alen < blen)
         return -1;
      return memcmp (((revk_settings_blob_t *) a)->data, ((revk_settings_blob_t *) b)->data, alen + sizeof (revk_settings_blob_t));
   }
#endif
#ifdef	REVK_SETTINGS_HAS_STRING
   if (s->type == REVK_SETTINGS_STRING)
      return strcmp (*((char **) a), *((char **) b));
#endif
   return memcmp (a, b, s->size);
}

static const char *
load_value (revk_settings_t * s, const char *d, int index, void *ptr)
{
   if (!ptr)
      ptr = s->ptr;
   else
      index = 0;
   if (index > 0)
   {
      if (s->malloc)
         ptr += index * sizeof (void *);
      else
         ptr += index * s->size;
   }
   const char *err = NULL;
   int a = s->array;
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
   if (d < e && s->hex)
   {
      // TODO
   }
   if (d < e && s->base64)
   {
      // TODO
   }
   switch (s->type)
   {
#ifdef	REVK_SETTINGS_HAS_NUMERIC
#ifdef  REVK_SETTINGS_HAS_SIGNED
   case REVK_SETTINGS_SIGNED:
#endif
#ifdef  REVK_SETTINGS_HAS_UNSIGNED
   case REVK_SETTINGS_UNSIGNED:
#endif
      if (s->malloc)
         err = "Malloc number not supported";
      else
      {
         err = parse_numeric (s, &ptr, &d, e);
         if (a && index < 0)
            while (!err && --a)
               err = parse_numeric (s, &ptr, &d, e);
         if (!err && d < e)
            err = "Extra data on end of number";
      }
      break;
#endif
#ifdef  REVK_SETTINGS_HAS_BIT
   case REVK_SETTINGS_BIT:
      if (s->malloc)
         err = "Malloc bit not supported";
      else
      {
         if (ptr)
            *(uint8_t *) ptr = ((d < e && (*d == '1' || *d == 't')) ? 1 : 0);
         else
         {
            if (d < e && (*d == '1' || *d == 't'))
               ((uint8_t *) & revk_settings_bits)[s->bit / 8] |= (1 << (s->bit & 7));
            else
               ((uint8_t *) & revk_settings_bits)[s->bit / 8] &= ~(1 << (s->bit & 7));
         }
      }
      break;
#endif
#ifdef  REVK_SETTINGS_HAS_BLOB
   case REVK_SETTINGS_BLOB:
      if (!s->malloc)
         err = "Fixed blob not supported";
      else
      {
         void **p = (void **) ptr;
         if (d < e)
         {
            free (*p);
            *p = mallocspi (sizeof (revk_settings_blob_t) + e - d);
            ((revk_settings_blob_t *) (*p))->len = (e - d);
            memcpy ((*p) + sizeof (revk_settings_blob_t), d, e - d);
         } else
         {
            free (*p);
            *p = mallocspi (sizeof (revk_settings_blob_t));
            ((revk_settings_blob_t *) (*p))->len = 0;
         }
         if (a && index < 0)
            while (--a)
            {
               free (*p);
               *p = mallocspi (sizeof (revk_settings_blob_t));
               ((revk_settings_blob_t *) (*p))->len = 0;
               p++;
            }
      }
      break;
#endif
#ifdef  REVK_SETTINGS_HAS_STRING
   case REVK_SETTINGS_STRING:
      {
         if (!s->malloc)
         {                      // Fixed
            if (e - d + 1 > s->size)
               err = "Too long";
            else
            {
               memcpy (ptr, d, e - d);
               ((char *) ptr)[e - d] = 0;
            }
            if (a && index < 0)
               while (--a)
               {
                  memset (ptr, 0, s->size);
                  ptr += s->size;
               }
         } else
         {                      // Malloc
            void **p = (void **) ptr;
            free (*p);
            if (d)
               *p = strndup (d, (int) (e - d));
            else
               *p = strdup ("");
            if (a && index < 0)
               while (--a)
               {
                  free (*++p);
                  *p = strdup ("");
               }
         }
      }
      break;
#endif
#ifdef  REVK_SETTINGS_HAS_OCTET
   case REVK_SETTINGS_OCTET:
      if (s->malloc)
         err = "Malloc octet not supported";
      else
      {
         if (d < e)
         {
            if (e - d != s->size)
               err = "Wrong length";
            else
               memcpy (ptr, d, e - d);
         } else
            memset (ptr, 0, s->size);
         if (a && index < 0)
            while (--a)
            {
               memset (ptr, 0, s->size);
               ptr += s->size;
            }
      }
      break;
#endif
   }
   if (err)
      ESP_LOGE (TAG, "%s %s", s->name, err);
   return err;
}

void
revk_settings_load (const char *tag, const char *appname)
{                               // Scan NVS to load values to settings
   for (revk_settings_t * s = revk_settings; s->len; s++)
      load_value (s, s->def, -1, NULL);
   // Scan
   for (int revk = 0; revk < 2; revk++)
   {
      struct zap_s
      {
         struct zap_s *next;
         char tag[0];
      } *zap = NULL;
      nvs_open_from_partition (revk ? tag : "nvs", revk ? tag : appname, NVS_READWRITE, &nvs[revk]);
      nvs_iterator_t i = NULL;
      if (!nvs_entry_find (revk ? tag : "nvs", revk ? tag : appname, NVS_TYPE_ANY, &i))
      {
         do
         {
            nvs_entry_info_t info = { 0 };
            void addzap (void)
            {
               struct zap_s *z = malloc (sizeof (*z) + strlen (info.key) + 1);
               strcpy (z->tag, info.key);
               z->next = zap;
               zap = z;
            }
            if (!nvs_entry_info (i, &info))
            {
               int l = strlen (info.key);
               revk_settings_t *s;
               const char *err = NULL;
               for (s = revk_settings; s->len; s++)
               {
                  if (!s->array && s->len == l && !memcmp (s->name, info.key, l))
                  {             // Exact match
                     err = nvs_get (s, info.key, 0);
                     break;
                  } else if (s->array && s->len + 1 == l && !memcmp (s->name, info.key, s->len) && (info.key[s->len] & 0x80))
                  {             // Array match, new
                     err = nvs_get (s, info.key, info.key[s->len] - 0x80);
                     break;
                  } else if (s->array && s->len < l && !memcmp (s->name, info.key, s->len) && isdigit ((int) info.key[s->len]))
                  {             // Array match, old
                     err = nvs_get (s, info.key, atoi (info.key + s->len) - 1);
                     if (!err)
                        err = "Old style record in nvs, being replaced";
                     addzap ();
                     break;
                  }
               }
               if (!s->len)
               {
                  addzap ();
                  err = "Not found";
               }
               if (err)
                  ESP_LOGE (tag, "NVS %s Failed %s", info.key, err);
               else
                  nvs_found[(s - revk_settings) / 8] |= (1 << ((s - revk_settings) & 7));
            }
         }
         while (!nvs_entry_next (&i));
      }
      nvs_release_iterator (i);
      while (zap)
      {
         struct zap_s *z = zap->next;
         if (nvs_erase_key (nvs[revk], zap->tag))
            ESP_LOGE (tag, "Erase %s failed", zap->tag);
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
         ESP_LOGE (TAG, "Erase %s", zap->tag);
#endif
         free (zap);
         zap = z;
      }
   }
   for (revk_settings_t * s = revk_settings; s->len; s++)
   {
      if (s->fix && !(nvs_found[(s - revk_settings) / 8] & (1 << ((s - revk_settings) & 7))))
      {                         // Fix, save to flash
         if (!s->array)
            nvs_put (s, s->name, 0, NULL);
         else
            for (int i = 0; i < s->array; i++)
               nvs_put (s, s->name, i, NULL);
      }
   }
}

const char *
revk_setting_dump (void)
{
   const char *err = NULL;
   jo_t j = NULL;
   void send (void)
   {                            // Sends the settings - this deliberately uses the revk_id not the hostname as it is "seen" by any device listening
      if (!j)
         return;
      const char *an = appname,
         *sl = "/";
      if (!prefixapp)
         an = sl = "";
      char *topic = NULL;
      asprintf (&topic, "%s%s%s/%s", prefixsetting, sl, an, revk_id);
      revk_mqtt_send (NULL, 0, topic, &j);
      free (topic);
   }
   int maxpacket = MQTT_MAX;
   maxpacket -= 50;             // for headers
#ifdef	CONFIG_REVK_MESH
   maxpacket -= MESH_PAD;
#endif
   char *buf = mallocspi (maxpacket);   // Allows for topic, header, etc
   if (!buf)
      return "malloc";
   revk_setting_group_t group = { 0 };
   for (revk_settings_t * s = revk_settings; s->len; s++)
   {
      if (s->secret)
         continue;
      if (s->group && (group[s->group / 8] & (1 << (s->group & 7))))
         continue;              // Already done
      jo_t p = NULL;
      void start (void)
      {
         if (!p)
         {
            if (j)
               p = jo_copy (j);
            else
            {
               p = jo_create_mem (buf, maxpacket);
               jo_object (p, NULL);
            }
         }
      }
      const char *failed (void)
      {
         err = NULL;
         if (p && (err = jo_error (p, NULL)))
            jo_free (&p);       // Did not fit
         return err;
      }
#if 0
      void addvalue (revk_setting_t * s, const char *tag, int n)
      {                         // Add a value
         start ();
         void *data = s->data;
         const char *defval = s->defval ? : "";
         if (!(s->flags & SETTING_BOOLEAN))
            data += (s->size ? : sizeof (void *)) * n;
         if (s->flags & SETTING_BINDATA)
         {                      // Binary data
            int len = s->size;
            if (!len)
            {                   // alloc'd with len at start
               revk_bindata_t *d = *(void **) data;
               len = d->len;
               data = d->data;
            }
            if (s->flags & SETTING_HEX)
               jo_base16 (p, tag, data, len);
            else
               jo_base64 (p, tag, data, len);
         } else if (!s->size)
         {
            char *v = *(char **) data;
            if (v)
            {
               jo_string (p, tag, v);   // String
            } else
               jo_null (p, tag);        // Null string - should not happen
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
               jo_bool (p, tag, (v >> n) & 1);
            } else
            {                   // numeric
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
                     t += sprintf (t, "%llX", v);
                  else if (bits)
                     t += sprintf (t, "%llu", v);
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
                  jo_string (p, tag, temp);
               else
                  jo_lit (p, tag, temp);
            }
         }
      }
      void addsub (setting_t * s, const char *tag, int n)
      {                         // n is 0 based
         if (s->parent)
         {
            if (!tag || (!n && hasdef (s)) || !isempty (s, n))
            {
               start ();
               jo_object (p, tag);
               setting_t *q;
               for (q = setting; q; q = q->next)
                  if (q->child && !strncmp (q->name, s->name, s->namelen))
                     if ((!n && hasdef (q)) || !isempty (q, n))
                        addvalue (q, q->name + s->namelen, n);
               jo_close (p);
            }
         } else
            addvalue (s, tag, n);
      }
      void addsetting (void)
      {                         // Add a whole setting
         if (s->parent)
         {
            if (s->array)
            {                   // Array above
               if (max || hasdef (s))
               {
                  start ();
                  jo_array (p, s->name);
                  for (int n = 0; n < max; n++)
                     addsub (s, NULL, n);
                  jo_close (p);
               }
            } else
               addsub (s, s->name, 0);
         } else if (s->array)
         {
            if (max || hasdef (s))
            {
               start ();
               jo_array (p, s->name);
               for (int n = 0; n < max; n++)
                  addvalue (s, NULL, n);
               jo_close (p);
            }
         } else if (hasdef (s) || !isempty (s, 0))
            addvalue (s, s->name, 0);
      }
      addsetting ();
      if (failed () && j)
      {
         send ();               // Failed, clear what we were sending and try again
         addsetting ();
      }
#endif
      if (!failed ())
      {                         // Fitted, move forward
         if (p)
         {
            jo_free (&j);
            j = p;
         }
      } else
      {
         jo_t j = jo_make (NULL);
         jo_string (j, "description", "Setting did not fit");
         jo_string (j, prefixsetting, s->name);
         if (err)
            jo_string (j, "reason", err);
         revk_error (TAG, &j);
      }
   }
   send ();
   free (buf);
   return NULL;
}

const char *
revk_setting (jo_t j)
{
   jo_rewind (j);
   jo_type_t t;
   if ((t = jo_here (j)) != JO_OBJECT)
      return "Not an object";
   char change = 0;
   const char *err = NULL;
   char tag[16];
   revk_setting_bits_t found = { 0 };
   void scan (int plen)
   {
      while (!err && (t = jo_next (j)) == JO_TAG)
      {
         int l = jo_strlen (j);
         if (l + plen > sizeof (tag) - 1)
            err = "Too long";
         else
         {
            jo_strncpy (j, tag + plen, l + 1);
            revk_settings_t *s;
            for (s = revk_settings; s->len && (s->len != plen + l || (plen && s->dot != plen) || strcmp (s->name, tag)); s++);
            void store (int index)
            {
               if (!s->len)
               {
                  err = "Not found";
                  return;
               }
               found[(s - revk_settings) / 8] |= (1 << ((s - revk_settings) & 7));
               if (s->array && index >= s->array)
               {
                  err = "Too many entries";
                  return;
               }
               char *val = NULL;
               if (t == JO_NULL)
               {                // Default
                  if (s->def)
                  {
                     val = (char *) s->def;
#ifdef	REVK_SETTINGS_HAS_NUMERIC
                     if (s->array && (1
#ifdef	REVK_SETTINGS_HAS_SIGNED
                                      || s->type == REVK_SETTINGS_SIGNED
#endif
#ifdef	REVK_SETTINGS_HAS_UNSIGNED
                                      || s->type == REVK_SETTINGS_UNSIGNED
#endif
                         ))
                     {          // Skip to the entry
                        if (s->dq && *val == '"')
                           val++;
                        int i = index;
                        while (i--)
                        {
                           while (*val && *val != ',' && *val != ' ' && *val != '\t')
                              val++;
                           while (*val && *val == ' ')
                              val++;
                           if (*val && (*val == ',' || *val == '\t'))
                              val++;
                           while (*val && *val == ' ')
                              val++;
                        }
                     }
#endif
                     val = strdup (val);
#ifdef	REVK_SETTINGS_HAS_NUMERIC
                     if (s->array && (1
#ifdef	REVK_SETTINGS_HAS_SIGNED
                                      || s->type == REVK_SETTINGS_SIGNED
#endif
#ifdef	REVK_SETTINGS_HAS_UNSIGNED
                                      || s->type == REVK_SETTINGS_UNSIGNED
#endif
                         ))
                     {
                        char *v = val;
                        while (*v && *v != ' ' && *v != ',' && *v != '\t' && (s->dq && *v != '"'))
                           v++;
                        *v = 0;
                     }
#endif
                  }
               } else if (t != JO_CLOSE)
                  val = jo_strdup (j);
               int len = s->malloc ? sizeof (void *) : s->size ? : 1;
               uint8_t *temp = mallocspi (len);
               if (!temp)
                  err = "malloc";
               else
               {
                  memset (temp, 0, len);
                  uint8_t dofree = s->malloc;
                  uint8_t bit = 0;
                  void *ptr = s->ptr ? : &bit;
#ifdef	REVK_SETTINGS_HAS_BIT
                  if (s->type == REVK_SETTINGS_BIT)
                     bit = ((((uint8_t *) & revk_settings_bits)[s->bit / 8] & (1 << (s->bit & 7))) ? 1 : 0);
                  else
#endif
                  if (s->malloc)
                     ptr += index * sizeof (void *);
                  else
                     ptr += index * s->size;
                  err = load_value (s, val, index, temp);
                  if (value_cmp (s, ptr, temp))
                  {             // Change
                     if (s->live)
                     {          // Apply live
#ifdef	REVK_SETTINGS_HAS_BIT
                        if (s->type == REVK_SETTINGS_BIT)
                        {
                           if (bit)
                              ((uint8_t *) & revk_settings_bits)[s->bit / 8] |= (1 << (s->bit & 7));
                           else
                              ((uint8_t *) & revk_settings_bits)[s->bit / 8] &= ~(1 << (s->bit & 7));
                        } else
#endif
                        {
                           if (s->malloc)
                              free (*(void **) ptr);
                           memcpy (ptr, temp, len);
                           dofree = 0;
                        }
                     }
                     if (t == JO_NULL && !s->fix)
                        err = nvs_erase (s, s->name);
                     else
                        err = nvs_put (s, s->name, index, temp);
                     if (!err && !s->live)
                        change = 1;
                  } else if (t == JO_NULL && !s->fix)
                     err = nvs_erase (s, s->name);
                  if (dofree)
                     free (*(void **) temp);
                  free (temp);
               }
               free (val);
            }
            t = jo_next (j);
            void zapdef (void)
            {
               if (s->array)
                  for (int i = 0; i < s->array; i++)
                     store (i);
               else
                  store (0);
            }
            if (t == JO_NULL)
            {
               if (s->len)
                  zapdef ();
               else if (!plen)
               {
                  for (s = revk_settings; s->len && (!s->group || s->dot != l || strncmp (s->name, tag, l)); s++);
                  if (s->len)
                  {             // object reset
                     int group = s->group;
                     for (s = revk_settings; s->len; s++)
                        if (s->group == group)
                           zapdef ();
                  }
               } else
                  err = "Invalid null";
            } else if (t == JO_OBJECT)
            {                   // Object
               if (plen)
                  err = "Nested too far";
               else if (s->len)
                  err = "Not an object";
               else
               {
                  for (s = revk_settings; s->len && (!s->group || s->dot != l || strncmp (s->name, tag, l)); s++);
                  if (!s->len)
                     err = "Unknown object";
                  else
                  {
                     int group = s->group;
                     scan (l);
                     t = JO_NULL;       // Default
                     for (s = revk_settings; s->len; s++)
                        if (s->group == group && !(found[(s - revk_settings) / 8] & (1 << ((s - revk_settings) & 7))))
                           zapdef ();
                  }
               }
            } else if (t == JO_ARRAY)
            {                   // Array
               if (!s->array)
                  err = "Unexpected array";
               else
               {
                  int index = 0;
                  while ((t = jo_next (j)) != JO_CLOSE && index < s->array)
                  {
                     store (index);
                     index++;
                  }
                  while (index < s->array)
                  {             // NULLs
                     store (index);
                     index++;
                  }
               }
            } else
               store (0);
         }
      }
   }
   scan (0);
   if (err)
      ESP_LOGE (TAG, "Failed %s at [%s]", err, jo_debug (j));
   if (change)
      revk_restart ("Settings changed", 5);
   return err ? : "";
}

void
revk_settings_commit (void)
{
#ifdef  CONFIG_REVK_SETTINGS_DEBUG
   ESP_LOGE (TAG, "NVC commit");
#endif
   REVK_ERR_CHECK (nvs_commit (nvs[0]));
   REVK_ERR_CHECK (nvs_commit (nvs[1]));
}

#endif
