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
  #if defined(__AVR__)
    #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 48
  #else
    #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 96
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

struct Signal
{
  String SignalName;
  dataType DataType;
  void *Address;
  bool Updated = false;
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

// paramCount is whatever the frame carried, so it can be 0: for number, switch and select an
// absent value is left to the handler, which is what makes query and toggle usage possible.
// Text is the exception - an empty value is a value there (it clears the field), so a text
// handler always receives exactly one parameter.
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
  BLAECK_STATE_MESSAGE = 1 // a message channel the command owns (see BlaeckOwnState)
};

// Where a typed command's state comes from. Constructed implicitly from a name, so the
// common case needs no extra syntax and reads exactly as it always did:
//
//   onNumberCommand("SET_FREQ", onSetFreq, F("Frequency"), 0.0f, 2.0f, 0.01f);  // a signal
//   onTextCommand("SET_LABEL", onSetLabel, nullptr, 32);                        // no state
//
// The other form is spelled out, because it does more than name something:
//
//   onNumberCommand("SET_OFFSET", onSetOffset,
//                   BlaeckOwnState(F("Offset"), OffsetState), -100.0f, 100.0f, 0.1f);
//
// which makes the command carry its own state rather than mirror a signal. Registering it
// announces the value once, so a host that was already connected when the board reset sees the
// state the device came up with. The channel belongs to the command and is not offered as a
// sensor of its own: the control is where that value is read.
struct BlaeckCommandState
{
  const __FlashStringHelper *name = nullptr;
  BlaeckStateTextGetter getStateText = nullptr;
  uint8_t source = BLAECK_STATE_SIGNAL;

  // Deliberately not explicit: a bare F("Frequency") or nullptr has to keep working.
  BlaeckCommandState(const __FlashStringHelper *signalName = nullptr)
      : name(signalName) {}

  BlaeckCommandState(const __FlashStringHelper *channelName, BlaeckStateTextGetter getter, uint8_t src)
      : name(channelName), getStateText(getter), source(src) {}
};

// State the command carries itself: it declares a message channel of that name, asks the
// getter for the value whenever a host polls the channel catalog, and announces it once at
// registration - so a host already connected when the board resets is corrected.
//
// The channel belongs to the command and cannot be reached from the sketch: addMessageChannel()
// and writeMessage() both refuse that name. Its value comes from the getter and nowhere else,
// which is what makes the catalog and the pushes agree. Push a change with
// writeCommandState(); see BlaeckStateTextGetter for what the getter may do.
//
// Independent of the signal table, so a device that adds no signals can still report what its
// controls are set to. Requires BLAECK_ENABLE_COMMAND_META - without it the typed helpers keep
// no metadata, so no channel is declared and no state is published.
inline BlaeckCommandState BlaeckOwnState(const __FlashStringHelper *name, BlaeckStateTextGetter getStateText)
{
  return BlaeckCommandState(name, getStateText, BLAECK_STATE_MESSAGE);
}

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
  BLAECK_ACK_TOO_LONG = 5      // rejected: text value longer than the advertised max length
};

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
  // Add a Signal
  void addSignal(String signalName, bool *value);
  void addSignal(String signalName, byte *value);
  void addSignal(String signalName, short *value);
  void addSignal(String signalName, unsigned short *value);
  void addSignal(String signalName, int *value);
  void addSignal(String signalName, unsigned int *value);
  void addSignal(String signalName, long *value);
  void addSignal(String signalName, unsigned long *value);
  void addSignal(String signalName, float *value);
  void addSignal(String signalName, double *value);
  void addSignal(String signalName, const char *value);

  // Delete all Signals
  void deleteSignals();
  bool hasSignalOverflow() const { return _signalOverflowOccurred; }
  uint16_t getSignalOverflowCount() const { return _signalOverflowCount; }

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
  bool addMessageChannel(const char *channelName);
  bool addMessageChannel(const char *channelName, const __FlashStringHelper *icon);
  // `diagnostic` groups the sensor under the HA device's diagnostic section.
  bool addMessageChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic);
  // `getStateText` makes this channel report a current value: the library calls it
  // while building the 0x90 catalog, so a host that polls learns the value as it is
  // at that moment and the sketch never has to push it just to keep the host in step.
  // Because the value is fetched rather than stored, it cannot go stale - a change
  // made anywhere in the sketch is reported correctly without remembering to update
  // anything. Build the text in a function-local static and return it. Pass nullptr
  // for a plain log channel, which then carries no value in the catalog.
  // The catalog is read when the host asks for it, so a host that already holds one
  // keeps the value it read - across a device restart included, where the sketch's
  // variables are back at their startup values. Call writeMessage() once in setup()
  // to announce that too.
  bool addMessageChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic,
                         BlaeckStateTextGetter getStateText);
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
  bool addEventChannel(const char *channelName);
  bool addEventChannel(const char *channelName, const __FlashStringHelper *icon);
  // `diagnostic` groups the entity under the HA device's diagnostic section.
  bool addEventChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic);
  // Declares the channel and its event types in one call: `eventTypes` is a
  // comma-separated list, F("started,stopped"), read left to right so position defines
  // each type's index exactly as call order does with addEventType(). Equivalent to
  // calling addEventType() once per name, and mixable with it - use this for a set
  // known at compile time, addEventType() when the list is built conditionally.
  // Returns false if the channel could not be declared; individual types that do not
  // fit the pool are dropped and reported on DebugRef, as with addEventType().
  bool addEventChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic,
                       const __FlashStringHelper *eventTypes);

  // Append an event type to a declared channel. Call order defines the index
  // used on the wire: the first type added to a channel is index 0, the next 1.
  // `eventType` must outlive the call (use F("...")). Types are held in one pool
  // shared by all channels. Returns false if the channel was never declared, the
  // pool is full, or the channel already has that type.
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
  bool onCommand(const char *command, BlaeckCommandHandler handler);
  void onAnyCommand(BlaeckAnyCommandHandler handler);
  void clearAllCommandHandlers();

  // ----- Typed command registration (Home Assistant discovery metadata) -----
  // Same runtime behavior as onCommand(), but attach metadata so the device can
  // describe the command in a 0xA0 "Command List" frame (BLAECK.WRITE_COMMANDS).
  // stateSignal (nullable): name of the signal that mirrors this command's value
  // (closed-loop -> HA state_topic + logged); pass nullptr for an optimistic /
  // open-loop control. All metadata strings must be F()/PROGMEM literals with
  // program lifetime (stored as pointers, never copied).
  // Number values outside [min,max], bad select indices and non-0/1 switch
  // values are rejected (handler skipped) and reported on DebugRef.
  // step is HA display resolution only; the firmware does not round. It is the one number
  // that can be left unspecified: pass 0 and the host omits it, letting Home Assistant apply
  // its own default. min and max have no such escape - they are the range the firmware itself
  // validates against, so every number command must state them.
  // Home Assistant refuses a step below 0.001, so a finer resolution than that cannot be
  // shown; the host raises it to 0.001 rather than let the whole entity be rejected.
  // category (optional): Home Assistant entity_category. Leave at BLAECK_CAT_NONE for a
  // primary control; BLAECK_CAT_CONFIG marks a device setting, which Home Assistant then
  // keeps off its auto-generated dashboards.
  // state: the signal that mirrors this command's value, given as a bare F("Name") - or
  // BlaeckOwnState(F("Name"), getter) to have the command carry its own state instead.
  // nullptr for an optimistic / open-loop control. See BlaeckCommandState.
  bool onNumberCommand(const char *command, BlaeckCommandHandler handler,
                       BlaeckCommandState state,
                       float min, float max, float step,
                       const __FlashStringHelper *unit = nullptr,
                       BlaeckEntityCategory category = BLAECK_CAT_NONE);
  bool onSwitchCommand(const char *command, BlaeckCommandHandler handler,
                       BlaeckCommandState state,
                       BlaeckEntityCategory category = BLAECK_CAT_NONE);
  bool onSelectCommand(const char *command, BlaeckCommandHandler handler,
                       BlaeckCommandState state,
                       const __FlashStringHelper *optionsCsv,
                       BlaeckEntityCategory category = BLAECK_CAT_NONE);
  bool onButtonCommand(const char *command, BlaeckCommandHandler handler,
                       BlaeckEntityCategory category = BLAECK_CAT_NONE);
  // Copies option `index` of a select command's declared list into `out`, so a sketch can
  // render the option it just selected without keeping a second copy of the names. The
  // list already lives in flash; this is the only way to read it back.
  // Returns false and leaves `out` empty if the command is not a declared select, the
  // index is past the end, or the option would not fit - a truncated name is no use,
  // since a host matches state against the declared options exactly.
  bool getSelectOption(const char *command, byte index, char *out, byte outSize) const;
  // HA text entity: the host sends the value percent-encoded (so commas and other
  // delimiters survive the frame); the device percent-decodes it in place before
  // the handler runs, so the handler receives the raw UTF-8 text. maxLength is the
  // advertised limit (in decoded bytes) enforced before dispatch; a longer value
  // is rejected (BLAECK_ACK_TOO_LONG). The handler always receives exactly one
  // parameter: an empty value is a value (it clears the field), and a host sending
  // no parameter at all is normalized to an empty string before dispatch.
  bool onTextCommand(const char *command, BlaeckCommandHandler handler,
                     BlaeckCommandState state = BlaeckCommandState(),
                     unsigned int maxLength = 255,
                     BlaeckEntityCategory category = BLAECK_CAT_NONE);

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
  int findSignalIndex(String signalName);
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
#if BLAECK_ENABLE_COMMAND_META
  void writeCommandsFrame(unsigned long MessageID);
  void _annotateCommand(const char *command, uint8_t kind,
                        const __FlashStringHelper *stateSignal,
                        float mn, float mx, float st,
                        const __FlashStringHelper *unit,
                        const __FlashStringHelper *options,
                        uint8_t category,
                        uint8_t stateSource);
  byte _validateTypedCommand(byte handlerIndex);
  // Declares the channel a typed command owns. Separate from addMessageChannel() so that one
  // can refuse an owned name outright rather than needing a "unless it is mine" exception.
  bool _addOwnedMessageChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText);
  // Declares and announces the channel for a command registered with BlaeckOwnState. A no-op
  // for any other kind of state, so every typed helper can call it unconditionally.
  void _declareOwnState(const char *command, const BlaeckCommandState &state);

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
  bool _signalOverflowOccurred = false;
  uint16_t _signalOverflowCount = 0;

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
    // Declared by a typed command through BlaeckOwnState, which makes the channel that
    // command's alone: addMessageChannel() and writeMessage() both refuse the name, so the
    // value on its topic can only ever come from the getter above.
    bool ownedByCommand = false;
    bool diagnostic = false;
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
    bool diagnostic = false;
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
  // Stands in for a text command's absent value. Must be writable: the text branch
  // percent-decodes params[0] in place, which a string literal could not survive.
  char _emptyTextScratch[1] = {0};
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
};

#endif //  BLAECKSERIAL_H
