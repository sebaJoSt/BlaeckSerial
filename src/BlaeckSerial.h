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

#ifndef BLAECK_COMMAND_MAX_HANDLERS_DEFAULT
  #if defined(__AVR__)
    // Scale with available SRAM: each handler entry costs roughly
    // MAX_COMMAND_NAME_COUNT + a function pointer (~28 bytes on AVR).
    // Larger-SRAM AVRs (Mega 2560, ATmega1284, ...) get a generous limit;
    // small ones (Uno/Nano/Leonardo) get a modest one to conserve SRAM.
    #if defined(RAMEND) && (RAMEND >= 0x10FF)
      #define BLAECK_COMMAND_MAX_HANDLERS_DEFAULT 12
    #else
      #define BLAECK_COMMAND_MAX_HANDLERS_DEFAULT 6
    #endif
  #else
    #define BLAECK_COMMAND_MAX_HANDLERS_DEFAULT 12
  #endif
#endif

#ifndef BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT
  #if defined(__AVR__)
    #define BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT 24
  #else
    #define BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT 40
  #endif
#endif

#ifndef BLAECK_COMMAND_MAX_PARAMS_DEFAULT
  #define BLAECK_COMMAND_MAX_PARAMS_DEFAULT 10
#endif

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

// Messages (Home Assistant text/log channels).
// When ON, the device can declare named message channels with
// addMessageChannel(), emit the 0x90 "Message Channel List" frame in response to
// BLAECK.WRITE_MESSAGE_CHANNELS, and send free-text lines on those channels with
// writeMessage() (0x95). Turn OFF to save SRAM/flash on tiny targets; both
// writeMessage() and addMessageChannel() then compile away, and
// BLAECK.WRITE_MESSAGE_CHANNELS answers with an empty 0x90 so a polling host
// does not wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_MESSAGES
  #define BLAECK_ENABLE_MESSAGES 1
#endif

// Two things create a message channel, and both spend a slot here: addMessageChannel(), and a
// typed command's withOwnState(), which gives that command a channel of its own to carry its
// value on.
#ifndef BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT
  #if defined(__AVR__)
    // Each entry costs BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT + ~3 bytes of SRAM.
    #if defined(RAMEND) && (RAMEND >= 0x10FF)
      #define BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT 6
    #else
      #define BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT 3
    #endif
  #else
    #define BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT 8
  #endif
#endif

#ifndef BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT
  #if defined(__AVR__)
    #define BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT 16
  #else
    #define BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT 32
  #endif
#endif

// Events (Home Assistant event entities).
// When ON, the device can declare named event channels with addEventChannel(),
// give each a closed list of event types with addEventType(), emit the 0x80
// "Event Channel List" frame in response to BLAECK.WRITE_EVENT_CHANNELS, and
// report an occurrence with writeEvent() (0x85).
// Unlike a message, an event carries no text: the frame holds only the channel
// and event type indices, so the wording is fixed at compile time and a host
// needs the 0x80 catalog to interpret it. Use a message channel for anything
// that has to carry a runtime value.
// Turn OFF to save SRAM/flash on tiny targets; the API then compiles away, and
// BLAECK.WRITE_EVENT_CHANNELS answers with an empty 0x80 so a polling host does
// not wait out its timeout.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_EVENTS
  #define BLAECK_ENABLE_EVENTS 1
#endif

#ifndef BLAECK_EVENT_MAX_CHANNELS_DEFAULT
  #if defined(__AVR__)
    // Each entry costs BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT + ~3 bytes of SRAM.
    #if defined(RAMEND) && (RAMEND >= 0x10FF)
      #define BLAECK_EVENT_MAX_CHANNELS_DEFAULT 4
    #else
      #define BLAECK_EVENT_MAX_CHANNELS_DEFAULT 2
    #endif
  #else
    #define BLAECK_EVENT_MAX_CHANNELS_DEFAULT 6
  #endif
#endif

#ifndef BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT
  #if defined(__AVR__)
    #define BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT 16
  #else
    #define BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT 32
  #endif
#endif

// Event types are held in one pool shared by every channel, so a channel that
// needs ten types and one that needs two are both served without sizing every
// channel for the worst case. Each entry costs ~3 bytes of SRAM.
#ifndef BLAECK_EVENT_MAX_TYPES_DEFAULT
  #if defined(__AVR__)
    #if defined(RAMEND) && (RAMEND >= 0x10FF)
      #define BLAECK_EVENT_MAX_TYPES_DEFAULT 16
    #else
      #define BLAECK_EVENT_MAX_TYPES_DEFAULT 8
    #endif
  #else
    #define BLAECK_EVENT_MAX_TYPES_DEFAULT 24
  #endif
#endif


typedef enum DataType
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
// (0xF0 SignalMetaFlags bits 3-4). MEASUREMENT is a value that goes up and down
// and is meaningful at any instant; TOTAL and TOTAL_INCREASING are running sums,
// the latter one that only ever grows and may reset to zero.
//
// NONE is what a signal that never called withStateClass() carries, and a host
// keeps no statistics on it. Nothing is assumed on a signal's behalf: a value
// that should be graphed over time has to say so.
enum BlaeckStateClass
{
  BLAECK_STATE_CLASS_NONE = 0,
  BLAECK_STATE_CLASS_MEASUREMENT = 1,
  BLAECK_STATE_CLASS_TOTAL = 2,
  BLAECK_STATE_CLASS_TOTAL_INCREASING = 3
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
  BLAECK_SIG_STATE_CLASS_MASK = 0x0018, // bits 3-4
  BLAECK_SIG_DIAGNOSTIC = 0x0020,
  BLAECK_SIG_DISABLED_BY_DEFAULT = 0x0040,
  BLAECK_SIG_FORCE_UPDATE = 0x0080,
  BLAECK_SIG_HAS_DISPLAY_PRECISION = 0x0100,
  BLAECK_SIG_HAS_OPTIONS = 0x0200
};
static const byte BLAECK_SIG_STATE_CLASS_SHIFT = 3;

struct Signal
{
  String SignalName;
  dataType DataType;
  void *Address;
  bool Updated = false;
#if BLAECK_ENABLE_SIGNAL_META
  // Flash pointers, so a declared unit costs 2 bytes of SRAM and not its length.
  const __FlashStringHelper *Unit = nullptr;
  const __FlashStringHelper *DeviceClass = nullptr;
  const __FlashStringHelper *Icon = nullptr;
  const __FlashStringHelper *Options = nullptr;
  uint16_t MetaFlags = 0;
  uint8_t DisplayPrecision = 0;
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

// Supplies a message channel's current value on demand. Called while the 0x90 catalog frame is
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
  BLAECK_STATE_MESSAGE = 1 // a message channel the command owns (see withOwnState())
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
class BlaeckMessageChannelRef;
class BlaeckEventChannelRef;

class BlaeckSerial
{
public:
  // ----- Constructor -----
  BlaeckSerial();

  // ----- Destructor -----
  ~BlaeckSerial();

  // ----- Initialize -----
  void begin(Stream *Ref, unsigned int Size);
  void begin(Stream *Ref, unsigned int Size, Stream *DebugRef);

  // Set these variables in your Arduino sketch
  String DeviceName = "Unknown";
  String DeviceHWVersion = "n/a";
  String DeviceFWVersion = "n/a";

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
  BlaeckBoolSignalRef addSignal(String signalName, bool *value);
  BlaeckNumericSignalRef addSignal(String signalName, byte *value);
  BlaeckNumericSignalRef addSignal(String signalName, short *value);
  BlaeckNumericSignalRef addSignal(String signalName, unsigned short *value);
  BlaeckNumericSignalRef addSignal(String signalName, int *value);
  BlaeckNumericSignalRef addSignal(String signalName, unsigned int *value);
  BlaeckNumericSignalRef addSignal(String signalName, long *value);
  BlaeckNumericSignalRef addSignal(String signalName, unsigned long *value);
  BlaeckNumericSignalRef addSignal(String signalName, float *value);
  BlaeckNumericSignalRef addSignal(String signalName, double *value);
  BlaeckTextSignalRef addSignal(String signalName, const char *value);

  // Delete all Signals
  void deleteSignals();
  // Signals that could not be added - past the capacity begin() was given, or all of them when
  // the board had no RAM for the table at all, which is why the flag can be set before the
  // first addSignal(). Named like hasRejectedCommands() and hasRejectedChannels(): three tables,
  // one question, one shape of answer.
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

  // ----- Messages (Home Assistant text/log channel, 0x95) -----
  // With BLAECK_ENABLE_MESSAGES=0 these still compile but do nothing, so a
  // sketch can be built for a tiny target without being rewritten.
  //
  // Declare a message channel. Channels must be declared up-front (typically in
  // setup()) so the host can announce every text sensor before the first line
  // arrives; writeMessage() on an undeclared channel is dropped.
  // `channelName` is copied; `icon` must outlive the call (use a string literal
  // or F("mdi:...")). Returns false if the name is empty/too long or the table
  // is full.
  BlaeckMessageChannelRef addMessageChannel(const char *channelName);
  void clearAllMessageChannels();

  // Send the 0x90 "Message Channel List" frame (the declared-channel catalog).
  // Sent automatically in response to BLAECK.WRITE_MESSAGE_CHANNELS.
  void writeMessageChannels();
  void writeMessageChannels(unsigned long messageID);

  // Send a free-text status/log message on a declared channel to the serial host.
  // Fire-and-forget: a host may surface it (e.g. a Home Assistant text sensor
  // per declared channel) but it is never stored as signal data. The frame
  // carries the channel's index in the 0x90 catalog rather than its name, so the
  // catalog must reach the host first. No CRC (like the 0xA0/0xA5 frames). Text
  // longer than 65535 bytes is truncated. Messages on channels that were never
  // passed to addMessageChannel() are dropped.
  void writeMessage(const char *channelName, const char *text);
  void writeMessage(const char *channelName, const char *text, unsigned long messageID);

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
  // Update value and write directly - by name
  void write(String signalName, bool value);
  void write(String signalName, byte value);
  void write(String signalName, short value);
  void write(String signalName, unsigned short value);
  void write(String signalName, int value);
  void write(String signalName, unsigned int value);
  void write(String signalName, long value);
  void write(String signalName, unsigned long value);
  void write(String signalName, float value);
  void write(String signalName, double value);
  void write(String signalName, const char *value);

  void write(String signalName, bool value, unsigned long messageID);
  void write(String signalName, byte value, unsigned long messageID);
  void write(String signalName, short value, unsigned long messageID);
  void write(String signalName, unsigned short value, unsigned long messageID);
  void write(String signalName, int value, unsigned long messageID);
  void write(String signalName, unsigned int value, unsigned long messageID);
  void write(String signalName, long value, unsigned long messageID);
  void write(String signalName, unsigned long value, unsigned long messageID);
  void write(String signalName, float value, unsigned long messageID);
  void write(String signalName, double value, unsigned long messageID);
  void write(String signalName, const char *value, unsigned long messageID);

  void write(String signalName, bool value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, byte value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, short value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, unsigned short value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, int value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, unsigned int value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, long value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, unsigned long value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, float value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, double value, unsigned long messageID, unsigned long long timestamp);
  void write(String signalName, const char *value, unsigned long messageID, unsigned long long timestamp);

  // Index of a registered signal, or -1 if there is none by that name. Resolve once in setup()
  // and use the by-index calls below on anything that runs often: the by-name calls build a
  // temporary String from their argument on every call, which is a heap allocation per write.
  int findSignalIndex(String signalName);

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
  void update(String signalName, bool value);
  void update(String signalName, byte value);
  void update(String signalName, short value);
  void update(String signalName, unsigned short value);
  void update(String signalName, int value);
  void update(String signalName, unsigned int value);
  void update(String signalName, long value);
  void update(String signalName, unsigned long value);
  void update(String signalName, float value);
  void update(String signalName, double value);

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
  void markSignalUpdated(String signalName);
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

  // Message channels that could not be declared - a full table, a name too long, a name a
  // command already owns - each reported on DebugRef and counted here. A command's
  // withOwnState() channel counts too: when it cannot be declared the command keeps no state at
  // all, so a full message channel table costs a control's value, not just a channel.
  bool hasRejectedMessageChannels() const { return _rejectedMessageChannelCount > 0; }
  uint16_t getRejectedMessageChannelCount() const { return _rejectedMessageChannelCount; }

  // Event channels and event types that could not be declared. Counted together because both
  // sit behind BLAECK_ENABLE_EVENTS and a type belongs to a channel, so either answer sends you
  // to the same place - BLAECK_EVENT_MAX_CHANNELS_DEFAULT or BLAECK_EVENT_MAX_TYPES_DEFAULT,
  // and DebugRef says which.
  bool hasRejectedEventChannels() const { return _rejectedEventChannelCount > 0; }
  uint16_t getRejectedEventChannelCount() const { return _rejectedEventChannelCount; }

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
  // A number command must state its range, since that is what the firmware validates against;
  // one that never calls withRange() accepts anything. Values outside [min,max], bad select
  // indices and non-0/1 switch values are rejected before dispatch and reported on DebugRef.
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
  void setSignalName(int signalIndex, String signalName);
  void _setTimedDataState(bool timedActivated, unsigned long timedInterval_ms);
  void _parseCommandTokens(const char *raw);
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
  int _registerSignal(const String &signalName, dataType type, void *address);
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
  int _registerMessageChannel(const char *channelName);
  int _registerEventChannel(const char *channelName, const __FlashStringHelper *eventTypes);
  // Appends one pool entry per field of a comma-separated list, in order, so a field's position
  // is its wire index - the same rule call order gives addEventType().
  void _addEventTypesCsv(byte channelIndex, const __FlashStringHelper *eventTypes);
#if BLAECK_ENABLE_COMMAND_META
  void writeCommandsFrame(unsigned long MessageID);
  byte _validateTypedCommand(byte handlerIndex);
  // Declares the channel a typed command owns. Separate from addMessageChannel() so that one
  // can refuse an owned name outright rather than needing a "unless it is mine" exception.
  bool _addOwnedMessageChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText);
  // Declares and announces the channel a command carries itself, for withOwnState(). Announcing
  // at registration is what corrects a host that was already connected when the board reset.
  // False when the channel could not be declared, so withOwnState() can leave the command
  // without state rather than advertising a channel absent from the 0x90 catalog.
  bool _declareOwnState(byte handlerIndex, const __FlashStringHelper *channelName,
                        BlaeckStateTextGetter getStateText);

  static void _percentDecodeInPlace(char *s);
  static long _flashCsvIndexOf(const __FlashStringHelper *csv, const char *value);
#endif
  // Number of comma-separated fields in a flash CSV. Deliberately outside the
  // command-metadata guard: it counts a select command's options and an event
  // channel's type list, and those features are enabled independently.
  static byte _flashCsvOptionCount(const __FlashStringHelper *csv);
#if BLAECK_ENABLE_MESSAGES
  void writeMessageChannelsFrame(unsigned long MessageID);
  // Index of a declared channel, or -1 when the name was never declared.
  int _findMessageChannel(const char *channelName) const;
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
  int _signalIndex = 0;
  unsigned int _signalCapacity = 0;
  bool _signalRegistrationFailed = false;
  uint16_t _rejectedSignalCount = 0;
  uint16_t _rejectedCommandCount = 0;
  uint16_t _rejectedMessageChannelCount = 0;
  uint16_t _rejectedEventChannelCount = 0;

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

  static const int MAXIMUM_CHAR_COUNT = BLAECK_COMMAND_MAX_CHARS_DEFAULT;
  static const byte MAX_COMMAND_HANDLERS = BLAECK_COMMAND_MAX_HANDLERS_DEFAULT;
  static const byte MAX_COMMAND_PARAM_COUNT = BLAECK_COMMAND_MAX_PARAMS_DEFAULT;
  static const byte MAX_COMMAND_NAME_COUNT = BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT;
#if BLAECK_ENABLE_MESSAGES
  static const byte MAX_MESSAGE_CHANNELS = BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT;
  static const byte MAX_MESSAGE_NAME_COUNT = BLAECK_MESSAGE_MAX_NAME_CHARS_DEFAULT;
#endif
#if BLAECK_ENABLE_EVENTS
  static const byte MAX_EVENT_CHANNELS = BLAECK_EVENT_MAX_CHANNELS_DEFAULT;
  static const byte MAX_EVENT_NAME_COUNT = BLAECK_EVENT_MAX_NAME_CHARS_DEFAULT;
  static const byte MAX_EVENT_TYPES = BLAECK_EVENT_MAX_TYPES_DEFAULT;
#endif
  char receivedChars[MAXIMUM_CHAR_COUNT];
  char COMMAND[MAXIMUM_CHAR_COUNT] = {0};
  int PARAMETER[10];
  // STRING_01: Max. 15 chars allowed  + Null Terminator '\0' = 16
  // In case more than 15 chars are sent, the rest is cut off in function void parseData()
  char STRING_01[16];

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
  void _bufStr0(const String &s)
  {
    _bufStr(s.c_str());
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
  void _bufDevice(const String &name,
                  const String &hw, const String &fw);

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
  CommandHandlerEntry _commandHandlers[MAX_COMMAND_HANDLERS];
#if BLAECK_ENABLE_MESSAGES
  struct MessageChannelEntry
  {
    char name[MAX_MESSAGE_NAME_COUNT];
    const __FlashStringHelper *icon = nullptr;
    // Asked for the channel's value while the 0x90 catalog is built, so what the
    // catalog reports cannot lag behind the sketch - there is no stored copy to
    // go stale, and nothing the sketch has to remember to refresh.
    BlaeckStateTextGetter getStateText = nullptr;
    const __FlashStringHelper *deviceClass = nullptr;
    const __FlashStringHelper *options = nullptr;
    // Declared by a typed command through withOwnState(), which makes the channel that
    // command's alone: addMessageChannel() and writeMessage() both refuse the name, so the
    // value on its topic can only ever come from the getter above.
    bool ownedByCommand = false;
    bool diagnostic = false;
    bool disabledByDefault = false;
    bool forceUpdate = false;
    bool inUse = false;
  };
  MessageChannelEntry _messageChannels[MAX_MESSAGE_CHANNELS];

  // Equality between a flash string and a RAM one. strcmp_P reads its FIRST argument from RAM,
  // which is the wrong way round here, so the flash side is read with pgm_read_byte.
  static bool _flashStringEqualsName(const __FlashStringHelper *flashName, const char *name);
  // The 0x95 frame itself, by channel index. Reached by writeMessage() after its guards and by
  // writeCommandState() for a channel those guards deliberately refuse.
  void _writeMessageFrame(int channelIndex, const char *text, unsigned long messageID);
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
  EventChannelEntry _eventChannels[MAX_EVENT_CHANNELS];

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
  EventTypeEntry _eventTypes[MAX_EVENT_TYPES];
  byte _eventTypeCount = 0;
#endif
  BlaeckAnyCommandHandler _anyCommandHandler = nullptr;
  char _parsedTokenBuffer[MAXIMUM_CHAR_COUNT] = {0};
  char _parsedCommand[MAX_COMMAND_NAME_COUNT] = {0};
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
#if BLAECK_ENABLE_MESSAGES
  // Monotonic message id stamped into the 0x95 Message frame header.
  unsigned long _messageMsgId = 0;
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
  void parseData();

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
  friend class BlaeckMessageChannelRef;
  friend class BlaeckEventChannelRef;
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
        e->stateSource = BLAECK_STATE_MESSAGE;
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
  /* State the command carries itself: it declares a message channel of that name - taking a */ \
  /* slot from BLAECK_MESSAGE_MAX_CHANNELS_DEFAULT like any other - and asks */                 \
  /* the getter for the value, instead of mirroring a signal. The channel belongs to the */  \
  /* command - addMessageChannel() and writeMessage() both refuse the name - so what the */  \
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
};

class BlaeckSwitchCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckSwitchCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckSwitchCommandRef)
};

class BlaeckSelectCommandRef : public BlaeckCommandRefBase
{
public:
  BlaeckSelectCommandRef(BlaeckSerial *owner, int16_t index) : BlaeckCommandRefBase(owner, index) {}
  BLAECK_COMMAND_REF_SHARED(BlaeckSelectCommandRef)

  // The closed list this control offers, comma-separated. A value that is not an index into it
  // is rejected before dispatch. getSelectOption() reads a name back out, so the list lives in
  // flash once instead of being repeated in the sketch.
  BlaeckSelectCommandRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
    _setOptions(optionsCsv);
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

  // The signal this handle names, or nullptr when the table was full - which is
  // what makes every method safe to call unconditionally.
  Signal *_signal() const
  {
    if (_owner == nullptr || _index < 0 || _owner->Signals == nullptr)
      return nullptr;
    return &_owner->Signals[_index];
  }

  void _setFlash(const __FlashStringHelper *value, uint16_t bit)
  {
#if BLAECK_ENABLE_SIGNAL_META
    if (Signal *s = _signal())
    {
      // Together, so the bit and the field can never disagree.
      switch (bit)
      {
      case BLAECK_SIG_HAS_UNIT:         s->Unit = value; break;
      case BLAECK_SIG_HAS_DEVICE_CLASS: s->DeviceClass = value; break;
      default:                          s->Icon = value; break;
      }
      if (value != nullptr)
        s->MetaFlags |= bit;
      else
        s->MetaFlags &= (uint16_t)~bit;
    }
#else
    (void)value;
    (void)bit;
#endif
  }

  void _setBit(uint16_t bit, bool on)
  {
#if BLAECK_ENABLE_SIGNAL_META
    if (Signal *s = _signal())
    {
      if (on)
        s->MetaFlags |= bit;
      else
        s->MetaFlags &= (uint16_t)~bit;
    }
#else
    (void)bit;
    (void)on;
#endif
  }

  void _setStateClass(BlaeckStateClass stateClass)
  {
#if BLAECK_ENABLE_SIGNAL_META
    if (Signal *s = _signal())
    {
      s->MetaFlags &= (uint16_t)~BLAECK_SIG_STATE_CLASS_MASK;
      s->MetaFlags |= (uint16_t)(((uint16_t)stateClass << BLAECK_SIG_STATE_CLASS_SHIFT) &
                                 BLAECK_SIG_STATE_CLASS_MASK);
    }
#else
    (void)stateClass;
#endif
  }

  void _setOptions(const __FlashStringHelper *optionsCsv)
  {
#if BLAECK_ENABLE_SIGNAL_META
    if (Signal *s = _signal())
    {
      s->Options = optionsCsv;
      if (optionsCsv != nullptr)
        s->MetaFlags |= BLAECK_SIG_HAS_OPTIONS;
      else
        s->MetaFlags &= (uint16_t)~BLAECK_SIG_HAS_OPTIONS;
    }
#else
    (void)optionsCsv;
#endif
  }

  void _setDisplayPrecision(uint8_t decimals)
  {
#if BLAECK_ENABLE_SIGNAL_META
    if (Signal *s = _signal())
    {
      s->DisplayPrecision = decimals;
      s->MetaFlags |= BLAECK_SIG_HAS_DISPLAY_PRECISION;
    }
#else
    (void)decimals;
#endif
  }

  BlaeckSerial *_owner;
  int16_t _index;
};

// What every signal accepts, whatever it holds. Repeated per handle so the chain keeps its
// datatype all the way down, and so an editor offers exactly what applies when the dot is typed.
#define BLAECK_SIGNAL_REF_SHARED(TYPE)                                                    \
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
// Mirrors BlaeckMessageChannelRef: a string signal and a message channel become the same Home
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
// hasRejectedChannels() is where that shows up. The methods are always defined - with
// BLAECK_ENABLE_MESSAGES=0 or BLAECK_ENABLE_EVENTS=0 they store nothing, so a sketch needs no
// #ifdef.
//
// Mirrors BlaeckTextSignalRef: a message channel and a string signal become the same Home
// Assistant entity, so they carry the same fields. Change one, change the other. The five
// shared modifiers are spelled out here rather than taken from BLAECK_SIGNAL_REF_SHARED,
// because a channel keeps plain members where a signal keeps a flag word.
class BlaeckMessageChannelRef
{
public:
  BlaeckMessageChannelRef(BlaeckSerial *owner, int16_t index) : _owner(owner), _index(index) {}

  // Material Design Icons name, e.g. F("mdi:pulse").
  BlaeckMessageChannelRef &withIcon(const __FlashStringHelper *icon)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].icon = icon;
#else
    (void)icon;
#endif
    return *this;
  }

  // Groups the sensor under Home Assistant's diagnostic section.
  BlaeckMessageChannelRef &diagnostic(bool on = true)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].diagnostic = on;
#else
    (void)on;
#endif
    return *this;
  }

  // Makes the channel report a current value: the library calls the getter while building the
  // 0x90 catalog, so a host that polls learns the value as it is at that moment and the sketch
  // never pushes just to keep it in step. Because it is fetched rather than stored it cannot go
  // stale. Build the text in a function-local static and return it. Left out, the channel is a
  // plain log channel and carries no value in the catalog.
  BlaeckMessageChannelRef &withStateText(BlaeckStateTextGetter getStateText)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].getStateText = getStateText;
#else
    (void)getStateText;
#endif
    return *this;
  }

  // The closed set of values this channel reports, comma-separated. Home Assistant needs
  // withDeviceClass(F("enum")) alongside it and rejects the list without one. Every value
  // reported must be in the list: one that is not raises rather than being shown. A unit is
  // ignored alongside options rather than refused.
  BlaeckMessageChannelRef &withOptions(const __FlashStringHelper *optionsCsv)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].options = optionsCsv;
#else
    (void)optionsCsv;
#endif
    return *this;
  }

  // What the channel's text is, for a host that renders it: F("timestamp") or F("date") make a
  // channel carrying an ISO 8601 value show as a time rather than as the string it is. Only
  // meaningful on a device that knows what day it is - one with an RTC or a network clock.
  BlaeckMessageChannelRef &withDeviceClass(const __FlashStringHelper *deviceClass)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].deviceClass = deviceClass;
#else
    (void)deviceClass;
#endif
    return *this;
  }

  // Registers the entity but leaves it switched off until someone enables it.
  BlaeckMessageChannelRef &disabledByDefault(bool on = true)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].disabledByDefault = on;
#else
    (void)on;
#endif
    return *this;
  }

  // Report every line, even one identical to the last. A host otherwise collapses a repeated
  // line into the entry it already has, so a heartbeat that says the same thing each time
  // leaves no trace of having run.
  BlaeckMessageChannelRef &forceUpdate(bool on = true)
  {
#if BLAECK_ENABLE_MESSAGES
    if (_index >= 0 && _owner != nullptr)
      _owner->_messageChannels[_index].forceUpdate = on;
#else
    (void)on;
#endif
    return *this;
  }

private:
  BlaeckSerial *_owner;
  int16_t _index;
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

  // What the channel reports: F("button"), F("doorbell") or F("motion"). Home Assistant's
  // vocabulary, carried as written - this library does not hold the list, for the same reason
  // signals do not. It does validate it, so a name it does not know fails discovery and the
  // entity never appears.
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
