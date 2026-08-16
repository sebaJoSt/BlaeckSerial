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
//                 README.md for the three ways to do it.
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
  // two-bit field could hold only four. Bits 11-15 stay reserved.
  BLAECK_SIG_STATE_CLASS_MASK = 0x0038, // bits 3-5
  BLAECK_SIG_DIAGNOSTIC = 0x0040,
  BLAECK_SIG_DISABLED_BY_DEFAULT = 0x0080,
  BLAECK_SIG_FORCE_UPDATE = 0x0100,
  BLAECK_SIG_HAS_DISPLAY_PRECISION = 0x0200,
  BLAECK_SIG_HAS_OPTIONS = 0x0400
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

enum BlaeckIntervalMode
{
  BLAECK_INTERVAL_CLIENT = -1,
  BLAECK_INTERVAL_OFF = -2
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

// Command kind for Home Assistant discovery (0xA0 Command List frame).
enum BlaeckCommandKind
{
  BLAECK_CMD_PLAIN = 0,  // registered via onCommand(): no HA entity, but listed in 0xA0 for command palettes
  BLAECK_CMD_NUMBER = 1, // HA number   (value in [min,max])
  BLAECK_CMD_SWITCH = 2, // HA switch   (0/1)
  BLAECK_CMD_SELECT = 3, // HA select   (index into optionsCsv)
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

class BlaeckSignalRefBase;
class BlaeckNumericSignalRef;
class BlaeckTextSignalRef;
class BlaeckBoolSignalRef;
class BlaeckCommandRefBase;
class BlaeckNumberCommandRef;
class BlaeckSwitchCommandRef;
class BlaeckSelectCommandRef;
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
  const __FlashStringHelper *options = nullptr;
  const __FlashStringHelper *stateSignal = nullptr;
  uint8_t stateSource = BLAECK_STATE_SIGNAL;
  uint8_t category = BLAECK_CAT_NONE;
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
  BlaeckStateTextGetter getStateText = nullptr;
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

  void _setStateSignal(const __FlashStringHelper *signalName)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      e->stateSignal = signalName;
      e->stateSource = BLAECK_STATE_SIGNAL;
    }
#else
    (void)signalName;
#endif
  }

  // Typed own state: the command reports a variable rather than text a getter builds. Same
  // path as the getter form, so the channel is declared, claimed and announced identically -
  // only where the value comes from differs.
  void _setOwnState(const __FlashStringHelper *channelName, dataType valueType, const void *value,
                    bool selectIndex = false);

  void _setOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText);

  // Reported rather than stored silently: a range whose max is not above its min reads
  // everywhere downstream as no range at all - nothing is checked and nothing is
  // announced - so reversed arguments turn the checking off instead of tightening it.
  void _warnRangeIgnored(float mn, float mx) const;

  void _setRange(float mn, float mx, float st)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      e->meta_min = mn;
      e->meta_max = mx;
      e->meta_step = st;
      if (!(mx > mn))
        _warnRangeIgnored(mn, mx);
    }
#else
    (void)mn; (void)mx; (void)st;
#endif
  }

  void _setUnit(const __FlashStringHelper *unit)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
      e->unit = unit;
#else
    (void)unit;
#endif
  }

  void _setOptions(const __FlashStringHelper *optionsCsv)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
      e->options = optionsCsv;
#else
    (void)optionsCsv;
#endif
  }

  // Stored in meta_max, which a text command uses for nothing else.
  void _setMaxLength(unsigned int maxLength)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
      e->meta_max = (float)maxLength;
#else
    (void)maxLength;
#endif
  }

  void _setCategory(uint8_t category)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
      e->category = category;
#else
    (void)category;
#endif
  }

  BlaeckSerial *_owner;
  int16_t _index;
};

// Each kind re-exposes the modifiers that apply to it, returning its own type so the chain keeps
// its kind all the way down. The repetition is the point: this reads as a table of what each
// control accepts, and it is what an editor offers when the dot is typed.

// The four every command handle shares. A template rather than a macro so each method is a real
// declaration with a real comment: a comment inside a macro body documents nothing, because the
// compiler and the editor both see only the expansion. TYPE is the handle deriving from this, so
// each returns its own type and the chain keeps working.
template <class TYPE>
class BlaeckCommandRefShared : public BlaeckCommandRefBase
{
public:
  /*!
    @brief   Points the command at a signal that mirrors its value.

    A host then shows what the device holds rather than what was last sent, so a
    value the firmware clamped or refused is visible. Leave it out for a control that
    is assumed to have taken effect.

    @param   signalName  A signal already added with addSignal().
    @return  The same handle, for chaining.

    @code
      Blaeck.addSignal(F("LED_State"), &ledState);
      Blaeck.onSwitchCommand("LED", onLED).withStateSignal(F("LED_State"));
    @endcode
  */
  TYPE &withStateSignal(const __FlashStringHelper *signalName)
  {
    _setStateSignal(signalName);
    return _self();
  }

  /*!
    @brief   Gives the command a state channel of its own, filled by a getter.

    An alternative to withStateSignal() for a control whose value is not a logged
    measurement. The channel belongs to the command, so addStateChannel() and
    writeState() both refuse the name and the value can only come from one place.
    It needs no signals at all, so a device that logs nothing can still report what
    its controls are set to.

    Push a change with writeCommandState(); otherwise the channel is read only when
    a host asks.

    @param   channelName   Name for the channel. Takes a slot from the state channel
                           table, so count it in withStateChannels().
    @param   getStateText  Called to produce the current value as text.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_OFFSET", onSetOffset)
          .withOwnState(F("Offset"), offsetText);
    @endcode
  */
  TYPE &withOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText)
  {
    _setOwnState(channelName, getStateText);
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

protected:
  BlaeckCommandRefShared(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}

private:
  TYPE &_self() { return *static_cast<TYPE *>(this); }
};

class BlaeckNumberCommandRef : public BlaeckCommandRefShared<BlaeckNumberCommandRef>
{
public:
  BlaeckNumberCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<BlaeckNumberCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefShared<BlaeckNumberCommandRef>::withOwnState;

  /*!
    @brief   Declares the range the firmware accepts.

    A value outside it is rejected before the handler runs, so the handler can take
    what it is given. A number command that never calls this accepts anything.

    @param   min   Lowest value accepted.
    @param   max   Highest value accepted. Has to be above min: a max that is not
                   leaves the command with no range at all, accepting anything and
                   declaring nothing, which a debug stream reports.
    @param   step  Display resolution only - never rounded to, and not validated.
                   Pass 0 to leave it unsaid and let the host choose. A host may
                   refuse a step below 0.001 and raise it rather than reject the
                   whole control, so a finer resolution cannot be shown.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq).withRange(0.0f, 2.0f, 0.01f);
    @endcode
  */
  BlaeckNumberCommandRef &withRange(float min, float max, float step = 0.0f)
  {
    _setRange(min, max, step);
    return *this;
  }

  /*!
    @brief   Declares the unit shown beside the input.

    A label only: nothing is converted, and the handler is passed whatever number
    was sent.

    @param   unit  Symbol as an F() literal. Non-ASCII must be UTF-8:
                   F("\xC2\xB0" "C") is the degree sign followed by C.
    @return  The same handle, for chaining.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq).withUnit(F("Hz"));
    @endcode
  */
  BlaeckNumberCommandRef &withUnit(const __FlashStringHelper *unit)
  {
    _setUnit(unit);
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
    _setOwnState(channelName, Blaeck_int, value);
    return *this;
  }

  // Carries this command's state as an unsigned int, 16-bit on AVR and 32-bit elsewhere, read
  // directly instead of asking a getter for text. Sent typed, so the host renders the number and
  // the sketch never formats one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned int *value)
  {
    _setOwnState(channelName, Blaeck_uint, value);
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

  // Carries this command's state as a double, 32-bit on AVR and 64-bit elsewhere, read directly
  // instead of asking a getter for text. Sent typed, so the host renders the number and the
  // sketch never formats one. One overload per numeric type, as addSignal() has.
  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, double *value)
  {
    _setOwnState(channelName, Blaeck_double, value);
    return *this;
  }
};

class BlaeckSwitchCommandRef : public BlaeckCommandRefShared<BlaeckSwitchCommandRef>
{
public:
  BlaeckSwitchCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<BlaeckSwitchCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefShared<BlaeckSwitchCommandRef>::withOwnState;

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

class BlaeckSelectCommandRef : public BlaeckCommandRefShared<BlaeckSelectCommandRef>
{
public:
  BlaeckSelectCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<BlaeckSelectCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefShared<BlaeckSelectCommandRef>::withOwnState;

  /*!
    @brief   Declares the closed set of values this control accepts.

    The list lives in flash once instead of being repeated in the sketch, and the
    library validates against it: a value that is neither a listed name nor a valid
    index is rejected before the handler runs. The handler is always handed the
    index as text, whichever form the host sent, so atoi(params[0]) is enough.
    getSelectOptionNameAt() reads a name back out when one is wanted.

    @param   optionsCsv  Comma-separated names as an F() literal, in the order their
                         indices follow.
    @return  The same handle, for chaining.

    @note    Do not name an option "none" in any casing. A host may read that state as
             "no option selected" and blank the control instead of showing it.

    @code
      Blaeck.onSelectCommand("SET_WAVE", onSetWave)
          .withOptions(F("Sine,Square,Triangle,Sawtooth"));
    @endcode
  */
  BlaeckSelectCommandRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    _setOptions(optionsCsv);
    return *this;
  }

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
};

class BlaeckTextCommandRef : public BlaeckCommandRefShared<BlaeckTextCommandRef>
{
public:
  BlaeckTextCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefShared<BlaeckTextCommandRef>(owner, index) {}

  // Declaring withOwnState() below would otherwise hide the getter form inherited from the
  // base - C++ hides by name, not by signature. The macro this replaced pasted both into one
  // scope, so nothing had to say this.
  using BlaeckCommandRefShared<BlaeckTextCommandRef>::withOwnState;

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

    @param   maxLength  Limit in decoded bytes. Left unsaid it is 255.
                        sizeof(buffer) - 1 is usually the right value.
    @return  The same handle, for chaining.

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

    For something worth having available without adding to what a host shows by
    default.

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

private:
  TYPE &_self() { return *static_cast<TYPE *>(this); }
};

// Any of the nine numeric datatypes. The only shape with decimals to show and a value that
// accumulates, so it is the only one carrying a state class or a display precision.
class BlaeckNumericSignalRef : public BlaeckSignalRefShared<BlaeckNumericSignalRef>
{
public:
  BlaeckNumericSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckNumericSignalRef>(owner, index) {}

  /*!
    @brief   Declares the symbol shown after the value.

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
};

// A string signal. No unit, no decimals to round and nothing to keep statistics on: all three
// say the state is a number, and a host then refuses the text.
//
// Mirrors BlaeckTextStateRef: a string signal and a state channel become the same entity on a
// host, so they carry the same fields. Change one, change the other.
class BlaeckTextSignalRef : public BlaeckSignalRefShared<BlaeckTextSignalRef>
{
public:
  BlaeckTextSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckTextSignalRef>(owner, index) {}

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
  BlaeckBoolSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefShared<BlaeckBoolSignalRef>(owner, index) {}
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

  void _setStateClass(BlaeckStateClass stateClass)
  {
#if BLAECK_ENABLE_STATE_CHANNELS
    if (auto *e = _entry())
    {
      e->metaFlags &= (uint16_t)~BLAECK_SCH_STATE_CLASS_MASK;
      e->metaFlags |= (uint16_t)(((uint16_t)stateClass << BLAECK_SCH_STATE_CLASS_SHIFT) &
                                 BLAECK_SCH_STATE_CLASS_MASK);
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
      e->displayPrecision = decimals;
      e->metaFlags |= BLAECK_SCH_HAS_DISPLAY_PRECISION;
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
      Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:pulse"));
    @endcode
  */
  TYPE &withIcon(const __FlashStringHelper *icon)
  {
    if (auto *e = _entry())
      e->icon = icon;
    return _self();
  }

  /*!
    @brief   Files the channel as describing the device rather than its work.

    Keeps a status line out of the way of the controls and readings a dashboard is
    opened for.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:pulse")).diagnostic();
    @endcode
  */
  TYPE &diagnostic(bool on = true)
  {
    if (auto *e = _entry())
      e->diagnostic = on;
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
      Blaeck.addStateChannel(F("LastSeen")).withDeviceClass(F("timestamp"));
    @endcode
  */
  TYPE &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
    if (auto *e = _entry())
      e->deviceClass = deviceClass;
    return _self();
  }

  /*!
    @brief   Registers the channel but leaves it switched off until someone enables it.

    @param   on  Pass false to undo it, or a variable to decide at runtime.
    @return  The same handle, for chaining.

    @code
      Blaeck.addStateChannel(F("BuildInfo")).disabledByDefault();
    @endcode
  */
  TYPE &disabledByDefault(bool on = true)
  {
    if (auto *e = _entry())
      e->disabledByDefault = on;
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
      Blaeck.addStateChannel(F("Heartbeat")).forceUpdate();
    @endcode
  */
  TYPE &forceUpdate(bool on = true)
  {
    if (auto *e = _entry())
      e->forceUpdate = on;
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
    if (auto *e = _entry())
    {
      e->unit = unit;
      e->metaFlags |= BLAECK_SCH_HAS_UNIT;
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

    @code
      Blaeck.addStateChannel(F("Offset")).withStateText(offsetText);
    @endcode
  */
  BlaeckTextStateRef &withStateText(BlaeckStateTextGetter getStateText)
  {
    if (auto *e = _entry())
      e->getStateText = getStateText;
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

    @code
      Blaeck.addStateChannel(F("Mode"))
          .withDeviceClass(F("enum"))
          .withOptions(F("idle,running,fault"));
    @endcode
  */
  BlaeckTextStateRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    if (auto *e = _entry())
      e->options = optionsCsv;
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
    @brief   Announces that the device has restarted.

    Lets a host tell a rebooted device from one that has simply gone quiet - the
    values look identical otherwise. Sent once per boot, by read() on its first
    call, so a sketch needs this only to get it out before it starts reading.

    @code
      Blaeck.writeRestarted();
    @endcode
  */
  void writeRestarted();

  // As writeRestarted(), with messageID stamped into the frame header.
  void writeRestarted(unsigned long messageID);

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

  // As writeDevices(), with messageID stamped into the frame header.
  void writeDevices(unsigned long messageID);

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

  // As writeSymbols(), with messageID stamped into the frame header.
  void writeSymbols(unsigned long messageID);

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

    @code
      Blaeck.writeSignalConfig();
    @endcode
  */
  void writeSignalConfig();
  void writeSignalConfig(unsigned long messageID);

  // ----- Commands -----

  /*!
    @brief   Sends the list of commands this device accepts.

    Every registered command, plain and typed. A plain one carries only its name; a
    typed one carries what it controls - its kind, its range, its options - which is
    what lets a host build a control for it rather than just list it.
    Answers <BLAECK.WRITE_COMMANDS>.

    @code
      Blaeck.writeCommands();
    @endcode
  */
  void writeCommands();
  void writeCommands(unsigned long messageID);

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
      Blaeck.addStateChannel(F("Status")).withIcon(F("mdi:pulse")).diagnostic();
      Blaeck.addStateChannel(F("Amplitude"), &Amplitude).withUnit(F("V"));
    @endcode
  */
  BlaeckTextStateRef addStateChannel(const char *channelName);
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
  BlaeckTextStateRef addStateChannel(const __FlashStringHelper *channelName);
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

    @warning A host addresses a channel by its position in the catalog, so
             re-declaring changes what those positions mean. Follow with
             writeStateChannels(), or values are filed against the wrong channels.

    @warning This clears the channels a command claimed with withOwnState(), and
             those cannot be declared again - withOwnState() builds on the handle
             onNumberCommand() and its siblings return, so it only runs where the
             command is registered. writeCommandState() then finds no channel and
             publishes nothing. To re-declare controls, clear the commands too and
             register both together.

    @code
      Blaeck.clearAllStateChannels();
      Blaeck.addStateChannel(F("Status"));
      Blaeck.writeStateChannels();
    @endcode
  */
  void clearAllStateChannels();

  /*!
    @brief   Sends the list of declared state channels.

    Answers <BLAECK.WRITE_STATE_CHANNELS>, and is what a host needs before any value
    means anything - a value names its channel by position in this list.

    @code
      Blaeck.writeStateChannels();
    @endcode
  */
  void writeStateChannels();
  void writeStateChannels(unsigned long messageID);

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
  void writeState(const char *channelName, const char *text);
  void writeState(const char *channelName, const char *text, unsigned long messageID);

  // Report whatever the channel currently holds - the variable a typed channel points at, or
  // the text its getter builds. The only way to push a numeric channel, since there is nothing
  // for the caller to pass: the value already lives where the channel was told to look.
  void writeState(const char *channelName);
  void writeState(const char *channelName, unsigned long messageID);

  // The same four, named with F().
  void writeState(const __FlashStringHelper *channelName, const char *text);
  void writeState(const __FlashStringHelper *channelName, const char *text, unsigned long messageID);
  void writeState(const __FlashStringHelper *channelName);
  void writeState(const __FlashStringHelper *channelName, unsigned long messageID);

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
  void writeCommandState(const char *command, unsigned long messageID);

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
             without types could neither report nor be shown.

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
    @return  True if it was added. False if it does not fit, names a channel that
             was never declared, or duplicates one the channel already has - each
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

    @warning A host addresses a channel and a type by their positions, so follow
             with writeEventChannels() or reported events land under the wrong names.

    @code
      Blaeck.clearAllEventChannels();
      Blaeck.addEventChannel(F("Activity"), F("idle_warning,resumed"));
      Blaeck.writeEventChannels();
    @endcode
  */
  void clearAllEventChannels();

  /*!
    @brief   Sends the list of declared event channels and their types.

    Answers <BLAECK.WRITE_EVENT_CHANNELS>, and is what a host needs before any event
    means anything - an event names its channel and type by position in this list.

    @code
      Blaeck.writeEventChannels();
    @endcode
  */
  void writeEventChannels();
  void writeEventChannels(unsigned long messageID);

  /*!
    @brief   Reports that something happened on a declared channel.

    Fire and forget: a host may show it, but it is never stored as logged data.

    @param   channelName  A channel already passed to addEventChannel().
    @param   eventType    One of the types that channel declared.

    @warning An event on a channel or type that was never declared is dropped,
             silently. The list has to reach the host first, too, since an event
             names both by position rather than by name.

    @code
      Blaeck.writeEvent(F("Activity"), F("idle_warning"));
    @endcode
  */
  void writeEvent(const char *channelName, const __FlashStringHelper *eventType);
  void writeEvent(const char *channelName, const __FlashStringHelper *eventType, unsigned long messageID);

  // The same two, named with F().
  void writeEvent(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType);
  void writeEvent(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType, unsigned long messageID);

  // ----- Data Write -----

  /*!
    @brief   Stores a signal's value and sends it at once, by name.

    Independent of the timed interval, so a sketch can push a value the moment
    something happens and stream nothing the rest of the time - pair it with
    setIntervalMs(BLAECK_INTERVAL_OFF).

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

  void write(const char *signalName, bool value, unsigned long messageID);
  void write(const char *signalName, byte value, unsigned long messageID);
  void write(const char *signalName, short value, unsigned long messageID);
  void write(const char *signalName, unsigned short value, unsigned long messageID);
  void write(const char *signalName, int value, unsigned long messageID);
  void write(const char *signalName, unsigned int value, unsigned long messageID);
  void write(const char *signalName, long value, unsigned long messageID);
  void write(const char *signalName, unsigned long value, unsigned long messageID);
  void write(const char *signalName, float value, unsigned long messageID);
  void write(const char *signalName, double value, unsigned long messageID);
  void write(const char *signalName, const char *value, unsigned long messageID);

  void write(const char *signalName, bool value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, byte value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, short value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, unsigned short value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, int value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, unsigned int value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, long value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, unsigned long value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, float value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, double value, unsigned long messageID, unsigned long long timestamp);
  void write(const char *signalName, const char *value, unsigned long messageID, unsigned long long timestamp);

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

  void write(int signalIndex, bool value, unsigned long messageID);
  void write(int signalIndex, byte value, unsigned long messageID);
  void write(int signalIndex, short value, unsigned long messageID);
  void write(int signalIndex, unsigned short value, unsigned long messageID);
  void write(int signalIndex, int value, unsigned long messageID);
  void write(int signalIndex, unsigned int value, unsigned long messageID);
  void write(int signalIndex, long value, unsigned long messageID);
  void write(int signalIndex, unsigned long value, unsigned long messageID);
  void write(int signalIndex, float value, unsigned long messageID);
  void write(int signalIndex, double value, unsigned long messageID);
  void write(int signalIndex, const char *value, unsigned long messageID);

  void write(int signalIndex, bool value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, byte value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, short value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, unsigned short value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, int value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, unsigned int value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, long value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, unsigned long value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, float value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, double value, unsigned long messageID, unsigned long long timestamp);
  void write(int signalIndex, const char *value, unsigned long messageID, unsigned long long timestamp);

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
    @brief   Sends every signal, tagged so a host can match it to a request.

    @param   messageID  Number the host sent with its request, echoed back.

    @code
      Blaeck.writeAllData(42);
    @endcode
  */
  void writeAllData(unsigned long messageID);

  /*!
    @brief   Sends every signal, timestamped by the caller.

    For a sketch holding a better clock than the timestamp callback reaches, or one
    writing values it recorded earlier.

    @param   messageID  Number the host sent with its request, echoed back.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.writeAllData(42, 1723600000000000ULL);
    @endcode
  */
  void writeAllData(unsigned long messageID, unsigned long long timestamp);

  /*!
    @brief   Sends every signal, but only once the interval has elapsed.

    Returns having done nothing until it is due, so it is safe to call on every pass
    of loop(). This is the half of tick() that writes; call it directly for a device
    that sends data but answers no commands.

    The interval is whatever setIntervalMs() fixed, or whatever the host asked for.

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
    @brief   Sends every signal when due, tagged so a host can match it to a request.
    @param   msg_id  Number the host sent with its request, echoed back.

    @code
      Blaeck.timedWriteAllData(42);
    @endcode
  */
  void timedWriteAllData(unsigned long msg_id);

  /*!
    @brief   Sends every signal when due, timestamped by the caller.
    @param   messageID  Number the host sent with its request, echoed back.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.timedWriteAllData(42, 1723600000000000ULL);
    @endcode
  */
  void timedWriteAllData(unsigned long messageID, unsigned long long timestamp);

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
    @brief   Sends the changed signals, tagged so a host can match it to a request.
    @param   messageID  Number the host sent with its request, echoed back.

    @code
      Blaeck.writeUpdatedData(42);
    @endcode
  */
  void writeUpdatedData(unsigned long messageID);

  /*!
    @brief   Sends the changed signals, timestamped by the caller.
    @param   messageID  Number the host sent with its request, echoed back.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.writeUpdatedData(42, 1723600000000000ULL);
    @endcode
  */
  void writeUpdatedData(unsigned long messageID, unsigned long long timestamp);

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
    @brief   Sends the changed signals when due, tagged for a host's request.
    @param   msg_id  Number the host sent with its request, echoed back.

    @code
      Blaeck.timedWriteUpdatedData(42);
    @endcode
  */
  void timedWriteUpdatedData(unsigned long msg_id);

  /*!
    @brief   Sends the changed signals when due, timestamped by the caller.
    @param   messageID  Number the host sent with its request, echoed back.
    @param   timestamp  Microseconds, in whatever epoch setTimestampMode() implies.

    @code
      Blaeck.timedWriteUpdatedData(42, 1723600000000000ULL);
    @endcode
  */
  void timedWriteUpdatedData(unsigned long messageID, unsigned long long timestamp);

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
    @brief   Reads and sends as tick(), tagged so a host can match the response.

    Left to tick(), the library supplies a number saying whether the interval was
    fixed by the sketch or set by the host.

    @param   messageID  Number the host sent with its request, echoed back.

    @code
      Blaeck.tick(42);
    @endcode
  */
  void tick(unsigned long messageID);

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

  /*!
    @brief   Reads and sends changed signals, tagged for a host's request.
    @param   messageID  Number the host sent with its request, echoed back.

    @code
      Blaeck.tickUpdated(42);
    @endcode
  */
  void tickUpdated(unsigned long messageID);

  // ----- Timed Data configuruation -----

  /*!
    @brief   Fixes how often timed data is sent, or who decides.

    @param   interval_ms  A value of 0 or more locks that interval in milliseconds and
                          BLAECK.ACTIVATE / BLAECK.DEACTIVATE stop having an effect.
                          BLAECK_INTERVAL_OFF locks timed data off, ignoring ACTIVATE.
                          BLAECK_INTERVAL_CLIENT leaves it to the host, the default.
    @note    A value that is none of these is refused and the previous mode stays.

    @code
      Blaeck.setIntervalMs(BLAECK_INTERVAL_OFF);
    @endcode
  */
  void setIntervalMs(long interval_ms);
  /*!
    @brief   Reports the interval as set, in milliseconds.

    @return  The interval in ms, or one of the BLAECK_INTERVAL_* modes - so a
             negative return is a mode rather than a duration. In client-controlled
             mode this reports the mode, not whatever a host has since asked for.

    @code
      if (Blaeck.getIntervalMs() == BLAECK_INTERVAL_OFF)
        Serial.println(F("timed data is locked off"));
    @endcode
  */
  long getIntervalMs() const { return _fixedInterval_ms; }

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

    @param   command  Name a host sends to invoke it.
    @param   handler  Called with the raw parameters, whatever they are.

    @note    No registration returns anything to check. A command that fails to
             register - table full, name too long, a null argument - is reported on
             the debug stream and counted, so one look at hasRejectedCommands()
             after them all answers for every command, whichever helper declared it.

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

    @warning A host is still holding the old list, so follow with writeCommands().

    @code
      Blaeck.clearAllCommandHandlers();
      Blaeck.onSwitchCommand("LED", onLED);
      Blaeck.writeCommands();
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
  static void _setChannelName(const char *&slot, bool &inFlash, const char *ram, const __FlashStringHelper *flash);
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
  //       .withStateSignal(F("Frequency"));
  //
  // The kind is the factory's name because it decides the entity and is not optional; every
  // modifier is. Each helper hands back the handle for its own kind, so a modifier that does
  // not apply - a range on a text command - does not compile rather than quietly doing nothing.
  //
  // All metadata strings must be F()/PROGMEM literals with program lifetime: they are stored as
  // pointers, never copied.
  //
  // What the firmware validates before dispatch, reporting each on DebugRef: values outside a
  // declared [min,max], bad select indices, and non-0/1 switch values. A number command that
  // declares no range is not checked and the catalog carries no limits for it, so a host builds
  // the control from its own defaults - state a range wherever the sketch has one to state.

  /*!
    @brief   Registers a command taking a number.

    The handler reads atof(params[0]).

    @param   command  Name a host sends to invoke it.
    @param   handler  Called once a value has been accepted.
    @return  Handle describing the control. Chainable.

    @note    The value still has to be a number - text that is not one is refused
             whether or not a range was declared. Without withRange() its size is
             unchecked, and the catalog carries no limits for a host to build from.

    @code
      Blaeck.onNumberCommand("SET_FREQ", onSetFreq)
          .withRange(0.0f, 2.0f, 0.01f)
          .withUnit(F("Hz"));
    @endcode
  */
  BlaeckNumberCommandRef onNumberCommand(const char *command, BlaeckCommandHandler handler);

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
    @return  Handle describing the control. Chainable.

    @warning Declare the list with withOptions(), or there is nothing to validate
             against and nothing for a host to offer.

    @code
      Blaeck.onSelectCommand("SET_WAVE", onSetWave)
          .withOptions(F("Sine,Square,Triangle,Sawtooth"))
          .withOwnState(F("Wave"), &waveIndex);
    @endcode
  */
  BlaeckSelectCommandRef onSelectCommand(const char *command, BlaeckCommandHandler handler);

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

    The other direction to getSelectOptionNameAt(). Matching is case-insensitive, the
    same rule an incoming command value is matched by.

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
  // The four little-endian bytes a built-in BLAECK.* command carries, as one number:
  // a message id on most of them, the interval on BLAECK.ACTIVATE. A field the frame
  // did not carry counts as 0, which is what the old fixed parameter array held.
  unsigned long _parsedMsgId() const;
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

  void writeData(unsigned long messageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp);
  void writeDataFrame(unsigned long MessageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp);

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

  // Micros overflow tracking for D2 (uint64 timestamp)
  unsigned long _prevMicros = 0;
  unsigned long long _overflowCount = 0;

  bool _timedActivated = false;
  bool _timedFirstTime = true;
  unsigned long _timedFirstTimeDone_ms = 0;
  unsigned long _timedSetPoint_ms = 0;
  unsigned long _timedInterval_ms = 1000;
  long _fixedInterval_ms = BLAECK_INTERVAL_CLIENT;

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
#if BLAECK_ENABLE_STATE_CHANNELS
  StateChannelEntry *_stateChannels = nullptr;
  uint16_t _stateChannelCapacity = DEFAULT_STATE_CHANNELS;
  uint16_t _stateChannelSlots() const { return _stateChannels != nullptr ? _stateChannelCapacity : 0; }
  bool _ensureStateChannelTable();
  // The text a channel reports, or nullptr when it has none: its getter, the text its
  // stateValue points at, or the option an index names. Resolved into `buf` only in the
  // last case; the others hand back a pointer they already had.
  const char *_channelText(const StateChannelEntry &e, char *buf, byte bufSize) const;

  // The 0x90 flag word for one channel. Both writer paths call this so the bits are decided
  // once: the buffered and unbuffered writers are otherwise the same code twice, and a flag
  // added to only one of them would make a board's catalog depend on how it was configured.
  uint16_t _stateChannelFlags(const StateChannelEntry &e, bool hasStateValue) const;

  // Wire code for a datatype, the same 0x00-0x0A a 0xB0 symbol carries.
  static byte _dtypeCode(dataType t);

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
  void _writeStateFrame(int channelIndex, const char *text, unsigned long messageID);
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
  // Monotonic message id stamped into the 0xA5 Command Ack frame header.
  unsigned long _commandAckMsgId = 0;
#if BLAECK_ENABLE_STATE_CHANNELS
  // Monotonic message id stamped into the 0x95 State frame header.
  unsigned long _stateMsgId = 0;
#endif
#if BLAECK_ENABLE_EVENTS
  // Monotonic message id stamped into the 0x85 Event frame header.
  unsigned long _eventMsgId = 0;
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

inline void BlaeckCommandRefBase::_setOwnState(const __FlashStringHelper *channelName,
                                               dataType valueType, const void *value,
                                               bool selectIndex)
{
#if BLAECK_ENABLE_COMMAND_META
  if (auto *e = _entry())
  {
    if (_owner->_declareOwnState((byte)_index, channelName, nullptr, valueType, value, selectIndex))
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
    if (_owner->_declareOwnState((byte)_index, channelName, getStateText))
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
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    // Together, so the bit and the field can never disagree.
    switch (bit)
    {
    case BLAECK_SIG_HAS_UNIT:         m->Unit = value; break;
    case BLAECK_SIG_HAS_DEVICE_CLASS: m->DeviceClass = value; break;
    default:                          m->Icon = value; break;
    }
    if (value != nullptr)
      m->MetaFlags |= bit;
    else
      m->MetaFlags &= (uint16_t)~bit;
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
    if (on)
      m->MetaFlags |= bit;
    else
      m->MetaFlags &= (uint16_t)~bit;
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
    m->MetaFlags &= (uint16_t)~BLAECK_SIG_STATE_CLASS_MASK;
    m->MetaFlags |= (uint16_t)(((uint16_t)stateClass << BLAECK_SIG_STATE_CLASS_SHIFT) &
                               BLAECK_SIG_STATE_CLASS_MASK);
  }
#else
  (void)stateClass;
#endif
}

inline void BlaeckSignalRefBase::_setOptions(const __FlashStringHelper *optionsCsv)
{
#if BLAECK_ENABLE_SIGNAL_META
  if (SignalMeta *m = _owner != nullptr ? _owner->_ensureSignalMeta(_index) : nullptr)
  {
    m->Options = optionsCsv;
    if (optionsCsv != nullptr)
      m->MetaFlags |= BLAECK_SIG_HAS_OPTIONS;
    else
      m->MetaFlags &= (uint16_t)~BLAECK_SIG_HAS_OPTIONS;
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
    m->DisplayPrecision = decimals;
    m->MetaFlags |= BLAECK_SIG_HAS_DISPLAY_PRECISION;
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

inline BlaeckEventChannelRef BlaeckEventChannelRef::withIcon(const __FlashStringHelper *icon)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    _owner->_eventChannels[_index].icon = icon;
#else
  (void)icon;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::diagnostic(bool on)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    _owner->_eventChannels[_index].diagnostic = on;
#else
  (void)on;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::withDeviceClass(const __FlashStringHelper *deviceClass)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    _owner->_eventChannels[_index].deviceClass = deviceClass;
#else
  (void)deviceClass;
#endif
  return *this;
}

inline BlaeckEventChannelRef BlaeckEventChannelRef::disabledByDefault(bool on)
{
#if BLAECK_ENABLE_EVENTS
  if (_index >= 0 && _owner != nullptr)
    _owner->_eventChannels[_index].disabledByDefault = on;
#else
  (void)on;
#endif
  return *this;
}

#endif //  BLAECKSERIAL_H
