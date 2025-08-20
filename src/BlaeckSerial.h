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

#include <Wire.h>
#include <Arduino.h>
#include <CRC32.h>
#include <CRC16.h>

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
  BLAECK_UNIXTIME = 2
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
  void beginMaster(Stream *Ref, unsigned int Size, uint32_t WireClockFrequency);
  void beginSlave(Stream *Ref, unsigned int Size, byte SlaveID);

  // Set these variables in your Arduino sketch
  String DeviceName = "Unknown";
  String DeviceHWVersion = "n/a";
  String DeviceFWVersion = "n/a";

  const String LIBRARY_NAME = "BlaeckSerial";
  const String LIBRARY_VERSION = "5.0.0";

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

  // ----- Data Update -----
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
  void timedWriteAllData();
  void timedWriteAllData(unsigned long msg_id);

  // ----- Data Write Updated -----
  void writeUpdatedData();
  void writeUpdatedData(unsigned long messageID);
  void timedWriteUpdatedData();
  void timedWriteUpdatedData(unsigned long msg_id);

  // ----- Tick -----
  void tick();
  void tick(unsigned long messageID);
  void tickUpdated();
  void tickUpdated(unsigned long messageID);

  // ----- Timed Data configuruation -----
  void setTimedData(bool timedActivated, unsigned long timedInterval_ms);

  // ----- Read  -----
  void read();

  // ----- Command callback  -----
  void setCommandCallback(void (*callback)(char *command, int *parameter, char *string_01));

  // ----- Before data write callback  -----
  // In single device or master mode the function is called just before sending data over serial,
  // In slave mode the function is called just before sending data over i2c to master. Because the attached function is inside a ISR (interrupt service routine) it should as short and fast as possible.
  void setBeforeWriteCallback(void (*callback)());

  // Timestamp configuration methods
  void setTimestampMode(BlaeckTimestampMode mode);
  void setTimestampCallback(unsigned long (*callback)());
  BlaeckTimestampMode getTimestampMode() const { return _timestampMode; }
  bool hasValidTimestampCallback() const;

private:
  int findSignalIndex(String signalName);

  void timedWriteData(unsigned long messageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated);
  void tick(unsigned long messageID, bool onlyUpdated);

  void writeData(unsigned long messageID, int signalIndex_start, int signalIndex_end, bool onlyUpdated);
  void writeLocalData(unsigned long MessageID, int signalIndex_start, int signalIndex_end, bool send_eol, bool onlyUpdated);
  void writeSlaveData(bool send_eol, bool onlyUpdated);

  void writeLocalSymbols(unsigned long MessageID, bool send_eol);
  void writeSlaveSymbols(bool send_eol);

  void writeLocalDevices(unsigned long MessageID, bool send_eol);
  void writeSlaveDevices(bool send_eol);

  void scanI2CSlaves(char addressStart, char addressEnd);

  void wireSlaveTransmitToMaster();
  void wireSlaveReceive();

  void wireSlaveTransmitSingleSymbol();
  void wireSlaveTransmitSingleDataPoint(bool onlyUpdated);
  void wireSlaveTransmitSingleDevice();
  void wireSlaveTransmitStatusByte();

  bool slaveFound(const unsigned int index);
  void storeSlave(const unsigned int index, const boolean value);

  Stream *StreamRef;
  Signal *Signals;
  int _signalIndex = 0;

  bool _writeRestartedAlreadyDone = false;
  bool _sendRestartFlag = true;

  bool _timedActivated = false;
  bool _timedFirstTime = true;
  unsigned long _timedFirstTimeDone_ms = 0;
  unsigned long _timedSetPoint_ms = 0;
  unsigned long _timedInterval_ms = 1000;

  masterSlaveConfig _masterSlaveConfig = Single;
  byte _slaveID;
  unsigned char _slaveFound[128 / 8]; // 128 bit storage
  String _slaveSymbolPrefix;

  byte _wireMode = 0;
  int _wireSignalIndex = 0;
  int _wireDeviceIndex = 0;

  static const int MAXIMUM_CHAR_COUNT = 64;
  char receivedChars[MAXIMUM_CHAR_COUNT];
  char COMMAND[MAXIMUM_CHAR_COUNT] = {0};
  int PARAMETER[10];
  // STRING_01: Max. 15 chars allowed  + Null Terminator '\0' = 16
  // In case more than 15 chars are sent, the rest is cut off in function void parseData()
  char STRING_01[16];

  CRC32 _crc;
  CRC16 _crcWire;
  CRC16 _crcWireCalc;

  static BlaeckSerial *_pSingletonInstance;

  static void OnReceiveHandler()
  {
    if (_pSingletonInstance)
      _pSingletonInstance->wireSlaveTransmitToMaster();
  }

  static void OnSendHandler(int numBytes)
  {
    if (_pSingletonInstance)
      _pSingletonInstance->wireSlaveReceive();
  }

  void (*_commandCallback)(char *command, int *parameter, char *string01) = nullptr;
  bool recvWithStartEndMarkers();
  void parseData();

  void (*_beforeWriteCallback)() = nullptr;

  BlaeckTimestampMode _timestampMode = BLAECK_NO_TIMESTAMP;
  unsigned long (*_timestampCallback)() = nullptr;

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
    short val;
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