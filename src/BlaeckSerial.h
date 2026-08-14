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


// Eleven values, so the underlying type is pinned to a byte rather than left as the int
// a compiler picks by default: this type is a field in every signal entry and every state
// channel entry, and a byte there is a byte per entry. The enumerators are unchanged, so
// nothing that names one is affected.
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

// How a signal's value accumulates over time, for Home Assistant statistics
// (0xF0 SignalMetaFlags bits 3-5). MEASUREMENT is a value that goes up and down
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
class BlaeckBeginRef;

class BlaeckSerial
{
public:
  // ----- Constructor -----
  BlaeckSerial();

  // ----- Destructor -----
  ~BlaeckSerial();

  // ----- Initialize -----
  // The returned handle sizes the tables and names a debug stream, and may be
  // ignored:
  //
  //   BlaeckSerial.begin(&Serial)
  //       .withSignals(50)
  //       .withStateChannels(12)
  //       .withDebugStream(&Serial1);
  //
  // Every number is optional and starts at a per-board default; a table is
  // allocated in full by the first entry added to it, so a table a sketch never
  // uses costs nothing. The chain may run after begin() returns because nothing
  // is allocated until the first add.
  BlaeckBeginRef begin(Stream *Ref);
  // Shorthand for begin(Ref).withSignals(Size).
  BlaeckBeginRef begin(Stream *Ref, unsigned int Size);

  // Set these variables in your Arduino sketch. Unlike a signal name, these are not
  // copied - what they point at must outlive them, which a string literal does.
  const char *DeviceName = "Unknown";
  const char *DeviceHWVersion = "n/a";
  const char *DeviceFWVersion = "n/a";

  // ----- Signals -----
  // Add a Signal. The returned handle describes how the signal is presented and
  // may be ignored:
  //
  //   BlaeckSerial.addSignal("Temperature", &Temperature)
  //       .withUnit(F("\xC2\xB0" "C"))
  //       .withDeviceClass(F("temperature"))
  //       .withStateClass(BLAECK_STATE_CLASS_MEASUREMENT)
  //       .withDisplayPrecision(1);
  //
  // A signal that describes nothing gets no 0xF0 entry and costs nothing on the
  // wire. When the signal table is full the handle is dead: the chain still
  // compiles and runs, and stores nothing - the missing signal is the real
  // problem and hasRejectedSignals() already reports it.
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

  // The same eleven, named with F(). The name then stays in flash and the signal keeps a
  // 2-byte pointer to it instead of a copy, which is most of what a signal costs in SRAM:
  //
  //   BlaeckSerial.addSignal(F("Temperature"), &Temperature);
  //
  // Only for a name fixed at compile time. A name built at runtime - snprintf into a
  // buffer, say - must use the overloads above, which copy it.
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

  // Delete all Signals
  void deleteSignals();
  // Signals that could not be added - past the capacity the table was given, or all of them
  // when the board had no RAM for the table at all. Both surface at the first addSignal(),
  // which is where the table is built. Named like hasRejectedCommands() and
  // hasRejectedChannels(): three tables, one question, one shape of answer.
  bool hasRejectedSignals() const { return _signalRegistrationFailed; }
  uint16_t getRejectedSignalCount() const { return _rejectedSignalCount; }

  // Signal Count
  int SignalCount;

  // ----- Device Restarted -----
  void writeRestarted();
  void writeRestarted(unsigned long messageID);

  // ----- Devices -----
  void writeDevices();
  void writeDevices(unsigned long messageID);

  // ----- Symbols -----
  void writeSymbols();
  void writeSymbols(unsigned long messageID);

  // ----- Signal Config (Home Assistant discovery catalog, 0xF0) -----
  // Carries only the signals that describe something, so a device where none do
  // answers with an empty frame. With BLAECK_ENABLE_SIGNAL_META=0 that is always
  // the case.
  void writeSignalConfig();
  void writeSignalConfig(unsigned long messageID);

  // ----- Commands (Home Assistant discovery catalog, 0xA0) -----
  void writeCommands();
  void writeCommands(unsigned long messageID);

  // ----- State channels (0x90 / 0x95) -----
  // With BLAECK_ENABLE_STATE_CHANNELS=0 these still compile but do nothing, so a
  // sketch can be built for a tiny target without being rewritten.
  //
  // Declare a state channel. Channels must be declared up-front (typically in
  // setup()) so the host can announce every text sensor before the first line
  // arrives; writeState() on an undeclared channel is dropped.
  // `channelName` is copied; `icon` must outlive the call (use a string literal
  // or F("mdi:...")). Returns false if the name is empty/too long or the table
  // is full.
  // Declares a channel. Which overload is called settles the channel's type, and the type
  // settles the handle: a numeric channel takes withUnit()/withStateClass()/
  // withDisplayPrecision(), a text one takes withStateText()/withOptions(), and asking for
  // the wrong one does not compile. Pass a pointer and the library reads that variable when
  // the value is wanted, exactly as addSignal() does; pass none and the channel carries only
  // what writeState() hands it.
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
  void clearAllStateChannels();

  // Send the 0x90 "State Channel List" frame (the declared-channel catalog).
  // Sent automatically in response to BLAECK.WRITE_STATE_CHANNELS.
  void writeStateChannels();
  void writeStateChannels(unsigned long messageID);

  // Report a value on a declared channel. Fire-and-forget: a host may surface it
  // (e.g. a Home Assistant sensor per declared channel) but it is never stored as
  // signal data. The frame carries the channel's index in the 0x90 catalog rather
  // than its name, so the catalog must reach the host first. No CRC (like the
  // 0xA0/0xA5 frames). Text longer than 255 bytes is truncated - the same cap a
  // string signal has, and the same one Home Assistant puts on a state - and the
  // truncation is reported once per channel on the debug stream. Values on
  // channels that were never passed to addStateChannel() are dropped.
  void writeState(const char *channelName, const char *text);
  void writeState(const char *channelName, const char *text, unsigned long messageID);

  // Report whatever the channel currently holds - the variable a typed channel points at, or
  // the text its getter builds. The only way to push a numeric channel, since there is nothing
  // for the caller to pass: the value already lives where the channel was told to look.
  void writeState(const char *channelName);
  void writeState(const char *channelName, unsigned long messageID);

  // Publish a command's own state now: asks the getter it was registered with and sends the
  // value on the channel the command owns. The push is what makes a change visible at once -
  // the catalog only reports on demand, when a host asks for it.
  //
  // Pass the handler's own `command` parameter and there is no literal to keep in step:
  //
  //   void onSetOffset(const char *command, const char *const *params, byte paramCount)
  //   { Offset = ...; BlaeckSerial.writeCommandState(command); }
  //
  // Does nothing for a command with no state of its own, or one whose state is a signal -
  // there the signal's own write is what reports it.
  void writeCommandState(const char *command);
  void writeCommandState(const char *command, unsigned long messageID);

  // ----- Events (Home Assistant event entities, 0x85) -----
  // With BLAECK_ENABLE_EVENTS=0 these still compile but do nothing.
  //
  // Declare an event channel, then give it its event types with addEventType().
  // Both must happen up-front (typically in setup()) so the host can announce
  // the entity, including its list of types, before the first event arrives.
  // `channelName` is copied; `icon` must outlive the call (use a string literal
  // or F("mdi:...")). Returns false if the name is empty/too long or the table
  // is full.
  // eventTypes is the closed set this channel may report, comma-separated and read left to
  // right, so position defines each type's index. It is not optional: writeEvent() resolves
  // against this list, and a host needs it to announce the entity at all, so a channel without
  // one could neither emit nor be shown. Use addEventType() to append more conditionally.
  BlaeckEventChannelRef addEventChannel(const char *channelName, const __FlashStringHelper *eventTypes);

  // Append an event type to a declared channel. Call order defines the index used on the wire:
  // the first type added to a channel is index 0, the next 1. addEventChannel() declares the set
  // known at compile time; use this to append to it conditionally. `eventType`
  // must outlive the call (use F("...")). Types are held in one pool shared by all channels.
  // A type that does not fit, names an undeclared channel, or duplicates one the channel
  // already has is dropped, reported on DebugRef, and counted by hasRejectedEventChannels().
  bool addEventType(const char *channelName, const __FlashStringHelper *eventType);
  void clearAllEventChannels();

  // Send the 0x80 "Event Channel List" frame (the declared-channel catalog).
  // Sent automatically in response to BLAECK.WRITE_EVENT_CHANNELS.
  void writeEventChannels();
  void writeEventChannels(unsigned long messageID);

  // Report that an event occurred on a declared channel. Fire-and-forget: a host
  // may surface it (e.g. a Home Assistant event entity per channel) but it is
  // never stored as signal data. The frame carries only the channel and event
  // type indices from the 0x80 catalog, which must reach the host first, and no
  // CRC. Events on channels or types that were never declared are dropped.
  void writeEvent(const char *channelName, const __FlashStringHelper *eventType);
  void writeEvent(const char *channelName, const __FlashStringHelper *eventType, unsigned long messageID);

  // ----- Data Write -----
  // Update value and write directly - by name. Independent of the timed interval, so a sketch
  // can push a value the moment something happens and stream nothing otherwise
  // (setIntervalMs(BLAECK_INTERVAL_OFF)).
  //
  // A momentary signal clears itself here rather than asking a host to time it out:
  //
  //   if (triggered && !Pulse)
  //   {
  //     Pulse = true;
  //     pulseSince = millis();
  //     BlaeckSerial.write("Pulse", Pulse);
  //   }
  //   if (Pulse && millis() - pulseSince >= 2000)
  //   {
  //     Pulse = false;
  //     BlaeckSerial.write("Pulse", Pulse);
  //   }
  //
  // Both edges then reach every consumer alike - a dashboard, the logged table, a second host -
  // and the stored data says how long the value was true. A host-side timer would leave the
  // table holding one reading and the duration existing nowhere but that host.
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

  // Index of a registered signal, or -1 if there is none by that name. Resolve once in setup()
  // and use the by-index calls below on anything that runs often: the by-name calls walk every
  // signal and compare its name, which costs more the more signals there are.
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
  // Update value and mark Signal as updated - by name
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
  // Use these mark functions for cases where you don't want to change the value
  void markSignalUpdated(int signalIndex);
  void markSignalUpdated(const char *signalName);
  void markAllSignalsUpdated();
  void clearAllUpdateFlags();
  // Check if any Signals are marked as updated
  bool hasUpdatedSignals();

  // ----- Data Write All -----
  void writeAllData();
  void writeAllData(unsigned long messageID);
  void writeAllData(unsigned long messageID, unsigned long long timestamp);
  void timedWriteAllData();
  void timedWriteAllData(unsigned long msg_id);
  void timedWriteAllData(unsigned long messageID, unsigned long long timestamp);

  // ----- Data Write Updated -----
  void writeUpdatedData();
  void writeUpdatedData(unsigned long messageID);
  void writeUpdatedData(unsigned long messageID, unsigned long long timestamp);
  void timedWriteUpdatedData();
  void timedWriteUpdatedData(unsigned long msg_id);
  void timedWriteUpdatedData(unsigned long messageID, unsigned long long timestamp);

  // ----- Tick -----
  void tick();
  void tick(unsigned long messageID);
  void tickUpdated();
  void tickUpdated(unsigned long messageID);

  // ----- Timed Data configuruation -----
  // interval_ms semantics:
  //   >= 0                    fixed interval lock in ms (ACTIVATE/DEACTIVATE ignored)
  //   BLAECK_INTERVAL_OFF     timed data locked off (ACTIVATE ignored)
  //   BLAECK_INTERVAL_CLIENT  client-controlled mode (default)
  // Invalid values are rejected and the previous mode remains active.
  void setIntervalMs(long interval_ms);
  long getIntervalMs() const { return _fixedInterval_ms; }

  // ----- Read  -----
  void read();

  // ----- Command callback  -----
  // A command not registered - table full, name too long, or a null argument - is reported on
  // DebugRef and counted; see hasRejectedCommands(). No registration returns a value to check,
  // so one look after them all answers for every command whichever helper declared it.
  void onCommand(const char *command, BlaeckCommandHandler handler);
  void onAnyCommand(BlaeckAnyCommandHandler handler);
  void clearAllCommandHandlers();
  bool hasRejectedCommands() const { return _rejectedCommandCount > 0; }
  uint16_t getRejectedCommandCount() const { return _rejectedCommandCount; }

  // State channels that could not be declared - a full table, a name too long, a name a
  // command already owns - each reported on DebugRef and counted here. A command's
  // withOwnState() channel counts too: when it cannot be declared the command keeps no state at
  // all, so a full state channel table costs a control's value, not just a channel.
  bool hasRejectedStateChannels() const { return _rejectedStateChannelCount > 0; }
  uint16_t getRejectedStateChannelCount() const { return _rejectedStateChannelCount; }

  // Event channels and event types that could not be declared. Counted together because both
  // sit behind BLAECK_ENABLE_EVENTS and a type belongs to a channel, so either answer sends you
  // to the same place - withEventChannels() or withEventTypes() on the begin() chain, and a
  // debug stream says which.
  bool hasRejectedEventChannels() const
  {
    return _rejectedEventChannelCount > 0 || _rejectedEventTypeCount > 0;
  }
  uint16_t getRejectedEventChannelCount() const
  {
    return (uint16_t)(_rejectedEventChannelCount + _rejectedEventTypeCount);
  }

  // Anything at all that a table had no room for, across all five tables.
  bool hasRejections() const;
  // Says what was dropped and the exact begin() call that would have kept it, one line per
  // table, and prints nothing at all when there is nothing to report - so a sketch can end
  // setup() with it unconditionally. Returns whether anything was printed.
  //
  //   BlaeckSerial.printRejections(&Serial);
  //
  // Meant for the stream the sketch already talks on, including the one Blaeck itself uses:
  // called from setup() it cannot land inside a frame, since nothing has been written yet.
  // A debug stream reports the same thing as it happens, naming each dropped entry; this is
  // the summary for a board with only one Serial.
  bool printRejections(Stream *out);

  // ----- Typed command registration (Home Assistant discovery metadata) -----
  // Same runtime behavior as onCommand(), but the returned handle describes the control so the
  // device can declare it in a 0xA0 "Command List" frame (BLAECK.WRITE_COMMANDS):
  //
  //   BlaeckSerial.onNumberCommand("SET_FREQ", onSetFreq)
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
  // A number command must state its range. The firmware validates against it - values outside
  // [min,max], bad select indices and non-0/1 switch values are rejected before dispatch and
  // reported on DebugRef - and a host needs it too: Home Assistant defaults an undeclared range
  // to 0..100 step 1, so a command accepting 0..500 gets a control that stops at 100, and one
  // accepting 0..1 gets a control with only two positions.
  BlaeckNumberCommandRef onNumberCommand(const char *command, BlaeckCommandHandler handler);
  BlaeckSwitchCommandRef onSwitchCommand(const char *command, BlaeckCommandHandler handler);
  BlaeckSelectCommandRef onSelectCommand(const char *command, BlaeckCommandHandler handler);
  BlaeckButtonCommandRef onButtonCommand(const char *command, BlaeckCommandHandler handler);
  BlaeckTextCommandRef onTextCommand(const char *command, BlaeckCommandHandler handler);

  // Copies option `index` of a select command's declared list into `out`, so a sketch can
  // render the option it just selected without keeping a second copy of the names. The
  // list already lives in flash; this is the only way to read it back.
  // Returns false and leaves `out` empty if the command is not a declared select, the
  // index is past the end, or the option would not fit - a truncated name is no use,
  // since a host matches state against the declared options exactly.
  bool getSelectOption(const char *command, byte index, char *out, byte outSize) const;

  // ----- Before data write callback  -----
  // Called just before signal data is sent, in normal loop context
  // (safe to use Serial, delay, etc.).
  void setBeforeWriteCallback(void (*callback)());

  // Timestamp configuration methods
  void setTimestampMode(BlaeckTimestampMode mode);
  void setTimestampCallback(unsigned long long (*callback)());
  BlaeckTimestampMode getTimestampMode() const { return _timestampMode; }
  bool hasValidTimestampCallback() const;

  // Buffered writes: assemble entire frame in RAM before sending.
  // Default: OFF on AVR (saves SRAM), ON everywhere else.
  // Override at compile time with BLAECK_BUFFERED_WRITES_DEFAULT,
  // or at runtime with setBufferedWrites().
  void setBufferedWrites(bool enabled);
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
  void _resetCommandMeta(byte handlerIndex, uint8_t kind);
  // The one place a channel is declared, mirroring _registerCommand(): the table index, or -1
  // having counted the rejection and said why on DebugRef. Re-declaring a name returns its
  // existing slot with the metadata cleared, so what a previous declaration said cannot linger.
  // An event channel keeps its already-declared types and their indices.
  // The type and the variable are settled at registration because they come from which
  // overload the sketch called, not from a modifier a handle could offer - which is what
  // keeps a channel from ever holding a type its value does not match.
  int _registerStateChannel(const char *channelName, dataType valueType = Blaeck_string,
                              const void *value = nullptr);
  int _registerEventChannel(const char *channelName, const __FlashStringHelper *eventTypes);
  // Appends one pool entry per field of a comma-separated list, in order, so a field's position
  // is its wire index - the same rule call order gives addEventType().
  void _addEventTypesCsv(byte channelIndex, const __FlashStringHelper *eventTypes);
#if BLAECK_ENABLE_COMMAND_META
  void writeCommandsFrame(unsigned long MessageID);
  byte _validateTypedCommand(byte handlerIndex);
  // Declares the channel a typed command owns. Separate from addStateChannel() so that one
  // can refuse an owned name outright rather than needing a "unless it is mine" exception.
  bool _addOwnedStateChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText,
                             dataType valueType = Blaeck_string, const void *value = nullptr);
  // Declares and announces the channel a command carries itself, for withOwnState(). Announcing
  // at registration is what corrects a host that was already connected when the board reset.
  // False when the channel could not be declared, so withOwnState() can leave the command
  // without state rather than advertising a channel absent from the 0x90 catalog.
  bool _declareOwnState(byte handlerIndex, const __FlashStringHelper *channelName,
                        BlaeckStateTextGetter getStateText, dataType valueType, const void *value);
  bool _declareOwnState(byte handlerIndex, const __FlashStringHelper *channelName,
                        BlaeckStateTextGetter getStateText);

  static void _percentDecodeInPlace(char *s);
  static long _flashCsvIndexOf(const __FlashStringHelper *csv, const char *value);
#endif
  // Number of comma-separated fields in a flash CSV. Deliberately outside the
  // command-metadata guard: it counts a select command's options and an event
  // channel's type list, and those features are enabled independently.
  static byte _flashCsvOptionCount(const __FlashStringHelper *csv);
#if BLAECK_ENABLE_STATE_CHANNELS
  void writeStateChannelsFrame(unsigned long MessageID);
  // Index of a declared channel, or -1 when the name was never declared.
  int _findStateChannel(const char *channelName) const;
#endif
#if BLAECK_ENABLE_EVENTS
  void writeEventChannelsFrame(unsigned long MessageID);
  // Index of a declared event channel, or -1 when the name was never declared.
  int _findEventChannel(const char *channelName) const;
  // Position of an event type within its own channel's list, or -1 when that
  // channel never declared it.
  int _findEventType(byte channelIndex, const __FlashStringHelper *eventType) const;
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
#if defined(__AVR__)
  static const byte MAX_COMMAND_NAME_COUNT = 24;
#else
  static const byte MAX_COMMAND_NAME_COUNT = 40;
#endif
  // The parse buffer is compared against two kinds of name: a registered command, which
  // _registerCommand refuses outright above MAX_COMMAND_NAME_COUNT, and a built-in, of
  // which BLAECK.WRITE_STATE_CHANNELS is the longest. It has to fit whichever is larger -
  // a built-in truncated on the way in would match nothing, and the device would go deaf
  // to it. The assert below keeps that true if a longer built-in is ever added.
  static const byte MAX_BUILTIN_COMMAND_COUNT = 28;
  static const byte MAX_PARSED_COMMAND_COUNT =
      MAX_COMMAND_NAME_COUNT > MAX_BUILTIN_COMMAND_COUNT ? MAX_COMMAND_NAME_COUNT
                                                         : MAX_BUILTIN_COMMAND_COUNT;
  static_assert(sizeof("BLAECK.WRITE_STATE_CHANNELS") <= MAX_BUILTIN_COMMAND_COUNT,
                "MAX_BUILTIN_COMMAND_COUNT must fit the longest BLAECK.* command name");
// Declared whether or not state channels are compiled in, because StateChannelEntry
// is - see there.
#if defined(__AVR__)
  static const byte MAX_STATE_NAME_COUNT = 16;
#else
  static const byte MAX_STATE_NAME_COUNT = 32;
#endif
#if BLAECK_ENABLE_EVENTS
  #if defined(__AVR__)
    static const byte MAX_EVENT_NAME_COUNT = 16;
  #else
    static const byte MAX_EVENT_NAME_COUNT = 32;
  #endif
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
  CommandHandlerEntry *_commandHandlers = nullptr;
  byte _commandCapacity = DEFAULT_COMMANDS;
  // Slots that exist right now: the capacity once the table has been built, and
  // zero before that. Every loop over a table is bounded by this, so a table
  // that was never needed - or that the board had no RAM for - simply has
  // nothing to walk.
  byte _commandSlots() const { return _commandHandlers != nullptr ? _commandCapacity : 0; }
  // Allocates the table on first use. False when the board had no RAM for it,
  // which is reported the same way a full table is.
  bool _ensureCommandTable();
  // Declared even with BLAECK_ENABLE_STATE_CHANNELS=0, when no table of these is ever
  // built: BlaeckStateRefBase::_entry() names the type in its return type, and the
  // withIcon()/diagnostic()/... chain names its fields - all of which has to keep
  // compiling so a sketch needs no #ifdef around what it writes. A type on its own
  // costs nothing; only the table below is compiled away.
  struct StateChannelEntry
  {
    char name[MAX_STATE_NAME_COUNT];
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
    bool truncationWarned = false;
    bool inUse = false;
  };
#if BLAECK_ENABLE_STATE_CHANNELS
  StateChannelEntry *_stateChannels = nullptr;
  byte _stateChannelCapacity = DEFAULT_STATE_CHANNELS;
  byte _stateChannelSlots() const { return _stateChannels != nullptr ? _stateChannelCapacity : 0; }
  bool _ensureStateChannelTable();

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
  struct EventChannelEntry
  {
    char name[MAX_EVENT_NAME_COUNT];
    const __FlashStringHelper *icon = nullptr;
    const __FlashStringHelper *deviceClass = nullptr;
    bool diagnostic = false;
    bool disabledByDefault = false;
    bool inUse = false;
  };
  EventChannelEntry *_eventChannels = nullptr;
  byte _eventChannelCapacity = DEFAULT_EVENT_CHANNELS;
  byte _eventChannelSlots() const { return _eventChannels != nullptr ? _eventChannelCapacity : 0; }
  bool _ensureEventChannelTable();

  // One pool shared by every channel: each entry records which channel owns it,
  // so a channel with many types and one with few both fit without reserving a
  // per-channel array. Appended in call order, which is what defines the index
  // sent in the 0x85 frame.
  struct EventTypeEntry
  {
    byte channelIndex = 0;
    // Either a whole flash string, or one comma-separated field of one: addEventType()
    // stores its literal with field = WHOLE_STRING, while the CSV form of
    // addEventChannel() appends one entry per field, all sharing the same pointer. The
    // pool is walked identically either way, so nothing downstream has to know which.
    const __FlashStringHelper *text = nullptr;
    byte field = WHOLE_STRING;
  };
  static const byte WHOLE_STRING = 0xFF;

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
  byte _eventTypeCapacity = DEFAULT_EVENT_TYPES;
  byte _eventTypeSlots() const { return _eventTypes != nullptr ? _eventTypeCapacity : 0; }
  bool _ensureEventTypeTable();
  byte _eventTypeCount = 0;
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

// Handle to the just-initialised library, sizing the tables it will allocate.
// Returned by begin(), meant to be chained, not stored.
//
//   BlaeckSerial.begin(&Serial).withSignals(50).withStateChannels(12);
//
// A number given here is a capacity, not a reservation: nothing is allocated
// until the first entry is added to that table, so a table a sketch never uses
// costs nothing at all. That is also why the chain may run after begin() has
// returned - the numbers are read later, when the table is built.
//
// Raising a capacity after its table already exists is refused, not silently
// ignored: the table is sized once, and a sketch that asks late is asking for
// slots that cannot appear.
class BlaeckBeginRef
{
public:
  explicit BlaeckBeginRef(BlaeckSerial *owner) : _owner(owner) {}

  BlaeckBeginRef &withSignals(unsigned int count)
  {
    if (_owner != nullptr)
      _owner->_setTableCapacity(BlaeckSerial::TABLE_SIGNALS, count);
    return *this;
  }
  // Kept even when the feature is compiled out, so a sketch that sizes the table
  // still builds; the number is then simply not stored.
  BlaeckBeginRef &withStateChannels(byte count)
  {
#if BLAECK_ENABLE_STATE_CHANNELS
    if (_owner != nullptr)
      _owner->_setTableCapacity(BlaeckSerial::TABLE_STATE_CHANNELS, count);
#else
    (void)count;
#endif
    return *this;
  }
  BlaeckBeginRef &withEventChannels(byte count)
  {
#if BLAECK_ENABLE_EVENTS
    if (_owner != nullptr)
      _owner->_setTableCapacity(BlaeckSerial::TABLE_EVENT_CHANNELS, count);
#else
    (void)count;
#endif
    return *this;
  }
  BlaeckBeginRef &withEventTypes(byte count)
  {
#if BLAECK_ENABLE_EVENTS
    if (_owner != nullptr)
      _owner->_setTableCapacity(BlaeckSerial::TABLE_EVENT_TYPES, count);
#else
    (void)count;
#endif
    return *this;
  }
  BlaeckBeginRef &withCommands(byte count)
  {
    if (_owner != nullptr)
      _owner->_setTableCapacity(BlaeckSerial::TABLE_COMMANDS, count);
    return *this;
  }
  // Where the library says what it rejected and why. Without one, a full table
  // is silent apart from hasRejectedSignals() and its siblings.
  BlaeckBeginRef &withDebugStream(Stream *debugStream)
  {
    if (_owner != nullptr)
      _owner->_debugStream = debugStream;
    return *this;
  }

private:
  BlaeckSerial *_owner;
};

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
  BlaeckSerial::CommandHandlerEntry *_entry() const
  {
    if (_owner == nullptr || _index < 0)
      return nullptr;
    return &_owner->_commandHandlers[_index];
  }

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
  void _setOwnState(const __FlashStringHelper *channelName, dataType valueType, const void *value)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      if (_owner->_declareOwnState((byte)_index, channelName, nullptr, valueType, value))
      {
        e->stateSignal = channelName;
        e->stateSource = BLAECK_STATE_CHANNEL;
      }
    }
#else
    (void)channelName;
    (void)valueType;
    (void)value;
#endif
  }

  void _setOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText)
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

  void _setRange(float mn, float mx, float st)
  {
#if BLAECK_ENABLE_COMMAND_META
    if (auto *e = _entry())
    {
      e->meta_min = mn;
      e->meta_max = mx;
      e->meta_step = st;
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

#define BLAECK_COMMAND_REF_SHARED(TYPE)                                                     \
  /* The signal that mirrors this command's value, so a host shows what the device holds */ \
  /* rather than what was last sent. Leave it out for an optimistic control. */             \
  TYPE &withStateSignal(const __FlashStringHelper *signalName)                              \
  {                                                                                         \
    _setStateSignal(signalName);                                                            \
    return *this;                                                                           \
  }                                                                                         \
  /* State the command carries itself: it declares a state channel of that name - taking a */ \
  /* slot from the state channel table like any other - and asks */                 \
  /* the getter for the value, instead of mirroring a signal. The channel belongs to the */  \
  /* command - addStateChannel() and writeState() both refuse the name - so what the */  \
  /* catalog reports and what is pushed cannot disagree. Push a change with */               \
  /* writeCommandState(). Independent of the signal table, so a device that adds no signals */ \
  /* can still report what its controls are set to. */                                       \
  TYPE &withOwnState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText) \
  {                                                                                         \
    _setOwnState(channelName, getStateText);                                                \
    return *this;                                                                           \
  }                                                                                         \
  /* A device setting rather than a primary function. Home Assistant keeps both this and */  \
  /* diagnostic() off its auto-generated dashboards. */                                      \
  TYPE &config()                                                                            \
  {                                                                                         \
    _setCategory((uint8_t)BLAECK_CAT_CONFIG);                                               \
    return *this;                                                                           \
  }                                                                                         \
  TYPE &diagnostic()                                                                        \
  {                                                                                         \
    _setCategory((uint8_t)BLAECK_CAT_DIAGNOSTIC);                                           \
    return *this;                                                                           \
  }

class BlaeckNumberCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckNumberCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckNumberCommandRef)

  // The range the firmware validates against: a value outside it is rejected before dispatch.
  // A number command that never calls this accepts anything.
  // step is display resolution only and is never rounded to - pass 0 to leave it unsaid and let
  // Home Assistant apply its own. It refuses a step below 0.001, so a finer resolution cannot be
  // shown; the host raises it rather than let the whole entity be rejected.
  BlaeckNumberCommandRef &withRange(float min, float max, float step = 0.0f)
  {
    _setRange(min, max, step);
    return *this;
  }

  BlaeckNumberCommandRef &withUnit(const __FlashStringHelper *unit)
  {
    _setUnit(unit);
    return *this;
  }

  // The command's own state as a variable rather than as text: the library reads it where the
  // getter would have been called and sends it typed, so the host renders the number and the
  // sketch never formats one. One overload per numeric type, as addSignal() has.

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, byte *value)
  {
    _setOwnState(channelName, Blaeck_byte, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, short *value)
  {
    _setOwnState(channelName, Blaeck_short, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned short *value)
  {
    _setOwnState(channelName, Blaeck_ushort, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, int *value)
  {
    _setOwnState(channelName, Blaeck_int, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned int *value)
  {
    _setOwnState(channelName, Blaeck_uint, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, long *value)
  {
    _setOwnState(channelName, Blaeck_long, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, unsigned long *value)
  {
    _setOwnState(channelName, Blaeck_ulong, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, float *value)
  {
    _setOwnState(channelName, Blaeck_float, value);
    return *this;
  }

  BlaeckNumberCommandRef &withOwnState(const __FlashStringHelper *channelName, double *value)
  {
    _setOwnState(channelName, Blaeck_double, value);
    return *this;
  }
};

class BlaeckSwitchCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckSwitchCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckSwitchCommandRef)

  // A switch reports its own state as the bool it is: the host renders it as the on/off
  // payloads it declared, so the sketch never has to know which spelling those use.
  BlaeckSwitchCommandRef &withOwnState(const __FlashStringHelper *channelName, bool *value)
  {
    _setOwnState(channelName, Blaeck_bool, value);
    return *this;
  }
};

class BlaeckSelectCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckSelectCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckSelectCommandRef)

  // The closed list this control offers, comma-separated. A value that is not an index into it
  // is rejected before dispatch. getSelectOption() reads a name back out, so the list lives in
  // flash once instead of being repeated in the sketch.
  //
  // Avoid naming an option "none" in any casing: Home Assistant reads that state as "no option
  // selected" and blanks the control rather than showing it.
  BlaeckSelectCommandRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    _setOptions(optionsCsv);
    return *this;
  }

  // The buffer this control's state lives in. No getter and no formatting: the library reads
  // the characters where they sit, so a value the sketch already keeps needs no second copy.
  BlaeckSelectCommandRef &withOwnState(const __FlashStringHelper *channelName, const char *value)
  {
    _setOwnState(channelName, Blaeck_string, value);
    return *this;
  }
};

class BlaeckButtonCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckButtonCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckButtonCommandRef)
};

class BlaeckTextCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckTextCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckTextCommandRef)

  // The buffer this control's state lives in. No getter and no formatting: the library reads
  // the characters where they sit, so a value the sketch already keeps needs no second copy.
  BlaeckTextCommandRef &withOwnState(const __FlashStringHelper *channelName, const char *value)
  {
    _setOwnState(channelName, Blaeck_string, value);
    return *this;
  }

  // The advertised limit in decoded bytes, enforced before dispatch: a longer value is rejected
  // with BLAECK_ACK_TOO_LONG. Left unsaid it is 255. sizeof(buffer) - 1 is usually what you want.
  BlaeckTextCommandRef &withMaxLength(unsigned int maxLength)
  {
    _setMaxLength(maxLength);
    return *this;
  }
};

// Handle to the signal addSignal() just added, describing how it is presented.
// Returned by value and meant to be chained, not stored:
//
//   BlaeckSerial.addSignal("FreeMemory", &FreeMemory)
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

  void _setFlash(const __FlashStringHelper *value, uint16_t bit)
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

  void _setBit(uint16_t bit, bool on)
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

  void _setStateClass(BlaeckStateClass stateClass)
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

  void _setOptions(const __FlashStringHelper *optionsCsv)
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

  void _setDisplayPrecision(uint8_t decimals)
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

  void _setNameSuffix(uint8_t suffix)
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

  BlaeckSerial *_owner;
  int16_t _index;
};

// What every signal accepts, whatever it holds. Repeated per handle so the chain keeps its
// datatype all the way down, and so an editor offers exactly what applies when the dot is typed.
#define BLAECK_SIGNAL_REF_SHARED(TYPE)                                                    \
  /* Ends the name in a number, e.g. addSignal(F("Sine_"), &v).withNameSuffix(i + 1) */   \
  /* names a signal Sine_1. 0-255, and 0 is a number like any other. Worth doing for a */ \
  /* run of signals sharing a prefix: the prefix stays in flash and the digits are */     \
  /* produced when the name is sent, so nothing is copied to the heap - which is what */  \
  /* a name built with snprintf costs instead. */                                         \
  TYPE &withNameSuffix(uint8_t suffix)                                                    \
  {                                                                                       \
    _setNameSuffix(suffix);                                                               \
    return *this;                                                                         \
  }                                                                                       \
  /* What the value measures, e.g. F("temperature"). Home Assistant's vocabulary, */      \
  /* carried as written: this library does not hold the list, because the list grows */   \
  /* faster than firmware is reflashed. */                                                \
  TYPE &withDeviceClass(const __FlashStringHelper *deviceClass)                           \
  {                                                                                       \
    _setFlash(deviceClass, BLAECK_SIG_HAS_DEVICE_CLASS);                                  \
    return *this;                                                                         \
  }                                                                                       \
  /* Material Design Icons name, e.g. F("mdi:sine-wave"). */                              \
  TYPE &withIcon(const __FlashStringHelper *icon)                                         \
  {                                                                                       \
    _setFlash(icon, BLAECK_SIG_HAS_ICON);                                                 \
    return *this;                                                                         \
  }                                                                                       \
  /* Moves the entity off Home Assistant's auto-generated dashboards, for a value that */ \
  /* describes the device rather than what it measures. */                                \
  TYPE &diagnostic(bool on = true) { return _setBit(BLAECK_SIG_DIAGNOSTIC, on), *this; }  \
  /* Registers the entity but leaves it switched off until someone enables it. */         \
  TYPE &disabledByDefault(bool on = true)                                                 \
  {                                                                                       \
    _setBit(BLAECK_SIG_DISABLED_BY_DEFAULT, on);                                          \
    return *this;                                                                         \
  }                                                                                       \
  /* Report every reading, even one identical to the last. */                             \
  TYPE &forceUpdate(bool on = true)                                                       \
  {                                                                                       \
    _setBit(BLAECK_SIG_FORCE_UPDATE, on);                                                 \
    return *this;                                                                         \
  }

// Any of the nine numeric datatypes. The only shape with decimals to show and a value that
// accumulates, so it is the only one carrying a state class or a display precision.
class BlaeckNumericSignalRef : public BlaeckSignalRefBase
{
public:
  BlaeckNumericSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefBase(owner, index) {}
  BLAECK_SIGNAL_REF_SHARED(BlaeckNumericSignalRef)

  // Symbol shown after the value, e.g. F("Hz"). Non-ASCII must be UTF-8:
  // F("\xC2\xB0" "C") is the degree sign followed by C.
  BlaeckNumericSignalRef &withUnit(const __FlashStringHelper *unit)
  {
    _setFlash(unit, BLAECK_SIG_HAS_UNIT);
    return *this;
  }

  BlaeckNumericSignalRef &withStateClass(BlaeckStateClass stateClass)
  {
    _setStateClass(stateClass);
    return *this;
  }

  // Decimal places to display. 0 is a real instruction - show it as an integer - and not the
  // same as saying nothing, which is why it needs its own flag bit where the state class
  // encodes its own absence.
  BlaeckNumericSignalRef &withDisplayPrecision(uint8_t decimals)
  {
    _setDisplayPrecision(decimals);
    return *this;
  }
};

// A string signal. No unit, no decimals to round and nothing to keep statistics on: all three
// tell Home Assistant the state is a number, and it then refuses the text.
//
// Mirrors BlaeckTextStateRef: a string signal and a state channel become the same Home
// Assistant entity, so they carry the same fields. Change one, change the other.
class BlaeckTextSignalRef : public BlaeckSignalRefBase
{
public:
  BlaeckTextSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefBase(owner, index) {}
  BLAECK_SIGNAL_REF_SHARED(BlaeckTextSignalRef)

  // The closed set of values this signal reports, comma-separated. Text only: the list is a set
  // of names, which is what Home Assistant's enum device class describes - a number has no such
  // set, and a bool becomes a binary sensor, which has no options at all. Home Assistant needs
  // withDeviceClass(F("enum")) alongside it and rejects the list without one. Every value
  // reported must be in the list: one that is not raises rather than being shown. A unit is
  // ignored alongside options rather than refused.
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
class BlaeckBoolSignalRef : public BlaeckSignalRefBase
{
public:
  BlaeckBoolSignalRef(BlaeckSerial *owner, int16_t index) : BlaeckSignalRefBase(owner, index) {}
  BLAECK_SIGNAL_REF_SHARED(BlaeckBoolSignalRef)
};

// Handle to the channel just declared. Returned by value and meant to be chained, not stored;
// a channel that could not be declared gives a dead handle that swallows the chain, and
// hasRejectedStateChannels() is where that shows up. The methods are always defined - with
// BLAECK_ENABLE_STATE_CHANNELS=0 they store nothing, so a sketch needs no #ifdef.
//
// Three kinds, mirroring the three signal handles, because a channel's value now has a type and
// the wrong modifier on the wrong type is a bug a host cannot report. Unit, state class and
// display precision each tell Home Assistant the state is a number, so on a text channel they
// do not merely do nothing - they make it refuse the text and show nothing at all. Splitting
// the handles turns that into a compile error.
class BlaeckStateRefBase
{
protected:
  BlaeckStateRefBase(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // The entry this handle names, or nullptr when registration was rejected.
  BlaeckSerial::StateChannelEntry *_entry() const
  {
#if BLAECK_ENABLE_STATE_CHANNELS
    if (_owner != nullptr && _index >= 0)
      return &_owner->_stateChannels[_index];
#endif
    return nullptr;
  }

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
#define BLAECK_STATE_REF_SHARED(TYPE)                                                     \
  /* Material Design Icons name, e.g. F("mdi:pulse"). */                                    \
  TYPE &withIcon(const __FlashStringHelper *icon)                                           \
  {                                                                                         \
    if (auto *e = _entry())                                                                 \
      e->icon = icon;                                                                       \
    return *this;                                                                           \
  }                                                                                         \
  /* Groups the entity under Home Assistant's diagnostic section. */                        \
  TYPE &diagnostic(bool on = true)                                                          \
  {                                                                                         \
    if (auto *e = _entry())                                                                 \
      e->diagnostic = on;                                                                   \
    return *this;                                                                           \
  }                                                                                         \
  /* What the value is, for a host that renders it. The list a host draws from depends on */ \
  /* the value type: F("timestamp") or F("date") suit text, F("voltage") a number. A name */ \
  /* from the wrong list fails discovery and the entity never appears. */                   \
  TYPE &withDeviceClass(const __FlashStringHelper *deviceClass)                             \
  {                                                                                         \
    if (auto *e = _entry())                                                                 \
      e->deviceClass = deviceClass;                                                         \
    return *this;                                                                           \
  }                                                                                         \
  /* Registers the entity but leaves it switched off until someone enables it. */           \
  TYPE &disabledByDefault(bool on = true)                                                   \
  {                                                                                         \
    if (auto *e = _entry())                                                                 \
      e->disabledByDefault = on;                                                            \
    return *this;                                                                           \
  }                                                                                         \
  /* Report every value, even one identical to the last. A host otherwise collapses a */    \
  /* repeat into the entry it already has, so a heartbeat that says the same thing each */  \
  /* time leaves no trace of having run. */                                                 \
  TYPE &forceUpdate(bool on = true)                                                         \
  {                                                                                         \
    if (auto *e = _entry())                                                                 \
      e->forceUpdate = on;                                                                  \
    return *this;                                                                           \
  }

// A channel carrying a number. Mirrors BlaeckNumericSignalRef: the same value becomes the same
// Home Assistant entity whichever way it arrives, so they carry the same fields. Change one,
// change the other. What differs is not the entity but the cadence - a signal is sampled into
// every logged row, a channel is pushed when it changes and never stored.
class BlaeckNumericStateRef : public BlaeckStateRefBase
{
public:
  BlaeckNumericStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefBase(owner, index) {}
  BLAECK_STATE_REF_SHARED(BlaeckNumericStateRef)

  // Symbol shown after the value, e.g. F("Hz"). Non-ASCII must be UTF-8:
  // F("\xC2\xB0" "C") is the degree sign followed by C.
  BlaeckNumericStateRef &withUnit(const __FlashStringHelper *unit)
  {
    if (auto *e = _entry())
    {
      e->unit = unit;
      e->metaFlags |= BLAECK_SCH_HAS_UNIT;
    }
    return *this;
  }

  // Declares that the value accumulates, which is what makes Home Assistant keep long-term
  // statistics on it - a channel is never written to the host's own store, so this is the only
  // way its history outlives the recorder's window.
  BlaeckNumericStateRef &withStateClass(BlaeckStateClass stateClass)
  {
    _setStateClass(stateClass);
    return *this;
  }

  // Decimal places to display. 0 is a real instruction - show it as an integer - and not the
  // same as saying nothing, which is why it needs its own flag bit where the state class
  // encodes its own absence.
  BlaeckNumericStateRef &withDisplayPrecision(uint8_t decimals)
  {
    _setDisplayPrecision(decimals);
    return *this;
  }
};

// A channel carrying text. Mirrors BlaeckTextSignalRef: no unit, no decimals to round and
// nothing to keep statistics on: all three tell Home Assistant the state is a number, and it
// then refuses the text.
class BlaeckTextStateRef : public BlaeckStateRefBase
{
public:
  BlaeckTextStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefBase(owner, index) {}
  BLAECK_STATE_REF_SHARED(BlaeckTextStateRef)

  // Makes the channel report a current value: the library calls the getter while building the
  // 0x90 catalog, so a host that polls learns the value as it is at that moment and the sketch
  // never pushes just to keep it in step. Because it is fetched rather than stored it cannot go
  // stale. Build the text in a function-local static and return it. Left out, the channel is a
  // plain log channel and carries no value in the catalog.
  //
  // Only text takes a getter. A numeric channel points at the variable instead, which needs no
  // function at all - and anything a getter would have computed can be assigned to a variable
  // first.
  BlaeckTextStateRef &withStateText(BlaeckStateTextGetter getStateText)
  {
    if (auto *e = _entry())
      e->getStateText = getStateText;
    return *this;
  }

  // The closed set of values this channel reports, comma-separated. Text only: the list is a set
  // of names, which is what Home Assistant's enum device class describes - a number has no such
  // set. Home Assistant needs withDeviceClass(F("enum")) alongside it and rejects the list
  // without one. Every value reported must be in the list: one that is not raises rather than
  // being shown. A unit is ignored alongside options rather than refused.
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
class BlaeckBoolStateRef : public BlaeckStateRefBase
{
public:
  BlaeckBoolStateRef(BlaeckSerial *owner, int16_t index) : BlaeckStateRefBase(owner, index) {}
  BLAECK_STATE_REF_SHARED(BlaeckBoolStateRef)
};


class BlaeckEventChannelRef
{
public:
  BlaeckEventChannelRef(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // Material Design Icons name, e.g. F("mdi:sine-wave").
  BlaeckEventChannelRef &withIcon(const __FlashStringHelper *icon)
  {
#if BLAECK_ENABLE_EVENTS
    if (_index >= 0 && _owner != nullptr)
      _owner->_eventChannels[_index].icon = icon;
#else
    (void)icon;
#endif
    return *this;
  }

  // Groups the entity under Home Assistant's diagnostic section.
  BlaeckEventChannelRef &diagnostic(bool on = true)
  {
#if BLAECK_ENABLE_EVENTS
    if (_index >= 0 && _owner != nullptr)
      _owner->_eventChannels[_index].diagnostic = on;
#else
    (void)on;
#endif
    return *this;
  }

  // What the channel reports: F("button"), F("doorbell") or F("motion") - those three and no
  // others. Carried as written, like a signal's, but Home Assistant validates this one against
  // an enum, so a name it does not know fails discovery and the entity never appears.
  //
  // F("doorbell") also requires the channel to declare a "ring" event type. Home Assistant warns
  // without it today and stops accepting it in 2027.4. F("button") has standard names too -
  // press_start, press_end, long_press_start, long_press_end, multi_press_ongoing,
  // multi_press_end - but none are required; declare only what the hardware can produce.
  BlaeckEventChannelRef &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
#if BLAECK_ENABLE_EVENTS
    if (_index >= 0 && _owner != nullptr)
      _owner->_eventChannels[_index].deviceClass = deviceClass;
#else
    (void)deviceClass;
#endif
    return *this;
  }

  // Registers the entity but leaves it switched off until someone enables it.
  BlaeckEventChannelRef &disabledByDefault(bool on = true)
  {
#if BLAECK_ENABLE_EVENTS
    if (_index >= 0 && _owner != nullptr)
      _owner->_eventChannels[_index].disabledByDefault = on;
#else
    (void)on;
#endif
    return *this;
  }

private:
  BlaeckSerial *_owner;
  int16_t _index;
};

#endif //  BLAECKSERIAL_H
