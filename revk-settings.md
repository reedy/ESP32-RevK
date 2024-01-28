# Settings library

This defines the *new* settings library. For the time being `CONFIG_REVK_OLD_SETTINGS` allows the old library.

The purpose of library is to manage *settings*, i.e. non volatile stored parameters and configuration for an application.  These are available in the C code as variables, and can be accessed and changed via MQTT.

## Generating settings

The settings are defined in one or more files, typically `settings.def`, and built using a tool `revk_settings` in to `settings.c` and `settings.h`. The tool is normally in `components/ESP32-RevK/` and built from `revk_settings.c`.

The make process needs to run `revk_settings` on the `.def` files to make `settings.c` and `settings.h`. Include `components/ESP32-RevK/settings.def` in the list of `.def` files.

The application build needs to include `settings.c` which defines the actual variables. You may want `settings.c` and `settings.h` in `.gitignore`.

The C code using settings need to `#include 'settings.h'` to have the `extern` references to the settings.

The settings are loaded in to the C variables when `revk_boot()` is called.

## Settings definitions

The settings definitions file consist of a line per setting, but can also have blank lines and lines starting `//` as a comment.

It can also include any lines starting with `#`. This is to allow `#ifdef CONFIG_`... Such lines are included in the output files in the appropriate place to allow conditional settings.

Each setting in the file has:-

- The setting type, followed by whitespace. E.g. `gpio` or `u8`.
- The setting name, followed by whitespace. E.g. `debug`.
- Optional default value for setting.
- Optional additional attributes.
- Optional comment (starting `//`).

The setting types are defined below.

The setting name is the name of the setting as seen in C code, e.g. `baud`, and used in JSON over MQTT. The name can also contain a `.`, e.g. `uart.tx`, to allow grouping in an object in JSON, but in the C code there is no `.`, e.g. `uarttx`.

The default value for a numeric setting can just be a number, e.g. `123`. For a string it can be in quotes, e.g. `"ota.rev.uk"`. The quotes can be omitted for simple text with no spaces and not starting with a `.`.
The default can also be (unquoted) a `CONFIG_...` reference.
Where not specified the defaults for all strings are an empty string `""` (not NULL), and values are all `0`.

Additional attributes are in the form of C structure initialised values, e.g. `.array=10`. These can be separated by commas or spaces.

Note the basic syntax of the definition files are checked, but not whether they attributes are correct, they will be reported when compiling `settings.c`.

## Data types

|Type|Meaning|
|----|-------|
|`bit`|A single bit value holding `0` or `1` (also `false` or `true` in JSON). This is implemented as a bit field in C and a `#define` to allow access by name.|
|`gpio`|A GPIO definition, see below|
|`blob`|Binary data (up to 64K if space in NVS), see below|
|`s`|String i.e. `char*`|
|`u8`|`uint8_t`|
|`u16`|`uint16_t`|
|`u32`|`uint32_t`|
|`u64`|`uint64_t`|
|`s8`|`int8_t`|
|`s16`|`int16_t`|
|`s32`|`int32_t`|
|`s64`|`int64_t`|

### GPIO

The `gpio` type makes a structure which has the following fields.

|Field|Meaning|
|-----|-------|
|`.set`|If the GPIO is defined. This should be tested before using the GPIO.|
|`.num`|The GPIO number, to use in `gpio_`... functions.|
|`.inv`|If the GPIO is to be treated as logically inverted. The value is used with a `-` prefix in JSON.|
|`.pullup`|If the GPIO is be pulled up. The value is used with a `↑` prefix in JSON.|
|`.pulldown`|If the GPIO is be pulled down. The value is used with a `↓` prefix in JSON.|

In many cases, in JSON, the GPIO is just a number, but if it would not be valid in JSON, e.g. `↑4` or `-0`, then it is quoted as a string value.

### Binary

The `blob` format is a structure with `.len` and `.data` if `.array` is not set.

Note that `u8` with `.array` and `.hex` or `base64` is also assumed to be a simple JSON string, rather than an array of values.

## Attributes

Additional attributes relate to each setting as follows:-

|Attribute|Meaning|
|---------|-------|
|`.array`|A number defining how many entries this has, it creates an array in JSON (unless type is `c`).|
|`.live`|This setting can be updated `live` without a reboot. If the setting is changed then it is changed in memory (as well as being stored to NVS).|
|`.fix`|The setting is to be fixed, i.e. the default value is only used if not defined in NVS, and the value, even if default, is stored to NVS. This assumed for `gpio` type.|
|`.set`|The top bit of the value is set if the value is defined.|
|`.bitfield`|This is a string that are characters which can be prefixed (and./or suffixed) on a numeric value and set in the top bits of the value (see below).|
|`.hex`|The value should be hex encoded in JSON. Typically used with `c` and `.array` set|
|`.base64`|The value should be base64 encoded in JSON. Typically used with `c` and `.array` set|
|`.decimal`|Used with integer types this scales by specified number of digits. E.g. `.decimal=2` will show `123` as `1.23` in JSON. A `#define` is for the variable suffixed with `_scale` defining the scale, e.g. `100` for `.decimal=2`.|
|`.pass`|Set if this is a password|

The `.set` and `.bitfield` attributes can apply to a numeric value, and cause top bits in the integer value to be set. `.set` is always the top bit if present, so if you have a `u16` with `.set=1` and a value of `123` is set, it will be `32891`. The `.bitfield` defines a string of one or more utf-8 characters that represent bits starting from the top bit (or next bit if `.set` is used as well). When parsing, any of the bitfield characters can be before or after the number. When output, they are normally before rhe number, unless there is a space in the bitfield and those characters after the space come after the number (the space is not assigned a bit).

## JSON

- A simple variable, string or number, etc, is set using a JSON string or number.
- A simple variable can be set to `null` to set default.
- A bit is set with `0`/`1`, or `true`/`false`
- An array should be set using an array, any missing final elements are cleared to zero value
- A sub object can be set, or each value can be set directly. If an object, any missing sub values are set to default
- And array or object can be set to `null` to set to defaults
