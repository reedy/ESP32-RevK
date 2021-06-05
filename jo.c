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
   uint8_t comma:1;             // Set if comma needed / expected
   uint8_t tagok:1;             // We have skipped an expected tag already in parsing
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

static void *saferealloc(void *m, size_t len)
{
   void *n = realloc(m, len);
   if (m && m != n)
      free(m);
   return n;
}

static inline void jo_write(jo_t j, uint8_t c)
{                               // Write out byte
   if (!j || j->err || j->parse)
      return;
   if (j->ptr >= j->len && (!j->alloc || !(j->buf = saferealloc(j->buf, j->len += 100))))
   {
      j->err = "Out of space";
      return;
   }
   j->buf[j->ptr++] = c;
}

static inline int jo_peek(jo_t j)
{                               // Peek next raw byte (-1 for end/fail)
   if (!j || j->err || !j->parse || j->ptr >= j->len)
      return -1;
   return j->buf[j->ptr];
}

static inline int jo_read(jo_t j)
{                               // Read and advance next raw byte (-1 for end/fail)
   if (!j || j->err || !j->parse || j->ptr >= j->len)
      return -1;
   return j->buf[j->ptr++];
}

static int jo_read_str(jo_t j)
{                               // Read next (UTF-8) within a string, so decode escaping (-1 for end/fail)
   if (!j || !j->parse || j->err)
      return -1;
   int bad(const char *e) {     // Fail
      if (!j->err)
         j->err = e;
      return -1;
   }
   int utf8(void) {             // next character
      if (jo_peek(j) == '"')
         return -1;             // Clean end of string
      int c = jo_read(j),
          q = 0;
      if (c < 0)
         return c;
      if (c >= 0xF7)
         return bad("Bad UTF-8");
      if (c >= 0xF0)
      {                         // Note could check for F0 and next byte as bad
         c &= 0x07;
         q = 3;
      } else if (c >= 0xE0)
      {                         // Note could check for E0 and next byte as bad
         c &= 0x0F;
         q = 2;
      } else if (c >= 0xC0)
      {
         if (c < 0xC2)
            return bad("Bad UTF-8");
         c &= 0x1F;
         q = 1;
      } else if (c >= 0x80)
         return bad("Bat UTF-8");
      else if (c == '\\')
      {                         // Escape
         c = jo_read(j);
         if (c == 'u')
         {                      // Hex
            c = 0;
            for (int q = 4; q; q--)
            {
               int u = jo_read(j);
               if (u >= '0' && u <= '9')
                  c = (c << 4) + (u & 0xF);
               else if ((u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f'))
                  c = (c << 4) + 9 + (u & 0xF);
               else
                  return bad("bad hex escape");
            }
         }
#define esc(a,b) else if(c==a)c=b;
#define esco(a,b) esc(a,b)      // optional
         escapes
#undef esco
#undef esc
             else
            return bad("Bad escape");
      }
      while (q--)
      {                         // More UTF-8 characters
         int u = jo_read(j);
         if (u < 0x80 || u >= 0xC0)
            return bad("Bad UTF-8");
         c = (c << 6) + (u & 0x3F);
      }
      return c;
   }
   int c = utf8();
   if (c >= 0xD800 && c <= 0xDBFF)
   {                            // UTF16 Surrogates
      int c2 = utf8();
      if (c2 < 0xDC00 || c2 > 0xDFFF)
         return bad("Bad UTF-16, second part invalid");
      c = ((c & 0x3FF) << 10) + (c2 & 0x3FF) + 0x10000;
   }
   return c;
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
   return n;
}

char *jo_result(jo_t j)
{                               // Return JSON string - if writing then closes all levels and adds terminating null first. Does not free. NULL for error
   if (!j || j->err)
      return NULL;              // No good
   if (j->parse)
      return j->buf;            // As parsed
   if (!j->ptr)
   {
      j->err = "Empty";
      return NULL;
   }
   // Add closing...
   while (j->level)
      jo_close(j);
   jo_write(j, 0);              // Final null
   if (j->err)
      return NULL;
   return j->buf;
}

void jo_free(jo_t * jp)
{                               // Free jo_t and any allocated memory
   if (!jp)
      return;
   jo_t j = *jp;
   if (!j)
      return;
   *jp = NULL;
   if (j->alloc && j->buf)
      free(j->buf);
   free(j);
}

char *jo_result_free(jo_t * jp)
{                               // Return JSON string and frees JSON object. Null for error.
// Return value needs to be freed, intended to be used with jo_create_alloc()
   if (!jp)
      return NULL;
   jo_t j = *jp;
   if (!j)
      return NULL;
   *jp = NULL;
   char *res = jo_result(j);
   if (res && !j->alloc)
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
   if (j && !j->err && !j->parse && !j->alloc && j->ptr + j->level + 1 > j->len)
      return "No space to close";
   if (pos)
      *pos = (j ? j->ptr : -1);
   if (!j)
      return "No j";
   return j->err;
}

// Creating
// Note that tag is required if in an object and must be null if not

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

static const char *jo_write_check(jo_t j, const char *tag)
{                               // Check if we are able to write, and write the tag
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
   if (!j->level && j->ptr)
      return (j->err = "second value at top level");
   if (j->comma)
      jo_write(j, ',');
   j->comma = 1;
   if (tag)
   {
      jo_write_str(j, tag);
      jo_write(j, ':');
   }
   return j->err;
}

void jo_lit(jo_t j, const char *tag, const char *lit)
{
   if (jo_write_check(j, tag))
      return;
   while (*lit)
      jo_write(j, *lit++);
}

void jo_array(jo_t j, const char *tag)
{                               // Start an array
   if (jo_write_check(j, tag))
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
   if (jo_write_check(j, tag))
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
   if (jo_write_check(j, tag))
      return;
   jo_write_str(j, string);
}

void jo_stringf(jo_t j, const char *tag, const char *format, ...)
{                               // Add a string (formatted)
   if (jo_write_check(j, tag))
      return;
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
   if (jo_write_check(j, tag))
      return;
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

static inline int jo_ws(jo_t j)
{                               // Skip white space, and return peek at next
   int c = jo_peek(j);
   while (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      c = jo_read(j);
   return c;
}

jo_type_t jo_here(jo_t j)
{                               // Return what type of thing we are at - we are always at a value of some sort, which has a tag if we are in an object
   if (!j || j->err || !j->parse)
      return JO_END;
   int c = jo_ws(j);
   if (c < 0)
      return c;
   if (j->comma)
   {
      if (!j->level)
      {
         j->err = "Extra value at top level";
         return JO_END;
      }
      if (c != ',')
      {
         j->err = "Missing comma";
         return JO_END;
      }
      jo_read(j);
      c = jo_ws(j);
      j->comma = 0;             // Comma consumed
   }
   // These are a guess of type - a robust check is done by jo_next
   if (!j->tagok && j->level && (j->o[(j->level - 1) / 8] & (1 << ((j->level - 1) & 7))))
   {                            // We should be at a tag
      if (c != '"')
      {
         j->err = "Missing tag";
         return JO_END;
      }
      return JO_TAG;
   }
   if (c == '"')
      return JO_STRING;
   if (c == '{')
      return JO_OBJECT;
   if (c == '[')
      return JO_ARRAY;
   if (c == '}' || c == ']')
   {
      if (j->tagok)
      {
         j->err = "Missing value";
         return JO_END;
      }
      if (!j->level)
      {
         j->err = "Too many closed";
         return JO_END;
      }
      if (c != ((j->o[j->level / 8] & (1 << (j->level & 7))) ? '}' : ']'))
      {
         j->err = "Mismatched close";
         return JO_END;
      }
      return JO_CLOSE;
   }
   if (c == 'n')
      return JO_NULL;
   if (c == 't')
      return JO_TRUE;
   if (c == 'f')
      return JO_FALSE;
   if (c == '-' || (c >= '0' && c <= '9'))
      return JO_NUMBER;
   j->err = "Bad JSON";
   return JO_END;
}

jo_type_t jo_next(jo_t j)
{                               // Move to next value, this validates what we are skipping. A tag and its value are separate
   int c;
   if (!j || !j->parse || j->err)
      return JO_END;
   switch (jo_here(j))
   {
   case JO_END:                // End or error
      break;
   case JO_TAG:                // Tag
      jo_read(j); // "
      while (jo_read_str(j) >= 0);
      if (!j->err && jo_read(j) != '"')
         j->err = "Missing closing quote on tag";
      jo_ws(j);
      if (!j->err && jo_read(j) != ':')
         j->err = "Missing colon after tag";
      j->tagok = 1;             // We have skipped tag
      break;
   case JO_OBJECT:
      jo_read(j); // {
      if (j->level >= JO_MAX)
      {
         j->err = "JSON too deep";
         break;
      }
      j->o[j->level / 8] |= (1 << (j->level & 7));
      j->level++;
      j->comma = 0;
      j->tagok = 0;
      break;
   case JO_ARRAY:
      jo_read(j); // ]
      if (j->level >= JO_MAX)
      {
         j->err = "JSON too deep";
         break;
      }
      j->o[j->level / 8] &= ~(1 << (j->level & 7));
      j->level++;
      j->comma = 0;
      j->tagok = 0;
      break;
   case JO_CLOSE:
      jo_read(j); // }/]
      j->level--;               // Was checked by jo_here()
      j->comma = 1;
      j->tagok = 0;
      break;
   case JO_STRING:
      jo_read(j); // "
      while (jo_read_str(j) >= 0);
      if (!j->err && jo_read(j) != '"')
         j->err = "Missing closing quote on string";
      j->comma = 1;
      j->tagok = 0;
      break;
   case JO_NUMBER:
      if (jo_peek(j) == '-')
         jo_read(j);            // minus
      if ((c = jo_peek(j)) == '0')
         jo_read(j);            // just zero
      else if (c >= '1' && c <= '9')
         while ((c = jo_peek(j)) >= '0' && c <= '9')
            jo_read(j);         // int
      if (jo_peek(j) == '.')
      {                         // real
         jo_read(j);
         if ((c = jo_peek(j)) < '0' || c > '9')
            j->err = "Bad real, must be digits after decimal point";
         else
            while ((c = jo_peek(j)) >= '0' && c <= '9')
               jo_read(j);      // frac
      }
      if ((c = jo_peek(j)) == 'e' || c == 'E')
      {                         // exp
         jo_read(j);
         if ((c = jo_peek(j)) == '-' || c == '+')
            jo_read(j);
         if ((c = jo_peek(j)) < '0' || c > '9')
            j->err = "Bad exp";
         else
            while ((c = jo_peek(j)) >= '0' && c <= '9')
               jo_read(j);      // exp
      }
      j->comma = 1;
      j->tagok = 0;
      break;
   case JO_NULL:
      if (jo_read(j) != 'n' ||  //
          jo_read(j) != 'u' ||  //
          jo_read(j) != 'l' ||  //
          jo_read(j) != 'l')
         j->err = "Misspelled null";
      j->comma = 1;
      j->tagok = 0;
      break;
   case JO_TRUE:
      if (jo_read(j) != 't' ||  //
          jo_read(j) != 'r' ||  //
          jo_read(j) != 'u' ||  //
          jo_read(j) != 'e')
         j->err = "Misspelled true";
      j->comma = 1;
      j->tagok = 0;
      break;
   case JO_FALSE:
      if (jo_read(j) != 'f' ||  //
          jo_read(j) != 'a' ||  //
          jo_read(j) != 'l' ||  //
          jo_read(j) != 's' ||  //
          jo_read(j) != 'e')
         j->err = "Misspelled false";
      j->comma = 1;
      j->tagok = 0;
      break;
   }
   return jo_here(j);
}

static ssize_t jo_cpycmp(jo_t j, char *str, size_t max, uint8_t cmp)
{                               // Copy or compare (-1 for j<str, +1 for j>str)
   char *end = str + max;
   if (!j || !j->parse || j->err)
   {                            // No pointer
      if (!cmp)
         return -1;             // Invalid length
      if (!str)
         return 0;              // null==null?
      return 1;                 // str>null
   }
   jo_t p = jo_copy(j);
   int c = jo_peek(p);
   ssize_t result = 0;
   void process(int c) {        // Compare or copy or count, etc
      if (cmp)
      {                         // Comparing
         if (!str)
            return;             // Uh
         if (str >= end)
         {
            result = 1;        // str ended, so str<j
            return;
         }
         int c2 = *str++,
             q = 0;
         if (c2 >= 0xF0)
         {
            c2 &= 0x07;
            q = 3;
         } else if (c >= 0xE0)
         {
            c2 &= 0x0F;
            q = 2;
         } else if (c >= 0xC0)
         {
            c2 &= 0x1F;
            q = 1;
         }
         while (q-- && str < end && *str >= 0x80 && *str < 0xC0)
            c2 = (c2 << 6) + (*str++ & 0x3F);
         if (c < c2)
         {
            result = -1;         // str>j
            return;
         }
         if (c > c2)
         {
            result = 1;        // str<j
            return;
         }
      } else
      {                         // Copy or count
         void add(uint8_t v) {
            if (str && str < end)
               *str++ = v;
            result++;           // count
         }
         if (c >= 0xF0)
         {
            add(0xF0 + (c >> 18));
            add(0xC0 + ((c >> 12) & 0x3F));
            add(0xC0 + ((c >> 6) & 0x3F));
            add(0xC0 + (c & 0x3F));
         } else if (c >= 0xE0)
         {
            add(0xF0 + (c >> 12));
            add(0xC0 + ((c >> 6) & 0x3F));
            add(0xC0 + (c & 0x3F));
         } else if (c >= 0xC0)
         {
            add(0xF0 + (c >> 6));
            add(0xC0 + (c & 0x3F));
         } else
            add(c);
         if (str && str < end)
            *str = 0;           // Final null...
      }
   }
   if (c == '"')
   {                            // String
      jo_read(p);
      while ((c = jo_read_str(p)) >= 0 && (!cmp || !result))
         process(c);
   } else
   {                            // Literal or number
      while ((c = jo_read(p)) >= 0 && c > ' ' && c != ',' && c != ']' && c != '}' && (!cmp || !result))
         process(c);
   }
   if (!result && cmp && str && str < end)
      result = -1;               // j ended, do str>j
   jo_free(&p);
   return result;
}

ssize_t jo_strlen(jo_t j)
{                               // Return byte length, if a string or tag this is the decoded byte length, else length of literal
   return jo_cpycmp(j, NULL, 0, 0);
}

ssize_t jo_strncpy(jo_t j,char *target, size_t max)
{                               // Copy from current point to a string. If a string or a tag, remove quotes and decode/deescape
   return jo_cpycmp(j, target, max, 0);
}

ssize_t jo_strncmp(jo_t j,char *source, size_t max)
{                               // Compare from current point to a string. If a string or a tag, remove quotes and decode/deescape
   return jo_cpycmp(j, source, max, 1);
}

jo_type_t jo_skip(jo_t j)
{                               // Skip to next value at this level
   jo_type_t t = jo_here(j);
   if (t > JO_CLOSE)
   {
      int l = jo_level(j);
      do
         jo_next(j);
      while ((t = jo_next(j)) != JO_END && jo_level(j) > l);
   }
   return t;
}
