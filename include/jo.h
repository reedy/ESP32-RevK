// Lightweight JSON syntax management tool kit
// This toolkit works solely at a syntax level, and does not all management of whole JSON object hierarchy

#include <stddef.h>
#include <unistd.h>

// Types

typedef struct jo_s *jo_t;      // The JSON cursor used by all calls
typedef enum {                  // The parse data value type we are at
   JO_END,                      // Not a value, we are at the end, or in an error state
   // JO_END always at start
   JO_TAG,			// at a tag
   JO_OBJECT,                   // value is the '{' of an object, jo_next() goes in to object if it has things in it
   JO_ARRAY,                    // value is the '[' of an array, jo_next() goes in to array if it has things in it
   JO_CLOSE,			// at close of object or array
   JO_STRING,                   // value is the '"' of a string
   JO_NUMBER,                   // value is start of an number
   // Can test >= JO_NULL as test for literal
   JO_NULL,                     // value is the 'n' in a null
   // Can test >= JO_TRUE as test for bool
   JO_TRUE,                     // value is the 't' in true, can compare >= JO_TRUE for boolean
   JO_FALSE,                    // value if the 'f' in false
} jo_type_t;

// Setting up

jo_t jo_parse_str(const char *buf);
// Start parsing a null terminated JSON object string

jo_t jo_parse_mem(const char *buf, size_t len);
// Start parsing a JSON string in memory - does not need a null

jo_t jo_create_mem(char *buf, size_t len);
// Start creating JSON in memory at buf, max space len.

jo_t jo_create_alloc(void);
// Start creating JSON in memory, allocating space as needed.

jo_t jo_copy(jo_t);
// Copy object - copies the object, and if allocating memory, makes copy of the allocated memory too

char *jo_result(jo_t);
// Return JSON string - if writing then closes all levels and adds terminating null first. Does not free. NULL for error

void jo_free(jo_t *);
// Free jo_t and any allocated memory

char *jo_result_free(jo_t *);
// Return JSON string and frees JSON object. Null for error.
// Return value needs to be freed, intended to be used with jo_create_alloc()

int jo_level(jo_t);
// Current level, 0 being the top level

const char *jo_error(jo_t, int *pos);
// Return NULL if no error, else returns an error string.
// If pos is set then the offset in to the JSON is retported

// Creating
// Note that tag is required if in an object and must be null if not

void jo_array(jo_t, const char *tag);
// Start an array

void jo_object(jo_t, const char *tag);
// Start an object

void jo_close(jo_t);
// Close current array or object

void jo_string(jo_t, const char *tag, const char *string);
// Add a string

void jo_printf(jo_t, const char *tag, const char *format, ...);
// Add a string (formatted)

void jo_litf(jo_t, const char *tag, const char *format, ...);
// Add a literal string (formatted) - caller is expected to meet JSON rules - used typically for numeric values

void jo_base64(jo_t, const char *tag, const void *mem, size_t len);
// Add a base64 string

void jo_hex(jo_t, const char *tag, const void *mem, size_t len);
// Add a hex string

void jo_int(jo_t, const char *tag, int64_t);
// Add an integer

void jo_bool(jo_t, const char *tag, int);
// Add a bool (true if non zero passed)

void jo_null(jo_t, const char *tag);
// Add a null


// Parsing

jo_type_t jo_here(jo_t);
void jo_next(jo_t);
// Here returns where we are in the parse, and next moves to next element that can be parsed

const char *jo_tag(jo_t);
// Return pointer to the '"' at the start of the current value's tag

const char *jo_val(jo_t);
// Return pointer to the first character of the value, e.g. the '"' at start of a string type

int64_t jo_val_int(jo_t);
// Process the value and convert to integer

double jo_val_real(jo_t);
// Process the value and convert to real

int jo_val_bool(jo_t);
// Process the value and convert to bool (0 or 1)

ssize_t jo_strlen(const char *str);
// Return length of decoded JSON string, where str is pointing to the '"' at start of tag or string value

size_t jo_strncpy(char *target, const char *src, size_t max);
// This copies from src which is pointing to the '"' at start of tag or string value, to target

int jo_strncmp(char *target, const char *src, size_t max);
// This compares src which is pointing to the '"' at start of tag or string value, to target