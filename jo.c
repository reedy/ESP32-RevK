// Lightweight JSON syntax management tool kit
// This toolkit works solely at a syntax level, and does not all management of whole JSON object hierarchy

#include "jo.h"
#include <string.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"

#ifndef	JO_MAX
#define	JO_MAX	64
#endif

struct jo_s {                   // cursor to JSON object
   char *buf;                   // Start of JSON string
   const char *err;             // If in error state
   size_t ptr;                  // Pointer in to buf
   size_t len;                  // Max space for buf
   uint8_t parse:1;             // This is parsing, not generating
   uint8_t alloc:1;             // buf is malloced space
   uint8_t comma:1;             // Set if comma needed, i.e. object or array at current level has at least one entry
   uint8_t level;               // Current level
   uint8_t o[(JO_MAX + 7) / 8]; // Bit set at each level if level is object, else it is array
};

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char BASE16[] = "0123456789ABCDEF";

#define escapes \
        esc ('"', '"') \
        esc ('\\', '\\') \
        esco ('/', '/') \
        esc ('b', '\b') \
        esc ('f', '\f') \
        esc ('n', '\n') \
        esc ('r', '\r') \
        esc ('t', '\t') \

static jo_t jo_new(void)
{                               // Create a jo_t
   jo_t j = malloc(sizeof(*j));
   if (!j)
      return j;                 // Malloc fail
   memset(j, 0, sizeof(*j));
   return j;
}

static ssize_t jo_space(jo_t j, size_t need)
{                               // How much space we have
   // If need is set then allocates need first if possible
   // If need is set and not need space then returns -1
   if (!j || j->parse)
      return -1;
   if (j->alloc && j->len - j->ptr < need)
   {
      if (need < 100)
         need = 100;
      char *more = realloc(j->buf, j->len + need);
      if (more)
      {                         // Got more
         j->buf = more;
         j->len += need;
      }
   }
   if (j->len - j->ptr < need)
      return -1;                // Not enough
   return j->len - j->ptr;
}

// Setting up

jo_t jo_parse_str(const char *buf)
{                               // Start parsing a null terminated JSON object string
   if (!buf)
      return NULL;
   return jo_parse_mem(buf, strlen(buf));
}

jo_t jo_parse_mem(const char *buf, size_t len)
{                               // Start parsing a JSON string in memory - does not need a null
   if (!buf)
      return NULL;              // No buf
   jo_t j = jo_new();
   if (!j)
      return j;                 // malloc fail
   j->parse = 1;
   j->buf = (char *) buf;
   j->len = len;
   return j;
}

jo_t jo_create_mem(char *buf, size_t len)
{                               // Start creating JSON in memory at buf, max space len.
   jo_t j = jo_new();
   if (!j)
      return j;                 // malloc fail
   j->buf = buf;
   j->len = len;
   return j;
}

jo_t jo_create_alloc(void)
{                               // Start creating JSON in memory, allocating space as needed.
   jo_t j = jo_new();
   if (!j)
      return j;                 // malloc fail
   j->alloc = 1;
   return j;
}

jo_t jo_copy(jo_t j)
{                               // Copy object - copies the object, and if allocating memory, makes copy of the allocated memory too
   if (!j || j->err)
      return NULL;              // No j
   jo_t n = jo_new();
   if (!n)
      return n;                 // malloc fail
   memcpy(n, j, sizeof(*j));
   if (j->alloc && j->buf)
   {
      n->buf = malloc(j->len);
      if (!n->buf)
      {
         jo_free(&n);
         return NULL;           // malloc
      }
      memcpy(n->buf, j->buf, j->ptr);
   }
   return j;
}

char *jo_result(jo_t j)
{                               // Return JSON string
   if (!j || j->err)
      return NULL;              // No good
   if (j->parse)
      return j->buf;            // As parsed
   // Add closing...
   if (jo_space(j, j->level + 1) <= 0)
      return NULL;              // No space
   uint8_t l = j->level;
   char *p = j->buf + j->ptr;
   while (l--)
      *p++ = ((j->o[l / 8] & (1 << (l & 7))) ? '}' : ']');
   *p = 0;
   return j->buf;
}

void jo_free(jo_t * jp)
{                               // Free jo_t and any allocated memory
   if (!jp)
      return;
   jo_t j = *jp;
   if (j)
   {
      *jp = NULL;
      if (j->alloc && j->buf)
         free(j->buf);
      free(j);
   }
}

char *jo_result_free(jo_t * jp)
{                               // Return the JSON string, and free the jo_t object.
// NULL if error state, as per jo_result
// This is intended to be used with jo_create_alloc(), returning the allocated string (which will need freeing).
// If used with a fixed string, a strdup is done, so that the return value can always be freed
   if (!jp)
      return NULL;
   jo_t j = *jp;
   if (!j)
      return NULL;
   *jp = NULL;
   char *res = jo_result(j);
   if (res && j->alloc)
      res = strdup(res);
   free(j);
   return res;
}

int jo_level(jo_t j)
{                               // Current level, 0 being the top level
   if (!j)
      return -1;
   return j->level;
}

const char *jo_error(jo_t j, int *pos)
{                               // Return NULL if no error, else returns an error string.
   if (!j)
      return "No j";
   return j->err;
}

// Creating
// Note that tag is required if in an object and must be null if not

static size_t jo_codedlen(const char *s)
{                               // How much space would this string take as JSON (including "'s)
   if (!s)
      return 4;                 // null
   int l = 2;                   // The first and last "
   while (*s)
   {
      uint8_t c = *s++;
#define esc(a,b) if(c==b){l+=2;continue;}
#define esco(a,b)               // optional
      escapes
#undef esco
#undef esc
          if (c < ' ')
      {
         l += 6;                // \u00XX
         continue;
      }
      l++;
   }
   return l;
}

static const char *jo_write_bad(jo_t j, const char *tag, int need)
{
   if (!j)
      return "No j";
   if (j->err)
      return j->err;
   if (j->parse)
      return (j->err = "Writing to parse");
   if (j->level && (j->o[(j->level - 1) / 8] & (1 << ((j->level - 1) & 7))))
   {
      if (!tag)
         return (j->err = "missing tag in object");
   } else if (tag)
      return (j->err = "tag in non object");
   if (need && tag)
      need += jo_codedlen(tag) + 1;
   if (need && j->comma)
      need++;
   if (jo_space(j, need) <= 0)
      return "No space";
   return NULL;
}

static void jo_write(jo_t j, uint8_t c)
{
   if (jo_space(j, 1) <= 0)
      return;
   j->buf[j->ptr++] = c;
}

static void jo_write_str(jo_t j, const char *s)
{
   jo_write(j, '"');
   while (*s)
   {
      uint8_t c = *s++;
#define esc(a,b) if(c==b){jo_write(j,'\\');jo_write(j,b);continue;}
#define esco(a,b)               // optional
      escapes
#undef esco
#undef esc
          if (c < ' ')
      {
         jo_write(j, '\\');
         jo_write(j, 'u');
         jo_write(j, '0');
         jo_write(j, '0');
         jo_write(j, BASE16[c >> 4]);
         jo_write(j, BASE16[c & 0xF]);
         continue;
      }
      jo_write(j, c);
   }
   jo_write(j, '"');
}

static void jo_write_tag(jo_t j, const char *tag)
{
   if (j->comma)
      jo_write(j, ',');
   j->comma = 1;
   if (!tag)
      return;
   jo_write_str(j, tag);
   jo_write(j, ':');
}

static void jo_lit(jo_t j, const char *tag, const char *lit)
{
   if (jo_write_bad(j, tag, strlen(lit)))
      return;
   while (*lit)
      jo_write(j, *lit++);
}

void jo_array(jo_t j, const char *tag)
{                               // Start an array
   if (jo_write_bad(j, tag, 1))
      return;
   if (j->level >= JO_MAX)
   {
      j->err = "JSON too deep";
      return;
   }
   j->o[j->level / 8] &= ~(1 << (j->level & 7));
   j->level++;
   j->comma = 0;
   jo_write(j, '[');
}

void jo_object(jo_t j, const char *tag)
{                               // Start an object
   if (jo_write_bad(j, tag, 1))
      return;
   if (j->level >= JO_MAX)
   {
      j->err = "JSON too deep";
      return;
   }
   j->o[j->level / 8] |= (1 << (j->level & 7));
   j->level++;
   j->comma = 0;
   jo_write(j, '{');
}

void jo_close(jo_t j)
{                               // Close current array or object
   if (jo_space(j, 1) <= 0)
      return;
   if (!j->level)
   {
      j->err = "JSON too many closes";
      return;
   }
   j->level--;
   j->comma = 1;
   jo_write(j, (j->o[j->level / 8] & (1 << (j->level & 7))) ? '}' : ']');
}

void jo_string(jo_t j, const char *tag, const char *string)
{                               // Add a string
   if (!string)
   {
      jo_null(j, tag);
      return;
   }
   if (jo_write_bad(j, tag, jo_codedlen(string)))
      return;
   jo_write_tag(j, tag);
   jo_write_str(j, string);
}

void jo_printf(jo_t j, const char *tag, const char *format, ...)
{                               // Add a string (formatted)
   if (jo_write_bad(j, tag, 0))
      return;
   jo_write_tag(j, tag);
   char *v = NULL;
   va_list ap;
   va_start(ap, format);
   vasprintf(&v, format, ap);
   if (!v)
   {
      j->err = "malloc for printf";
      return;
   }
   jo_write_str(j, v);
   free(v);
}

void jo_litf(jo_t j, const char *tag, const char *format, ...)
{                               // Add a literal (formatted)
   char temp[100];
   va_list ap;
   va_start(ap, format);
   vsnprintf(temp, sizeof(temp), format, ap);
   va_end(ap);
   jo_lit(j, tag, temp);
}

static void jo_baseN(jo_t j, const char *tag, const void *src, size_t slen, uint8_t bits, const char *alphabet)
{                               // base 16/32/64 binary to string
   size_t dlen = (slen * 8 + bits - 1) / bits;
   if (jo_write_bad(j, tag, dlen + 2))
      return;
   jo_write_tag(j, tag);
   jo_write(j, '"');
   unsigned int i = 0,
       b = 0,
       v = 0;
   while (i < slen)
   {
      b += 8;
      v = (v << 8) + ((uint8_t *) src)[i++];
      while (b >= bits)
      {
         b -= bits;
         jo_write(j, alphabet[(v >> b) & ((1 << bits) - 1)]);
      }
   }
   if (b)
   {                            // final bits
      b += 8;
      v <<= 8;
      b -= bits;
      jo_write(j, alphabet[(v >> b) & ((1 << bits) - 1)]);
      while (b)
      {                         // padding
         while (b >= bits)
         {
            b -= bits;
            jo_write(j, '=');
         }
         if (b)
            b += 8;
      }
   }
   jo_write(j, '"');
}

void jo_base64(jo_t j, const char *tag, const void *mem, size_t len)
{                               // Add a base64 string
   jo_baseN(j, tag, mem, len, 6, BASE64);
}

void jo_base32(jo_t j, const char *tag, const void *mem, size_t len)
{                               // Add a base32 string
   jo_baseN(j, tag, mem, len, 5, BASE32);
}

void jo_hex(jo_t j, const char *tag, const void *mem, size_t len)
{                               // Add a hex string
   jo_baseN(j, tag, mem, len, 4, BASE16);
}

void jo_int(jo_t j, const char *tag, int64_t val)
{                               // Add an integer
   jo_litf(j, tag, "%lld", val);
}

void jo_bool(jo_t j, const char *tag, int val)
{                               // Add a bool (true if non zero passed)
   jo_lit(j, tag, val ? "true" : "false");
}

void jo_null(jo_t j, const char *tag)
{                               // Add a null
   jo_lit(j, tag, "null");
}

// Parsing

jo_type_t jo_here(jo_t j)
{                               // Return what type of thing we are at - we are always at a value of some sort, which has a tag if we are in an object
   return 0;                    // TODO
}

void jo_next(jo_t j)
{                               // Move to next value - not this will pass any closing } or ] to get there
// Typically one loops in an object until jo_level() is back to start level or below.
}

const char *jo_tag(jo_t j)
{                               // Return pointer to the '"' at the start of the current value's tag
   return NULL;                 // TODO
}

const char *jo_val(jo_t j)
{                               // Return pointer to the first character of the value, e.g. the '"' at start of a string type
   return NULL;                 // TODO
}

int64_t jo_val_int(jo_t j)
{                               // Process the value and convert to integer
   return 0;                    // TODO
}

double jo_val_real(jo_t j)
{                               // Process the value and convert to real
   return 0;                    // TODO
}

int jo_val_bool(jo_t j)
{                               // Process the value and convert to bool (0 or 1)
   return 0;                    // TODO
}

ssize_t jo_strlen(const char *str)
{                               // Return length of decoded JSON string, where str is pointing to the '"' at start of tag or string value
   return 0;                    // TODO
}

size_t jo_strncpy(char *target, const char *src, size_t max)
{                               // This copies from src which is pointing to the '"' at start of tag or string value, to target
   return 0;                    // TODO
}

int jo_strncmp(char *target, const char *src, size_t max)
{                               // This compares src which is pointing to the '"' at start of tag or string value, to target
   return 0;                    // TODO
}
