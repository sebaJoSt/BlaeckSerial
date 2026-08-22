/*
        File: BlaeckSerial.h
        Author: Sebastian Strobl

    A library which extends Serial functionality to transmit binary data.
    Also included is a Message Parser for incoming data in the syntax of
    <HelloWorld, 12, 47>. The parsed command 'HelloWorld' and its parameters
    are available in your own sketch by attaching a callback function.

    The library is heavily inspired by Nick Dodd's
    AdvancedSerial Library https://github.com/Nick1787/AdvancedSerial/
    The message parser uses code from Robin2's Arduino forum thread
    "Serial Basic Input" https://forum.arduino.cc/index.php?topic=396450.0
*/

#ifndef BLAECKSERIAL_H
#define BLAECKSERIAL_H

#define BLAECKSERIAL_VERSION "7.0.0"
#define BLAECKSERIAL_VERSION_MAJOR 7
#define BLAECKSERIAL_VERSION_MINOR 0
#define BLAECKSERIAL_VERSION_PATCH 0
#define BLAECKSERIAL_NAME "BlaeckSerial"

#include <Arduino.h>
// Umbrella header rather than <CRC32.h> / <CRC16.h>: the individual headers
// collided with core headers on ArduinoCore-mbed once (see BlaeckTCP 6.0.1).
// CRC.h pulls in CRC8/12/16/32/64 and is what BlaeckTCP has used since.
#include <CRC.h>
#include <new>
#include <string.h>
#include <limits.h>

// Compile-time settings. Override the defaults below, e.g.:
//   #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
//
// IMPORTANT: an override MUST reach every translation unit - your sketch AND
// BlaeckSerial.cpp. These values size members of class BlaeckSerial, so a
// setting seen by only one of them gives the class two different layouts
// (an ODR violation) and corrupts memory silently. All-or-nothing, never half.
//
//   PlatformIO:   build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
//   Arduino IDE:  a BlaeckSerialConfig.h in your sketch folder is NOT picked
//                 up without extra setup, because the sketch folder is not on
//                 the compiler's include path. See "Configuration" in
//                 docs/configuration.md for the three ways to do it.
#if defined __has_include
  #if __has_include(<BlaeckSerialConfig.h>)
    #include <BlaeckSerialConfig.h>
  #endif
#endif

// Buffered writes
// ---------------
// When ON:  each Blaeck data frame is first assembled in a RAM buffer
//           (60 + signalCapacity * 30 bytes) and then sent with a single
//           StreamRef->write(buf, len) call.
// When OFF: the frame is streamed out directly via many small
//           StreamRef->write(byte) / print() calls as it is produced
//           (no extra RAM buffer needed).
//
// Per-board defaults:
//   - AVR (Uno, Mega, Nano, ...):                 OFF
//       Saves scarce SRAM; the classic ATmega USB-to-serial bridges
//       handle small writes fine.
//   - ArduinoCore-mbed (Giga R1, Portenta, Nicla, Opta,
//                       Nano 33 BLE, Nano RP2040 Connect):  OFF
//       If the USB host closes the port while a bulk
//       Serial.write(buf, len) is in progress, the Mbed USBSerial
//       implementation can get stuck permanently: the call never
//       returns, even after the host reopens the port, so the sketch's
//       main loop stops processing commands until the board is reset.
//       Per-byte writes do not exhibit this behavior.
//   - Everything else (Uno R4 WiFi, ESP32, SAMD, RP2040 non-mbed, ...): ON
//       Required on Uno R4 WiFi where the RA4M1 -> ESP32-S3 USB bridge
//       drops bytes if fed many small writes; bulk writes are reliable
//       on the other targets too.
//
// Override at compile time by defining BLAECK_BUFFERED_WRITES_DEFAULT
// (or via BlaeckSerialConfig.h / build_flags), or at runtime with
// setBufferedWrites(true|false).
#ifndef BLAECK_BUFFERED_WRITES_DEFAULT
  #if defined(__AVR__) || defined(ARDUINO_ARCH_MBED)
    #define BLAECK_BUFFERED_WRITES_DEFAULT false
  #else
    #define BLAECK_BUFFERED_WRITES_DEFAULT true
  #endif
#endif

#ifndef BLAECK_COMMAND_MAX_CHARS_DEFAULT
  // 128 is the smallest buffer that puts a 32-byte text command within reach of any input: a
  // byte costs up to three characters once percent-encoded, so 3*32 plus a command name and its
  // delimiters. Below it the frame runs out before the advertised length does, and a value the
  // device would have accepted cannot reach it at all.
  //
  // Scaled with SRAM like the handler table below, since three buffers are this size: small AVRs
  // keep 48, where 128 would cost an eighth of their memory for a command kind many never use.
  #if defined(__AVR__)
    #if defined(RAMEND) && (RAMEND >= 0x10FF)
      #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
    #else
      #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 48
    #endif
  #else
    #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
  #endif
#endif

// How many commands, signals or channels a table holds is no longer a macro: say it in the
// sketch, on the begin() chain - BLAECK.begin(&Serial).withCommands(16). See "Table sizes"
// further down for the defaults each table starts from.

// Command metadata (Home Assistant discovery catalog).
// When ON, the typed command registration helpers (onNumberCommand/
// onSwitchCommand/onSelectCommand/onButtonCommand) store parameter metadata and
// the device can emit a 0xA0 "Command List" frame in response to
// BLAECK.WRITE_COMMANDS. Turn OFF to save SRAM/flash on tiny targets; the typed
// helpers then behave exactly like plain onCommand() (no metadata), and
// BLAECK.WRITE_COMMANDS answers with an empty 0xA0 so a polling host does not
// wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_COMMAND_META
  #define BLAECK_ENABLE_COMMAND_META 1
#endif

// Signal metadata (Home Assistant discovery for sensors).
// When ON, addSignal() returns a handle whose withUnit()/withDeviceClass()/
// withStateClass()/withIcon()/withDisplayPrecision()/diagnostic()/
// disabledByDefault()/forceUpdate() describe how a signal is presented, and the
// device can emit a 0xF0 "Signal Config" frame in response to
// BLAECK.WRITE_SIGNAL_CONFIG. Costs ~9 bytes of SRAM per signal, so turn OFF on
// tiny targets: the handle's methods still compile and simply store nothing, so
// a sketch needs no #ifdef, and BLAECK.WRITE_SIGNAL_CONFIG answers with an empty
// 0xF0 so a polling host does not wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_SIGNAL_META
  #define BLAECK_ENABLE_SIGNAL_META 1
#endif

// State channels (a value the device reports when it changes).
// When ON, the device can declare named state channels with
// addStateChannel(), emit the 0x90 "State Channel List" frame in response to
// BLAECK.WRITE_STATE_CHANNELS, and report a value on those channels with
// writeState() (0x95). A state channel carries the current value of something -
// text or a number - pushed when it changes and never written to a data store,
// which is what separates it from a signal: a signal is sampled into every
// logged row on the logging interval. Turn OFF to save SRAM/flash on tiny targets; both
// writeState() and addStateChannel() then compile away, and
// BLAECK.WRITE_STATE_CHANNELS answers with an empty 0x90 so a polling host
// does not wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_STATE_CHANNELS
  #define BLAECK_ENABLE_STATE_CHANNELS 1
#endif

// Two things create a state channel, and both spend a slot here: addStateChannel(), and a
// typed command's withOwnState(), which gives that command a channel of its own to carry its
// value on. Size the table with BLAECK.begin(&Serial).withStateChannels(n).

// Events (Home Assistant event entities).
// When ON, the device can declare named event channels with addEventChannel(),
// give each a closed list of event types with addEventType(), emit the 0x80
// "Event Channel List" frame in response to BLAECK.WRITE_EVENT_CHANNELS, and
// report an occurrence with writeEvent() (0x85).
// Unlike a state channel, an event carries no value: the frame holds only the channel
// and event type indices, so the wording is fixed at compile time and a host
// needs the 0x80 catalog to interpret it. Use a state channel for anything
// that has to carry a runtime value.
// Turn OFF to save SRAM/flash on tiny targets; the API then compiles away, and
// BLAECK.WRITE_EVENT_CHANNELS answers with an empty 0x80 so a polling host does
// not wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_EVENTS
  #define BLAECK_ENABLE_EVENTS 1
#endif

// Size the channel table with BLAECK.begin(&Serial).withEventChannels(n).

// Event types are held in one pool shared by every channel, so a channel that
// needs ten types and one that needs two are both served without sizing every
// channel for the worst case. Each entry costs ~3 bytes of SRAM. Size the pool
// with BLAECK.begin(&Serial).withEventTypes(n).


// Every built-in command name, written once. read() dispatches on these rather than on
// literals of its own, and MAX_BUILTIN_COMMAND_COUNT is checked against this same list,
// so a name too long for the parse buffer fails the build instead of arriving truncated
// and matching nothing - a device deaf to one of its own commands.
// Adding a built-in means adding it here, which is also what makes it reachable.
#define BLAECK_BUILTIN_WRITE_SYMBOLS "BLAECK.WRITE_SYMBOLS"
#define BLAECK_BUILTIN_WRITE_SIGNAL_CONFIG "BLAECK.WRITE_SIGNAL_CONFIG"
#define BLAECK_BUILTIN_WRITE_DATA "BLAECK.WRITE_DATA"
#define BLAECK_BUILTIN_GET_DEVICES "BLAECK.GET_DEVICES"
#define BLAECK_BUILTIN_WRITE_COMMANDS "BLAECK.WRITE_COMMANDS"
#define BLAECK_BUILTIN_WRITE_STATE_CHANNELS "BLAECK.WRITE_STATE_CHANNELS"
#define BLAECK_BUILTIN_WRITE_EVENT_CHANNELS "BLAECK.WRITE_EVENT_CHANNELS"
#define BLAECK_BUILTIN_ACTIVATE "BLAECK.ACTIVATE"
#define BLAECK_BUILTIN_DEACTIVATE "BLAECK.DEACTIVATE"

#define BLAECK_BUILTIN_COMMAND_LIST(X)  \
  X(BLAECK_BUILTIN_WRITE_SYMBOLS)       \
  X(BLAECK_BUILTIN_WRITE_SIGNAL_CONFIG) \
  X(BLAECK_BUILTIN_WRITE_DATA)          \
  X(BLAECK_BUILTIN_GET_DEVICES)         \
  X(BLAECK_BUILTIN_WRITE_COMMANDS)      \
  X(BLAECK_BUILTIN_WRITE_STATE_CHANNELS)\
  X(BLAECK_BUILTIN_WRITE_EVENT_CHANNELS)\
  X(BLAECK_BUILTIN_ACTIVATE)            \
  X(BLAECK_BUILTIN_DEACTIVATE)

// Eleven values, so the underlying type is pinned to a byte rather than left as the int
// a compiler picks by default: this type is a field in every signal entry and every state
// channel entry, and a byte there is a byte per entry. The enumerators are unchanged, so
// nothing that names one is affected.
// Room for one resolved select option while a frame is built - a stack buffer for the length
// of that frame, not storage per channel.
#ifndef BLAECK_STATE_MAX_OPTION_CHARS
  #define BLAECK_STATE_MAX_OPTION_CHARS 24
#endif

typedef enum DataType : uint8_t
{
  Blaeck_bool,
  Blaeck_byte,
  Blaeck_short,
  Blaeck_ushort,
  Blaeck_int,
  Blaeck_uint,
  Blaeck_long,
  Blaeck_ulong,
  Blaeck_float,
  Blaeck_double,
  Blaeck_string
} dataType;

// The enumerators are not the wire codes and nothing treats them as such: _dtypeCode() is
// the one mapping, and the schema hash, the symbol list and the state frames all go through
// it. Reorder this list or insert a type and nothing on the wire moves.

// What a state channel carries, when there is no variable whose type could say it. The second
// argument to addStateChannel() always names the type - as a pointer when the channel reads a
// variable, as one of these when it carries only what writeState() hands it.
//
// Three types rather than one enum because the tag chooses the handle that comes back, and a
// handle only offers the modifiers its kind can use: withUnit() on a text channel makes a host
// refuse the text, so it should not compile. An enum value cannot select a return type.
struct BlaeckTextTag
{
};
struct BlaeckBoolTag
{
};
struct BlaeckNumericTag
{
  /*!
    @brief   Which numeric type the tag names.

    @code
      Blaeck.addStateChannel(F("Temperature"), BlaeckFloat);
    @endcode
  */
  dataType t;
};

constexpr BlaeckTextTag BlaeckText{};
constexpr BlaeckBoolTag BlaeckBool{};
constexpr BlaeckNumericTag BlaeckByte{Blaeck_byte};
constexpr BlaeckNumericTag BlaeckShort{Blaeck_short};
constexpr BlaeckNumericTag BlaeckUShort{Blaeck_ushort};
constexpr BlaeckNumericTag BlaeckInt{Blaeck_int};
constexpr BlaeckNumericTag BlaeckUInt{Blaeck_uint};
constexpr BlaeckNumericTag BlaeckLong{Blaeck_long};
constexpr BlaeckNumericTag BlaeckULong{Blaeck_ulong};
constexpr BlaeckNumericTag BlaeckFloat{Blaeck_float};
constexpr BlaeckNumericTag BlaeckDouble{Blaeck_double};

// How a signal's value accumulates over time, which is what lets a host keep
// statistics on it (0xF0 SignalMetaFlags bits 3-5). MEASUREMENT is a value that goes up and down
// and is meaningful at any instant; TOTAL and TOTAL_INCREASING are running sums,
// the latter one that only ever grows and may reset to zero. MEASUREMENT_ANGLE is
// a measurement that wraps, averaged the short way round.
//
// NONE is what a signal that never called withStateClass() carries, and a host
// keeps no statistics on it. Nothing is assumed on a signal's behalf: a value
// that should be graphed over time has to say so.
enum BlaeckStateClass
{
  BLAECK_STATE_CLASS_NONE = 0,
  BLAECK_STATE_CLASS_MEASUREMENT = 1,
  BLAECK_STATE_CLASS_TOTAL = 2,
  BLAECK_STATE_CLASS_TOTAL_INCREASING = 3,
  // An angle averages the long way round otherwise: the arithmetic mean of 350 and 10
  // is 180, the circular mean is 0. For a wind vane, a compass heading, a phase.
  BLAECK_STATE_CLASS_MEASUREMENT_ANGLE = 4
};

// 0xF0 SignalMetaFlags. The stored word is the wire value: a with*/adjective call
// sets its bit and its field together, so there is one source of truth and a zero
// word means the signal declares nothing at all - which is why disabledByDefault
// is stated inverted and forceUpdate is not.
enum BlaeckSignalMetaFlag
{
  BLAECK_SIG_HAS_UNIT = 0x0001,
  BLAECK_SIG_HAS_DEVICE_CLASS = 0x0002,
  BLAECK_SIG_HAS_ICON = 0x0004,
  // Three bits, not two: Home Assistant defines five state classes counting none, and a
  // two-bit field could hold only four. Bits 12-15 stay reserved.
  BLAECK_SIG_STATE_CLASS_MASK = 0x0038, // bits 3-5
  BLAECK_SIG_DIAGNOSTIC = 0x0040,
  BLAECK_SIG_DISABLED_BY_DEFAULT = 0x0080,
  BLAECK_SIG_FORCE_UPDATE = 0x0100,
  BLAECK_SIG_HAS_DISPLAY_PRECISION = 0x0200,
  BLAECK_SIG_HAS_OPTIONS = 0x0400,
  BLAECK_SIG_HAS_DISPLAY_NAME = 0x0800
};
static const byte BLAECK_SIG_STATE_CLASS_SHIFT = 3;

// 0x90 StateChannelFlags. A different word from the signal one above and laid out
// differently - the two catalogs grew apart - so the names are kept separate rather
// than shared, and the frame writer reads these instead of spelling the bits twice.
// Unit, state class and display precision only mean anything on a channel whose
// value type is numeric; a text channel leaves them unset.
enum BlaeckStateChannelFlag
{
  BLAECK_SCH_HAS_ICON = 0x0001,
  BLAECK_SCH_DIAGNOSTIC = 0x0002,
  BLAECK_SCH_HAS_STATE_VALUE = 0x0004,
  BLAECK_SCH_HAS_DEVICE_CLASS = 0x0008,
  BLAECK_SCH_DISABLED_BY_DEFAULT = 0x0010,
  BLAECK_SCH_FORCE_UPDATE = 0x0020,
  BLAECK_SCH_HAS_OPTIONS = 0x0040,
  BLAECK_SCH_HAS_UNIT = 0x0080,
  BLAECK_SCH_STATE_CLASS_MASK = 0x0700, // bits 8-10
  BLAECK_SCH_HAS_DISPLAY_PRECISION = 0x0800
};
static const byte BLAECK_SCH_STATE_CLASS_SHIFT = 8;

#if BLAECK_ENABLE_SIGNAL_META
// What a signal says about itself beyond its name and datatype: everything the 0xF0
// Signal Metadata frame carries. Kept out of the signal entry and allocated only when
// a sketch describes a signal, because most signals describe nothing and this record
// is bigger than the entry that would otherwise hold it.
struct SignalMeta
{
  // Flash pointers, so a declared unit costs 2 bytes of SRAM and not its length.
  const __FlashStringHelper *Unit = nullptr;
  const __FlashStringHelper *DeviceClass = nullptr;
  const __FlashStringHelper *Icon = nullptr;
  const __FlashStringHelper *Options = nullptr;
  const __FlashStringHelper *DisplayName = nullptr;
  uint16_t MetaFlags = 0;
  uint8_t DisplayPrecision = 0;
};
#endif

struct Signal
{
  // Either a copy this library owns, or a pointer into flash when the sketch named the
  // signal with F(). NameInFlash says which, and nothing reads this field directly - the
  // _signalName* helpers do, because reading a flash address as if it were RAM is silent
  // garbage on AVR rather than a crash.
  const char *SignalName = nullptr;
  dataType DataType;
  void *Address;
  // Two bits, not two bytes: this pair is one byte per signal, and a signal table is the
  // biggest thing most sketches ask this library for. Neither can carry an initializer
  // here - C++11 forbids one on a bit-field - so all three are set when a signal is
  // registered, alongside DataType and Address, which have never had one either.
  uint8_t Updated : 1;
  uint8_t NameInFlash : 1;
  // Whether NameSuffix means anything, which is what lets a suffix of 0 be a real name
  // - Sine_0 - rather than the absence of one.
  uint8_t HasSuffix : 1;
  // Appended to the name as decimal digits when HasSuffix, so a run of signals sharing a
  // prefix can name themselves from one flash string instead of a heap copy each. Never
  // stored as text: the digits are produced where the name is read.
  uint8_t NameSuffix;
#if BLAECK_ENABLE_SIGNAL_META
  // Null until the sketch describes this signal. Owned by the entry; see _ensureSignalMeta.
  SignalMeta *Meta = nullptr;
#endif
};

enum BlaeckTimestampMode
{
  BLAECK_NO_TIMESTAMP = 0,
  BLAECK_MICROS = 1,
  BLAECK_UNIX = 2,
  BLAECK_RTC = BLAECK_UNIX // Deprecated alias
};

// paramCount is 0 only for a plain onCommand or a button. Every typed command declared that it
// takes a value, so a frame without one is rejected before dispatch and never reaches a handler
// - query or toggle semantics belong on a plain command, which declares no contract and is
// passed through untouched.
typedef void (*BlaeckCommandHandler)(const char *command, const char *const *params, byte paramCount);
typedef void (*BlaeckAnyCommandHandler)(const char *command, const char *const *params, byte paramCount);

// Supplies a state channel's current value on demand. Called while the 0x90 catalog frame is
// being built, so it must return promptly and must not block. The returned text is copied into
// the frame immediately, so a function-local static is the natural place to build it. Returning
// nullptr means "no value right now" and leaves the channel's value out of the catalog.
typedef const char *(*BlaeckStateTextGetter)();

// The same idea for a channel carrying a number, one per type so the function returns what the
// channel was declared as and there is no mapping to remember. Read where the pointer form would
// have read the variable, so a value worked out from other state cannot lag the state it is
// worked out from - which a variable holding a calculation can.
typedef bool (*BlaeckStateBoolGetter)();
typedef byte (*BlaeckStateByteGetter)();
typedef short (*BlaeckStateShortGetter)();
typedef unsigned short (*BlaeckStateUShortGetter)();
typedef int (*BlaeckStateIntGetter)();
typedef unsigned int (*BlaeckStateUIntGetter)();
typedef long (*BlaeckStateLongGetter)();
typedef unsigned long (*BlaeckStateULongGetter)();
typedef float (*BlaeckStateFloatGetter)();
typedef double (*BlaeckStateDoubleGetter)();

// Command kind for Home Assistant discovery (0xA0 Command List frame).
enum BlaeckCommandKind
{
  BLAECK_CMD_PLAIN = 0,  // registered via onCommand(): no HA entity, but listed in 0xA0 for command palettes
  BLAECK_CMD_NUMBER = 1, // HA number   (value in [min,max])
  BLAECK_CMD_SWITCH = 2, // HA switch   (0/1)
  BLAECK_CMD_SELECT = 3, // HA select   (index into selectOptions)
  BLAECK_CMD_BUTTON = 4, // HA button   (no value)
  BLAECK_CMD_TEXT = 5    // HA text     (free text, percent-encoded on the wire)
};

// Home Assistant entity category for a command's entity (0xA0 CommandFlags bits 5-6).
// NONE leaves the entity a primary control. CONFIG marks a device setting rather than
// a primary function; DIAGNOSTIC is meant for read-only entities, so on a command it
// only fits a button that triggers an identify/self-test mechanism. Both non-NONE
// values move the entity out of Home Assistant's auto-generated dashboards.
enum BlaeckEntityCategory
{
  BLAECK_CAT_NONE = 0,      // primary control (default)
  BLAECK_CAT_CONFIG = 1,    // HA entity_category "config"
  BLAECK_CAT_DIAGNOSTIC = 2 // HA entity_category "diagnostic"
};

// How a host should render a number command's input (0xA0 CommandFlags bits 9-10). AUTO is
// what a command that never called withMode() carries, and leaves the choice to the host -
// which is why it is zero: the bits stay clear and nothing is claimed. BOX asks for a typed
// field, SLIDER for a dragged one.
//
// A hint, not a constraint. The range is what bounds the value; this only says how it is
// most usefully entered, and a host is free to ignore it.
enum BlaeckNumberMode
{
  BLAECK_NUMBER_MODE_AUTO = 0,   // host decides (default)
  BLAECK_NUMBER_MODE_BOX = 1,    // HA mode "box"
  BLAECK_NUMBER_MODE_SLIDER = 2  // HA mode "slider"
};

// How a host should render a text command's input, carried in the same bits 9-10 as the number
// mode above. The two share the bits because a command has one kind: bits read against a number
// are a render hint, against a text command an input hint, and no entry is ever both. Sharing
// costs nothing per entry in RAM and leaves the reserved bits for what needs its own.
//
// PLAIN is the default and zero, for the same reason AUTO is. PASSWORD asks a host to mask the
// field while it is typed.
//
// Presentation only, and worth being plain about: the value still crosses the wire and the
// broker as the characters it is. It hides a value from someone reading over a shoulder, not
// from anything on the network. ESPHome's password mode means the same and no more.
enum BlaeckTextMode
{
  BLAECK_TEXT_MODE_PLAIN = 0,   // shown as typed (default)
  BLAECK_TEXT_MODE_PASSWORD = 1 // HA mode "password"
};

// The longest text value Home Assistant accepts. Not a limit of this library or of the frame -
// the field on the wire is a uint16 - but of every host we know of: an entity's state is capped
// at 255 characters there, and its MQTT text schema refuses a max above that outright, losing
// the whole control rather than shortening it. withMaxLength() checks against this so the loss
// is a line on the debug stream at startup instead of a control that never appears.
#define BLAECK_TEXT_MAX_LENGTH 255

// What a typed command's state name refers to, carried in the 0xA0 entry as one byte after
// that name. The library sets it from how the command was declared; a sketch does not pass
// it. Kept public because it is part of the frame's vocabulary.
enum BlaeckStateSource
{
  BLAECK_STATE_SIGNAL = 0, // an addSignal() signal
  BLAECK_STATE_CHANNEL = 1 // a state channel the command owns (see withOwnState())
};

// Acknowledgement reason for the 0xA5 Command Ack frame. Sent back to the
// serial host after a command is dispatched so a host (e.g. Loggbok) can confirm
// the command was applied and surface accept/reject feedback.
// status = 0 accepted, 1 rejected.
enum BlaeckCommandAckReason
{
  BLAECK_ACK_OK = 0,           // accepted: delivered to a handler, validation passed
  BLAECK_ACK_UNKNOWN = 1,      // rejected: no handler / could not deliver
  BLAECK_ACK_OUT_OF_RANGE = 2, // rejected: number outside [min, max]
  BLAECK_ACK_BAD_SWITCH = 3,   // rejected: switch value not 0/1
  BLAECK_ACK_BAD_SELECT = 4,   // rejected: select value not a valid index/option
  BLAECK_ACK_TOO_LONG = 5,      // rejected: text value longer than the advertised max length
  BLAECK_ACK_MISSING_VALUE = 6, // rejected: a typed command arrived without its value
  BLAECK_ACK_TRUNCATED = 7      // rejected: frame did not fit - too many parameters, or longer than the receive buffer
};

// Warns when the value of a call is thrown away. Used on the two typed helpers whose handle
// carries a requirement: dropping that handle is how a sketch skips the requirement, and it is
// the one case the type system cannot catch on its own. A warning rather than an error because
// GCC offers no way to make it one, and nothing here is unsafe - the command still runs, it just
// describes itself with a control the host has to guess at.
#if defined(__GNUC__)
#define BLAECK_NODISCARD __attribute__((warn_unused_result))
#else
#define BLAECK_NODISCARD
#endif

class BlaeckSignalRefBase;
class BlaeckNumericSignalRef;
class BlaeckTextSignalRef;
class BlaeckBoolSignalRef;
class BlaeckCommandRefBase;
class BlaeckNumberCommandRef;
class BlaeckNumberCommandNeedsRange;
class BlaeckSwitchCommandRef;
class BlaeckSelectCommandRef;
class BlaeckSelectCommandNeedsOptions;
class BlaeckButtonCommandRef;
class BlaeckTextCommandRef;
class BlaeckStateRefBase;
class BlaeckNumericStateRef;
class BlaeckTextStateRef;
class BlaeckBoolStateRef;
class BlaeckEventChannelRef;
class BlaeckSerial;

// Handle to the just-initialised library, sizing the tables it will allocate.
// Returned by begin(), meant to be chained, not stored.
//
//   Blaeck.begin(&Serial).withSignals(50).withStateChannels(12);
//
// A number given here is a capacity, not a reservation: nothing is allocated
// until the first entry is added to that table, so a table a sketch never uses
// costs nothing at all. That is also why the chain may run after begin() has
// returned - the numbers are read later, when the table is built.
//
// Raising a capacity after its table already exists is refused, not silently
// ignored: the table is sized once, and a sketch that asks late is asking for
// slots that cannot appear.
//
// Every table starts from a default sized for the board - generous on a 32-bit
// core, careful on an Uno - so these are only needed to go past it. The state
// and event calls remain when those features are compiled out: the call still
// builds and the number is simply not stored, so a feature switch never breaks
// a chain.
//
// RAM is what a sketch sizes against, and it decides long before any cap does. Each
// sizer says what one of its entries costs; a Mega's 8 KB is gone at a few hundred of
// anything, where an ESP32 has 320 KB and room for thousands. A table that cannot be
// allocated says so and drops every entry, which is a truer answer than a number.
//
// Declared here rather than after BlaeckSerial, where the bodies live, so that the
// type is complete where begin() names it as a return type. A forward declaration
// is enough for the compiler, but an editor that binds the return type at the
// declaration and never revisits it then offers no members on the chain - which is
// most of this API.
class BlaeckBeginRef
{
public:
  explicit BlaeckBeginRef(BlaeckSerial *owner) : _owner(owner) {}

  /*!
    @brief   Makes room for a number of signals.

    A signal costs 9 bytes on AVR, so this is a RAM decision more than anything else.

    @param   count  Signals to make room for. At most 32767; a
                    larger literal fails the build.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withSignals(50);
    @endcode
  */
  BlaeckBeginRef &withSignals(unsigned int count);

  /*!
    @brief   Makes room for a number of state channels.

    Counts the channels declared with addStateChannel() and the one a command builds
    with withOwnState() - that channel comes out of this table, not withCommands().

    A channel costs 26 bytes on AVR.

    @param   count  State channels to make room for. At most 32767; a
                    larger literal fails the build.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withStateChannels(12);
    @endcode
  */
  BlaeckBeginRef &withStateChannels(unsigned int count);

  /*!
    @brief   Makes room for a number of event channels.

    An event channel costs 10 bytes on AVR.

    @param   count  Event channels to make room for. At most 32767; a
                    larger literal fails the build.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withEventChannels(4);
    @endcode
  */
  BlaeckBeginRef &withEventChannels(unsigned int count);

  /*!
    @brief   Makes room for a number of event types, across all channels.

    Types share one table, so this is the sum across channels rather than the most
    any one channel has: four channels of five types each need 20. Sharing is what
    makes that cheap - a channel with two types costs two slots, where a table per
    channel would give every channel room for the largest. A type costs 5 bytes on AVR,
    the cheapest entry the library keeps.

    @param   count  Event types to make room for, added up across every channel.
                    At most 32767; a
                    larger literal fails the build.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withEventChannels(4).withEventTypes(20);
    @endcode
  */
  BlaeckBeginRef &withEventTypes(unsigned int count);

  /*!
    @brief   Makes room for a number of commands.

    Plain onCommand() registrations and the typed onNumberCommand(),
    onSelectCommand() and friends share this table.

    A command costs 48 bytes on AVR, the largest entry there is - and one that reports
    its own state takes a 26-byte state channel with it, so size withStateChannels()
    to match.

    @param   count  Commands to make room for. At most 32767; a
                    larger literal fails the build.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withCommands(8);
    @endcode
  */
  BlaeckBeginRef &withCommands(unsigned int count);

  /*!
    @brief   Names a stream for the library to report problems on.

    Where it says what it rejected and why. Without one, a full table is silent
    apart from hasRejectedSignals() and its siblings.

    @param   debugStream  Stream to write to. May be the same one the data goes to.
    @return  The same handle, for chaining.

    @code
      Blaeck.begin(&Serial).withSignals(50).withDebugStream(&Serial);
    @endcode
  */
  BlaeckBeginRef &withDebugStream(Stream *debugStream);

private:
  BlaeckSerial *_owner;
};

// The per-entry records behind the tables. They live here rather than inside
// BlaeckSerial because the handle classes name them, and those are declared above
// BlaeckSerial so that the types it returns are complete where it declares them.
//
// In a namespace, not at file scope: these are implementation, and a sketch that
// happens to define its own StateChannelEntry should not collide with one of ours.
// BlaeckSerial typedefs each back into itself, so nothing else had to change.
namespace blaeck_detail
{
// Sizes the command-name array in CommandHandlerEntry. AVR keeps it short because
// every registered command pays for it in SRAM whether its name is long or not.
#if defined(__AVR__)
static const byte MAX_COMMAND_NAME_COUNT = 24;
#else
static const byte MAX_COMMAND_NAME_COUNT = 40;
#endif

// An EventTypeEntry field length meaning "to the end of the string" rather than a count.
static const byte WHOLE_STRING = 0xFF;

// F("") is nothing to say, and every modifier that takes a flash string reads it that way:
// an empty string is stored as no declaration at all rather than announced as a blank. It
// has to be read with pgm_read_byte, because on AVR the pointer is a flash address and
// dereferencing it would test whatever RAM happens to be at that number.
inline bool flashStrEmpty(const __FlashStringHelper *value)
{
  return value != nullptr && pgm_read_byte(reinterpret_cast<PGM_P>(value)) == 0;
}

// Whether a CSV list can serve as one, and the refusal on `debug` when it cannot: a list with
// no entries, or with a blank entry, leaves a host nothing to offer and nothing it may report.
// Written once and called from every withOptions() - a signal's, a channel's and a command's -
// because the reasoning and the sentence are the same wherever the list came from. `name` is
// what the message names, read as flash when nameInFlash says so.
// Defined below BlaeckSerial, which owns the two CSV helpers it asks.
bool optionsAccepted(const __FlashStringHelper *optionsCsv, Stream *debug,
                     const char *name, bool nameInFlash);

// Whether a channel is still free to take a getter, and whether the getter returns what the
// channel carries. A channel reports its value one way, and a getter is asked before a variable
// is read - so one added to a channel declared with a variable would silence it rather than join
// it. A getter of the wrong type would be called through the wrong signature.
bool stateGetterAccepted(const void *stateValue, dataType want, dataType have,
                         const __FlashStringHelper *method, Stream *debug,
                         const char *name, bool nameInFlash);

// What a switch's own-state getter said, reduced to the "1" or "0" a switch speaks everywhere
// else - the two values its handler accepts, and the two payloads a host is told to match
// against. A sketch may return whatever reads best in its own code; the spellings below are
// the ones anyone would reach for, in either case.
//
// Anything else is not guessed at. It returns nullptr, which every caller already treats as
// "nothing to report" - honest about not knowing, where "0" would assert that the switch is
// off. A getter returning nothing keeps that meaning too.
inline const char *switchStateText(const char *value)
{
  if (value == nullptr)
    return nullptr;

  // Lowercased into a buffer sized to the longest spelling below, so anything longer is
  // already not one of them and leaves early. Compared by length and character rather than
  // with strcasecmp, which is not on every core this library builds for, and without a table
  // of literals, which on AVR would sit in RAM for the life of the sketch.
  char w[6];
  byte n = 0;
  while (value[n] != '\0')
  {
    if (n >= sizeof(w) - 1)
      return nullptr;
    char c = value[n];
    w[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    n++;
  }
  w[n] = '\0';

  // Static, so what is returned outlives this call - the caller reads it after the getter's
  // own buffer is out of reach.
  static const char kOn[] = "1";
  static const char kOff[] = "0";

  switch (n)
  {
  case 1:
    if (w[0] == '1') return kOn;
    if (w[0] == '0') return kOff;
    break;
  case 2:
    if (w[0] == 'o' && w[1] == 'n') return kOn;
    if (w[0] == 'n' && w[1] == 'o') return kOff;
    break;
  case 3:
    if (w[0] == 'y' && w[1] == 'e' && w[2] == 's') return kOn;
    if (w[0] == 'o' && w[1] == 'f' && w[2] == 'f') return kOff;
    break;
  case 4:
    if (w[0] == 't' && w[1] == 'r' && w[2] == 'u' && w[3] == 'e') return kOn;
    break;
  case 5:
    if (w[0] == 'f' && w[1] == 'a' && w[2] == 'l' && w[3] == 's' && w[4] == 'e') return kOff;
    break;
  }
  return nullptr;
}

struct CommandHandlerEntry
{
  char command[MAX_COMMAND_NAME_COUNT];
  BlaeckCommandHandler handler = nullptr;
  bool inUse = false;
#if BLAECK_ENABLE_COMMAND_META
  uint8_t kind = BLAECK_CMD_PLAIN;
  float meta_min = 0.0f;
  float meta_max = 0.0f;
  float meta_step = 0.0f;
  const __FlashStringHelper *unit = nullptr;
  const __FlashStringHelper *deviceClass = nullptr;
  const __FlashStringHelper *icon = nullptr;
  const __FlashStringHelper *displayName = nullptr;
  const __FlashStringHelper *options = nullptr;
  const __FlashStringHelper *stateSignal = nullptr;
  // Buttons only: the fixed argument list a press carries, or nullptr for the bare press
  // that is the default.
  const __FlashStringHelper *pressPayload = nullptr;
  uint8_t stateSource = BLAECK_STATE_SIGNAL;
  uint8_t category = BLAECK_CAT_NONE;
  bool disabledByDefault = false;
  // Bits 9-10 of the 0xA0 flags, read against the kind: a render hint on a number, an input
  // hint on a text command. One byte serves both because an entry has one kind.
  uint8_t mode = 0;
#endif
};

struct StateChannelEntry
{
  // Either a copy this library owns, or a pointer into flash when the channel was named
  // with F(). nameInFlash says which, and nothing reads this field directly - the helpers
  // do, because reading a flash address as if it were RAM is silent garbage on AVR rather
  // than a crash. A pointer where a fixed array used to be: 14 bytes an entry on AVR, and
  // an F() name now costs the entry nothing at all.
  const char *name = nullptr;
  bool nameInFlash = false;
  const __FlashStringHelper *icon = nullptr;
  // Asked for the channel's value while the 0x90 catalog is built, so what the
  // catalog reports cannot lag behind the sketch - there is no stored copy to
  // go stale, and nothing the sketch has to remember to refresh.
  // A channel is asked for its value one way, and valueType says which member is live: text
  // channels use the first, every other type the second. Both are function pointers of the same
  // width, so carrying both costs an entry nothing.
  union
  {
    BlaeckStateTextGetter getStateText = nullptr;
    void (*getNumber)();
  };
  const __FlashStringHelper *deviceClass = nullptr;
  const __FlashStringHelper *options = nullptr;
  const __FlashStringHelper *unit = nullptr;
  // The variable this channel reports, read where the getter above would have been called.
  // A channel has one or the other, never both: a numeric channel points at a variable, a
  // text channel may instead compute its value in a getter. valueType says which shape the
  // bytes take on the wire and is sent whether or not there is a value to send yet.
  const void *stateValue = nullptr;
  dataType valueType = Blaeck_string;
  // Bits the plain members above cannot hold: state class needs three values-wide room and
  // display precision needs to distinguish 0 from unset. The rest of the 0x90 word is built
  // from the members at write time.
  uint16_t metaFlags = 0;
  uint8_t displayPrecision = 0;
  // Declared by a typed command through withOwnState(), which makes the channel that
  // command's alone: addStateChannel() and writeState() both refuse the name, so the
  // value on its topic can only ever come from the getter or variable above.
  bool ownedByCommand = false;
  bool diagnostic = false;
  bool disabledByDefault = false;
  bool forceUpdate = false;
  // Said once per channel, not once per push: a value that is too long is usually too
  // long every time, and a warning on every push would bury the log it is trying to help.
  // stateValue points at a byte holding an option index rather than at text, and the
  // channel reports the option that index names. Set only by a select command's
  // withOwnState(), which is the one case where the two cannot be told apart: both are
  // a string channel with a pointer.
  bool stateIsSelectIndex = false;
  // The counterpart: a select channel carrying the option name itself, from a getter or from
  // a buffer the sketch keeps. Checked against the declared list rather than resolved from it.
  bool stateIsSelectName = false;
  // The getter above belongs to a switch command, so what it returns is normalised to "1" or
  // "0" before it leaves. Set only where the two cannot be told apart: a text channel with a
  // getter, which is every own-state form but the typed bool one.
  bool stateIsSwitchBool = false;
  // Latches the warning below, so a value the library cannot report is complained about once
  // rather than on every push. A channel belongs to one command, so a switch's unreadable text
  // and a select's overlong option name can share it. Mutable because the channel is read
  // through a const reference where the value is fetched, and this records only that the
  // complaint has been made.
  mutable bool stateWarned = false;
  bool truncationWarned = false;
  bool inUse = false;
};

struct EventChannelEntry
{
  // Either a copy this library owns, or a pointer into flash when the channel was named
  // with F(). nameInFlash says which, and nothing reads this field directly - the helpers
  // do, because reading a flash address as if it were RAM is silent garbage on AVR rather
  // than a crash. A pointer where a fixed array used to be: 14 bytes an entry on AVR, and
  // an F() name now costs the entry nothing at all.
  const char *name = nullptr;
  bool nameInFlash = false;
  const __FlashStringHelper *icon = nullptr;
  const __FlashStringHelper *deviceClass = nullptr;
  bool diagnostic = false;
  bool disabledByDefault = false;
  bool inUse = false;
};

struct EventTypeEntry
{
  uint16_t channelIndex = 0;
  // Either a whole flash string, or one comma-separated field of one: addEventType()
  // stores its literal with field = WHOLE_STRING, while the CSV form of
  // addEventChannel() appends one entry per field, all sharing the same pointer. The
  // pool is walked identically either way, so nothing downstream has to know which.
  const __FlashStringHelper *text = nullptr;
  byte field = WHOLE_STRING;
};

} // namespace blaeck_detail

// Handle to the command a typed helper just registered, describing the control it drives.
// Returned by value and meant to be chained, not stored.
//
// The modifiers live here so that every kind shares one implementation, and each kind's handle
// re-exposes only the ones that apply to it - which is what makes a range on a text command a
// compile error rather than a silent no-op. Nothing here validates: a command whose registration
// was rejected gets a dead handle that swallows the chain, and hasRejectedCommands() is where
// that shows up.
//
// The methods are always defined. With BLAECK_ENABLE_COMMAND_META=0 they store nothing and the
// typed helpers behave exactly like onCommand(), so a sketch needs no #ifdef.
class BlaeckCommandRefBase
{
protected:
  BlaeckCommandRefBase(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // The entry this handle names, or nullptr when registration was rejected.
  blaeck_detail::CommandHandlerEntry * _entry() const;

  // Out of line for the same reason as _entry(); see BlaeckStateRefBase::_markDirty().
  void _markDirty() const;

  void _setStateSignal(const __FlashStringHelper *signalName)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      // Compared before it is written, here and in every modifier below: a modifier is
      // idempotent, so a sketch may call one every pass through loop(), and marking on
      // assignment would announce the whole catalog every pass.
      if (e->stateSignal != signalName || e->stateSource != BLAECK_STATE_SIGNAL)
      {
        e->stateSignal = signalName;
        e->stateSource = BLAECK_STATE_SIGNAL;
        _markDirty();
      }
    }
#else
    (void)signalName;
#endif
  }

  // Typed own state: the command reports a variable rather than text a getter builds. Same
  // path as the getter form, so the channel is declared and claimed identically - only where
  // the value comes from differs.
  void _setOwnState(const __FlashStringHelper *channelName, dataType valueType, const void *value,
                    bool selectIndex = false);

  void _setOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText);

  // Reported rather than stored silently: a range whose max is not above its min reads
  // everywhere downstream as no range at all - nothing is checked and nothing is
  // announced - so reversed arguments turn the checking off instead of tightening it.
  void _warnRangeIgnored(float mn, float mx) const;

  // Same reasoning for a step that is not a positive number. A step is declared by being
  // above zero, so a negative one - or a NaN, which fails every comparison - reads as no
  // step at all and is dropped rather than announced.
  void _warnStepIgnored(float st) const;

  // A step this library keeps and sends, but that a host may not accept. Reported because
  // what is lost is the whole control rather than the step, and nothing else says so.
  void _warnStepTooFine(float st) const;

  // A maximum length no host will build a control from. Refused rather than clamped: a limit
  // quietly made smaller than the buffer it was taken from is how a sketch comes to believe it
  // has room it does not, and the number the caller wrote is worth naming back to them.
  void _warnMaxLengthTooLong(unsigned int maxLength) const;

  // Whether a list has anything in it. A list with no entries is not a list: a host has
  // nothing to offer and _validateTypedCommand() has nothing to accept, so the command is
  // dead at both ends - the same reasoning addEventChannel() applies to a channel declared
  // with no event types, and refused here in the same way rather than stored.
  bool _optionsAccepted(const __FlashStringHelper *optionsCsv) const;

  void _setRange(float mn, float mx, float st)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      if (e->meta_min != mn || e->meta_max != mx || e->meta_step != st)
      {
        e->meta_min = mn;
        e->meta_max = mx;
        e->meta_step = st;
        _markDirty();
      }
      if (!(mx > mn))
        _warnRangeIgnored(mn, mx);
      // 0 is how a caller says "no resolution" on purpose - the parameter is mandatory, so it
      // was passed deliberately - and only a value meant as a step that cannot serve as one is
      // worth reporting.
      if (st != 0.0f && !(st > 0.0f))
        _warnStepIgnored(st);
      // Sent as declared either way - the wire has no such limit and neither does this
      // library - but a host may. Home Assistant refuses a step below 0.001 at the point it
      // reads the announcement, and refuses the whole control with it rather than the step
      // alone, so a sketch asking for finer resolution loses the control and is told nothing.
      else if (st > 0.0f && st < 0.001f)
        _warnStepTooFine(st);
    }
#else
    (void)mn; (void)mx; (void)st;
#endif
  }

  void _setUnit(const __FlashStringHelper *unit)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (blaeck_detail::flashStrEmpty(unit))
      unit = nullptr;
    if (auto *e = _entry())
    {
      if (e->unit != unit)
      {
        e->unit = unit;
        _markDirty();
      }
    }
#else
    (void)unit;
#endif
  }

  // Empty reads as "not declared", as it does for every flash-backed modifier. It matters more
  // here than most: a host validates the device class against a fixed list, and a blank one
  // fails that check and takes the whole entity with it.
  void _setDeviceClass(const __FlashStringHelper *deviceClass)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (blaeck_detail::flashStrEmpty(deviceClass))
      deviceClass = nullptr;
    if (auto *e = _entry())
    {
      if (e->deviceClass != deviceClass)
      {
        e->deviceClass = deviceClass;
        _markDirty();
      }
    }
#else
    (void)deviceClass;
#endif
  }

  void _setIcon(const __FlashStringHelper *icon)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (blaeck_detail::flashStrEmpty(icon))
      icon = nullptr;
    if (auto *e = _entry())
    {
      if (e->icon != icon)
      {
        e->icon = icon;
        _markDirty();
      }
    }
#else
    (void)icon;
#endif
  }

  void _setPressPayload(const __FlashStringHelper *pressPayload)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (blaeck_detail::flashStrEmpty(pressPayload))
      pressPayload = nullptr;
    if (auto *e = _entry())
    {
      if (e->pressPayload != pressPayload)
      {
        e->pressPayload = pressPayload;
        _markDirty();
      }
    }
#else
    (void)pressPayload;
#endif
  }

  void _setDisplayName(const __FlashStringHelper *displayName)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (blaeck_detail::flashStrEmpty(displayName))
      displayName = nullptr;
    if (auto *e = _entry())
    {
      if (e->displayName != displayName)
      {
        e->displayName = displayName;
        _markDirty();
      }
    }
#else
    (void)displayName;
#endif
  }

  void _setOptions(const __FlashStringHelper *optionsCsv)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      // Refused rather than stored, so an empty list leaves the entry as it was: never
      // declared, or still holding the list an earlier call gave it.
      if (!_optionsAccepted(optionsCsv))
        return;
      if (e->options != optionsCsv)
      {
        e->options = optionsCsv;
        _markDirty();
      }
    }
#else
    (void)optionsCsv;
#endif
  }

  // Stored in meta_max, which a text command uses for nothing else.
  void _setMaxLength(unsigned int maxLength)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      // Refused, so the entry keeps the 255 it was registered with: the one length every host
      // accepts, and shorter than what was asked for rather than longer, so nothing the sketch
      // then copies is sized on a promise this library did not keep.
      if (maxLength > BLAECK_TEXT_MAX_LENGTH)
      {
        _warnMaxLengthTooLong(maxLength);
        return;
      }
      if (e->meta_max != (float)maxLength)
      {
        e->meta_max = (float)maxLength;
        _markDirty();
      }
    }
#else
    (void)maxLength;
#endif
  }

  void _setCategory(uint8_t category)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      if (e->category != category)
      {
        e->category = category;
        _markDirty();
      }
    }
#else
    (void)category;
#endif
  }

  void _setDisabledByDefault(bool on)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      if (e->disabledByDefault != on)
      {
        e->disabledByDefault = on;
        _markDirty();
      }
    }
#else
    (void)on;
#endif
  }

  // Set by withMode(), which only a number and a text command offer - so nothing has to check
  // the kind here, and the value is read back against it. Zero is the default of both, which is
  // what makes an undeclared mode cost nothing: the bits stay clear and the host applies its own
  // default rather than being told one.
  void _setMode(uint8_t mode)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      if (e->mode != mode)
      {
        e->mode = mode;
        _markDirty();
      }
    }
#else
    (void)mode;
#endif
  }

  BlaeckSerial *_owner;
  int16_t _index;
};

// Each kind re-exposes the modifiers that apply to it, returning its own type so the chain keeps
// its kind all the way down. The repetition is the point: this reads as a table of what each
// control accepts, and it is what an editor offers when the dot is typed.

// The four every command handle shares, including a button's. A template rather than a macro so
// each method is a real declaration with a real comment: a comment inside a macro body documents
// nothing, because the compiler and the editor both see only the expansion. TYPE is the handle
// deriving from this, so each returns its own type and the chain keeps working.
template <class TYPE>
class BlaeckCommandRefShared : public BlaeckCommandRefBase
{
public:
  /*!
    @brief   Declares the label a host shows in place of the name.

    A command name is an identifier: the host sends it back to invoke the command, so
    it is written to be matched rather than read - and a control ends up labelled
    SET_FREQ. A display name splits the two: the wire keeps saying SET_FREQ, the
    screen says "Frequency".

    Presentation only. The name still identifies the command everywhere, so adding
    this to a command already deployed relabels it and breaks nothing that sends it.

    @param   displayName  What to show, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
          .withRange(0.0f, 2.0f, 0.01f)
          .withDisplayName(F("Frequency"));
    @endcode
  */
  TYPE &withDisplayName(const __FlashStringHelper *displayName)
  {
    _setDisplayName(displayName);
    return _self();
  }

  /*!
    @brief   Declares the icon a host shows beside the control.

    Every kind takes one, and it is the one modifier that is purely a picture: it
    changes nothing about what the control accepts or reports. Where a device class
    is available it is the better choice, since a host derives an icon from it and
    gets the wording and units that go with it too; reach for an icon when no class
    fits, or when the sketch wants a particular one anyway.

    @param   icon  Material Design Icons name, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.onButtonCommand("CALIBRATE", onCalibrate).withIcon(F("mdi:tune"));
    @endcode
  */
  TYPE &withIcon(const __FlashStringHelper *icon)
  {
    _setIcon(icon);
    return _self();
  }

  /*!
    @brief   Files the command as a setting rather than a primary function.

    A host keeps configuration controls off the dashboard it generates, so the ones
    that matter day to day are not buried among the ones set once.

    @return  The same handle, for chaining.

    @code
      Blaeck.onTextCommand("SET_LABEL", onSetLabel).withMaxLength(32).config();
    @endcode
  */
  TYPE &config()
  {
    _setCategory((uint8_t)BLAECK_CAT_CONFIG);
    return _self();
  }

  /*!
    @brief   Files the command as describing the board rather than what it does.

    Kept off a generated dashboard for the same reason as config(): a reboot button
    is not what someone opens the dashboard to see.

    @return  The same handle, for chaining.

    @code
      Blaeck.onButtonCommand("REBOOT", onReboot).diagnostic();
    @endcode
  */
  TYPE &diagnostic()
  {
    _setCategory((uint8_t)BLAECK_CAT_DIAGNOSTIC);
    return _self();
  }

  /*!
    @brief   Registers the control but leaves it switched off until someone enables it.

    Home Assistant's enabled_by_default: the entity is created but hidden until someone
    turns it on. The device still accepts the command either way - this is what a host
    shows, not what the firmware answers.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.onButtonCommand("CALIBRATE", onCalibrate).disabledByDefault();
    @endcode
  */
  TYPE &disabledByDefault(bool on = true)
  {
    _setDisabledByDefault(on);
    return _self();
  }

protected:
  BlaeckCommandRefShared(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}

  TYPE &_self() { return *static_cast<TYPE *>(this); }
};

// The two state modifiers, on a layer of their own because a button has no state to report.
// Home Assistant's MQTT button subscribes to nothing - there is no state_topic in its schema,
// and what it shows as the entity's state is a timestamp of the last press that Home Assistant
// writes itself. So a state a button declared could never arrive anywhere, and saying so here
// costs a compile error rather than a channel that quietly goes nowhere. Every other kind
// derives from this; the button derives from the base above.
template <class TYPE>
class BlaeckCommandRefStateful : public BlaeckCommandRefShared<TYPE>
{
public:
  /*!
    @brief   Points the command at a signal that mirrors its value.

    A host then shows what the device holds rather than what was last sent, so a
    value the firmware clamped or refused is visible. Leave it out for a control that
    is assumed to have taken effect.

    The signal is logged, which is what decides between this and withOwnState():
    use this for a setpoint that belongs in the data next to what it controls, and
    withOwnState() for one that does not. This names a signal that already exists,
    where withOwnState() creates the channel it reports on.

    @param   signalName  A signal already added with addSignal().
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("LED_State"), &ledState);
      Blaeck.onSwitchCommand("LED", onLED).withStateFromSignal(F("LED_State"));
    @endcode
  */
  TYPE &withStateFromSignal(const __FlashStringHelper *signalName)
  {
    this->_setStateSignal(signalName);
    return this->_self();
  }

  /*!
    @brief   Gives the command a state channel of its own, filled by a getter.

    The counterpart to withStateFromSignal(), for a value that should not be logged:
    this creates the channel rather than naming a signal that exists. The channel
    belongs to the command, so addStateChannel() and writeState() both refuse the
    name and the value can only come from one place. It needs no signals at all, so
    a device that logs nothing can still report what its controls are set to.

    Push a change with writeCommandState(); otherwise the channel is read only when
    a host asks.

    @param   channelName   Name for the channel. Takes a slot from the state channel
                           table, so count it in withStateChannels().
    @param   getStateText  Called to produce the current value as text.
    @return  The same handle, for chaining.

    @warning The getter runs while a frame is being assembled and must not send one,
             for the reason given on withStateText().

    @code
      Blaeck.onNumberCommand("SET_OFFSET", onSetOffset)
          .withRange(-100.0f, 100.0f, 0.1f)
          .withOwnState(F("Offset"), offsetText);
    @endcode
  */
  TYPE &withOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText)
  {
    this->_setOwnState(channelName, getStateText);
    return this->_self();
  }

protected:
  BlaeckCommandRefStateful(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<TYPE>(owner, index) {}
};

class BlaeckNumberCommandRef : public BlaeckCommandRefStateful<BlaeckNumberCommandRef>
{
public:
  BlaeckNumberCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefStateful<BlaeckNumberCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefStateful<BlaeckNumberCommandRef>::withOwnState;

  /*!
    @brief   Declares the unit shown beside the input.

    A label only: nothing is converted, and the handler is passed whatever number
    was sent.

    @param   unit  Symbol as an F() literal. Non-ASCII must be UTF-8:
                   F("\xC2\xB0" "C") is the degree sign followed by C.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq).withRange(0.0f, 2.0f, 0.01f).withUnit(F("Hz"));
    @endcode
  */
  BlaeckNumberCommandRef &withUnit(const __FlashStringHelper *unit)
  {
    _setUnit(unit);
    return *this;
  }

  /*!
    @brief   Asks a host to render the input as a typed box or a dragged slider.

    A hint about entry, not about validity: the range is what bounds the value, and
    a host is free to ignore this. Leave it out and the host decides, which is the
    right answer for most controls - say it only where one form is clearly wrong,
    such as a setpoint read to two decimals that no one can hit by dragging.

    @param   mode  BLAECK_NUMBER_MODE_BOX for a typed field,
                   BLAECK_NUMBER_MODE_SLIDER for a dragged one.
                   BLAECK_NUMBER_MODE_AUTO is the default and declares nothing.
    @return  The same handle, for chaining.

    @note    A slider offers min + n*step, computed in floating point, so a host can
             hand back 21.200000000000003 for a step of 0.1. Nothing is rounded on
             the way in - a step is display resolution and never validated - so a
             sketch that needs the round number snaps to it itself.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
          .withRange(0.0f, 2.0f, 0.01f)
          .withMode(BLAECK_NUMBER_MODE_BOX);
    @endcode
  */
  BlaeckNumberCommandRef &withMode(BlaeckNumberMode mode)
  {
    _setMode((uint8_t)mode);
    return *this;
  }

  /*!
    @brief   Names what kind of quantity this control sets.

    A host uses it to pick an icon and, for the classes it knows how to convert, to
    show the value in the reader's own units. The conversion runs both ways and the
    device is never part of it: a control declaring "temperature" in Celsius, read by
    someone whose system is set to Fahrenheit, is typed in Fahrenheit and arrives here
    already converted back to Celsius. So the range stays expressed in the unit the
    sketch declared, and validation keeps meaning what it says.

    @param   deviceClass  A Home Assistant number device class as an F() literal:
                          "temperature", "pressure", "power", "frequency", "voltage",
                          "humidity" and some fifty more. Lower case.
    @return  The same handle, for chaining.

    @note    The list is not the one a binary sensor draws from, and not quite the one
             a signal draws from either: a number takes no "enum", "timestamp" or
             "date". A class a host does not know fails its check and drops this one
             control, so a name worth guessing at is better left out.

    @warning Declare the matching unit with withUnit(). A converting class with no
             unit leaves a host converting from nothing, and the value it shows is
             then only as right as its assumption.

    @code
      Blaeck.onNumberCommand("SET_TEMP", onSetTemp)
          .withRange(5.0f, 30.0f, 0.5f)
          .withUnit(F("\xC2\xB0" "C"))
          .withDeviceClass(F("temperature"));
    @endcode
  */
  BlaeckNumberCommandRef &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    _setDeviceClass(deviceClass);
    return *this;
  }

  /*!
    @brief   Carries this command's state as a number read straight from a variable.

    Saves the sketch formatting anything: the value is sent typed and the host
    renders it. One overload per numeric type, as addSignal() has, and the getter
    form is still there for a value that has to be composed.

    @param   channelName  Name for the channel. Takes a slot from the state channel
                          table, so count it in withStateChannels().
    @param   value        Address of the variable to read. Must be a global.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_AMP", onSetAmp)
          .withRange(0.0f, 100.0f, 0.1f)
          .withOwnState(F("Amplitude"), &Amplitude);
    @endcode
  */
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, byte *value)
  {
    _setOwnState(channelName, Blaeck_byte, value);
    return *this;
  }

  // Carries this command's state as a short (signed 16-bit), read directly instead of asking a
  // getter for text. Sent typed, so the host renders the number and the sketch never formats
  // one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, short *value)
  {
    _setOwnState(channelName, Blaeck_short, value);
    return *this;
  }

  // Carries this command's state as an unsigned short (16-bit), read directly instead of asking
  // a getter for text. Sent typed, so the host renders the number and the sketch never formats
  // one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned short *value)
  {
    _setOwnState(channelName, Blaeck_ushort, value);
    return *this;
  }

  // Carries this command's state as an int, 16-bit on AVR and 32-bit elsewhere, read directly
  // instead of asking a getter for text. Sent typed, so the host renders the number and the
  // sketch never formats one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, int *value)
  {
#ifdef __AVR__
    _setOwnState(channelName, Blaeck_int, value);
#else
    _setOwnState(channelName, Blaeck_long, value);
#endif
    return *this;
  }

  // Carries this command's state as an unsigned int, 16-bit on AVR and 32-bit elsewhere, read
  // directly instead of asking a getter for text. Sent typed, so the host renders the number and
  // the sketch never formats one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned int *value)
  {
#ifdef __AVR__
    _setOwnState(channelName, Blaeck_uint, value);
#else
    _setOwnState(channelName, Blaeck_ulong, value);
#endif
    return *this;
  }

  // Carries this command's state as a long (signed 32-bit), read directly instead of asking a
  // getter for text. Sent typed, so the host renders the number and the sketch never formats
  // one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, long *value)
  {
    _setOwnState(channelName, Blaeck_long, value);
    return *this;
  }

  // Carries this command's state as an unsigned long (32-bit), read directly instead of asking a
  // getter for text. Sent typed, so the host renders the number and the sketch never formats
  // one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned long *value)
  {
    _setOwnState(channelName, Blaeck_ulong, value);
    return *this;
  }

  // Carries this command's state as a float (32-bit), read directly instead of asking a getter
  // for text. Sent typed, so the host renders the number and the sketch never formats one. One
  // overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, float *value)
  {
    _setOwnState(channelName, Blaeck_float, value);
    return *this;
  }

  // Carries this command's state as a double, read directly instead of asking a getter for text.
  // Sent typed, so the host renders the number and the sketch never formats one. One overload per
  // numeric type, as addSignal() has.
  //
  // Declared as a float on AVR, where a double is the same four bytes: sent as a double it would
  // be read out of the union eight bytes at a time, four of them stale.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, double *value)
  {
#ifdef __AVR__
    _setOwnState(channelName, Blaeck_float, value);
#else
    _setOwnState(channelName, Blaeck_double, value);
#endif
    return *this;
  }
};

class BlaeckSwitchCommandRef : public BlaeckCommandRefStateful<BlaeckSwitchCommandRef>
{
public:
  BlaeckSwitchCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefStateful<BlaeckSwitchCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  //
  // The inherited getter form works here as it does elsewhere, with one difference: what it
  // returns is read as on or off rather than passed through. "1", "on", "true" and "yes" are
  // on, "0", "off", "false" and "no" are off, in either case, and the channel carries "1" or
  // "0" whichever was written. Anything else reports nothing at all rather than guessing at
  // off. Use the bool overload below where there is a bool to point at; the getter is for a
  // switch whose position is worked out rather than stored.
  using BlaeckCommandRefStateful<BlaeckSwitchCommandRef>::withOwnState;

  /*!
    @brief   Says whether this switch drives a socket or something else.

    Only changes the icon and the wording a host uses for on and off. Nothing about
    the command changes, and leaving it out is right for most switches.

    @param   deviceClass  "outlet" for a mains socket, "switch" for anything else.
                          Those two are the whole list a switch may draw from - it is
                          not the one a number or a binary sensor uses.
    @return  The same handle, for chaining.

    @code
      Blaeck.onSwitchCommand("SET_RELAY", onSetRelay).withDeviceClass(F("outlet"));
    @endcode
  */
  BlaeckSwitchCommandRef &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    _setDeviceClass(deviceClass);
    return *this;
  }

  /*!
    @brief   Carries this switch's state as the bool it already is.

    The host renders it using the on and off payloads it declared, so the sketch
    never has to know which spelling those use.

    @param   channelName  Name for the channel. Takes a slot from the state channel
                          table, so count it in withStateChannels().
    @param   value        Address of the bool to read. Must be a global.
    @return  The same handle, for chaining.

    @code
      Blaeck.onSwitchCommand("SET_ENABLE", onSetEnable)
          .withOwnState(F("Enabled"), &Enabled);
    @endcode
  */
  BlaeckSwitchCommandRef &withOwnState(const __FlashStringHelper *channelName, bool *value)
  {
    _setOwnState(channelName, Blaeck_bool, value);
    return *this;
  }
};

class BlaeckSelectCommandRef : public BlaeckCommandRefStateful<BlaeckSelectCommandRef>
{
public:
  BlaeckSelectCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefStateful<BlaeckSelectCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefStateful<BlaeckSelectCommandRef>::withOwnState;

  /*!
    @brief   Carries this select's state as the option index the sketch switches on.

    The library resolves the index against the declared list and reports the option
    NAME, which is what a host expects of a select - so nothing has to hold that text
    or refresh it when the selection changes. A handler is already handed the index,
    so this is usually the variable it just assigned.

    Pass a buffer to the const char* form instead only if the sketch keeps the name
    for its own reasons.

    @param   channelName  Name for the channel. Takes a slot from the state channel
                          table, so count it in withStateChannels().
    @param   index        Address of the index variable. Must be a global.
    @return  The same handle, for chaining.

    @code
      Blaeck.onSelectCommand("SET_WAVE", onSetWave)
          .withOptions(F("Sine,Square,Triangle,Sawtooth"))
          .withOwnState(F("Wave"), &waveIndex);
    @endcode
  */
  BlaeckSelectCommandRef &withOwnState(const __FlashStringHelper *channelName, byte *index)
  {
    _setOwnState(channelName, Blaeck_string, index, true);
    return *this;
  }

  // The buffer the name already lives in. No getter and no formatting: the library reads the
  // characters where they sit, so a name the sketch keeps anyway needs no second copy. Keeping
  // it in step is then the sketch's job - the index form above has nothing to keep in step.
  BlaeckSelectCommandRef &withOwnState(const __FlashStringHelper *channelName, const char *value)
  {
    _setOwnState(channelName, Blaeck_string, value);
    return *this;
  }

};

class BlaeckButtonCommandRef : public BlaeckCommandRefShared<BlaeckButtonCommandRef>
{
public:
  BlaeckButtonCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<BlaeckButtonCommandRef>(owner, index) {}

  /*!
    @brief   Says what pressing this button does.

    Only changes the icon and how a host words the confirmation, but it is worth
    saying on the two presses that are worth thinking twice about.

    @param   deviceClass  "restart", "identify" or "update". Those three are the whole
                          list a button may draw from, and it is not the one a number
                          or a switch uses.
    @return  The same handle, for chaining.

    @note    A button is a trigger with no value, so diagnostic() usually belongs
             alongside this: a reboot is not what someone opens a dashboard to see.

    @code
      Blaeck.onButtonCommand("REBOOT", onReboot).withDeviceClass(F("restart")).diagnostic();
    @endcode
  */
  BlaeckButtonCommandRef &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    _setDeviceClass(deviceClass);
    return *this;
  }

  /*!
    @brief   Gives the press a fixed argument list.

    A press normally carries nothing and the handler runs with no parameters. This puts
    a payload behind it, so one button can stand for a call with its arguments already
    filled in - the label says "Activate all DUTs", the press sends "1,40", and the
    handler reads params[0] and params[1] as it would from any other sender.

    Arguments are comma separated, exactly as a command sent over the wire is written.

    @param   pressPayload  What a press sends, as an F() literal.
    @return  The same handle, for chaining.

    @warning Nothing checks this. A button's parameters are the one kind the library
             passes through unvalidated - there is no declared signature to check them
             against - so a payload with a typo, the wrong separator or too few
             arguments reaches the handler as written and is only found by what the
             handler then does with it. The constants in a small wrapper function are
             checked by the compiler; these are not.

    @note    A button is still one entity per command name, so a second preset over the
             same code is a second command sharing the same handler rather than a second
             payload on this one.

    @code
      Blaeck.onButtonCommand("DUT_ACTIVATE_ALL", onDutActivate)
          .withPressPayload(F("1,40"))
          .withDisplayName(F("Activate all DUTs"));
    @endcode
  */
  BlaeckButtonCommandRef &withPressPayload(const __FlashStringHelper *pressPayload)
  {
    _setPressPayload(pressPayload);
    return *this;
  }
};

class BlaeckTextCommandRef : public BlaeckCommandRefStateful<BlaeckTextCommandRef>
{
public:
  BlaeckTextCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefStateful<BlaeckTextCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefStateful<BlaeckTextCommandRef>::withOwnState;

  /*!
    @brief   Carries this control's state as the buffer the text already lives in.

    No getter and no formatting: the library reads the characters where they sit, so
    a value the sketch already keeps needs no second copy.

    @param   channelName  Name for the channel. Takes a slot from the state channel
                          table, so count it in withStateChannels().
    @param   value        The buffer. Must be a global, and must outlive the device.
    @return  The same handle, for chaining.

    @code
      Blaeck.onTextCommand("SET_LABEL", onSetLabel)
          .withMaxLength(sizeof(DeviceLabel) - 1)
          .withOwnState(F("DeviceLabel"), DeviceLabel);
    @endcode
  */
  BlaeckTextCommandRef &withOwnState(const __FlashStringHelper *channelName, const char *value)
  {
    _setOwnState(channelName, Blaeck_string, value);
    return *this;
  }

  /*!
    @brief   Declares the longest value this control accepts.

    Enforced before the handler runs - a longer value is rejected with
    BLAECK_ACK_TOO_LONG - so the handler can copy what it is given.

    @param   maxLength  Limit in decoded bytes, 255 at most. Left unsaid it is 255.
                        sizeof(buffer) - 1 is usually the right value.
    @return  The same handle, for chaining.

    @note    A value above 255 is ignored and the 255 kept, with a line on the debug
             stream naming both. Home Assistant caps an entity's state at 255 characters
             and refuses a text control declaring more, so passing a larger number
             through would cost the whole control and say nothing about why.

    @code
      Blaeck.onTextCommand("SET_LABEL", onSetLabel)
          .withMaxLength(sizeof(DeviceLabel) - 1);
    @endcode
  */
  BlaeckTextCommandRef &withMaxLength(unsigned int maxLength)
  {
    _setMaxLength(maxLength);
    return *this;
  }

  /*!
    @brief   Asks a host to mask this field while it is typed.

    @param   mode  BLAECK_TEXT_MODE_PASSWORD for a masked field.
                   BLAECK_TEXT_MODE_PLAIN is the default and declares nothing.
    @return  The same handle, for chaining.

    @warning Presentation, not protection. The value still travels the wire and any
             broker as the characters it is, and is as readable there as any other
             command. It hides a key from someone looking at the screen; it hides
             nothing from anything on the network.

    @code
      Blaeck.onTextCommand("SET_API_KEY", onSetApiKey)
          .withMaxLength(sizeof(ApiKey) - 1)
          .withMode(BLAECK_TEXT_MODE_PASSWORD);
    @endcode
  */
  BlaeckTextCommandRef &withMode(BlaeckTextMode mode)
  {
    _setMode((uint8_t)mode);
    return *this;
  }
};

// Number and select are the two kinds carrying metadata the control cannot be built without: a
// number is bounded by definition, and a select is its list of options. Home Assistant makes both
// of those explicit - a number entity always has a min, max and step, filling in 0..100 by 1 when
// told nothing, and its MQTT select schema marks options Required - so a device that stays silent
// does not get an unconstrained control, it gets someone else's guess. Silence is worse still on a
// select: with no list, every value fails validation here and the entity a host builds has nothing
// to choose from, so the command is dead at both ends.
//
// Neither can be enforced by the kind's own handle, because a chain that simply stops is valid
// C++. So the typed helper hands back one of these instead: a handle with exactly one method,
// which returns the full one. The requirement is then part of the type - .withUnit() before
// .withRange() names a member that does not exist yet - while the call still reads as the same
// chain, and nothing but the first link had to change. Registering without chaining at all is the
// one case left; BLAECK_NODISCARD makes the compiler mention it.
class BlaeckNumberCommandNeedsRange : public BlaeckCommandRefBase
{
public:
  BlaeckNumberCommandNeedsRange(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}

  /*!
    @brief   Declares the range the firmware accepts, and unlocks the rest of the chain.

    A value outside it is rejected before the handler runs, so the handler can take
    what it is given. Every other modifier lives on the handle this returns, so a
    number command cannot quietly go without one.

    @param   min   Lowest value accepted. A command that really does take anything
                   says so with bounds wide enough to show it.
    @param   max   Highest value accepted. Has to be above min: a max that is not
                   leaves the command with no range at all, accepting anything and
                   declaring nothing, which a debug stream reports.
    @param   step  Display resolution only - never rounded to, and not validated, so
                   a value between two steps is still accepted. Has no default,
                   because the host's is 1: leaving it out would quietly turn a range
                   in tenths into an integer one. Pass 0 to say nothing deliberately
                   and let the host choose.
    @return  The command's full handle, for chaining.

    @note    A host may have a resolution floor of its own - Home Assistant refuses a
             step below 0.001 - and drops that one control rather than the whole
             device. Ask for a finer step than the host allows and the control does
             not appear.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq).withRange(0.0f, 2.0f, 0.01f);
    @endcode
  */
  BlaeckNumberCommandRef withRange(float min, float max, float step)
  {
    _setRange(min, max, step);
    return BlaeckNumberCommandRef(_owner, _index);
  }
};

class BlaeckSelectCommandNeedsOptions : public BlaeckCommandRefBase
{
public:
  BlaeckSelectCommandNeedsOptions(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}

  /*!
    @brief   Declares the closed set of values this control accepts, and unlocks the rest
             of the chain.

    The list lives in flash once instead of being repeated in the sketch, and the
    library validates against it: a value that is neither a listed name nor a valid
    index is rejected before the handler runs. The handler is always handed the
    index as text, whichever form the host sent, so atoi(params[0]) is enough.
    getSelectOptionNameAt() reads a name back out when one is wanted.

    Every other modifier lives on the handle this returns, because a select with no
    list accepts nothing: each value is checked against the options, and there are
    none to match.

    @param   optionsCsv  Comma-separated names as an F() literal, in the order their
                         indices follow.
    @return  The command's full handle, for chaining.

    @note    Do not name an option "none" in any casing. A host may read that state as
             "no option selected" and blank the control instead of showing it.

    @warning An empty list is ignored rather than stored, and reported on the debug
             stream: it would leave the command as if no list had been given at all,
             which the catalog reports again as it goes out. A list holding a blank
             option is refused on the same ground - it would offer a choice showing
             nothing, and dropping it instead would renumber the options after it.

    @code
      Blaeck.onSelectCommand("SET_WAVE", onSetWave)
          .withOptions(F("Sine,Square,Triangle,Sawtooth"));
    @endcode
  */
  BlaeckSelectCommandRef withOptions(const __FlashStringHelper *optionsCsv)
  {
    _setOptions(optionsCsv);
    return BlaeckSelectCommandRef(_owner, _index);
  }
};

// Handle to the signal addSignal() just added, describing how it is presented.
// Returned by value and meant to be chained, not stored:
//
//   Blaeck.addSignal("FreeMemory", &FreeMemory)
//       .withUnit(F("B"))
//       .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
//       .withDisplayPrecision(0)
//       .diagnostic()
//       .disabledByDefault();
//
// Every method returns the handle again, so order does not matter and a call may be left out.
// with* carries a value, a bare adjective does not; the booleans take an argument so a sketch
// can drive them from a variable instead of an #if.
//
// Each datatype gets its own handle, so a modifier that cannot mean anything for that signal
// does not compile. A string has no decimals and no statistics; a bool becomes a binary sensor,
// which has no unit either. Nothing else catches those: the device does not validate what it
// declares and a host publishes what it is told, so this is the only place the mistake is
// caught at all.
//
// The methods are always defined. With BLAECK_ENABLE_SIGNAL_META=0 they store nothing, and with
// a full signal table the handle is dead and they store nothing either - so a chain is safe to
// write without checking anything first.
class BlaeckSignalRefBase
{
protected:
  BlaeckSignalRefBase(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // Names no signal. Index -1 is what a refused registration already returns, so an
  // unassigned handle and a dead one are the same thing to every setter below.
  BlaeckSignalRefBase() : _owner(nullptr), _index(-1) {}

  void _setFlash(const __FlashStringHelper *value, uint16_t bit);

  void _setBit(uint16_t bit, bool on);

  void _setStateClass(BlaeckStateClass stateClass);

  void _setOptions(const __FlashStringHelper *optionsCsv);

  void _setDisplayPrecision(uint8_t decimals);

  void _setNameSuffix(uint8_t suffix);

  BlaeckSerial *_owner;
  int16_t _index;
};

// What every signal accepts, whatever it holds. Repeated per handle so the chain keeps its
// datatype all the way down, and so an editor offers exactly what applies when the dot is typed.
// What every signal handle shares. A template rather than a macro, for the reason given on
// BlaeckCommandRefShared: a comment inside a macro body documents nothing.
template <class TYPE>
class BlaeckSignalRefShared : public BlaeckSignalRefBase
{
public:
  /*!
    @brief   Ends the signal's name in a number.

    For a run of signals sharing a prefix. The prefix stays in flash and the digits
    are produced when the name is sent, so nothing reaches the heap - which is what
    a name built with snprintf costs, once per signal.

    @param   suffix  0-255. Zero is a number like any other, not "no suffix".
    @return  The same handle, for chaining.

    @code
      for (int i = 0; i < 8; i++)
        Blaeck.addSignal(F("Sine_"), &sine[i]).withNameSuffix(i + 1);
    @endcode
  */
  TYPE &withNameSuffix(uint8_t suffix)
  {
    _setNameSuffix(suffix);
    return _self();
  }

  /*!
    @brief   Declares what the value measures.

    Carried as written. This library holds no list of its own, because the
    vocabulary grows faster than firmware is reflashed - so a class a host adds
    after this release still works.

    @param   deviceClass  The host's name for the quantity, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Uptime"), &Uptime).withDeviceClass(F("duration"));
    @endcode
  */
  TYPE &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    _setFlash(deviceClass, BLAECK_SIG_HAS_DEVICE_CLASS);
    return _self();
  }

  /*!
    @brief   Declares the icon a host shows beside the value.

    @param   icon  Material Design Icons name, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Output"), &Output).withIcon(F("mdi:sine-wave"));
    @endcode
  */
  TYPE &withIcon(const __FlashStringHelper *icon)
  {
    _setFlash(icon, BLAECK_SIG_HAS_ICON);
    return _self();
  }

  /*!
    @brief   Declares the label a host shows in place of the name.

    For when the name is doing a second job. A host that logs calls its stored column
    by the name, so an author who wants that column to say what it measured writes
    F("Output [V]") - and then reads the unit twice on screen. A display name splits
    the two: the column keeps saying [V], the screen says "Output".

    Presentation only. The name still identifies the signal everywhere - the symbol
    list, the stored column, and whatever a host builds from those - so adding this
    to a signal already deployed relabels it without moving anything.

    @param   displayName  What to show, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Output [V]"), &Output).withUnit(F("V")).withDisplayName(F("Output"));
    @endcode
  */
  TYPE &withDisplayName(const __FlashStringHelper *displayName)
  {
    _setFlash(displayName, BLAECK_SIG_HAS_DISPLAY_NAME);
    return _self();
  }

  /*!
    @brief   Files the signal as describing the device rather than measuring anything.

    Keeps it off a generated dashboard, so free memory and uptime do not crowd out
    the readings someone opened the dashboard for.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Uptime"), &Uptime).withUnit(F("s")).diagnostic();
    @endcode
  */
  TYPE &diagnostic(bool on = true)
  {
    _setBit(BLAECK_SIG_DIAGNOSTIC, on);
    return _self();
  }

  /*!
    @brief   Registers the signal but leaves it switched off until someone enables it.

    Home Assistant's enabled_by_default: the entity is created but hidden until someone
    turns it on. The device sends it either way.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("RawADC"), &rawAdc).disabledByDefault();
    @endcode
  */
  TYPE &disabledByDefault(bool on = true)
  {
    _setBit(BLAECK_SIG_DISABLED_BY_DEFAULT, on);
    return _self();
  }

  /*!
    @brief   Reports every reading, even one identical to the last.

    A host may otherwise keep only changes, which loses the evidence that a device
    is still measuring. Use it where a flat line and a dead sensor should look
    different.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Temperature"), &Temperature).forceUpdate();
    @endcode
  */
  TYPE &forceUpdate(bool on = true)
  {
    _setBit(BLAECK_SIG_FORCE_UPDATE, on);
    return _self();
  }

protected:
  BlaeckSignalRefShared(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefBase(owner, index) {}
  BlaeckSignalRefShared() : BlaeckSignalRefBase() {}

private:
  TYPE &_self() { return *static_cast<TYPE *>(this); }
};

// Any of the nine numeric datatypes. The only shape with decimals to show and a value that
// accumulates, so it is the only one carrying a state class or a display precision.
class BlaeckNumericSignalRef : public BlaeckSignalRefShared<BlaeckNumericSignalRef>
{
public:
  /*!
    @brief   Declares a handle that names no signal yet.

    For the one case where a handle is stored rather than chained: kept as a global so
    the sketch can change how a signal is shown after setup() has run. Assign what
    addSignal() returns to it. Until then it does nothing when called - the same as the
    handle for a signal that did not fit - so nothing has to be checked first.

    @code
      BlaeckNumericSignalRef OutputSignal;  // file scope

      void setup() { OutputSignal = Blaeck.addSignal(F("Output"), &Output); }
      void loop()  { OutputSignal.withIcon(F("mdi:sine-wave")); }
    @endcode
  */
  BlaeckNumericSignalRef() : BlaeckSignalRefShared<BlaeckNumericSignalRef>() {}

  /*!
    @brief   Declares the symbol shown after the value.

    This reaches how a host shows the value, not what it stores - a host that logs keeps
    only the signal name, so put the unit there too if it should survive. Both is normal,
    and costs only a host saying it twice: "Output [V]: 1.230 V".

    @param   unit  Symbol as an F() literal. Non-ASCII must be UTF-8.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Frequency"), &Frequency).withUnit(F("Hz"));
    @endcode
  */
  BlaeckNumericSignalRef &withUnit(const __FlashStringHelper *unit)
  {
    _setFlash(unit, BLAECK_SIG_HAS_UNIT);
    return *this;
  }

  /*!
    @brief   Declares how the value accumulates over time.

    This is what lets a host keep statistics - averages, totals, a graph over months
    rather than a current reading. A signal that never calls it carries NONE and a
    host keeps nothing.

    @param   stateClass  One of the four kinds; see BlaeckStateClass.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Output"), &Output)
          .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT);
    @endcode
  */
  BlaeckNumericSignalRef &withStateClass(BlaeckStateClass stateClass)
  {
    _setStateClass(stateClass);
    return *this;
  }

  /*!
    @brief   Declares how many decimal places a host shows.

    Presentation only - the value sent is unchanged.

    @param   decimals  Places to show. Zero is a real instruction, meaning show it
                       as an integer, and is not the same as saying nothing.
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("Output"), &Output).withDisplayPrecision(3);
    @endcode
  */
  BlaeckNumericSignalRef &withDisplayPrecision(uint8_t decimals)
  {
    _setDisplayPrecision(decimals);
    return *this;
  }

private:
  // Only addSignal() knows a real slot number, so only addSignal() may name one. A sketch
  // reaches a handle by keeping what it returns, never by building one.
  BlaeckNumericSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckNumericSignalRef>(owner, index) {}
  friend class BlaeckSerial;
};

// A string signal. No unit, no decimals to round and nothing to keep statistics on: all three
// say the state is a number, and a host then refuses the text.
//
// Mirrors BlaeckTextStateRef: a string signal and a state channel become the same entity on a
// host, so they carry the same fields. Change one, change the other.
class BlaeckTextSignalRef : public BlaeckSignalRefShared<BlaeckTextSignalRef>
{
public:
  /*!
    @brief   Declares a handle that names no signal yet.

    See BlaeckNumericSignalRef's default constructor: for a handle kept as a global and
    assigned what addSignal() returns. Does nothing until then.
  */
  BlaeckTextSignalRef() : BlaeckSignalRefShared<BlaeckTextSignalRef>() {}

  /*!
    @brief   Declares the closed set of values this signal reports.

    Text only. The list is a set of names, which is what an enum describes: a number
    has no such set, and a bool becomes a binary sensor, which has no options at all.

    @param   optionsCsv  Comma-separated names as an F() literal.
    @return  The same handle, for chaining.

    @note    A host may require withDeviceClass(F("enum")) alongside this and reject
             the list without it. Every value reported has to be in the list; one that
             is not raises rather than being shown.

    @note    A unit is ignored alongside options rather than refused.

    @warning An empty list, or one holding a blank option, is ignored rather than stored,
             and reported on the debug stream: it would hand a host a closed set it can
             neither offer nor report a value against.

    @code
      Blaeck.addSignal(F("Mode"), modeText)
          .withDeviceClass(F("enum"))
          .withOptions(F("idle,running,fault"));
    @endcode
  */
  BlaeckTextSignalRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    _setOptions(optionsCsv);
    return *this;
  }

private:
  BlaeckTextSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckTextSignalRef>(owner, index) {}
  friend class BlaeckSerial;
};

// A bool signal, which a host announces as a binary sensor - a shape with no unit at all, on
// top of having no decimals and no statistics.
//
// withDeviceClass() draws from a different list here than on the other signals: a binary sensor
// takes F("door"), F("motion"), F("smoke"), F("window") and the like, not F("temperature").
// Some names appear in both lists meaning different things - battery is a percentage on a
// numeric signal and low/normal on this one. A name from the wrong list fails discovery, and
// the entity never appears.
class BlaeckBoolSignalRef : public BlaeckSignalRefShared<BlaeckBoolSignalRef>
{
public:
  /*!
    @brief   Declares a handle that names no signal yet.

    See BlaeckNumericSignalRef's default constructor: for a handle kept as a global and
    assigned what addSignal() returns. Does nothing until then.
  */
  BlaeckBoolSignalRef() : BlaeckSignalRefShared<BlaeckBoolSignalRef>() {}

private:
  BlaeckBoolSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckBoolSignalRef>(owner, index) {}
  friend class BlaeckSerial;
};

// Handle to the channel just declared. Returned by value and meant to be chained, not stored;
// a channel that could not be declared gives a dead handle that swallows the chain, and
// hasRejectedStateChannels() is where that shows up. The methods are always defined - with
// BLAECK_ENABLE_STATE_CHANNELS=0 they store nothing, so a sketch needs no #ifdef.
//
// Three kinds, mirroring the three signal handles, because a channel's value now has a type and
// the wrong modifier on the wrong type is a bug a host cannot report. Unit, state class and
// display precision each say the state is a number, so on a text channel they do not merely
// do nothing - they make a host refuse the text and show nothing at all. Splitting
// the handles turns that into a compile error.
class BlaeckStateRefBase
{
protected:
  BlaeckStateRefBase(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // The entry this handle names, or nullptr when registration was rejected.
  blaeck_detail::StateChannelEntry * _entry() const;

  // Says the catalog no longer describes what the host holds. Out of line for the same
  // reason as _entry(): BlaeckSerial is still incomplete at this point in the header.
  void _markDirty() const;

  // Where a refusal is explained, or nullptr when no debug stream was named. Out of line for
  // the same reason.
  Stream *_debugStream() const;

  void _setStateClass(BlaeckStateClass stateClass)
  {
#if BLAECK_ENABLE_STATE_CHANNELS
    if (auto *e = _entry())
    {
      // Compared before it is written, here and in every modifier on this handle: a sketch
      // may call one every pass through loop(), and marking on assignment rather than on
      // change would announce the whole catalog every pass.
      const uint16_t flags =
          (uint16_t)((e->metaFlags & ~BLAECK_SCH_STATE_CLASS_MASK) |
                     (((uint16_t)stateClass << BLAECK_SCH_STATE_CLASS_SHIFT) &
                      BLAECK_SCH_STATE_CLASS_MASK));
      if (e->metaFlags != flags)
      {
        e->metaFlags = flags;
        _markDirty();
      }
    }
#else
    (void)stateClass;
#endif
  }

  void _setDisplayPrecision(uint8_t decimals)
  {
#if BLAECK_ENABLE_STATE_CHANNELS
    if (auto *e = _entry())
    {
      const uint16_t flags = (uint16_t)(e->metaFlags | BLAECK_SCH_HAS_DISPLAY_PRECISION);
      if (e->displayPrecision != decimals || e->metaFlags != flags)
      {
        e->displayPrecision = decimals;
        e->metaFlags = flags;
        _markDirty();
      }
    }
#else
    (void)decimals;
#endif
  }

  BlaeckSerial *_owner;
  int16_t _index;
};

// The modifiers every channel kind takes, whatever its value type.
// What every state channel handle shares. A template rather than a macro, for the reason given
// on BlaeckCommandRefShared: a comment inside a macro body documents nothing.
template <class TYPE>
class BlaeckStateRefShared : public BlaeckStateRefBase
{
public:
  /*!
    @brief   Declares the icon a host shows beside the channel.

    @param   icon  Material Design Icons name, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Status"), BlaeckText).withIcon(F("mdi:pulse"));
    @endcode
  */
  TYPE &withIcon(const __FlashStringHelper *icon)
  {
    if (blaeck_detail::flashStrEmpty(icon))
      icon = nullptr;
    if (auto *e = _entry())
    {
      if (e->icon != icon)
      {
        e->icon = icon;
        _markDirty();
      }
    }
    return _self();
  }

  /*!
    @brief   Files the channel as describing the device rather than its work.

    Keeps a status line out of the way of the controls and readings a dashboard is
    opened for.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Status"), BlaeckText).withIcon(F("mdi:pulse")).diagnostic();
    @endcode
  */
  TYPE &diagnostic(bool on = true)
  {
    if (auto *e = _entry())
    {
      if (e->diagnostic != on)
      {
        e->diagnostic = on;
        _markDirty();
      }
    }
    return _self();
  }

  /*!
    @brief   Declares what the value is, for a host that renders it specially.

    @param   deviceClass  The host's name for the kind of value, as an F() literal.
    @return  The same handle, for chaining.

    @warning The name has to come from the list for this channel's value type -
             F("timestamp") or F("date") for text, F("voltage") for a number. One
             from the wrong list fails discovery and the entity never appears at
             all, rather than appearing unstyled.

    @code
      Blaeck.addStateChannel(F("LastSeen"), BlaeckText).withDeviceClass(F("timestamp"));
    @endcode
  */
  TYPE &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    if (blaeck_detail::flashStrEmpty(deviceClass))
      deviceClass = nullptr;
    if (auto *e = _entry())
    {
      if (e->deviceClass != deviceClass)
      {
        e->deviceClass = deviceClass;
        _markDirty();
      }
    }
    return _self();
  }

  /*!
    @brief   Registers the channel but leaves it switched off until someone enables it.

    Home Assistant's enabled_by_default: the entity is created but hidden until someone
    turns it on. The device sends it either way.
    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("BuildInfo"), BlaeckText).disabledByDefault();
    @endcode
  */
  TYPE &disabledByDefault(bool on = true)
  {
    if (auto *e = _entry())
    {
      if (e->disabledByDefault != on)
      {
        e->disabledByDefault = on;
        _markDirty();
      }
    }
    return _self();
  }

  /*!
    @brief   Reports every value, even one identical to the last.

    A host otherwise collapses a repeat into the entry it already has, so a channel
    that says the same thing each time leaves no trace of having been written - and
    a device that stopped looks like one with nothing to report.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Heartbeat"), BlaeckText).forceUpdate();
    @endcode
  */
  TYPE &forceUpdate(bool on = true)
  {
    if (auto *e = _entry())
    {
      if (e->forceUpdate != on)
      {
        e->forceUpdate = on;
        _markDirty();
      }
    }
    return _self();
  }

protected:
  BlaeckStateRefShared(BlaeckSerial *owner, int16_t index) : BlaeckStateRefBase(owner, index) {}

private:
  TYPE &_self() { return *static_cast<TYPE *>(this); }
};

// A channel carrying a number. Mirrors BlaeckNumericSignalRef: the same value becomes the same
// entity on a host whichever way it arrives, so they carry the same fields. Change one,
// change the other. What differs is not the entity but the cadence - a signal is sampled into
// every logged row, a channel is pushed when it changes and never stored.
class BlaeckNumericStateRef : public BlaeckStateRefShared<BlaeckNumericStateRef>
{
public:
  BlaeckNumericStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefShared<BlaeckNumericStateRef>(owner, index) {}

  /*!
    @brief   Declares the symbol shown after the value.

    @param   unit  Symbol as an F() literal. Non-ASCII must be UTF-8.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Amplitude"), &Amplitude).withUnit(F("V"));
    @endcode
  */
  BlaeckNumericStateRef &withUnit(const __FlashStringHelper *unit)
  {
    if (blaeck_detail::flashStrEmpty(unit))
      unit = nullptr;
    if (auto *e = _entry())
    {
      // The bit the 0x90 writer actually sends is rebuilt from the pointer, so it is cleared
      // here too rather than left set over a unit that is no longer there.
      const uint16_t flags = (unit != nullptr) ? (uint16_t)(e->metaFlags | BLAECK_SCH_HAS_UNIT)
                                               : (uint16_t)(e->metaFlags & ~BLAECK_SCH_HAS_UNIT);
      if (e->unit != unit || e->metaFlags != flags)
      {
        e->unit = unit;
        e->metaFlags = flags;
        _markDirty();
      }
    }
    return *this;
  }

  /*!
    @brief   Declares that the value accumulates, so a host keeps statistics on it.

    Worth more here than on a signal: a channel is never written to the host's own
    store, so this is the only way its history outlives the recorder's window.

    @param   stateClass  One of the four kinds; see BlaeckStateClass.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Amplitude"), &Amplitude)
          .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT);
    @endcode
  */
  BlaeckNumericStateRef &withStateClass(BlaeckStateClass stateClass)
  {
    _setStateClass(stateClass);
    return *this;
  }

  /*!
    @brief   Declares how many decimal places a host shows.

    Presentation only - the value sent is unchanged.

    @param   decimals  Places to show. Zero means show it as an integer, which is
                       not the same as saying nothing.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Amplitude"), &Amplitude).withDisplayPrecision(2);
    @endcode
  */
  BlaeckNumericStateRef &withDisplayPrecision(uint8_t decimals)
  {
    _setDisplayPrecision(decimals);
    return *this;
  }

  /*!
    @brief   Makes the channel work its value out when it is wanted.

    For a value derived from other state - an average, a percentage, a relationship
    between two variables. A channel pointed at a variable reports the variable, which
    is the value itself and so cannot be out of date; a variable holding a calculation
    is a copy of one, and is only right until something it was worked out from moves.

    The getter is asked while the catalog is built, which happens at startup, when the
    channel list changes, and whenever a host asks - moments the sketch cannot
    anticipate, and so cannot refresh a variable for.

    @param   getStateValue  Called to produce the value. Must return what the channel
                            was declared as; a getter of another type is refused and
                            says so on the debug stream.
    @return  The same handle, for chaining.

    @warning The getter runs while a frame is being assembled, so it must not send one.
             Read variables and compute - nothing else.

    @code
      Blaeck.addStateChannel(F("Efficiency"), BlaeckFloat).withStateValue(efficiency);
    @endcode
  */
  BlaeckNumericStateRef &withStateValue(BlaeckStateByteGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_byte;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateShortGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_short;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateUShortGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_ushort;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateIntGetter getStateValue)
  {
    if (auto *e = _entry())
    {
#ifdef __AVR__
    const dataType want = Blaeck_int;
#else
    const dataType want = Blaeck_long;
#endif
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateUIntGetter getStateValue)
  {
    if (auto *e = _entry())
    {
#ifdef __AVR__
    const dataType want = Blaeck_uint;
#else
    const dataType want = Blaeck_ulong;
#endif
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateLongGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_long;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateULongGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_ulong;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateFloatGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_float;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

  BlaeckNumericStateRef &withStateValue(BlaeckStateDoubleGetter getStateValue)
  {
    if (auto *e = _entry())
    {
#ifdef __AVR__
    const dataType want = Blaeck_float;
#else
    const dataType want = Blaeck_double;
#endif
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }

};

// A channel carrying text. Mirrors BlaeckTextSignalRef: no unit, no decimals to round and
// nothing to keep statistics on: all three say the state is a number, and a host then refuses
// the text.
class BlaeckTextStateRef : public BlaeckStateRefShared<BlaeckTextStateRef>
{
public:
  BlaeckTextStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefShared<BlaeckTextStateRef>(owner, index) {}

  /*!
    @brief   Makes the channel report a current value, fetched when asked.

    The getter is called while the catalog is built, so a host that polls learns the
    value as it is at that moment and the sketch never pushes just to keep it in
    step. Being fetched rather than stored, it cannot go stale. Left out, the channel
    is a plain log channel and carries no value until something writes one.

    @param   getStateText  Called to produce the value as text. Build it in a
                           function-local static and return that.
    @return  The same handle, for chaining.

    @note    Only text takes a getter. A numeric channel points at the variable
             instead, which needs no function at all - and anything a getter would
             have computed can be assigned to a variable first.

    @warning The getter runs while a frame is being assembled, so it must not send
             one: writeState(), writeEvent() and the catalog writers all start a new
             frame, and the half-built one they interrupt goes out malformed. Read a
             variable and format it - nothing else.

    @code
      Blaeck.addStateChannel(F("Offset"), BlaeckText).withStateText(offsetText);
    @endcode
  */
  BlaeckTextStateRef &withStateText(BlaeckStateTextGetter getStateText)
  {
    if (auto *e = _entry())
    {
      // Refused rather than stored, the way withOptions() refuses a list it cannot use.
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, Blaeck_string, e->valueType,
                                              F("withStateText"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getStateText != getStateText)
      {
        e->getStateText = getStateText;
        _markDirty();
      }
    }
    return *this;
  }

  /*!
    @brief   Declares the closed set of values this channel reports.

    Text only. The list is a set of names, which is what an enum describes; a number
    has no such set.

    @param   optionsCsv  Comma-separated names as an F() literal.
    @return  The same handle, for chaining.

    @note    A host may require withDeviceClass(F("enum")) alongside this and reject
             the list without it. Every value reported has to be in the list; one that
             is not raises rather than being shown.

    @note    A unit is ignored alongside options rather than refused.

    @warning An empty list, or one holding a blank option, is ignored rather than stored,
             and reported on the debug stream, for the reason given on the signal form.

    @code
      Blaeck.addStateChannel(F("Mode"), BlaeckText)
          .withDeviceClass(F("enum"))
          .withOptions(F("idle,running,fault"));
    @endcode
  */
  BlaeckTextStateRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    if (auto *e = _entry())
    {
      // Refused rather than stored, for the reason given on the command form.
      if (!blaeck_detail::optionsAccepted(optionsCsv, _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->options != optionsCsv)
      {
        e->options = optionsCsv;
        _markDirty();
      }
    }
    return *this;
  }
};

// A channel carrying a bool, which a host announces as a binary sensor - a shape with no unit
// at all, on top of having no decimals and no statistics.
//
// withDeviceClass() draws from a different list here than on a numeric channel: a binary sensor
// takes F("door"), F("motion"), F("smoke"), F("window") and the like, not F("temperature").
// Some names appear in both lists meaning different things - battery is a percentage on a
// numeric channel and low/normal on this one.
class BlaeckBoolStateRef : public BlaeckStateRefShared<BlaeckBoolStateRef>
{
public:
  BlaeckBoolStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefShared<BlaeckBoolStateRef>(owner, index) {}

  /*!
    @brief   Makes the channel work its value out when it is wanted.

    For a value derived from other state - an average, a percentage, a relationship
    between two variables. A channel pointed at a variable reports the variable, which
    is the value itself and so cannot be out of date; a variable holding a calculation
    is a copy of one, and is only right until something it was worked out from moves.

    The getter is asked while the catalog is built, which happens at startup, when the
    channel list changes, and whenever a host asks - moments the sketch cannot
    anticipate, and so cannot refresh a variable for.

    @param   getStateValue  Called to produce the value. Must return what the channel
                            was declared as; a getter of another type is refused and
                            says so on the debug stream.
    @return  The same handle, for chaining.

    @warning The getter runs while a frame is being assembled, so it must not send one.
             Read variables and compute - nothing else.

    @code
      Blaeck.addStateChannel(F("Running"), BlaeckBool).withStateValue(isRunning);
    @endcode
  */
  BlaeckBoolStateRef &withStateValue(BlaeckStateBoolGetter getStateValue)
  {
    if (auto *e = _entry())
    {
    const dataType want = Blaeck_bool;
      if (!blaeck_detail::stateGetterAccepted(e->stateValue, want, e->valueType,
                                              F("withStateValue"), _debugStream(), e->name, e->nameInFlash))
        return *this;
      if (e->getNumber != (void (*)())getStateValue)
      {
        e->getNumber = (void (*)())getStateValue;
        _markDirty();
      }
    }
    return *this;
  }
};


class BlaeckEventChannelRef
{
public:
  BlaeckEventChannelRef(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  /*!
    @brief   Declares the icon a host shows beside the channel.

    @param   icon  Material Design Icons name, as an F() literal.
    @return  The same handle, for chaining.

    @code
      Blaeck.addEventChannel(F("Activity"), F("idle,resumed")).withIcon(F("mdi:pulse"));
    @endcode
  */
  BlaeckEventChannelRef withIcon(const __FlashStringHelper *icon);

  /*!
    @brief   Files the channel as describing the device rather than its work.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addEventChannel(F("Faults"), F("brownout,watchdog")).diagnostic();
    @endcode
  */
  BlaeckEventChannelRef diagnostic(bool on = true);

  /*!
    @brief   Declares what kind of thing the channel reports.

    Unlike a signal's device class, this one is a closed set: a host validates it
    against exactly three names, so anything else fails discovery and the entity never
    appears at all.

    @param   deviceClass  F("button"), F("doorbell") or F("motion"). Those three,
                          as an F() literal.
    @return  The same handle, for chaining.

    @note    F("doorbell") also requires the channel to declare a "ring" event type.
             A host may warn without it now and refuse it later.

    @note    F("button") has standard type names - press_start, press_end,
             long_press_start, long_press_end, multi_press_ongoing, multi_press_end -
             but none are required. Declare only what the hardware can produce.

    @code
      Blaeck.addEventChannel(F("Doorbell"), F("ring")).withDeviceClass(F("doorbell"));
    @endcode
  */
  BlaeckEventChannelRef withDeviceClass(const __FlashStringHelper *deviceClass);

  /*!
    @brief   Registers the channel but leaves it switched off until someone enables it.

    Home Assistant's enabled_by_default: the entity is created but hidden until someone
    turns it on. The device sends it either way.
    An event is not retained, so turning the channel on shows what happens next, not what
    was missed.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addEventChannel(F("Debug"), F("trace")).disabledByDefault();
    @endcode
  */
  BlaeckEventChannelRef disabledByDefault(bool on = true);

private:
  BlaeckSerial *_owner;
  int16_t _index;
};

// A word these comments use throughout: a HOST is whatever is on the other end of the link -
// Loggbok, blaecktcpy, or something of your own.
//
// Hosts come in two kinds, and the difference decides which half of this API means anything.
// One reads the signals and records the values, and needs nothing but their names and types.
// The other also builds controls and displays from what the device declares about itself -
// units, icons, ranges, options - and it is only to that kind that withUnit(), withIcon(),
// withDeviceClass(), withStateClass(), diagnostic() and their neighbours say anything at all.
// Setting BLAECK_ENABLE_SIGNAL_META to 0 removes that half outright, which is the clearest
// statement of how separable it is: everything else keeps working.
//
// What these comments do not do is name which program enforces a given rule. A device class
// from the wrong list may be refused by the program reading the frames or by whatever it
// hands them to, and a sketch sees the same thing either way - the entity never appears. So
// the effect is described rather than the culprit.
class BlaeckSerial
{
public:
  // ----- Constructor -----
  BlaeckSerial();

  // ----- Destructor -----
  ~BlaeckSerial();

  // ----- Initialize -----

  /*!
    @brief   Starts BlaeckSerial on a stream and returns a handle that sizes its tables.

    The first call a sketch makes, after opening the stream itself. What a device
    publishes afterwards comes in three kinds: a signal is a value sampled and logged
    over time; a state channel reports a current value that is shown but not logged,
    whether that is a status line or what a control is set to; an event channel reports
    discrete occurrences from a list named up front. The WaveformGenerator example
    uses all three.

    @param   Ref  Stream the device talks over - Serial, Serial1, or any other Stream.
    @return  Handle for sizing the tables and naming a debug stream. Chainable, and
             safe to ignore entirely.
    @note    Every capacity on the handle is optional and starts at a per-board
             default. A table is allocated in full by the first entry added to it, so
             a table the sketch never uses costs nothing - which is also why the chain
             may run after begin() has returned.

    @code
      Serial.begin(115200);
      Blaeck.begin(&Serial)
          .withSignals(50)
          .withStateChannels(12)
          .withDebugStream(&Serial1);
    @endcode
  */
  BlaeckBeginRef begin(Stream *Ref);

  /*!
    @brief   Starts BlaeckSerial and sizes the signal table in one call.

    Shorthand for begin(Ref).withSignals(Size), for a sketch that adds signals and
    nothing else.

    @param   Ref   Stream the device talks over.
    @param   Size  Signals to make room for.
    @return  Handle for sizing the remaining tables, as begin(Stream *) returns.

    @code
      Blaeck.begin(&Serial, 8);
    @endcode
  */
  BlaeckBeginRef begin(Stream *Ref, unsigned int Size);

  /*!
    @brief  The name the device calls itself. Defaults to "Unknown".

    What a host lists it by, and groups its signals and controls under.

    @note   Kept as a pointer, not copied. A quoted literal is always safe; a name
            built at runtime has to live in a global buffer, not one inside a function.

    @code
      Blaeck.DeviceName = "Waveform Generator Demo";
    @endcode
  */
  const char *DeviceName = "Unknown";

  /*!
    @brief  The board this firmware runs on. Defaults to "n/a".

    Shown alongside the device; nothing is inferred from it.

    @note   Kept as a pointer, not copied. A quoted literal is always safe; a name
            built at runtime has to live in a global buffer, not one inside a function.

    @code
      Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
    @endcode
  */
  const char *DeviceHWVersion = "n/a";

  /*!
    @brief  The sketch's own version. Defaults to "n/a".

    Tells one firmware from another across a fleet that is half updated.

    @note   Kept as a pointer, not copied. A quoted literal is always safe; a name
            built at runtime has to live in a global buffer, not one inside a function.

    @code
      Blaeck.DeviceFWVersion = "1.0";
    @endcode
  */
  const char *DeviceFWVersion = "n/a";

  // ----- Signals -----

  /*!
    @brief   Registers a value to be sampled and logged over time.

    A signal is a reading, not a setting - it is read on every interval and kept as
    history. What a control is set to belongs on a state channel instead, which is
    shown but never logged.

    The library keeps the pointer rather than the value, so it reads whatever the
    variable holds at the moment it writes. The variable has to be a global.

    @param   signalName  Name a host lists and logs the signal under.
    @param   value       Address of the variable to read. One overload per type.
    @return  Handle describing how a host should present the signal. Chainable, and
             safe to ignore - a signal that describes nothing costs nothing.
    @note    The handle may also be kept and used later, which is how a signal changes
             how it looks while the sketch runs - an icon that follows what the device
             is doing. It names an owner and a slot, so it cannot be declared empty;
             give it an index of -1, the slot a refused registration returns, and it
             stays inert until setup() assigns the real one. The handles the state
             channel and command calls return work the same way.
    @note    When the signal table is full the handle is dead: the chain still
             compiles and runs, and stores nothing. The missing signal is the real
             problem, and hasRejectedSignals() reports it.

    @note    The first call describing a signal allocates. If that fails the signal
             still sends its value, undescribed, and printRejections() says so.

    @code
      Blaeck.addSignal("Temperature", &Temperature)
          .withUnit(F("\xC2\xB0" "C"))
          .withDeviceClass(F("temperature"))
          .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
          .withDisplayPrecision(1);
    @endcode
  */
  BlaeckBoolSignalRef addSignal(const char *signalName, bool *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, byte *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, short *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, unsigned short *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, int *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, unsigned int *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, long *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, unsigned long *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, float *value);
  BlaeckNumericSignalRef addSignal(const char *signalName, double *value);
  BlaeckTextSignalRef addSignal(const char *signalName, const char *value);

  /*!
    @brief   Registers a signal whose name is kept in flash.

    The same eleven overloads, named with F(). The name then stays in flash and the
    signal holds a pointer to it rather than a copy, saving the length of the name
    plus one byte of SRAM for every signal declared this way.

    @param   signalName  Name as an F() literal, fixed at compile time.
    @param   value       Address of the variable to read.
    @return  Handle describing how a host should present the signal.
    @note    Only for a name fixed at compile time. A name built at runtime -
             snprintf into a buffer, say - needs the const char* overloads, which
             copy it.

    @code
      Blaeck.addSignal(F("Temperature"), &Temperature);
    @endcode
  */
  BlaeckBoolSignalRef addSignal(const __FlashStringHelper *signalName, bool *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, byte *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, short *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, unsigned short *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, int *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, unsigned int *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, long *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, unsigned long *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, float *value);
  BlaeckNumericSignalRef addSignal(const __FlashStringHelper *signalName, double *value);
  BlaeckTextSignalRef addSignal(const __FlashStringHelper *signalName, const char *value);

  /*!
    @brief   Empties the signal table so it can be filled again.

    For a device whose set of signals changes while it runs. The table keeps the
    capacity it was given; what it held is freed, along with the rejection counts
    and the schema hash.

    @warning Call writeSymbols() once the table is refilled. Until then a host is
             reading the values against the old list and filing them under the
             wrong names.

    @code
      Blaeck.deleteSignals();
      Blaeck.addSignal(F("Temperature"), &Temperature);
      Blaeck.writeSymbols();
    @endcode
  */
  void deleteSignals();

  /*!
    @brief   Reports whether any signal could not be added.

    Either the table was already full, or the board had no RAM for it at all. Both
    surface at the first addSignal(), which is where the table is built.

    @return  True if at least one signal was dropped.

    @code
      if (Blaeck.hasRejectedSignals())
        Serial.println(F("Raise withSignals() on the begin() chain."));
    @endcode
  */
  bool hasRejectedSignals() const { return _signalRegistrationFailed; }

  /*!
    @brief   Counts the signals that could not be added.

    Where hasRejectedSignals() only says whether any were. Counted even with no
    debug stream attached, so a sketch can report the shortfall itself.

    @return  How many were dropped.

    @code
      Serial.println(Blaeck.getRejectedSignalCount());
    @endcode
  */
  uint16_t getRejectedSignalCount() const { return _rejectedSignalCount; }

  /*!
    @brief   How many signals are registered.

    Also one past the highest index write() and update() accept. Counts what was
    added, so a signal the table had no room for is not in it - getRejectedSignalCount()
    has those.

    @note    Maintained by the library. Assigning to it resizes nothing and leaves
             the count disagreeing with the table.

    @code
      for (int i = 0; i < Blaeck.SignalCount; i++)
        Blaeck.markSignalUpdated(i);
    @endcode
  */
  int SignalCount;

  // ----- Device Restarted -----

  /*!
    @brief   Announces that the device has started running.

    Sent once per boot - the first start as well as a restart, which a host can
    neither distinguish nor needs to: both mean the values it holds are from before
    and should be discarded. Sent by read() on its first call, so a sketch needs this
    only to get it out before it starts reading.

    Lets a host tell a device that came back from one that has simply gone quiet - the
    values look identical otherwise.

    Everything the device declares follows it unasked: the state channels with their
    current values, the event channels, the commands and what the signals say about
    themselves. A host that stayed connected is holding what the last run declared and
    has no reason to ask again, so the device tells it.

    @code
      Blaeck.writeRestarted();
    @endcode
  */
  void writeRestarted();

  // ----- Devices -----

  /*!
    @brief   Sends the device's name and versions.

    What <BLAECK.GET_DEVICES> answers with, so it is normally driven by the host
    asking. Call it to announce the device unprompted.

    @code
      Blaeck.writeDevices();
    @endcode
  */
  void writeDevices();

  // ----- Symbols -----

  /*!
    @brief   Sends the list of what this device measures.

    Every signal's name, datatype and position, which is what lets a host make sense
    of the values at all. Answers <BLAECK.WRITE_SYMBOLS>.

    @warning Call it after adding, deleting or renaming a signal. Until then a host
             is reading values against a list that no longer describes them, and
             files them under the wrong names rather than failing.

    @code
      Blaeck.deleteSignals();
      Blaeck.addSignal(F("Temperature"), &Temperature);
      Blaeck.writeSymbols();
    @endcode
  */
  void writeSymbols();

  // ----- Signal Config -----

  /*!
    @brief   Sends what the signals declare about how they are presented.

    Units, icons, device classes and the rest - meaningless to a host that only
    records values, and everything to one that builds controls and displays.
    Answers <BLAECK.WRITE_SIGNAL_CONFIG>.

    Carries only the signals that describe something, so a device where none do
    answers with an empty reply rather than nothing at all - which is what keeps a
    polling host from waiting out its timeout. With BLAECK_ENABLE_SIGNAL_META=0
    that is always the case.

    Changing an icon or a unit while the sketch runs sends this on its own, so a
    sketch calls it to answer a poll rather than to keep a host current.

    @code
      Blaeck.writeSignalConfig();
    @endcode
  */
  void writeSignalConfig();

  // ----- Commands -----

  /*!
    @brief   Sends the list of commands this device accepts.

    Every registered command, plain and typed. A plain one carries only its name; a
    typed one carries what it controls - its kind, its range, its options - which is
    what lets a host build a control for it rather than just list it.
    Answers <BLAECK.WRITE_COMMANDS>.

    Registering or clearing a command sends this by itself, as does starting up.

    @code
      Blaeck.writeCommands();
    @endcode
  */
  void writeCommands();

  // ----- State channels -----
  // With BLAECK_ENABLE_STATE_CHANNELS=0 these still compile but do nothing, so a
  // sketch can be built for a tiny target without being rewritten.

  /*!
    @brief   Declares a channel reporting what something is set to.

    A state channel reports a current value that is shown but never logged, whether
    that is a status line or what a control is set to. A signal is the opposite - it
    is sampled on every interval and kept as history.

    Which overload is called settles the channel's type, and the type settles the
    handle: a numeric channel takes withUnit(), withStateClass() and
    withDisplayPrecision(), a text one takes withStateText() and withOptions(), and
    asking for the wrong one does not compile.

    Pass a pointer and the library reads that variable when the value is wanted,
    exactly as addSignal() does. Pass none and the channel carries only what
    writeState() hands it.

    @param   channelName  Name a host lists the channel under. Copied, so a buffer
                          may be reused straight away.
    @return  Handle describing how a host should present the channel.

    @code
      Blaeck.addStateChannel(F("Status"), BlaeckText).withIcon(F("mdi:pulse")).diagnostic();
      Blaeck.addStateChannel(F("Amplitude"), &Amplitude).withUnit(F("V"));
    @endcode
  */
  BlaeckTextStateRef addStateChannel(const char *channelName, BlaeckTextTag);
  BlaeckBoolStateRef addStateChannel(const char *channelName, BlaeckBoolTag);
  BlaeckNumericStateRef addStateChannel(const char *channelName, BlaeckNumericTag type);
  BlaeckTextStateRef addStateChannel(const char *channelName, const char *value);
  BlaeckBoolStateRef addStateChannel(const char *channelName, bool *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, byte *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, short *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, unsigned short *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, int *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, unsigned int *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, long *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, unsigned long *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, float *value);
  BlaeckNumericStateRef addStateChannel(const char *channelName, double *value);

  // The same twelve, named with F(). The name is copied into the channel exactly as a RAM
  // name is - a channel always holds its own copy - so F() keeps the literal out of SRAM
  // rather than changing how the channel stores it. Truncated at MAX_STATE_NAME_COUNT, the
  // same limit the const char* form applies.
  BlaeckTextStateRef addStateChannel(const __FlashStringHelper *channelName, BlaeckTextTag);
  BlaeckBoolStateRef addStateChannel(const __FlashStringHelper *channelName, BlaeckBoolTag);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, BlaeckNumericTag type);
  BlaeckTextStateRef addStateChannel(const __FlashStringHelper *channelName, const char *value);
  BlaeckBoolStateRef addStateChannel(const __FlashStringHelper *channelName, bool *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, byte *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, short *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, unsigned short *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, int *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, unsigned int *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, long *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, unsigned long *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, float *value);
  BlaeckNumericStateRef addStateChannel(const __FlashStringHelper *channelName, double *value);
  /*!
    @brief   Forgets every declared state channel.

    Leaves the table empty and its capacity untouched, for a device that re-declares
    what it offers while running.

    Channels a command claimed with withOwnState() are left: they belong to the
    command, which this has not cleared, and withOwnState() runs only on the handle
    onNumberCommand() and its siblings return - so a channel taken here could be
    declared again only by registering its command again. clearAllCommandHandlers()
    releases those, along with the commands that own them.

    @note    Re-declaring changes what a position means, and the new list is sent
             for that reason without being asked for - so a host is never left
             reading values against the list this cleared.

    @code
      Blaeck.clearAllStateChannels();
      Blaeck.addStateChannel(F("Status"), BlaeckText);
    @endcode
  */
  void clearAllStateChannels();

  /*!
    @brief   Sends the list of declared state channels.

    Answers <BLAECK.WRITE_STATE_CHANNELS>, and is what a host needs before any value
    means anything - a value names its channel by position in this list.

    A sketch rarely has to call it: the list goes out when the device starts, and
    again whenever the channels or what they declare have moved.

    @code
      Blaeck.writeStateChannels();
    @endcode
  */
  void writeStateChannels();

  /*!
    @brief   Reports a value on a declared channel.

    Fire and forget: a host may show it, but it is never stored as logged data. Use
    it for a status line, or for what a control is set to.

    @param   channelName  A channel already passed to addStateChannel(). A value on
                          one that was not is dropped.
    @param   text         The value. Longer than 255 bytes is truncated, and the
                          truncation is reported once per channel on the debug
                          stream.

    @warning Dropped without a word for two more reasons: a channel carrying a
             number (use writeState(channelName)), or one a command owns (use
             writeCommandState()). A debug stream says which.

    @warning The channel list has to reach the host first, since a value names its
             channel by position in that list rather than by name.

    @code
      char text[40];
      snprintf(text, sizeof(text), "up %lu s", millis() / 1000UL);
      Blaeck.writeState(F("Status"), text);
    @endcode
  */
  // No message id, unlike the catalog writers: a push answers no request, so there is nothing
  // to pair it with. The frame still carries the field, always zero.
  void writeState(const char *channelName, const char *text);

  // Report whatever the channel currently holds - the variable a typed channel points at, or
  // the text its getter builds. The only way to push a numeric channel, since there is nothing
  // for the caller to pass: the value already lives where the channel was told to look.
  void writeState(const char *channelName);

  // The same two, named with F().
  void writeState(const __FlashStringHelper *channelName, const char *text);
  void writeState(const __FlashStringHelper *channelName);

  /*!
    @brief   Reports a number on a channel declared by tag.

    For a channel that holds no variable of its own. The value is converted to
    whatever the channel was declared as, so the literal you write does not have to
    match it - 20 on a float channel sends 20.0.

    @param   channelName  A channel declared with a tag. One declared with a variable
                          or a getter reads its own value and refuses this.
    @param   value        Converted to the channel's declared type.

    @code
      Blaeck.writeState(F("Temperature"), 20.5f);
    @endcode
  */
  void writeState(const char *channelName, bool value);
  void writeState(const char *channelName, byte value);
  void writeState(const char *channelName, short value);
  void writeState(const char *channelName, unsigned short value);
  void writeState(const char *channelName, int value);
  void writeState(const char *channelName, unsigned int value);
  void writeState(const char *channelName, long value);
  void writeState(const char *channelName, unsigned long value);
  void writeState(const char *channelName, float value);
  void writeState(const char *channelName, double value);

  // The same ten, named with F().
  void writeState(const __FlashStringHelper *channelName, bool value);
  void writeState(const __FlashStringHelper *channelName, byte value);
  void writeState(const __FlashStringHelper *channelName, short value);
  void writeState(const __FlashStringHelper *channelName, unsigned short value);
  void writeState(const __FlashStringHelper *channelName, int value);
  void writeState(const __FlashStringHelper *channelName, unsigned int value);
  void writeState(const __FlashStringHelper *channelName, long value);
  void writeState(const __FlashStringHelper *channelName, unsigned long value);
  void writeState(const __FlashStringHelper *channelName, float value);
  void writeState(const __FlashStringHelper *channelName, double value);

  /*!
    @brief   Publishes a command's own state now.

    Asks the getter the command was registered with and sends the value on the channel
    the command owns. The push is what makes a change visible at once: the catalog
    reports only on demand, when a host asks for it.

    @param   command  Pass the handler's own command parameter and there is no literal
                      to keep in step with the registration.
    @note    Does nothing for a command with no state of its own, or one whose state is
             a signal - there the signal's own write is what reports it.

    @code
      void onSetOffset(const char *command, const char *const *params, byte paramCount)
      {
        Offset = (float)atof(params[0]);
        Blaeck.writeCommandState(command);
      }
    @endcode
  */
  void writeCommandState(const char *command);

  // ----- Events -----
  // With BLAECK_ENABLE_EVENTS=0 these still compile but do nothing.

  /*!
    @brief   Declares a channel reporting discrete occurrences from a fixed list.

    An event is something that happened, where a signal is a value that is true
    continuously and a state channel is what something is set to.

    @param   channelName  Name a host lists the channel under. Copied, so a buffer
                          may be reused straight away.
    @param   eventTypes   The closed set this channel may report, comma-separated
                          and read left to right, so position fixes each type's
                          index.
    @return  Handle describing how a host should present the channel.

    @warning eventTypes is not optional. writeEvent() resolves against this list,
             and a host needs it to announce the channel at all, so one declared
             without types could neither report nor be shown. A blank type is
             refused with it: it would hold an index nothing could be reported
             under, and dropping it instead would renumber every type after it.

    @code
      Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"))
          .withIcon(F("mdi:pulse"));
    @endcode
  */
  BlaeckEventChannelRef addEventChannel(const char *channelName, const __FlashStringHelper *eventTypes);

  // The same, named with F(). The channel name is copied as it is from a RAM name;
  // eventTypes was already flash-only.
  BlaeckEventChannelRef addEventChannel(const __FlashStringHelper *channelName, const __FlashStringHelper *eventTypes);

  /*!
    @brief   Appends one event type to a channel already declared.

    addEventChannel() takes the set known at compile time; this adds to it
    conditionally, for types a board only has when some hardware is fitted.

    @param   channelName  A channel already declared.
    @param   eventType    The type to add. Must outlive the call, so use F().
    @return  True if it was added. False if it is blank, does not fit, names a channel
             that was never declared, or duplicates one the channel already has - each
             reported on the debug stream and counted by hasRejectedEventChannels().

    @note    Call order fixes each type's index within its channel, so appending is
             safe but reordering is not. Types are held in one pool shared by every
             channel; see withEventTypes().

    @code
      Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"));
      if (hasBatteryMonitor)
        Blaeck.addEventType(F("Activity"), F("low_battery"));
    @endcode
  */
  bool addEventType(const char *channelName, const __FlashStringHelper *eventType);
  bool addEventType(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType);
  /*!
    @brief   Forgets every declared event channel, and every event type with them.

    The types share one pool, so emptying the channels empties it. Both tables keep
    their capacity.

    @note    The new list goes out unasked, and ahead of any event reported after
             this - an occurrence names its channel and its type by position, and
             is the one thing that cannot be put right afterwards.

    @code
      Blaeck.clearAllEventChannels();
      Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"));
    @endcode
  */
  void clearAllEventChannels();

  /*!
    @brief   Sends the list of declared event channels and their types.

    Answers <BLAECK.WRITE_EVENT_CHANNELS>, and is what a host needs before any event
    means anything - an event names its channel and type by position in this list.

    Like the other catalogs it is sent at startup and after any change, and ahead of
    an event that would otherwise be read against a list already out of date.

    @code
      Blaeck.writeEventChannels();
    @endcode
  */
  void writeEventChannels();

  /*!
    @brief   Reports that something happened on a declared channel.

    Fire and forget: a host may show it, but it is never stored as logged data.

    @param   channelName  A channel already passed to addEventChannel().
    @param   eventType    One of the types that channel declared.

    @warning An event on a channel or type that was never declared is dropped,
             silently. The type is matched exactly, so a difference in case is a type
             that was never declared. The list has to reach the host first, too, since an event
             names both by position rather than by name.

    @code
      Blaeck.writeEvent(F("Activity"), F("idle_warning"));
    @endcode
  */
  void writeEvent(const char *channelName, const __FlashStringHelper *eventType);

  // The same one, named with F().
  void writeEvent(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType);

  // ----- Data Write -----

  /*!
    @brief   Stores a signal's value and sends it at once, by name.

    Independent of the timed interval, so a sketch can push a value the moment
    something happens - and, on a device no host ever activates, stream nothing
    the rest of the time.

    @param   signalName  Name the signal was added under.
    @param   value       What to store and send.
    @note    Writing both edges of a momentary signal here, rather than asking a host
             to time it out, is what makes the stored data say how long the value was
             true. A host-side timer leaves the table holding one reading, with the
             duration existing nowhere but that host.

    @code
      if (triggered && !Pulse)
      {
        Pulse = true;
        pulseSince = millis();
        Blaeck.write("Pulse", Pulse);
      }
      if (Pulse && millis() - pulseSince >= 2000)
      {
        Pulse = false;
        Blaeck.write("Pulse", Pulse);
      }
    @endcode
  */
  void write(const char *signalName, bool value);
  void write(const char *signalName, byte value);
  void write(const char *signalName, short value);
  void write(const char *signalName, unsigned short value);
  void write(const char *signalName, int value);
  void write(const char *signalName, unsigned int value);
  void write(const char *signalName, long value);
  void write(const char *signalName, unsigned long value);
  void write(const char *signalName, float value);
  void write(const char *signalName, double value);
  void write(const char *signalName, const char *value);


  void write(const char *signalName, bool value, unsigned long long timestamp);
  void write(const char *signalName, byte value, unsigned long long timestamp);
  void write(const char *signalName, short value, unsigned long long timestamp);
  void write(const char *signalName, unsigned short value, unsigned long long timestamp);
  void write(const char *signalName, int value, unsigned long long timestamp);
  void write(const char *signalName, unsigned int value, unsigned long long timestamp);
  void write(const char *signalName, long value, unsigned long long timestamp);
  void write(const char *signalName, unsigned long value, unsigned long long timestamp);
  void write(const char *signalName, float value, unsigned long long timestamp);
  void write(const char *signalName, double value, unsigned long long timestamp);
  void write(const char *signalName, const char *value, unsigned long long timestamp);

  /*!
    @brief   Finds a registered signal's index by name.

    Resolve once in setup() and use the by-index calls on anything that runs often:
    a by-name call walks every signal comparing names, which costs more the more
    signals there are.

    @param   signalName  The name the signal was added with.
    @return  Its index, or -1 if there is no signal by that name.

    @code
      int tempIndex = Blaeck.findSignalIndex("Temperature");
      Blaeck.write(tempIndex, readSensor());
    @endcode
  */
  int findSignalIndex(const char *signalName);

  // Update value and write directly - by index
  void write(int signalIndex, bool value);
  void write(int signalIndex, byte value);
  void write(int signalIndex, short value);
  void write(int signalIndex, unsigned short value);
  void write(int signalIndex, int value);
  void write(int signalIndex, unsigned int value);
  void write(int signalIndex, long value);
  void write(int signalIndex, unsigned long value);
  void write(int signalIndex, float value);
  void write(int signalIndex, double value);
  void write(int signalIndex, const char *value);


  void write(int signalIndex, bool value, unsigned long long timestamp);
  void write(int signalIndex, byte value, unsigned long long timestamp);
  void write(int signalIndex, short value, unsigned long long timestamp);
  void write(int signalIndex, unsigned short value, unsigned long long timestamp);
  void write(int signalIndex, int value, unsigned long long timestamp);
  void write(int signalIndex, unsigned int value, unsigned long long timestamp);
  void write(int signalIndex, long value, unsigned long long timestamp);
  void write(int signalIndex, unsigned long value, unsigned long long timestamp);
  void write(int signalIndex, float value, unsigned long long timestamp);
  void write(int signalIndex, double value, unsigned long long timestamp);
  void write(int signalIndex, const char *value, unsigned long long timestamp);

  // ----- Data Update -----

  /*!
    @brief   Stores a new value and marks the signal as changed, without sending.

    What writeUpdatedData() and tickUpdated() then carry. Use it where values change
    rarely and sending an unchanged one is waste; use write() to send immediately.

    @param   signalName  The name the signal was added with.
    @param   value       The new value. One overload per type.

    @code
      Blaeck.update("Temperature", readSensor());
      Blaeck.tickUpdated();
    @endcode
  */
  void update(const char *signalName, bool value);
  void update(const char *signalName, byte value);
  void update(const char *signalName, short value);
  void update(const char *signalName, unsigned short value);
  void update(const char *signalName, int value);
  void update(const char *signalName, unsigned int value);
  void update(const char *signalName, long value);
  void update(const char *signalName, unsigned long value);
  void update(const char *signalName, float value);
  void update(const char *signalName, double value);
  // Text, matching the write() form. The buffer is not copied: the signal is repointed at it,
  // so it has to stay alive until the next tickUpdated() sends it - longer than write() needs,
  // which sends before it returns.
  void update(const char *signalName, const char *value);

  // Update value and mark Signal as updated - by index
  void update(int signalIndex, bool value);
  void update(int signalIndex, byte value);
  void update(int signalIndex, short value);
  void update(int signalIndex, unsigned short value);
  void update(int signalIndex, int value);
  void update(int signalIndex, unsigned int value);
  void update(int signalIndex, long value);
  void update(int signalIndex, unsigned long value);
  void update(int signalIndex, float value);
  void update(int signalIndex, double value);
  void update(int signalIndex, const char *value);

  // ----- Mark Signals as Updated -----

  /*!
    @brief   Marks a signal as changed without changing its value.

    writeUpdatedData() carries only marked signals. update() marks one as it writes
    the new value; this marks one whose variable the sketch has already set itself.

    @param   signalIndex  Position of the signal, as findSignalIndex() returns it.

    @code
      Temperature = readSensor();
      Blaeck.markSignalUpdated("Temperature");
    @endcode
  */
  void markSignalUpdated(int signalIndex);
  void markSignalUpdated(const char *signalName);

  /*!
    @brief   Marks every signal, so the next writeUpdatedData() carries them all.

    For the first write after a host connects, where "what changed" is not yet a
    question it can have an answer to.

    @code
      Blaeck.markAllSignalsUpdated();
      Blaeck.writeUpdatedData();
    @endcode
  */
  void markAllSignalsUpdated();

  /*!
    @brief   Clears every mark, discarding changes rather than sending them.

    The next writeUpdatedData() then carries nothing until something is marked again.

    @code
      Blaeck.clearAllUpdateFlags();
    @endcode
  */
  void clearAllUpdateFlags();

  /*!
    @brief   Reports whether any signal is marked as changed.

    @return  True if the next writeUpdatedData() would carry something.

    @code
      if (Blaeck.hasUpdatedSignals())
        Blaeck.writeUpdatedData();
    @endcode
  */
  bool hasUpdatedSignals();

  // ----- Data Write All -----

  /*!
    @brief   Sends every signal's current value now, whatever the interval says.

    Answers <BLAECK.WRITE_DATA> when a host asks. A sketch calls it to send on an
    occasion of its own - a threshold crossing, say - rather than on a schedule.

    @code
      if (Temperature > 40.0f)
        Blaeck.writeAllData();
    @endcode
  */
  void writeAllData();

  /*!
    @brief   Sends every signal, timestamped by the caller.

    For a sketch holding a better clock than the timestamp callback reaches, or one
    writing values it recorded earlier.

    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.writeAllData(1723600000000000ULL);
    @endcode
  */
  void writeAllData(unsigned long long timestamp);

  /*!
    @brief   Sends every signal, but only once the interval has elapsed.

    Returns having done nothing until it is due, so it is safe to call on every pass
    of loop(). This is the half of tick() that writes; call it directly for a device
    that sends data but answers no commands.

    The interval is whatever the host asked for with BLAECK.ACTIVATE, readable with
    getIntervalMs().

    @code
      void loop()
      {
        Temperature = readSensor();
        Blaeck.timedWriteAllData();
      }
    @endcode
  */
  void timedWriteAllData();

  /*!
    @brief   Sends every signal when due, timestamped by the caller.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.timedWriteAllData(1723600000000000ULL);
    @endcode
  */
  void timedWriteAllData(unsigned long long timestamp);

  // ----- Data Write Updated -----

  /*!
    @brief   Sends only the signals that changed, and clears their marks.

    Carries the signals marked by update() or markSignalUpdated() since the last
    write. For a device whose values change rarely: an unchanged signal costs
    nothing, where writeAllData() sends all of them every time.

    @code
      Blaeck.update("Temperature", readSensor());
      Blaeck.writeUpdatedData();
    @endcode
  */
  void writeUpdatedData();

  /*!
    @brief   Sends the changed signals, timestamped by the caller.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.writeUpdatedData(1723600000000000ULL);
    @endcode
  */
  void writeUpdatedData(unsigned long long timestamp);

  /*!
    @brief   Sends the changed signals, but only once the interval has elapsed.

    Safe to call on every pass of loop(). This is the half of tickUpdated() that writes.

    @code
      void loop()
      {
        Blaeck.timedWriteUpdatedData();
      }
    @endcode
  */
  void timedWriteUpdatedData();

  /*!
    @brief   Sends the changed signals when due, timestamped by the caller.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.timedWriteUpdatedData(1723600000000000ULL);
    @endcode
  */
  void timedWriteUpdatedData(unsigned long long timestamp);

  // ----- Tick -----

  /*!
    @brief   Reads whatever arrived, then sends the signals if the interval is due.

    The one call most sketches need in loop(). Equivalent to read() followed by
    timedWriteAllData(), so a device that answers commands and sends no data of its
    own wants read() alone.

    @code
      void loop()
      {
        Temperature = readSensor();
        Blaeck.tick();
      }
    @endcode
  */
  void tick();

  /*!
    @brief   Reads whatever arrived, then sends only the signals that changed.

    As tick(), but an unchanged signal costs nothing - where tick() sends every
    signal on every interval.

    @code
      void loop()
      {
        Blaeck.update("Temperature", readSensor());
        Blaeck.tickUpdated();
      }
    @endcode
  */
  void tickUpdated();

  // ----- Timed Data -----

  /*!
    @brief   Reports how often timed data is being sent, in milliseconds.

    The host owns this: it is whatever the last BLAECK.ACTIVATE asked for. A sketch
    cannot set it, so this is how a sketch observes a decision made on its behalf -
    to show it on a state channel, or to keep it across a power cut.

    @return  The interval in ms. Meaningless while isTimedDataActive() is false,
             where it holds whatever was last asked for.

    @code
      if (Blaeck.isTimedDataActive())
        Serial.println(Blaeck.getIntervalMs());
    @endcode
  */
  unsigned long getIntervalMs() const { return _timedInterval_ms; }
  /*!
    @brief   Reports whether timed data is being sent at all.

    Switched by BLAECK.ACTIVATE and BLAECK.DEACTIVATE, and by nothing on the device.

    @return  True between an ACTIVATE and the DEACTIVATE that ends it.

    @code
      if (!Blaeck.isTimedDataActive())
        Serial.println(F("nobody has asked for data yet"));
    @endcode
  */
  bool isTimedDataActive() const { return _timedActivated; }

  // ----- Read  -----

  /*!
    @brief   Reads whatever has arrived and dispatches it.

    Handles the built-in BLAECK.* commands and the handlers a sketch registered.
    Writes no data of its own, so this is what a device that answers commands but
    logs nothing calls in loop() - anything logging calls tick() instead.

    Returns as soon as there is nothing to read, so it is safe on every pass.

    @code
      void loop()
      {
        Blaeck.read();
      }
    @endcode
  */
  void read();

  // ----- Command callback  -----

  /*!
    @brief   Registers a command whose parameters the sketch parses itself.

    Nothing is declared about the value, so a host knows the command exists but
    cannot build a control for it. Use a typed helper - onNumberCommand(),
    onSwitchCommand() and the rest - where it should appear as a control.

    @param   command  Name a host sends to invoke it. It may not begin with `#` or `@`,
                      which open a received frame's prefix section, nor with `BLAECK.`,
                      which is the library's own namespace. Such a name is refused
                      rather than registered where nothing can reach it cleanly.
    @param   handler  Called with the raw parameters, whatever they are.

    @note    No registration returns anything to check. A command that fails to
             register - table full, name too long, a reserved name, a null argument -
             is reported on the debug stream and counted, so one look at
             hasRejectedCommands() after them all answers for every command,
             whichever helper declared it.

    @code
      Blaeck.onCommand("SwitchLED", onSwitchLED);
    @endcode
  */
  void onCommand(const char *command, BlaeckCommandHandler handler);
  /*!
    @brief   Registers a handler that runs for every command.

    On top of whichever registered handler matched - not instead of one, and not
    only for the unmatched. For logging or forwarding what arrives.

    @param   handler  Called for every command, after any matching handler.

    @note    It also decides how an unknown command is answered. With a catch-all
             installed, one that matched nothing is acknowledged as accepted, on the
             grounds that this handler saw it. Without one it is answered as
             unknown, so a host can tell.

    @code
      Blaeck.onAnyCommand(onAny);
    @endcode
  */
  void onAnyCommand(BlaeckAnyCommandHandler handler);

  /*!
    @brief   Forgets every registered command, the catch-all included.

    Leaves the table empty and its capacity untouched, for a device that re-declares
    what it offers while running.

    Also releases the state channels those commands claimed with withOwnState(),
    which nothing could reach once their command is gone. Clearing this table and the
    state channels empties both, in either order.

    @note    Both lists are sent unasked afterwards: the commands, and the state
             channels released with them.

    @code
      Blaeck.clearAllCommandHandlers();
      Blaeck.onSwitchCommand("LED", onLED);
    @endcode
  */
  void clearAllCommandHandlers();

  /*!
    @brief   Reports whether any command failed to register.

    @return  True if at least one was dropped.

    @code
      if (Blaeck.hasRejectedCommands())
        Serial.println(F("Raise withCommands() on the begin() chain."));
    @endcode
  */
  bool hasRejectedCommands() const { return _rejectedCommandCount > 0; }

  /*!
    @brief   Counts the commands that failed to register.

    @return  How many were dropped, where hasRejectedCommands() only says whether
             any were.

    @code
      Serial.println(Blaeck.getRejectedCommandCount());
    @endcode
  */
  uint16_t getRejectedCommandCount() const { return _rejectedCommandCount; }

  /*!
    @brief   Compares a string in RAM against one kept in flash, copying neither.

    A command handler is handed a plain const char*, and comparing one with a literal
    puts that literal in SRAM for the life of the sketch. F() leaves it in flash, and
    this reads it a byte at a time.

    @param   ram    The string to compare, typically a handler's command parameter.
    @param   flash  An F() or PROGMEM literal.
    @return  True if the two match.
    @note    Call it through the object rather than the class. The library uses it for
             its own BLAECK.* names, which keeps about 196 bytes off SRAM on AVR - a
             tenth of an Uno - for text that never changes.

    @code
      Blaeck.onAnyCommand([](const char *command, const char *const *params, byte count)
      {
        if (Blaeck.equalsFlash(command, F("RESET")))
          Uptime = 0;
      });
    @endcode
  */
  static bool equalsFlash(const char *ram, const __FlashStringHelper *flash);

  /*!
    @brief   Formats a float into a buffer, without printf or the heap.

    For text a sketch composes itself - a status line carrying several values in one
    string. For a value a host should render, prefer a signal or a typed state
    channel and let the host format it.

    Needed because "%f" prints "?" on AVR unless float support is linked in, and
    dtostrf() is not on every core. Unlike dtostrf() this takes the buffer size, so
    it cannot be made to overrun.

    @param   value     The number to format.
    @param   decimals  Places after the point.
    @param   out       Buffer to write into.
    @param   outSize   Size of that buffer, terminator included.
    @return  out, so it can be handed straight to snprintf().

    @note    Exact to about seven significant digits, which is all a float holds.
             Asking for more may differ from printf in the last place - the same is
             true of the core on AVR, where double is float.

    @code
      char freq[10];
      Blaeck.toText(Frequency, 2, freq, sizeof(freq));
    @endcode
  */
  static char *toText(float value, byte decimals, char *out, byte outSize);

  /*!
    @brief   Copies a name out of flash into a buffer.

    Public because a sketch has the same problem the library does: reading a flash
    address as if it were RAM gives silent rubbish on AVR rather than failing.

    @param   flash    The F() literal to read.
    @param   out      Buffer to copy into. Truncated at outSize - 1, always
                      terminated.
    @param   outSize  Size of that buffer, terminator included.
    @return  How many characters were written.

    @code
      char name[16];
      Blaeck.copyFlashName(F("Temperature"), name, sizeof(name));
    @endcode
  */
  static byte copyFlashName(const __FlashStringHelper *flash, char *out, byte outSize);

  // Stores a channel name: a flash pointer as it stands, a RAM name as a copy this library
  // owns. Mirrors _setSignalName, including why the pointer is tested before the flag.
  static bool _setChannelName(const char *&slot, bool &inFlash, const char *ram, const __FlashStringHelper *flash);
  // Equality against a stored channel name, whichever memory it lives in.
  static bool _channelNameEquals(const char *stored, bool inFlash, const char *candidate);
  static bool _channelNameEqualsFlash(const char *stored, bool inFlash, const __FlashStringHelper *candidate);

  /*!
    @brief   Reports whether any state channel could not be declared.

    A full table, a name too long, or a name a command already owns.

    @return  True if at least one was dropped.

    @note    A command's withOwnState() channel counts here. When it cannot be
             declared the command keeps no state at all, so a full state channel
             table costs a control's value rather than just a channel.

    @code
      if (Blaeck.hasRejectedStateChannels())
        Serial.println(F("Raise withStateChannels() on the begin() chain."));
    @endcode
  */
  bool hasRejectedStateChannels() const { return _rejectedStateChannelCount > 0; }
  /*!
    @brief   Counts the state channels that could not be declared.

    @return  How many were dropped, where hasRejectedStateChannels() only says
             whether any were.

    @code
      Serial.println(Blaeck.getRejectedStateChannelCount());
    @endcode
  */
  uint16_t getRejectedStateChannelCount() const { return _rejectedStateChannelCount; }

  /*!
    @brief   Reports whether any event channel or type could not be declared.

    Counted together because a type belongs to a channel, so either answer points to
    the same place: withEventChannels() or withEventTypes() on the begin() chain.
    A debug stream says which.

    @return  True if at least one channel or type was dropped.

    @code
      if (Blaeck.hasRejectedEventChannels())
        Blaeck.printRejections(&Serial);
    @endcode
  */
  bool hasRejectedEventChannels() const
  {
    return _rejectedEventChannelCount > 0 || _rejectedEventTypeCount > 0;
  }
  /*!
    @brief   Counts the event channels and types that could not be declared.

    @return  Channels and types added together, for the reason they are asked about
             together. printRejections() names the begin() call to raise.

    @code
      Serial.println(Blaeck.getRejectedEventChannelCount());
    @endcode
  */
  uint16_t getRejectedEventChannelCount() const
  {
    return (uint16_t)(_rejectedEventChannelCount + _rejectedEventTypeCount);
  }

  /*!
    @brief   Reports whether anything at all was dropped, across every table.

    One question covering signals, state channels, event channels, event types and
    commands.

    @return  True if any table had no room for something.

    @code
      if (Blaeck.hasRejections())
        Blaeck.printRejections(&Serial);
    @endcode
  */
  bool hasRejections() const;
  /*!
    @brief   Prints what was dropped and the begin() call that would have kept it.

    One line per table, and nothing at all when there is nothing to report - so a
    sketch can end setup() with it unconditionally.

    @param   out  Where to print. Meant for the stream the sketch already talks on,
                  including the one the library itself uses: called from setup() it
                  cannot land inside a frame, since nothing has been written yet.
    @return  True if anything was printed.
    @note    A debug stream reports the same thing as it happens, naming each dropped
             entry. This is the summary for a board with only one Serial.

    @code
      Blaeck.printRejections(&Serial);
    @endcode
  */
  bool printRejections(Stream *out);

  // ----- Typed command registration (Home Assistant discovery metadata) -----
  // Same runtime behavior as onCommand(), but the returned handle describes the control so the
  // device can declare it in a 0xA0 "Command List" frame (BLAECK.WRITE_COMMANDS):
  //
  //   Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
  //       .withRange(0.0f, 2.0f, 0.01f)
  //       .withUnit(F("Hz"))
  //       .withStateFromSignal(F("Frequency"));
  //
  // The kind is the factory's name because it decides the entity and is not optional; every
  // modifier is. Each helper hands back the handle for its own kind, so a modifier that does
  // not apply - a range on a text command - does not compile rather than quietly doing nothing.
  //
  // All metadata strings must be F()/PROGMEM literals with program lifetime: they are stored as
  // pointers, never copied.
  //
  // What the firmware validates before dispatch, reporting each on DebugRef: values outside a
  // declared [min,max], bad select indices, and non-0/1 switch values. A number and a select
  // carry metadata their control cannot be built without - the bounds and the option list - so
  // those two helpers hand back a handle offering only that call, and the rest of the chain
  // opens once it is made. Dropping the handle entirely is the one way past it, which
  // BLAECK_NODISCARD asks the compiler to warn about and the catalog writer reports.

  /*!
    @brief   Registers a command taking a number.

    The handler reads atof(params[0]).

    @param   command  Name a host sends to invoke it.
    @param   handler  Called once a value has been accepted.
    @return  Handle offering withRange(), which returns the full one. Chainable.

    @note    The range comes first because a number is bounded by definition: the rest
             of the modifiers live on the handle withRange() returns, so the limits
             cannot be left out by ending the chain early. The value still has to be a
             number - text that is not one is refused whichever range was declared.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
          .withRange(0.0f, 2.0f, 0.01f)
          .withUnit(F("Hz"));
    @endcode
  */
  BLAECK_NODISCARD BlaeckNumberCommandNeedsRange onNumberCommand(const char *command, BlaeckCommandHandler handler);

  /*!
    @brief   Registers a command that is on or off.

    The handler receives "0" or "1"; anything else is rejected before it runs, so
    there is nothing to guard against.

    @param   command  Name a host sends to invoke it.
    @param   handler  Called once a value has been accepted.
    @return  Handle describing the control. Chainable.

    @code
      Blaeck.onSwitchCommand("SET_ENABLE", onSetEnable)
          .withOwnState(F("Enabled"), &Enabled);
    @endcode
  */
  BlaeckSwitchCommandRef onSwitchCommand(const char *command, BlaeckCommandHandler handler);

  /*!
    @brief   Registers a command choosing from a list.

    A host may send an option by name or by index; the handler always receives the
    index, so it reads atoi(params[0]) either way.

    @param   command  Name a host sends to invoke it.
    @param   handler  Called once a value has been accepted.
    @return  Handle offering withOptions(), which returns the full one. Chainable.

    @note    The list comes first because it is what a select is: every value is
             validated against it, so without one nothing can be accepted and a host
             has nothing to offer. The rest of the modifiers live on the handle
             withOptions() returns, so the list cannot be left out.

    @code
      Blaeck.onSelectCommand("SET_WAVE", onSetWave)
          .withOptions(F("Sine,Square,Triangle,Sawtooth"))
          .withOwnState(F("Wave"), &waveIndex);
    @endcode
  */
  BLAECK_NODISCARD BlaeckSelectCommandNeedsOptions onSelectCommand(const char *command, BlaeckCommandHandler handler);

  /*!
    @brief   Registers a command that is just a press.

    It carries no value, so the handler runs with no parameters.

    @param   command  Name a host sends to invoke it.
    @param   handler  Called on each press.
    @return  Handle describing the control. Chainable.

    @code
      Blaeck.onButtonCommand("STATUS", onStatus);
    @endcode
  */
  BlaeckButtonCommandRef onButtonCommand(const char *command, BlaeckCommandHandler handler);

  /*!
    @brief   Registers a command taking free text.

    The handler receives it percent-decoded and no longer than withMaxLength() said,
    so it can copy what it is given.

    @param   command  Name a host sends to invoke it.
    @param   handler  Called once a value has been accepted.
    @return  Handle describing the control. Chainable.

    @code
      Blaeck.onTextCommand("SET_LABEL", onSetLabel)
          .withMaxLength(sizeof(DeviceLabel) - 1)
          .config();
    @endcode
  */
  BlaeckTextCommandRef onTextCommand(const char *command, BlaeckCommandHandler handler);

  /*!
    @brief   Copies the option at a given position from a select command's list.

    Lets a sketch show what is selected without keeping its own copy of the names.

    @param   command  Name the select command was registered with.
    @param   index    Position in the list given to withOptions(), counting from 0.
    @param   out      Buffer the name is copied into. Left empty unless true is returned.
    @param   outSize  Size of that buffer, terminator included.
    @return  True if the name was copied. False if the command is not a select, the
             index is past the end of the list, or the name would not fit.
    @note    A name too long for the buffer is refused rather than shortened: a
             truncated name would not match any option the device declared.

    @code
      char name[12];
      Blaeck.getSelectOptionNameAt("SET_WAVE", waveIndex, name, sizeof(name));
    @endcode
  */
  bool getSelectOptionNameAt(const char *command, byte index, char *out, byte outSize) const;

  /*!
    @brief   Finds the position of a named option in a select command's list.

    The other direction to getSelectOptionNameAt(). Matching is exact, the same rule an
    incoming command value is matched by: two options differing only in case are two
    entries a host lists separately, so both have to be reachable.

    @param   command     Name the select command was registered with.
    @param   optionName  Option to look for.
    @return  Its position in the list given to withOptions(), counting from 0. -1 if
             that command is not a declared select, or has no option of that name.
    @note    Mainly for a setting restored from storage. An index means something only
             against the exact list it came from: reorder the options in a later
             firmware and a stored index quietly selects something else. A stored name
             survives that, and one since removed returns -1 rather than landing on
             whichever option took its place.

    @code
      char saved[12];
      EEPROM.get(addr, saved);
      long i = Blaeck.getSelectOptionIndexOf("SET_WAVE", saved);
      waveIndex = (i >= 0) ? (byte)i : 0;
    @endcode
  */
  long getSelectOptionIndexOf(const char *command, const char *optionName) const;

  /*!
    @brief   Names a function to call just before signal data is written.

    Runs in normal loop context, so Serial and delay() are safe. For sampling
    something at the moment it is about to be sent rather than on a timer of its own.

    @param   callback  Called before each data write.

    @code
      Blaeck.setBeforeWriteCallback(readAllSensors);
    @endcode
  */
  void setBeforeWriteCallback(void (*callback)());

  /*!
    @brief   Chooses what timestamps the data a device sends.

    BLAECK_NO_TIMESTAMP is the default and sends none, leaving a host to time the
    arrival. BLAECK_MICROS needs nothing further - the library supplies micros()
    itself, tracking the overflow so the count keeps climbing past the ~71 minutes a
    uint32 of microseconds holds. BLAECK_UNIX needs a clock only the sketch can
    reach, so pass one to setTimestampCallback().

    @param   mode  One of the three; see BlaeckTimestampMode.

    @warning Switching mode restarts the overflow tracking, so call it in setup()
             rather than partway through a log: the timestamps either side of the
             change do not belong on one axis.

    @note    BLAECK_MICROS tracks the wrap as data is written, so a device
             writing less often than every ~71 minutes misses one and wants
             BLAECK_UNIX with a real clock instead.

    @code
      Blaeck.setTimestampMode(BLAECK_MICROS);
    @endcode
  */
  void setTimestampMode(BlaeckTimestampMode mode);

  /*!
    @brief   Names the clock to read for BLAECK_UNIX.

    An RTC, an NTP-backed time, whatever the board has.

    @param   callback  Returns microseconds since the Unix epoch.

    @note    Set it either side of setTimestampMode(BLAECK_UNIX) - that mode keeps a
             callback already given rather than replacing it.

    @code
      Blaeck.setTimestampCallback(unixMicros);
      Blaeck.setTimestampMode(BLAECK_UNIX);
    @endcode
  */
  void setTimestampCallback(unsigned long long (*callback)());

  /*!
    @brief   Reports the timestamp mode in force.

    @return  The mode as last set. What was asked for, not whether it can be
             honoured - a BLAECK_UNIX with no callback still reads back as
             BLAECK_UNIX.

    @code
      if (Blaeck.getTimestampMode() == BLAECK_NO_TIMESTAMP)
        Serial.println(F("data carries no time"));
    @endcode
  */
  BlaeckTimestampMode getTimestampMode() const { return _timestampMode; }

  /*!
    @brief   Reports whether the data will actually carry a timestamp.

    @return  True if a mode is set and a callback exists to read.

    @warning This is the one to check. BLAECK_UNIX without a callback stamps zero
             rather than failing, so every value lands at the epoch.

    @code
      if (!Blaeck.hasValidTimestampCallback())
        Serial.println(F("no clock - timestamps will be zero"));
    @endcode
  */
  bool hasValidTimestampCallback() const;

  /*!
    @brief   Chooses whether data is assembled in RAM before being sent.

    Buffering costs SRAM and sends each write in one go; writing straight out costs
    nothing and sends it piecemeal. Off by default on AVR, on everywhere else.

    @param   enabled  True to assemble in RAM first.

    @code
      Blaeck.setBufferedWrites(true);
    @endcode
  */
  void setBufferedWrites(bool enabled);
  /*!
    @brief   Reports whether data is assembled in RAM before sending.

    @return  True if buffering is on, whether set by setBufferedWrites() or by the
             platform default - worth checking on AVR, where it is off unless asked
             for.

    @code
      Serial.println(Blaeck.isBufferedWrites() ? F("buffered") : F("direct"));
    @endcode
  */
  bool isBufferedWrites() const { return _bufferedWrites; }

private:
  unsigned long long getTimeStamp();
  void setSignalName(int signalIndex, const char *signalName);
  // Points a slot at its name: a copy when ram is given, the flash address itself when
  // flash is. Frees whatever copy the slot held, so a reused slot cannot leak and the two
  // kinds cannot be mixed up. One of the two arguments is null.
  void _setSignalName(int signalIndex, const char *ram, const __FlashStringHelper *flash);
  // Frees everything the signal table owns and the table itself does not hold: name
  // copies - flash names own nothing, so they are skipped - and metadata records.
  // Walks the table by _signalCapacity, so it must run while that still describes the
  // allocated table - before begin() resets the capacity for the next one.
  void _freeSignalOwned();
#if BLAECK_ENABLE_SIGNAL_META
  // The metadata record for a signal, allocated on first use, because a signal that
  // describes nothing should not pay for the fields that would describe it. Returns null
  // when the handle is dead or there is no room, which every caller treats as "store
  // nothing" - the signal itself is unaffected either way.
  SignalMeta *_ensureSignalMeta(int16_t index);
#endif
  // The two kinds, read the one way that is right for each. Every use of a name goes
  // through these: the schema hash, both catalog writers, and the by-name lookups.
  bool _signalNameEquals(const Signal &s, const char *name) const;
  // Where a name's bytes are going. One walk serves all three writers, so a name cannot
  // be sent one way and hashed another.
  enum NameSink : uint8_t
  {
    NAME_SINK_BUFFER,
    NAME_SINK_STREAM,
    NAME_SINK_HASH
  };
  void _emitSignalName(const Signal &s, NameSink sink);
  void _emitNameByte(byte c, NameSink sink);
  // The suffix as decimal text; out must hold three chars. Static because it reads only
  // the entry, and shared so a name is matched, hashed and sent as the same bytes.
  static byte _signalSuffixDigits(const Signal &s, char *out);
  void _signalNameFeedHash(const Signal &s);
  void _bufSignalName0(const Signal &s);
  void _printSignalName(const Signal &s);
  void _setTimedDataState(bool timedActivated, unsigned long timedInterval_ms);
  void _parseCommandTokens(const char *raw);
  // Acts on what _parseCommandTokens() last produced; read() parses each frame once and
  // both the built-in commands and the registered handlers work from that.
  void _dispatchRegisteredHandlers(bool sendAck = true);

  // Send a 0xA5 Command Ack frame (cmdHash + status + reason) to the serial host.
  // The hash is FNV-1a of the exact received frame bytes. The frame carries no
  // CRC (like 0xA0).
  void _writeCommandAck(const char *rawCommand, byte status, byte reasonCode);
  static uint32_t _fnv1a32(const char *s);
  uint16_t _computeSchemaHash();
  inline void _schemaHashFeedByte(byte b)
  {
    _schemaHashAccum ^= ((uint16_t)b << 8);
    for (byte k = 0; k < 8; k++)
    {
      if (_schemaHashAccum & 0x8000)
        _schemaHashAccum = (_schemaHashAccum << 1) ^ 0x1021;
      else
        _schemaHashAccum <<= 1;
    }
  }

  void timedWriteData(unsigned long messageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp);
  void tick(unsigned long messageID, bool onlyUpdated);

  void writeAllData(unsigned long messageID, unsigned long long timestamp);

  // Store a value as the signal's declared type, converting; false when there is no such signal
  // or it carries text. Three, because no one C++ type carries the other ten without loss.
  bool _storeSigned(int signalIndex, long value);
  bool _storeUnsigned(int signalIndex, unsigned long value);
  bool _storeFloating(int signalIndex, double value);

  void writeData(unsigned long messageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp);
  void writeDataFrame(unsigned long MessageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp);

  // Stamp the id off the request that asked for them. Private: only the dispatcher has one,
  // and a sketch sending a catalog unasked calls the public no-argument form.
  void writeRestarted(unsigned long messageID);
  void writeDevices(unsigned long messageID);
  void writeSymbols(unsigned long messageID);
  void writeSignalConfig(unsigned long messageID);
  void writeCommands(unsigned long messageID);
  void writeStateChannels(unsigned long messageID);
  void writeEventChannels(unsigned long messageID);

  void writeSymbolsFrame(unsigned long MessageID);
#if BLAECK_ENABLE_SIGNAL_META
  void writeSignalConfigFrame(unsigned long MessageID);
#endif
  // The one place a signal is appended. The eleven public overloads differ only in
  // the datatype they record, so they all land here rather than repeating the
  // capacity check and the overflow bookkeeping eleven times.
  int _registerSignal(const char *signalName, dataType type, void *address);
  // The flash-named half of the same thing. Separate rather than a flag on the one above,
  // because the two names are read differently everywhere they are read.
  int _registerSignal(const __FlashStringHelper *signalName, dataType type, void *address);
  // What both of those do; only the name differs, and one of ram/flash is null.
  int _registerSignalCommon(const char *ram, const __FlashStringHelper *flash,
                            dataType type, void *address);
  // The one place a command is registered. Returns the handler table index, or -1 when the
  // command was rejected - table full, name too long, or a null argument - having counted it
  // and said so on DebugRef. The typed helpers hand that index to their handle, so a modifier
  // writes to the entry directly instead of looking it up by name again.
  int _registerCommand(const char *command, BlaeckCommandHandler handler, uint8_t kind);
  // Clears an entry's metadata to the defaults for its kind. Registering a name twice replaces
  // the command outright, so what the previous declaration said must not survive.
  void _resetCommandMeta(uint16_t handlerIndex, uint8_t kind);
  // The one place a channel is declared, mirroring _registerCommand(): the table index, or -1
  // having counted the rejection and said why on DebugRef. Re-declaring a name returns its
  // existing slot with the metadata cleared, so what a previous declaration said cannot linger.
  // An event channel keeps its already-declared types and their indices.
  // The type and the variable are settled at registration because they come from which
  // overload the sketch called, not from a modifier a handle could offer - which is what
  // keeps a channel from ever holding a type its value does not match.
  // Takes the name in whichever memory the caller has it: exactly one of channelName and
  // flashName is set. A flash name is stored as a pointer, so it costs the entry nothing
  // and never truncates; a RAM name is copied, and still cannot exceed the old limit.
  int _registerStateChannel(const char *channelName, const __FlashStringHelper *flashName, dataType valueType = Blaeck_string,
                              const void *value = nullptr);
  // Exactly one of channelName and flashName is set; see _registerStateChannel.
  int _registerEventChannel(const char *channelName, const __FlashStringHelper *flashName, const __FlashStringHelper *eventTypes);
  // Appends one pool entry per field of a comma-separated list, in order, so a field's position
  // is its wire index - the same rule call order gives addEventType().
  void _addEventTypesCsv(uint16_t channelIndex, const __FlashStringHelper *eventTypes);
#if BLAECK_ENABLE_COMMAND_META
  void writeCommandsFrame(unsigned long MessageID);
  byte _validateTypedCommand(uint16_t handlerIndex);
  // Declares the channel a typed command owns. Separate from addStateChannel() so that one
  // can refuse an owned name outright rather than needing a "unless it is mine" exception.
  bool _addOwnedStateChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText,
                             dataType valueType = Blaeck_string, const void *value = nullptr);
  // Declares and announces the channel a command carries itself, for withOwnState(). Announcing
  // at registration is what corrects a host that was already connected when the board reset.
  // False when the channel could not be declared, so withOwnState() can leave the command
  // without state rather than advertising a channel absent from the 0x90 catalog.
  bool _declareOwnState(uint16_t handlerIndex, const __FlashStringHelper *channelName,
                        BlaeckStateTextGetter getStateText, dataType valueType, const void *value,
                        bool selectIndex = false);
  bool _declareOwnState(uint16_t handlerIndex, const __FlashStringHelper *channelName,
                        BlaeckStateTextGetter getStateText);

  static void _percentDecodeInPlace(char *s);
  static long _flashCsvIndexOf(const __FlashStringHelper *csv, const char *value);
#endif
  // Number of comma-separated fields in a flash CSV. Deliberately outside the
  // command-metadata guard: it counts a select command's options and an event
  // channel's type list, and those features are enabled independently.
  static uint16_t _flashCsvOptionCount(const __FlashStringHelper *csv);

  // Whether any field of a flash CSV is empty or only blank space. A closed set fixes each
  // member's wire index by position, so a blank field cannot be dropped - that would renumber
  // every field after it, and those numbers are what events and select values are carried as -
  // and it cannot be chosen or reported either. Refused at declaration instead, which is the
  // one moment the sketch that wrote it is still what is being talked about.
  static bool _flashCsvHasBlankField(const __FlashStringHelper *csv);
#if BLAECK_ENABLE_STATE_CHANNELS
  void writeStateChannelsFrame(unsigned long MessageID);
  // Index of a declared channel, or -1 when the name was never declared.
  int _findStateChannel(const char *channelName) const;
  int _findStateChannel(const __FlashStringHelper *channelName) const;
#endif
#if BLAECK_ENABLE_EVENTS
  void writeEventChannelsFrame(unsigned long MessageID);
  // Index of a declared event channel, or -1 when the name was never declared.
  int _findEventChannel(const char *channelName) const;
  int _findEventChannel(const __FlashStringHelper *channelName) const;
  // Position of an event type within its own channel's list, or -1 when that
  // channel never declared it.
  int _findEventType(uint16_t channelIndex, const __FlashStringHelper *eventType) const;
  // Byte-wise equality of two PROGMEM strings. Needed because strcmp_P() reads
  // its first argument from RAM, which silently mismatches when both operands
  // are flash pointers.
  static bool _flashStringEquals(const __FlashStringHelper *a, const __FlashStringHelper *b);
#endif

  void writeDevicesFrame(unsigned long MessageID);

  // Emits a catalog frame with no entries. Used by the disabled builds of the
  // optional catalogs so a polling host gets an immediate "nothing here"
  // instead of waiting out its timeout: the host cannot tell from the library
  // version alone whether a feature was compiled out.
  void _writeEmptyFrame(byte msgKey, unsigned long msg_id);

  static void validatePlatformSizes();

  Stream *StreamRef;
  Stream *_debugStream = nullptr;
  Signal *Signals = nullptr;
  // Allocates the signal table on first use.
  bool _ensureSignalTable();
  int _signalIndex = 0;
  unsigned int _signalCapacity = 0;
  bool _signalRegistrationFailed = false;
  uint16_t _rejectedSignalCount = 0;
#if BLAECK_ENABLE_SIGNAL_META
  // Metadata records are allocated as they are described, so a heap too full costs a
  // description rather than a signal. No table size cures it, which is why it is
  // counted apart from the others and reported in its own words.
  uint16_t _rejectedSignalMetaCount = 0;
#endif
  uint16_t _rejectedCommandCount = 0;
  uint16_t _rejectedStateChannelCount = 0;
  uint16_t _rejectedEventChannelCount = 0;
  // The type pool is its own table, so what it drops needs its own count - the
  // cure is withEventTypes(), not withEventChannels().
  uint16_t _rejectedEventTypeCount = 0;

  bool _writeRestartedAlreadyDone = false;
  bool _sendRestartFlag = true;
  // True only while a data frame requested by BLAECK.WRITE_DATA is being written. Set around
  // that one call rather than passed down, because the value is wanted at the bottom of a
  // chain of overloads that all have their own public signatures.
  bool _frameRequested = false;

  // The data frame's flags byte, built in one place so the buffered writer and the direct one
  // cannot disagree about it. Bit 0 says this is the first frame after a restart, bit 1 that
  // the frame answers a request rather than the interval a host set. Bits 2-7 are reserved and
  // sent clear.
  byte _frameFlags(bool restarted) const
  {
    return (byte)((restarted ? 0x01 : 0x00) | (_frameRequested ? 0x02 : 0x00));
  }

  // Micros overflow tracking for D2 (uint64 timestamp)
  unsigned long _prevMicros = 0;
  unsigned long long _overflowCount = 0;

  bool _timedActivated = false;
  bool _timedFirstTime = true;
  unsigned long _timedFirstTimeDone_ms = 0;
  unsigned long _timedSetPoint_ms = 0;
  unsigned long _timedInterval_ms = 1000;

  // ── Table sizes ───────────────────────────────────────────────────
  // Which table a capacity call names. One setter for all of them, so the rule
  // about setting a capacity too late is written once.
  enum TableId
  {
    TABLE_SIGNALS,
    TABLE_STATE_CHANNELS,
    TABLE_EVENT_CHANNELS,
    TABLE_EVENT_TYPES,
    TABLE_COMMANDS
  };
  void _setTableCapacity(TableId table, unsigned int count);
  // Says what was dropped and the exact call that would have kept it. Table
  // names the chain method, e.g. F("withSignals").
  void _warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
                      const char *droppedName);
  void _warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
                      const __FlashStringHelper *droppedName);
  // One line of printRejections(), for a table that dropped something.
  void _printRejectionLine(Stream *out, const __FlashStringHelper *what,
                           const __FlashStringHelper *chainCall, uint16_t dropped,
                           unsigned int capacity);

  // The most any table will hold, whatever a sketch asks for. Not a number anyone
  // chose: every handle carries its slot as an int16_t and spends the negatives on
  // "registration failed", so INT16_MAX is the last slot one can name. The find
  // functions answer the same way, with a signed int and -1, and int is 16 bits on AVR.
  //
  // The same on every board, because it is bookkeeping rather than rationing, and RAM
  // says no long first: this many state channels is 852 KB of entries, where an ESP32
  // has 320 KB and a Mega 8. A board that cannot afford a table hears about it when the
  // allocation fails, which is a truer limit than any constant. What does vary per
  // board is where a table starts, below.
  static const uint16_t MAX_TABLE_ENTRIES = INT16_MAX;

  // Defaults a table starts from, raised per sketch on the begin() chain:
  // BLAECK.begin(&Serial).withSignals(50). Generous where SRAM is plentiful
  // and careful where it is not, because a table is allocated in full by the
  // first entry added to it - lazy allocation spares an unused table, not an
  // unused slot.
#if defined(__AVR__)
  #if defined(RAMEND) && (RAMEND >= 0x10FF)
    static const unsigned int DEFAULT_SIGNALS = 24;
    static const byte DEFAULT_STATE_CHANNELS = 8;
    static const byte DEFAULT_EVENT_CHANNELS = 6;
    static const byte DEFAULT_EVENT_TYPES = 20;
    static const byte DEFAULT_COMMANDS = 16;
  #else
    static const unsigned int DEFAULT_SIGNALS = 8;
    static const byte DEFAULT_STATE_CHANNELS = 3;
    static const byte DEFAULT_EVENT_CHANNELS = 2;
    static const byte DEFAULT_EVENT_TYPES = 8;
    static const byte DEFAULT_COMMANDS = 6;
  #endif
#else
  static const unsigned int DEFAULT_SIGNALS = 64;
  static const byte DEFAULT_STATE_CHANNELS = 32;
  static const byte DEFAULT_EVENT_CHANNELS = 24;
  static const byte DEFAULT_EVENT_TYPES = 64;
  static const byte DEFAULT_COMMANDS = 32;
#endif

  // Name widths stay fixed: they size a member inside an entry, not the number
  // of entries, so no chain call can change one without changing the layout of
  // a table that may already exist.
  static const int MAXIMUM_CHAR_COUNT = BLAECK_COMMAND_MAX_CHARS_DEFAULT;
  static const byte MAX_COMMAND_PARAM_COUNT = 10;
  static const byte MAX_COMMAND_NAME_COUNT = blaeck_detail::MAX_COMMAND_NAME_COUNT;
  // The parse buffer is compared against two kinds of name: a registered command, which
  // _registerCommand refuses outright above MAX_COMMAND_NAME_COUNT, and a built-in, of
  // which BLAECK.WRITE_STATE_CHANNELS is currently the longest at 27 characters. It has to
  // fit whichever is larger. Sizing it off the user-facing limit instead would cost four
  // bytes in every handler table entry rather than four once, and would widen how long a
  // name a sketch may register - a promise that could not be taken back.
  static const byte MAX_BUILTIN_COMMAND_COUNT = 28;
  // Measured against the list itself, not a copy of one name, so adding a longer built-in
  // fails the build here rather than going quietly unreachable.
#define BLAECK_ASSERT_BUILTIN_FITS(name)                        \
  static_assert(sizeof(name) <= MAX_BUILTIN_COMMAND_COUNT,      \
                "MAX_BUILTIN_COMMAND_COUNT must fit every name in BLAECK_BUILTIN_COMMAND_LIST");
  BLAECK_BUILTIN_COMMAND_LIST(BLAECK_ASSERT_BUILTIN_FITS)
#undef BLAECK_ASSERT_BUILTIN_FITS
  static const byte MAX_PARSED_COMMAND_COUNT =
      MAX_COMMAND_NAME_COUNT > MAX_BUILTIN_COMMAND_COUNT ? MAX_COMMAND_NAME_COUNT
                                                         : MAX_BUILTIN_COMMAND_COUNT;
// Declared whether or not state channels are compiled in, because StateChannelEntry
// is - see there.
#if defined(__AVR__)
  static const byte MAX_STATE_NAME_COUNT = 16;
#else
  static const byte MAX_STATE_NAME_COUNT = 32;
#endif
// Declared whether or not events are compiled in: the F() overloads size a buffer from
// it and stay callable either way, the same as the state constant above.
#if defined(__AVR__)
  static const byte MAX_EVENT_NAME_COUNT = 16;
#else
  static const byte MAX_EVENT_NAME_COUNT = 32;
#endif
  char receivedChars[MAXIMUM_CHAR_COUNT];

  CRC32 _crc;
  uint16_t _schemaHash = 0;
  uint16_t _schemaHashAccum = 0;

  // ── Buffered writes ───────────────────────────────────────────────
  bool _bufferedWrites = BLAECK_BUFFERED_WRITES_DEFAULT;
  byte *_frameBuf = nullptr;
  int _framePos = 0;
  int _frameBufSize = 0;
  bool _bufOverflow = false;
  bool _bufOverflowWarned = false;

  void _bufAllocate();
  // True when a frame may be assembled in RAM. The buffer is built here, at the
  // first buffered write rather than in begin(), so a sketch that never writes
  // a frame never pays for it and the size can follow the signals actually
  // added.
  bool _bufReady()
  {
    if (!_bufferedWrites)
      return false;
    if (_frameBuf == nullptr)
      _bufAllocate();
    return _frameBuf != nullptr;
  }
  bool _bufEnsure(size_t addLen);
  void _bufFree();
  void _bufReset()
  {
    _framePos = 0;
    _bufOverflow = false;
    _bufOverflowWarned = false;
  }
  void _bufByte(byte b)
  {
    if (_bufEnsure(1))
      _frameBuf[_framePos++] = b;
    else
      _bufOverflow = true;
  }
  void _bufBytes(const byte *data, size_t len)
  {
    if (_bufEnsure(len))
    {
      memcpy(_frameBuf + _framePos, data, len);
      _framePos += len;
    }
    else
      _bufOverflow = true;
  }
  void _bufStr(const char *s)
  {
    // Null reads as empty, the same as Arduino's Print does with a null char* - so a
    // DeviceName left unset writes an empty field rather than walking off address zero.
    if (s == nullptr)
      return;
    size_t n = strlen(s);
    if (_bufEnsure(n))
    {
      memcpy(_frameBuf + _framePos, s, n);
      _framePos += n;
    }
    else
      _bufOverflow = true;
  }
  void _bufStr0(const char *s)
  {
    _bufStr(s);
    _bufByte(0);
  }
  void _bufFlashStr(const __FlashStringHelper *s)
  {
    if (s == nullptr)
      return;
    PGM_P p = reinterpret_cast<PGM_P>(s);
    byte c;
    while ((c = pgm_read_byte(p++)) != 0)
      _bufByte(c);
  }
  void _bufFlashStr0(const __FlashStringHelper *s)
  {
    _bufFlashStr(s);
    _bufByte(0);
  }
  void _bufSend()
  {
    if (_bufOverflow)
    {
      if (!_bufOverflowWarned && _debugStream != nullptr)
      {
        _debugStream->println("Buffered frame exceeds available memory; frame dropped.");
        _bufOverflowWarned = true;
      }
      return;
    }
    StreamRef->write(_frameBuf, _framePos);
    StreamRef->flush();
  }
  void _bufHeader(byte msgKey, unsigned long msgId);
  void _bufFooter()
  {
    _bufStr("/BLAECK>\r\n");
  }
  void _bufDevice(const char *name, const char *hw, const char *fw);

  static unsigned long long _microsWrapper()
  {
    return (unsigned long long)micros();
  }

  typedef blaeck_detail::CommandHandlerEntry CommandHandlerEntry;
  CommandHandlerEntry *_commandHandlers = nullptr;
  uint16_t _commandCapacity = DEFAULT_COMMANDS;
  // Slots that exist right now: the capacity once the table has been built, and
  // zero before that. Every loop over a table is bounded by this, so a table
  // that was never needed - or that the board had no RAM for - simply has
  // nothing to walk.
  uint16_t _commandSlots() const { return _commandHandlers != nullptr ? _commandCapacity : 0; }
  // Allocates the table on first use. False when the board had no RAM for it,
  // which is reported the same way a full table is.
  bool _ensureCommandTable();
  // Declared even with BLAECK_ENABLE_STATE_CHANNELS=0, when no table of these is ever
  // built: BlaeckStateRefBase::_entry() names the type in its return type, and the
  // withIcon()/diagnostic()/... chain names its fields - all of which has to keep
  // compiling so a sketch needs no #ifdef around what it writes. A type on its own
  // costs nothing; only the table below is compiled away.
  typedef blaeck_detail::StateChannelEntry StateChannelEntry;

  // A command is declared whether or not command metadata is compiled in, so this flag is
  // not guarded the way the other three are.
  bool _commandCatalogDirty = false;
#if BLAECK_ENABLE_SIGNAL_META
  bool _signalConfigDirty = false;
#endif

  // Sends every catalog whose declarations have moved since the host last saw them, and
  // nothing otherwise. Called where a change can have happened, and ahead of a push, which
  // is where one would do harm unannounced.
  //
  // Deliberately never for signals. A changed signal list mid-session is not a legal change -
  // a host has fixed its storage layout around the list it was given - and announcing one
  // could let it adopt the new schema and go on writing into a table whose columns no longer
  // describe it. deleteSignals() says to send the symbols by hand, and means it.
  void _flushCatalogs();

  // Wire code for a datatype, the same 0x00-0x0A a 0xB0 symbol carries. Declared out here
  // with the type above, and for the same reason: the symbol list and the schema hash call
  // it whether or not this board was built with state channels.
  static byte _dtypeCode(dataType t);
#if BLAECK_ENABLE_STATE_CHANNELS
  StateChannelEntry *_stateChannels = nullptr;
  uint16_t _stateChannelCapacity = DEFAULT_STATE_CHANNELS;
  uint16_t _stateChannelSlots() const { return _stateChannels != nullptr ? _stateChannelCapacity : 0; }
  bool _ensureStateChannelTable();
  // The text a channel reports, or nullptr when it has none: its getter, the text its
  // stateValue points at, or the option an index names. Resolved into `buf` only in the
  // last case; the others hand back a pointer they already had.
  const char *_channelText(const StateChannelEntry &e, char *buf, byte bufSize) const;

  // The opening of every complaint about a channel: the sentence, then the channel's own name
  // read the way it was stored. Four warnings differ only in what follows, and reading a flash
  // name as if it were RAM is silent garbage on AVR rather than a crash, so the branch is
  // written once here. Says nothing when no debug stream is attached.
  void _debugChannel(const __FlashStringHelper *prefix, const StateChannelEntry &e) const;

  // A select reporting an option by name rather than by index: checked against the list the
  // command declared, using the same matcher the command topic is read with, so what a device
  // reports and what it accepts cannot drift apart. Returns nullptr for a name that is not on
  // the list, and complains once.
  const char *_checkedSelectName(const StateChannelEntry &e, const char *text) const;

  // The 0x90 flag word for one channel. Both writer paths call this so the bits are decided
  // once: the buffered and unbuffered writers are otherwise the same code twice, and a flag
  // added to only one of them would make a board's catalog depend on how it was configured.
  uint16_t _stateChannelFlags(const StateChannelEntry &e, bool hasStateValue) const;


  // Lays a numeric channel's value into out (never more than 8 bytes) and returns the width.
  // Goes through the same converter unions the data writer uses, so a value is identical on
  // the wire whether it arrives as a signal or as a channel - including where a platform's
  // double is narrower than the union that carries it. Returns 0 for a string, which the
  // callers length-prefix or NUL-terminate themselves, and for a channel with no value.
  byte _channelValueBytes(const StateChannelEntry &e, byte *out);


  // Equality between a flash string and a RAM one. strcmp_P reads its FIRST argument from RAM,
  // which is the wrong way round here, so the flash side is read with pgm_read_byte.
  static bool _flashStringEqualsName(const __FlashStringHelper *flashName, const char *name);
  // The 0x95 frame itself, by channel index. Reached by writeState() after its guards and by
  // writeCommandState() for a channel those guards deliberately refuse.
  void _writeStateFrame(int channelIndex, const char *text, const byte *pushed = nullptr, byte pushedLen = 0);
  // A pushed number as the bytes of the declared type; see the definition for why one switch.
  byte _valueBytes(dataType declared, long s, unsigned long u, double d, byte *out);
  // The guards every writeState() push passes through, text and numeric alike; -1 to refuse.
  int _stateChannelForPush(const char *channelName, bool wantText);
  void _writeStateNumber(const char *channelName, long s, unsigned long u, double d);
#endif
#if BLAECK_ENABLE_EVENTS
  typedef blaeck_detail::EventChannelEntry EventChannelEntry;
  EventChannelEntry *_eventChannels = nullptr;
  uint16_t _eventChannelCapacity = DEFAULT_EVENT_CHANNELS;
  uint16_t _eventChannelSlots() const { return _eventChannels != nullptr ? _eventChannelCapacity : 0; }
  bool _ensureEventChannelTable();

  // One pool shared by every channel: each entry records which channel owns it,
  // so a channel with many types and one with few both fit without reserving a
  // per-channel array. Appended in call order, which is what defines the index
  // sent in the 0x85 frame.
  typedef blaeck_detail::EventTypeEntry EventTypeEntry;
  static const byte WHOLE_STRING = blaeck_detail::WHOLE_STRING;

  // Where this entry's name starts in its flash string, and how long it is. The whole
  // string for a WHOLE_STRING entry, else the field'th comma-separated field.
  static void _eventTypeExtent(const EventTypeEntry &e, unsigned int &start, unsigned int &len);
  // Whether the entry's name equals eventType. Both live in flash, so neither strcmp()
  // nor strcmp_P() applies - the same reason _findEventType reads with pgm_read_byte().
  static bool _eventTypeEquals(const EventTypeEntry &e, const __FlashStringHelper *eventType);
  // The entry's name, NUL-terminated, into the frame buffer. Declared here rather than
  // beside the other _buf helpers because it needs EventTypeEntry, declared just above.
  void _bufEventType0(const EventTypeEntry &e);
  EventTypeEntry *_eventTypes = nullptr;
  uint16_t _eventTypeCapacity = DEFAULT_EVENT_TYPES;
  uint16_t _eventTypeSlots() const { return _eventTypes != nullptr ? _eventTypeCapacity : 0; }
  bool _ensureEventTypeTable();
  uint16_t _eventTypeCount = 0;
#endif
  BlaeckAnyCommandHandler _anyCommandHandler = nullptr;
  char _parsedTokenBuffer[MAXIMUM_CHAR_COUNT] = {0};
  char _parsedCommand[MAX_PARSED_COMMAND_COUNT] = {0};
  const char *_parsedParamPtrs[MAX_COMMAND_PARAM_COUNT] = {0};
  byte _parsedParamCount = 0;
  // Set when the frame did not fit: strncpy shortened it, or the argument list hit the cap.
  // What was parsed is then not what was sent, so no handler may act on it.
  bool _parsedTruncated = false;
  // Set while a frame is being received, once its characters no longer fit and are being
  // dropped. The receive loop is the only place that can see it happen.
  bool _receiveOverflowed = false;
  // The message id the sender put in the frame's '#' prefix, echoed in the 0xA5 header so an
  // ack can be paired with the command it answers - the same field, and the same meaning, that
  // a BLAECK.* response echoes from its parameters. 0 when the frame carried none, which is
  // also what a sender may not send.
  uint16_t _parsedPrefixMsgId = 0;
  // How many characters of the received frame the prefix section took. The ack hashes what
  // follows it, so the hash covers the command as written rather than as addressed.
  byte _parsedPrefixLen = 0;
#if BLAECK_ENABLE_STATE_CHANNELS
  // Set when this catalog no longer describes what the host was last told, cleared by any
  // send of it. See _flushCatalogs().
  bool _stateCatalogDirty = false;
#endif
#if BLAECK_ENABLE_EVENTS
  bool _eventCatalogDirty = false;
#endif
#if BLAECK_ENABLE_COMMAND_META
  // Scratch buffer holding a select command's normalized index string, so a
  // name payload (e.g. from a Home Assistant select) is handed to index-based
  // handlers as its numeric index.
  char _selectIndexScratch[8] = {0};
#endif
  bool recvWithStartEndMarkers();

  void (*_beforeWriteCallback)() = nullptr;

  BlaeckTimestampMode _timestampMode = BLAECK_NO_TIMESTAMP;
  unsigned long long (*_timestampCallback)() = nullptr;

  union
  {
    bool val;
    byte bval[1];
  } boolCvt;

  union
  {
    short val;
    byte bval[2];
  } shortCvt;

  union
  {
    unsigned short val;
    byte bval[2];
  } ushortCvt;

  union
  {
    int val;
    byte bval[2];
  } intCvt;

  union
  {
    unsigned int val;
    byte bval[2];
  } uintCvt;

  union
  {
    long val;
    byte bval[4];
  } lngCvt;

  union
  {
    unsigned long val;
    byte bval[4];
  } ulngCvt;

  union
  {
    unsigned long long val;
    byte bval[8];
  } ullCvt;

  union
  {
    float val;
    byte bval[4];
  } fltCvt;

  union
  {
    double val;
    byte bval[8];
  } dblCvt;

  friend class BlaeckSignalRefBase;
  friend bool blaeck_detail::optionsAccepted(const __FlashStringHelper *, Stream *,
                                             const char *, bool);
  friend class BlaeckCommandRefBase;
  friend class BlaeckStateRefBase;
  friend class BlaeckEventChannelRef;
  friend class BlaeckBeginRef;
};

// ----- BlaeckBeginRef bodies -----
// They sit here because each one reaches into BlaeckSerial's privates, so it has to
// be complete first. The class itself is declared above BlaeckSerial, where begin()
// names it as a return type, and that is where these are documented.
//
// The blank line below matters: without it this note attaches to withSignals() as
// its documentation, and the hover shows this instead of what the declaration says.

// A capacity is nearly always a literal, and a literal above the cap is a mistake the
// build can catch instead of the board. GCC drops the call when the condition is false,
// and fails the link with this text when it holds - so a sketch hears about it before it
// is flashed, rather than from a debug stream that may not be attached yet.
//
// Only what is certain: RAM is the limit that actually bites, but no single table knows
// what the other four, the sketch and the stack have already claimed. 32767 is knowable
// here, so 32767 is what is checked.
#if defined(__GNUC__) && !defined(__clang__)
extern void blaeck_capacity_above_32767() __attribute__((error(
    "BLAECK: a table capacity above 32767 cannot be indexed - a handle names its slot "
    "with an int16_t. Ask for 32767 or fewer.")));
  #define BLAECK_CHECK_CAPACITY(count)                                     \
    do {                                                                   \
      if (__builtin_constant_p(count) &&                                   \
          (unsigned long)(count) > (unsigned long)BlaeckSerial::MAX_TABLE_ENTRIES) \
        blaeck_capacity_above_32767();                                     \
    } while (0)
#else
  #define BLAECK_CHECK_CAPACITY(count) do { } while (0)
#endif

inline BlaeckBeginRef &BlaeckBeginRef::withSignals(unsigned int count)
{
  BLAECK_CHECK_CAPACITY(count);
  if (_owner != nullptr)
    _owner->_setTableCapacity(BlaeckSerial::TABLE_SIGNALS, count);
  return *this;
}

inline BlaeckBeginRef &BlaeckBeginRef::withStateChannels(unsigned int count)
{
  BLAECK_CHECK_CAPACITY(count);
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_owner != nullptr)
    _owner->_setTableCapacity(BlaeckSerial::TABLE_STATE_CHANNELS, count);
#else
  (void)count;
#endif
  return *this;
}

inline BlaeckBeginRef &BlaeckBeginRef::withEventChannels(unsigned int count)
{
  BLAECK_CHECK_CAPACITY(count);
#if BLAECK_ENABLE_EVENTS
  if (_owner != nullptr)
    _owner->_setTableCapacity(BlaeckSerial::TABLE_EVENT_CHANNELS, count);
#else
  (void)count;
#endif
  return *this;
}

inline BlaeckBeginRef &BlaeckBeginRef::withEventTypes(unsigned int count)
{
  BLAECK_CHECK_CAPACITY(count);
#if BLAECK_ENABLE_EVENTS
  if (_owner != nullptr)
    _owner->_setTableCapacity(BlaeckSerial::TABLE_EVENT_TYPES, count);
#else
  (void)count;
#endif
  return *this;
}

inline BlaeckBeginRef &BlaeckBeginRef::withCommands(unsigned int count)
{
  BLAECK_CHECK_CAPACITY(count);
  if (_owner != nullptr)
    _owner->_setTableCapacity(BlaeckSerial::TABLE_COMMANDS, count);
  return *this;
}

inline BlaeckBeginRef &BlaeckBeginRef::withDebugStream(Stream *debugStream)
{
  if (_owner != nullptr)
    _owner->_debugStream = debugStream;
  return *this;
}




// ----- Bodies that reach into BlaeckSerial -----
// Their classes are declared above it, so that the types begin(), addSignal() and the
// onXCommand() helpers name as return values are complete where those are declared.
// Only the bodies have to wait for BlaeckSerial itself.

inline blaeck_detail::CommandHandlerEntry * BlaeckCommandRefBase::_entry() const
{
  if (_owner == nullptr || _index < 0)
    return nullptr;
  return &_owner->_commandHandlers[_index];
}

inline void BlaeckCommandRefBase::_markDirty() const
{
  if (_owner != nullptr)
    _owner->_commandCatalogDirty = true;
}

inline void BlaeckCommandRefBase::_warnRangeIgnored(float mn, float mx) const
{
#if BLAECK_ENABLE_COMMAND_META
  if (_owner == nullptr || _owner->_debugStream == nullptr)
    return;
  _owner->_debugStream->print(F("withRange ignored, max must be above min: "));
  if (auto *e = _entry())
  {
    _owner->_debugStream->print(e->command);
    _owner->_debugStream->print(' ');
  }
  _owner->_debugStream->print('[');
  _owner->_debugStream->print(mn);
  _owner->_debugStream->print(F(", "));
  _owner->_debugStream->print(mx);
  _owner->_debugStream->println(F("]. Any value is accepted and no range is declared."));
#else
  (void)mn;
  (void)mx;
#endif
}

inline void BlaeckCommandRefBase::_warnStepIgnored(float st) const
{
#if BLAECK_ENABLE_COMMAND_META
  if (_owner == nullptr || _owner->_debugStream == nullptr)
    return;
  _owner->_debugStream->print(F("step ignored, must be above zero: "));
  if (auto *e = _entry())
  {
    _owner->_debugStream->print(e->command);
    _owner->_debugStream->print(' ');
  }
  _owner->_debugStream->print(st);
  _owner->_debugStream->println(F(". No resolution is declared and the host chooses one."));
#else
  (void)st;
#endif
}

inline void BlaeckCommandRefBase::_warnStepTooFine(float st) const
{
#if BLAECK_ENABLE_COMMAND_META
  if (_owner == nullptr || _owner->_debugStream == nullptr)
    return;
  _owner->_debugStream->print(F("step below 0.001: "));
  if (auto *e = _entry())
  {
    _owner->_debugStream->print(e->command);
    _owner->_debugStream->print(' ');
  }
  // Six places, because the default two print the very value being complained about as 0.00.
  _owner->_debugStream->print(st, 6);
  _owner->_debugStream->println(F(". Sent as declared, but Home Assistant refuses the whole "
                                  "control rather than only the step."));
#else
  (void)st;
#endif
}

inline void BlaeckCommandRefBase::_warnMaxLengthTooLong(unsigned int maxLength) const
{
#if BLAECK_ENABLE_COMMAND_META
  if (_owner == nullptr || _owner->_debugStream == nullptr)
    return;
  _owner->_debugStream->print(F("max length above 255: "));
  if (auto *e = _entry())
  {
    _owner->_debugStream->print(e->command);
    _owner->_debugStream->print(' ');
  }
  _owner->_debugStream->print(maxLength);
  _owner->_debugStream->println(F(". Ignored, and 255 kept: Home Assistant caps an entity's "
                                  "state at 255 characters and refuses the control outright "
                                  "above it."));
#else
  (void)maxLength;
#endif
}

inline bool BlaeckCommandRefBase::_optionsAccepted(const __FlashStringHelper *optionsCsv) const
{
#if BLAECK_ENABLE_COMMAND_META
  auto *e = _entry();
  return blaeck_detail::optionsAccepted(optionsCsv,
                                        _owner != nullptr ? _owner->_debugStream : nullptr,
                                        e != nullptr ? e->command : nullptr, false);
#else
  (void)optionsCsv;
  return false;
#endif
}

inline void BlaeckCommandRefBase::_setOwnState(const __FlashStringHelper *channelName,
                                               dataType valueType, const void *value,
                                               bool selectIndex)
{
#if BLAECK_ENABLE_COMMAND_META
  if (auto *e = _entry())
  {
    if (_owner->_declareOwnState((uint16_t)_index, channelName, nullptr, valueType, value, selectIndex))
    {
      e->stateSignal = channelName;
      e->stateSource = BLAECK_STATE_CHANNEL;
    }
  }
#else
  (void)channelName;
  (void)valueType;
  (void)value;
  (void)selectIndex;
#endif
}

inline void BlaeckCommandRefBase::_setOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText)
{
#if BLAECK_ENABLE_COMMAND_META
  if (auto *e = _entry())
  {
    // Only claim the state if the channel was actually declared. Advertising one the 0x90
    // catalog does not carry would wire a control to a topic nothing ever publishes, and it
    // would sit at unknown forever - a full table costs the state, not the command.
    if (_owner->_declareOwnState((uint16_t)_index, channelName, getStateText))
    {
      e->stateSignal = channelName;
      e->stateSource = BLAECK_STATE_CHANNEL;
    }
  }
#else
  (void)channelName;
  (void)getStateText;
#endif
}

inline void BlaeckSignalRefBase::_setFlash(const __FlashStringHelper *value, uint16_t bit)
{
#if BLAECK_ENABLE_SIGNAL_META
  // Before anything else, so an empty literal takes the same path as leaving the call out.
  if (blaeck_detail::flashStrEmpty(value))
    value = nullptr;
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    // Together, so the bit and the field can never disagree.
    const __FlashStringHelper **slot;
    switch (bit)
    {
    case BLAECK_SIG_HAS_UNIT:         slot = &m->Unit; break;
    case BLAECK_SIG_HAS_DEVICE_CLASS: slot = &m->DeviceClass; break;
    case BLAECK_SIG_HAS_DISPLAY_NAME: slot = &m->DisplayName; break;
    default:                          slot = &m->Icon; break;
    }
    const uint16_t flags = (value != nullptr) ? (uint16_t)(m->MetaFlags | bit)
                                              : (uint16_t)(m->MetaFlags & ~bit);

    // Compared before it is written, not after. A modifier is idempotent, so a sketch may
    // reasonably call one every pass through loop() - withIcon(running ? A : B) - and a flag
    // set on assignment rather than on change would announce the whole catalog every pass.
    if (*slot != value || m->MetaFlags != flags)
    {
      *slot = value;
      m->MetaFlags = flags;
      _owner->_signalConfigDirty = true;
    }
  }
#else
  (void)value;
  (void)bit;
#endif
}

inline void BlaeckSignalRefBase::_setBit(uint16_t bit, bool on)
{
#if BLAECK_ENABLE_SIGNAL_META
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    const uint16_t flags = on ? (uint16_t)(m->MetaFlags | bit)
                              : (uint16_t)(m->MetaFlags & ~bit);
    if (m->MetaFlags != flags)
    {
      m->MetaFlags = flags;
      _owner->_signalConfigDirty = true;
    }
  }
#else
  (void)bit;
  (void)on;
#endif
}

inline void BlaeckSignalRefBase::_setStateClass(BlaeckStateClass stateClass)
{
#if BLAECK_ENABLE_SIGNAL_META
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    const uint16_t flags =
        (uint16_t)((m->MetaFlags & ~BLAECK_SIG_STATE_CLASS_MASK) |
                   (((uint16_t)stateClass << BLAECK_SIG_STATE_CLASS_SHIFT) &
                    BLAECK_SIG_STATE_CLASS_MASK));
    if (m->MetaFlags != flags)
    {
      m->MetaFlags = flags;
      _owner->_signalConfigDirty = true;
    }
  }
#else
  (void)stateClass;
#endif
}

inline void BlaeckSignalRefBase::_setOptions(const __FlashStringHelper *optionsCsv)
{
#if BLAECK_ENABLE_SIGNAL_META
  // Refused rather than stored, so a list that is not one leaves the signal as it was.
  if (_owner == nullptr || _index < 0 || _index >= _owner->_signalIndex ||
      !blaeck_detail::optionsAccepted(optionsCsv, _owner->_debugStream,
                                      _owner->Signals[_index].SignalName,
                                      _owner->Signals[_index].NameInFlash))
    return;
  if (SignalMeta *m = _owner->_ensureSignalMeta(_index))
  {
    const uint16_t flags = (optionsCsv != nullptr)
                               ? (uint16_t)(m->MetaFlags | BLAECK_SIG_HAS_OPTIONS)
                               : (uint16_t)(m->MetaFlags & ~BLAECK_SIG_HAS_OPTIONS);
    if (m->Options != optionsCsv || m->MetaFlags != flags)
    {
      m->Options = optionsCsv;
      m->MetaFlags = flags;
      _owner->_signalConfigDirty = true;
    }
  }
#else
  (void)optionsCsv;
#endif
}

inline void BlaeckSignalRefBase::_setDisplayPrecision(uint8_t decimals)
{
#if BLAECK_ENABLE_SIGNAL_META
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    const uint16_t flags = (uint16_t)(m->MetaFlags | BLAECK_SIG_HAS_DISPLAY_PRECISION);
    if (m->DisplayPrecision != decimals || m->MetaFlags != flags)
    {
      m->DisplayPrecision = decimals;
      m->MetaFlags = flags;
      _owner->_signalConfigDirty = true;
    }
  }
#else
  (void)decimals;
#endif
}

inline void BlaeckSignalRefBase::_setNameSuffix(uint8_t suffix)
{
  if (_owner == nullptr || _index < 0 || _owner->Signals == nullptr ||
      static_cast<unsigned int>(_index) >= _owner->_signalCapacity)
    return;
  Signal &s = _owner->Signals[_index];
  s.NameSuffix = suffix;
  s.HasSuffix = 1;
  // The name is part of the schema, and this changed it after registration computed
  // the hash. Without this a host would be told the schema had not moved.
  _owner->_schemaHash = _owner->_computeSchemaHash();
}

inline blaeck_detail::StateChannelEntry * BlaeckStateRefBase::_entry() const
{
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_owner != nullptr && _index >= 0)
    return &_owner->_stateChannels[_index];
#endif
  return nullptr;
}

inline void BlaeckStateRefBase::_markDirty() const
{
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_owner != nullptr)
    _owner->_stateCatalogDirty = true;
#endif
}

inline Stream *BlaeckStateRefBase::_debugStream() const
{
  return _owner != nullptr ? _owner->_debugStream : nullptr;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::withIcon(const __FlashStringHelper *icon)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    // Compared before it is written, as on the state and command handles: a modifier
    // called every pass through loop() must not announce the catalog every pass.
    if (_owner->_eventChannels[_index].icon != icon)
    {
      _owner->_eventChannels[_index].icon = icon;
      _owner->_eventCatalogDirty = true;
    }
#else
  (void)icon;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::diagnostic(bool on)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    if (_owner->_eventChannels[_index].diagnostic != on)
    {
      _owner->_eventChannels[_index].diagnostic = on;
      _owner->_eventCatalogDirty = true;
    }
#else
  (void)on;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::withDeviceClass(const __FlashStringHelper *deviceClass)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    if (_owner->_eventChannels[_index].deviceClass != deviceClass)
    {
      _owner->_eventChannels[_index].deviceClass = deviceClass;
      _owner->_eventCatalogDirty = true;
    }
#else
  (void)deviceClass;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::disabledByDefault(bool on)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    if (_owner->_eventChannels[_index].disabledByDefault != on)
    {
      _owner->_eventChannels[_index].disabledByDefault = on;
      _owner->_eventCatalogDirty = true;
    }
#else
  (void)on;
#endif
  return *this;
}

#endif //  BLAECKSERIAL_H
