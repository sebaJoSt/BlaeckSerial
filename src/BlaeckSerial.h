/*
        File: BlaeckSerial.h
        Author: Sebastian Strobl

    A library which extends Serial functionality to transmit binary data.
    It supports Master/Slave I2C configuration to include data from slaves.
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

#include <Wire.h>
#include <Arduino.h>
#include <CRC32.h>
#include <CRC16.h>
#include <new>
#include <string.h>
#include <limits.h>

// ---------------------------------------------------------------------------
// Command routing / I2C command framing helpers (transport-agnostic).
//
// Pure helpers that parse the optional "@<slaveID>:" command routing prefix and
// build/reassemble the master->slave command frames carried over I2C. Kept
// dependency-free (only <stdint.h>/<string.h>) so the same logic is easy to
// reason about and reuse.
//
// Routing prefix grammar (see blaeck-protocol):
//     <@<slaveID>:NAME,arg1,...>
// A leading '@', 1..3 decimal digits (0..127), then ':' addresses a specific
// master/slave board. Loggbok prepends it so a master can forward a command to
// the correct board even when boards share command names. Frames without the
// prefix are executed locally (unchanged legacy behaviour).
// ---------------------------------------------------------------------------

// Maximum addressable I2C slave id (7-bit addressing).
#define BLAECK_ROUTING_MAX_SLAVE_ID 127

// I2C wire-mode byte that marks a single-shot master->slave command delivery.
#define BLAECK_WIRE_MODE_COMMAND 6

// I2C wire-mode byte that requests a slave's command-catalog blob (0xE0 entries)
// for master-side aggregation. The reply is length-prefixed: the first two bytes
// of the first chunk are the total blob length (little-endian uint16), followed
// by exactly that many bytes of concatenated command entries spread across
// BLAECK_WIRE_BUFFER_SIZE-sized requestFrom() chunks.
#define BLAECK_WIRE_MODE_COMMAND_LIST 5

// Writes a little-endian uint16 length into a 2-byte buffer.
static inline void blaeckPutLen16(uint8_t *out, uint16_t len)
{
  out[0] = (uint8_t)(len & 0xFF);
  out[1] = (uint8_t)((len >> 8) & 0xFF);
}

// Reads a little-endian uint16 length from a 2-byte buffer.
static inline uint16_t blaeckGetLen16(const uint8_t *in)
{
  return (uint16_t)in[0] | ((uint16_t)in[1] << 8);
}

// Parses an optional "@<slaveID>:" routing prefix at the start of `buf`.
// If present and well-formed (leading '@', 1..3 decimal digits forming an id in
// 0..127, then ':'), writes the id to *slaveID, shifts the remaining command
// text to the front of `buf` in place (NUL-terminated), and returns true.
// Otherwise leaves `buf` unchanged and returns false. `buf` must be NUL-terminated.
static inline bool blaeckParseRoutingTarget(char *buf, uint8_t *slaveID)
{
  if (buf == NULL || buf[0] != '@')
    return false;

  const char *p = buf + 1;
  unsigned int id = 0;
  int digits = 0;
  while (p[0] >= '0' && p[0] <= '9' && digits < 3)
  {
    id = id * 10u + (unsigned int)(p[0] - '0');
    p++;
    digits++;
  }

  if (digits == 0 || p[0] != ':' || id > BLAECK_ROUTING_MAX_SLAVE_ID)
    return false;

  p++; // consume ':'
  size_t remaining = strlen(p);
  memmove(buf, p, remaining + 1); // include the NUL

  if (slaveID != NULL)
    *slaveID = (uint8_t)id;
  return true;
}

// Builds a single-shot mode-6 command frame into `out`: the mode byte followed
// by the (possibly truncated) payload. Returns the total number of bytes written.
// `outSize` is the capacity of `out` (should be the I2C wire buffer size); the
// payload is truncated so the frame never exceeds it.
static inline int blaeckBuildCommandFrame(const char *payload, uint8_t *out, size_t outSize)
{
  if (out == NULL || outSize < 1)
    return 0;

  out[0] = BLAECK_WIRE_MODE_COMMAND;
  size_t maxPayload = outSize - 1;
  size_t len = payload != NULL ? strlen(payload) : 0;
  if (len > maxPayload)
    len = maxPayload;

  for (size_t i = 0; i < len; i++)
    out[1 + i] = (uint8_t)payload[i];

  return (int)(1 + len);
}

// Reassembles the command text from a received mode-6 frame (`bytes` includes
// the leading mode byte). Writes a NUL-terminated command string into `out` and
// returns its length, or -1 if `bytes` is not a mode-6 frame. The result is
// truncated to fit `outSize`.
static inline int blaeckReassembleCommand(const uint8_t *bytes, int numBytes, char *out, size_t outSize)
{
  if (out == NULL || outSize < 1)
    return -1;
  if (bytes == NULL || numBytes < 1 || bytes[0] != BLAECK_WIRE_MODE_COMMAND)
    return -1;

  size_t n = 0;
  for (int i = 1; i < numBytes && n < outSize - 1; i++)
    out[n++] = (char)bytes[i];
  out[n] = '\0';
  return (int)n;
}

// Allow user overrides via a config file in the sketch folder.
// Create BlaeckSerialConfig.h in your sketch to override defaults, e.g.:
//   #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
// PlatformIO users can also use build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
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
// the device can emit a 0xE0 "Command List" frame in response to
// BLAECK.WRITE_COMMANDS. Turn OFF to save SRAM/flash on tiny targets; the typed
// helpers then behave exactly like plain onCommand() (no metadata, no 0xE0).
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_ENABLE_COMMAND_META
  #define BLAECK_ENABLE_COMMAND_META 1
#endif

// I2C (Wire) buffer size used for slave→master data packing.
// Auto-detected from Wire.h platform defines; falls back to 32.
// Only increase if BOTH master and slave support larger buffers;
// the value should match on both sides.
// Override via BlaeckSerialConfig.h or build flag.
#ifndef BLAECK_WIRE_BUFFER_SIZE
  #if defined(BUFFER_LENGTH)
    // AVR and most platforms define BUFFER_LENGTH in Wire.h
    #define BLAECK_WIRE_BUFFER_SIZE BUFFER_LENGTH
  #elif defined(I2C_BUFFER_LENGTH)
    // ESP32 defines I2C_BUFFER_LENGTH in Wire.h
    #define BLAECK_WIRE_BUFFER_SIZE I2C_BUFFER_LENGTH
  #else
    #define BLAECK_WIRE_BUFFER_SIZE 32
  #endif
#endif

typedef enum MasterSlaveConfig
{
  Single,
  Master,
  Slave
} masterSlaveConfig;

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

typedef void (*BlaeckCommandHandler)(const char *command, const char *const *params, byte paramCount);
typedef void (*BlaeckAnyCommandHandler)(const char *command, const char *const *params, byte paramCount);

// Command kind for Home Assistant discovery (0xE0 Command List frame).
enum BlaeckCommandKind
{
  BLAECK_CMD_PLAIN = 0,  // registered via onCommand(): no HA entity, but listed in 0xE0 for command palettes
  BLAECK_CMD_NUMBER = 1, // HA number   (value in [min,max])
  BLAECK_CMD_SWITCH = 2, // HA switch   (0/1)
  BLAECK_CMD_SELECT = 3, // HA select   (index into optionsCsv)
  BLAECK_CMD_BUTTON = 4, // HA button   (no value)
  BLAECK_CMD_TEXT = 5    // HA text     (free text, percent-encoded on the wire)
};

// Acknowledgement reason for the 0xF0 Command Ack frame. Sent back to the
// serial host after a command is dispatched so a host (e.g. Loggbok) can confirm
// the command was applied and surface accept/reject feedback.
// status = 0 accepted, 1 rejected.
enum BlaeckCommandAckReason
{
  BLAECK_ACK_OK = 0,           // accepted: delivered to a handler, validation passed
  BLAECK_ACK_UNKNOWN = 1,      // rejected: no handler / could not deliver to target slave
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
  void beginMaster(Stream *Ref, unsigned int Size, uint32_t WireClockFrequency);
  void beginMaster(Stream *Ref, unsigned int Size, uint32_t WireClockFrequency, Stream *DebugRef);
  void beginSlave(Stream *Ref, unsigned int Size, byte SlaveID);
  void beginSlave(Stream *Ref, unsigned int Size, byte SlaveID, Stream *DebugRef);

  // Set these variables in your Arduino sketch
  String DeviceName = "Unknown";
  String DeviceHWVersion = "n/a";
  String DeviceFWVersion = "n/a";

  // ----- Signals -----
  // Add a Signal
  void addSignal(String signalName, bool *value, bool prefixSlaveID = true);
  void addSignal(String signalName, byte *value, bool prefixSlaveID = true);
  void addSignal(String signalName, short *value, bool prefixSlaveID = true);
  void addSignal(String signalName, unsigned short *value, bool prefixSlaveID = true);
  void addSignal(String signalName, int *value, bool prefixSlaveID = true);
  void addSignal(String signalName, unsigned int *value, bool prefixSlaveID = true);
  void addSignal(String signalName, long *value, bool prefixSlaveID = true);
  void addSignal(String signalName, unsigned long *value, bool prefixSlaveID = true);
  void addSignal(String signalName, float *value, bool prefixSlaveID = true);
  void addSignal(String signalName, double *value, bool prefixSlaveID = true);
  void addSignal(String signalName, char *value, bool prefixSlaveID = true);

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

  // ----- Commands (Home Assistant discovery catalog, 0xE0) -----
  void writeCommands();
  void writeCommands(unsigned long messageID);

  // ----- Messages (Home Assistant text/log channel, 0x90) -----
  // Send a free-text status/log message on a named channel to the serial host.
  // Fire-and-forget: a host may surface it (e.g. a Home Assistant text sensor
  // auto-created per channel name) but it is never stored as signal data. The
  // frame carries no CRC (like the 0xE0/0xF0 frames). Text longer than 65535
  // bytes is truncated.
  //
  // Only Single and Master boards write directly to the serial host. On a Slave
  // this is a no-op (a debug message is emitted if a debug stream is set): a
  // slave has no direct link to the host, and I2C message forwarding is not
  // implemented. Report slave status from the master instead.
  void writeMessage(const char *channelName, const char *text);
  void writeMessage(const char *channelName, const char *text, unsigned long messageID);

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
  // Deprecated: use onCommand(...) / onAnyCommand(...)
  void setCommandCallback(void (*callback)(char *command, int *parameter, char *string_01));
  bool onCommand(const char *command, BlaeckCommandHandler handler);
  void onAnyCommand(BlaeckAnyCommandHandler handler);
  void clearAllCommandHandlers();

  // ----- Typed command registration (Home Assistant discovery metadata) -----
  // Same runtime behavior as onCommand(), but attach metadata so the device can
  // describe the command in a 0xE0 "Command List" frame (BLAECK.WRITE_COMMANDS).
  // stateSignal (nullable): name of the signal that mirrors this command's value
  // (closed-loop -> HA state_topic + logged); pass nullptr for an optimistic /
  // open-loop control. All metadata strings must be F()/PROGMEM literals with
  // program lifetime (stored as pointers, never copied).
  // Number values outside [min,max], bad select indices and non-0/1 switch
  // values are rejected (handler skipped) and reported on DebugRef.
  // step is HA display resolution only; the firmware does not round.
  bool onNumberCommand(const char *command, BlaeckCommandHandler handler,
                       const __FlashStringHelper *stateSignal,
                       float min, float max, float step,
                       const __FlashStringHelper *unit = nullptr);
  bool onSwitchCommand(const char *command, BlaeckCommandHandler handler,
                       const __FlashStringHelper *stateSignal);
  bool onSelectCommand(const char *command, BlaeckCommandHandler handler,
                       const __FlashStringHelper *stateSignal,
                       const __FlashStringHelper *optionsCsv);
  bool onButtonCommand(const char *command, BlaeckCommandHandler handler);
  // HA text entity: the host sends the value percent-encoded (so commas and other
  // delimiters survive the frame); the device percent-decodes it in place before
  // the handler runs, so the handler receives the raw UTF-8 text. maxLength is the
  // advertised limit (in decoded bytes) enforced before dispatch; a longer value
  // is rejected (BLAECK_ACK_TOO_LONG). Like the other typed commands this works on
  // Single/Master boards and on slaves reached via the "@<slaveID>:" routing
  // prefix (the owning board decodes and length-checks its own text command).
  bool onTextCommand(const char *command, BlaeckCommandHandler handler,
                     const __FlashStringHelper *stateSignal = nullptr,
                     unsigned int maxLength = 255);

  // ----- Before data write callback  -----
  // Called just before signal data is sent.
  //   Single/Master mode: runs in normal loop context (safe to use Serial, delay, etc.)
  //   Slave mode: runs inside the I2C receive ISR — keep it short, no Serial/delay!
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
  void setSignalName(int signalIndex, String signalName, bool prefixSlaveID);
  void _setTimedDataState(bool timedActivated, unsigned long timedInterval_ms);
  void _parseCommandTokens(const char *raw);
  void _dispatchRegisteredHandlers(bool sendAck = true);

  // Send a 0xF0 Command Ack frame (cmdHash + status + reason) to the serial host.
  // The hash is FNV-1a of the exact received frame bytes (routing prefix included
  // for master-forwarded commands). The frame carries no CRC (like 0xE0).
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
  void prepareMasterSlaveSkipMap(uint8_t *skipSlaves, byte &skippedSlaveCount, byte &firstSkippedSlaveID, byte &firstSkipReason);
  void writeLocalData(unsigned long MessageID, int signalIndex_start, int signalIndex_end, bool send_eol, bool onlyUpdated, unsigned long long timestamp);
  void writeSlaveData(bool send_eol, bool onlyUpdated, uint8_t *skipSlaves, byte &skippedSlaveCount, byte &firstSkippedSlaveID, byte &firstSkipReason);

  void writeLocalSymbols(unsigned long MessageID, bool send_eol);
  void writeSlaveSymbols(bool send_eol);
#if BLAECK_ENABLE_COMMAND_META
  void writeLocalCommands(unsigned long MessageID, bool send_eol);
  void _annotateCommand(const char *command, uint8_t kind,
                        const __FlashStringHelper *stateSignal,
                        float mn, float mx, float st,
                        const __FlashStringHelper *unit,
                        const __FlashStringHelper *options);
  byte _validateTypedCommand(byte handlerIndex);
  static void _percentDecodeInPlace(char *s);
  static byte _flashCsvOptionCount(const __FlashStringHelper *csv);
  static long _flashCsvIndexOf(const __FlashStringHelper *csv, const char *value);

  // Master-side aggregation of slave command catalogs (I2C wire-mode 5): request
  // each found slave's length-prefixed command blob and append it verbatim to the
  // 0xE0 frame. Slave-side: serialize this board's in-use command entries into a
  // RAM blob and stream it in BLAECK_WIRE_BUFFER_SIZE-sized chunks on request.
  void writeSlaveCommands(bool send_eol);
  void wireSlaveTransmitCommandChunk();
  // Serialize this board's in-use command entries on the fly, emitting (via
  // Wire.write) only the bytes whose position falls in [windowStart, windowStart+
  // windowLen). Returns the total serialized length. Pass windowLen == 0 to count
  // the total without emitting anything. Serialization is deterministic (the
  // command table and its flash strings are immutable at runtime), so re-walking
  // per chunk reproduces identical bytes without needing a RAM staging buffer.
  long _emitLocalCommandBlob(long windowStart, long windowLen);
#endif

  void writeLocalDevices(unsigned long MessageID, bool send_eol);
  void writeSlaveDevices(bool send_eol);

  void refreshI2CSlavesIfNeeded();
  void scanI2CSlaves(uint8_t addressStart, uint8_t addressEnd);

  void wireSlaveTransmitToMaster();
  void wireSlaveReceive(int numBytes);

  // Master->slave single-shot command delivery (I2C wire-mode 6) and the slave-side
  // deferred dispatch of a command received over I2C (run outside the Wire ISR).
  bool wireMasterTransmitCommand(byte slaveID, const char *payload);
  void _processPendingWireCommand();

  void wireSlaveTransmitSingleSymbol();
  void wireSlaveTransmitDataPoints(bool onlyUpdated);
  void wireSlaveTransmitSingleDevice();
  void wireSlaveTransmitStatusByte();
  byte _wireDataPointSize(byte dataType);

  bool slaveFound(const unsigned int index);
  void storeSlave(const unsigned int index, const boolean value);

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

  masterSlaveConfig _masterSlaveConfig = Single;
  byte _slaveID = 0;
  unsigned char _slaveFound[128 / 8] = {}; // 128 bit storage
  String _slaveSymbolPrefix;

  byte _wireMode = 0;
  int _wireSignalIndex = 0;
  int _wireDeviceIndex = 0;

  // Slave-side ingress for a command delivered by the master over I2C wire-mode 6.
  // Written inside the Wire receive ISR, drained in read() (main loop) so the user
  // command handler never runs in interrupt context.
  volatile bool _wireCommandPending = false;
  char _wireCommandBuf[BLAECK_WIRE_BUFFER_SIZE] = {0};

#if BLAECK_ENABLE_COMMAND_META
  // Slave-side streaming state for aggregating this board's command catalog to the
  // master (I2C wire-mode 5). The catalog is serialized on the fly per chunk (no
  // RAM staging buffer): _wireCommandCursor tracks the byte offset the master has
  // consumed, _wireCommandTotalLen caches the total length computed on the first
  // chunk. A fresh pass is signalled by wireSlaveReceive() resetting the cursor.
  long _wireCommandTotalLen = 0;
  long _wireCommandCursor = 0;
#endif

  bool _i2cScanInitialized = false;
  unsigned long _lastI2CScanMs = 0;
  static const unsigned long I2C_SCAN_INTERVAL_MS = 250;

  static const int MAXIMUM_CHAR_COUNT = BLAECK_COMMAND_MAX_CHARS_DEFAULT;
  static const byte MAX_COMMAND_HANDLERS = BLAECK_COMMAND_MAX_HANDLERS_DEFAULT;
  static const byte MAX_COMMAND_PARAM_COUNT = BLAECK_COMMAND_MAX_PARAMS_DEFAULT;
  static const byte MAX_COMMAND_NAME_COUNT = BLAECK_COMMAND_MAX_NAME_CHARS_DEFAULT;
  char receivedChars[MAXIMUM_CHAR_COUNT];
  char COMMAND[MAXIMUM_CHAR_COUNT] = {0};
  int PARAMETER[10];
  // STRING_01: Max. 15 chars allowed  + Null Terminator '\0' = 16
  // In case more than 15 chars are sent, the rest is cut off in function void parseData()
  char STRING_01[16];

  CRC32 _crc;
  CRC16 _crcWire;
  CRC16 _crcWireCalc;
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
  void _bufDevice(byte msc, byte sid, const String &name,
                  const String &hw, const String &fw);

  static BlaeckSerial *_pSingletonInstance;

  static void OnRequestHandler()
  {
    if (_pSingletonInstance)
      _pSingletonInstance->wireSlaveTransmitToMaster();
  }

  static void OnReceiveHandler(int numBytes)
  {
    if (_pSingletonInstance)
      _pSingletonInstance->wireSlaveReceive(numBytes);
  }

  static unsigned long long _microsWrapper()
  {
    return (unsigned long long)micros();
  }

  void (*_commandCallback)(char *command, int *parameter, char *string01) = nullptr;
  bool _commandCallbackDeprecationWarned = false;
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
#endif
  };
  CommandHandlerEntry _commandHandlers[MAX_COMMAND_HANDLERS];
  BlaeckAnyCommandHandler _anyCommandHandler = nullptr;
  char _parsedTokenBuffer[MAXIMUM_CHAR_COUNT] = {0};
  char _parsedCommand[MAX_COMMAND_NAME_COUNT] = {0};
  const char *_parsedParamPtrs[MAX_COMMAND_PARAM_COUNT] = {0};
  byte _parsedParamCount = 0;
  // Monotonic message id stamped into the 0xF0 Command Ack frame header.
  unsigned long _commandAckMsgId = 1;
  // Monotonic message id stamped into the 0x90 Message frame header.
  unsigned long _messageMsgId = 0;
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
};

#endif //  BLAECKSERIAL_H
