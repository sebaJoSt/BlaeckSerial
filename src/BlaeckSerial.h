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

#define BLAECKSERIAL_VERSION "6.0.0"
#define BLAECKSERIAL_VERSION_MAJOR 6
#define BLAECKSERIAL_VERSION_MINOR 0
#define BLAECKSERIAL_VERSION_PATCH 0
#define BLAECKSERIAL_NAME "BlaeckSerial"

#include <Wire.h>
#include <Arduino.h>
#include <CRC32.h>
#include <CRC16.h>
#include <new>
#include <limits.h>

// Allow user overrides via a config file in the sketch folder.
// Create BlaeckSerialConfig.h in your sketch to override defaults, e.g.:
//   #define BLAECK_COMMAND_MAX_CHARS_DEFAULT 128
// PlatformIO users can also use build_flags = -DBLAECK_COMMAND_MAX_CHARS_DEFAULT=128
#if defined __has_include
  #if __has_include(<BlaeckSerialConfig.h>)
    #include <BlaeckSerialConfig.h>
  #endif
#endif

// Buffered writes: assemble entire frames in RAM before a single
// StreamRef->write(buf, len) call.  Prevents byte-dropping on boards
// whose UART-to-USB bridge can't keep up with many small writes
// (e.g. Arduino Uno R4 WiFi / ESP32-S3 bridge).
// Default: OFF on AVR (saves SRAM), ON everywhere else.
// Can also be changed at runtime with setBufferedWrites().
#ifndef BLAECK_BUFFERED_WRITES_DEFAULT
  #if defined(__AVR__)
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
    #define BLAECK_COMMAND_MAX_HANDLERS_DEFAULT 4
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

// I2C (Wire) buffer size used for slave→master data packing.
// Must match on both master and slave for cross-platform setups.
// Defaults to 32 (AVR Wire TX buffer). Override via BlaeckSerialConfig.h
// or build flag for platforms with larger buffers (e.g. ESP32: 128).
#ifndef BLAECK_WIRE_BUFFER_SIZE
  #define BLAECK_WIRE_BUFFER_SIZE 32
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
  Blaeck_double
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
  void _dispatchRegisteredHandlers();
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

  void writeLocalDevices(unsigned long MessageID, bool send_eol);
  void writeSlaveDevices(bool send_eol);

  void refreshI2CSlavesIfNeeded();
  void scanI2CSlaves(uint8_t addressStart, uint8_t addressEnd);

  void wireSlaveTransmitToMaster();
  void wireSlaveReceive();

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
      _pSingletonInstance->wireSlaveReceive();
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
  };
  CommandHandlerEntry _commandHandlers[MAX_COMMAND_HANDLERS];
  BlaeckAnyCommandHandler _anyCommandHandler = nullptr;
  char _parsedTokenBuffer[MAXIMUM_CHAR_COUNT] = {0};
  char _parsedCommand[MAX_COMMAND_NAME_COUNT] = {0};
  const char *_parsedParamPtrs[MAX_COMMAND_PARAM_COUNT] = {0};
  byte _parsedParamCount = 0;
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
