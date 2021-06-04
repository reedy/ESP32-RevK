// Lightweight JSON syntax management tool kit
// This toolkit works solely at a syntax level, and does not all management of whole JSON object hierarchy

#include "jo.h"
#ifndef	JO_MAX
#define	JO_MAX	64
#endif

struct jo_s {                   // cursor to JSON object
   char *buf;                   // Start of JSON string
   const char *err;             // If in error state
   uint16_t ptr;                // Pointer in to buf
   uint16_t max;                // Max space for buf
   uint8_t parse:1;             // This is parsing, not generating
   uint8_t alloc:1;             // buf is malloced space
   uint8_t comma:1;             // Set if comma needed, i.e. object or array at current level has at least one entry
   uint8_t level;               // Current level
   uint8_t o[JO_MAX / 8];       // Bit set at each level if level is object, else it is array
};

// Setting up

jo_t *jo_parse_str(const char *buf)
{                               // Start parsing a null terminated JSON object string
   return NULL;                 // TODO
}

jo_t *jo_parse_mem(const char *buf, size_t len)
{                               // Start parsing a JSON string in memory - does not need a null
   return NULL;                 // TODO
}

jo_t *jo_create_mem(char *buf, size_t len)
{                               // Start creating JSON in memory at buf, max space len.
   return NULL;                 // TODO
}

jo_t jo_create_alloc(void)
{                               // Start creating JSON in memory, allocating space as needed.
   return NULL;                 // TODO
}

jo_t jo_copy(jo_t j)
{                               // Copy object - copies the object, and if allocating memory, makes copy of the allocated memory too
   return NULL;                 // TODO
}

char *jo_result(jo_t j)
{                               // Return JSON string
// If in error, this is NULL - so can be used to check for error state
// If writing, this has necessary closing brackets and NULL added first, but does not move the cursor
// Returns NULL if adding brackets and null would not fit
   return NULL;                 // TODO
}

void jo_free(jo_t j)
{                               // Free jo_t and any allocated memory
}

char *jo_result_free(jo_t j)
{                               // Return the JSON string, and free the jo_t object.
// NULL if error state, as per jo_result
// This is intended to be used with jo_create_alloc(), returning the allocated string (which will need freeing).
// If used with a fixed string, a strdup is done, so that the return value can always be freed
   return NULL;                 // TODO
}

int jo_level(jo_t j)
{                               // Current level, 0 being the top level
   return 0;                    // TODO
}

const char *jo_error(jo_t j, int *pos)
{                               // Return NULL if no error, else returns an error string.
// If pos is set then the offset in to the JSON is retported
   return NULL;                 // TODO
}

// Creating
// Note that tag is required if in an object and must be null if not

void jo_array(jo_t j, const char *tag)
{                               // Start an array
}

void jo_object(jo_t j, const char *tag)
{                               // Start an object
}

void jo_close(jo_t j)
{                               // Close current array or object
}

void jo_string(jo_t j, const char *tag, const char *string)
{                               // Add a string
}

void jo_printf(jo_t j, const char *tag, const char *format, ...)
{                               // Add a string (formatted)
}

void jo_base64(jo_t j, const char *tag, const void *mem, size_t len)
{                               // Add a base64 string
}

void jo_hex(jo_t j, const char *tag, const void *mem, size_t len)
{                               // Add a hex string
}

void jo_int(jo_t j, const char *tag, int64_t val)
{                               // Add an integer
}

void jo_bool(jo_t j, const char *tag, int val)
{                               // Add a bool (true if non zero passed)
}

void jo_null(jo_t j, const char *tag)
{                               // Add a null
}


// Parsing

jo_type_t jo_here(jo_t j)
{                               // Return what type of thing we are at - we are always at a value of some sort, which has a tag if we are in an object
   return 0;                    // TODO
}

void jo_next(jo_t j)
{                               // Move to next value - not this will pass any closing } or ] to get there
// Typically one loops in an object until j_level() is back to start level or below.
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
