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

  /**
           @brief Set these variables in your Arduino sketch
    */
  String DeviceName = "Unknown";
  String DeviceHWVersion = "n/a";
  String DeviceFWVersion = "n/a";

  const String LIBRARY_NAME = "BlaeckSerial";
  const String LIBRARY_VERSION = "4.2.0";

  // ----- Signals -----
  // add or delete signals
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

  void deleteSignals();

  // ----- Devices -----
  void writeDevices();
  void writeDevices(unsigned long messageID);

  // ----- Symbols -----
  void writeSymbols();
  void writeSymbols(unsigned long messageID);

  // ----- Data -----
  void writeData();
  void writeData(unsigned long messageID);

  // ----- Timed Data -----
  /**
           @brief Call this function every some milliseconds for writing timed data; default messageId = 185273099
    */
  void timedWriteData();
  /**
           @brief Call this function every some milliseconds for writing timed data
           @param messageId --> A unique message ID which echoes back to transmitter to indicate a response to a message.
    */
  void timedWriteData(unsigned long messageID);
  /**
           @brief Call this function for timed data settings
    */
  void setTimedData(bool timedActivated, unsigned long timedInterval_ms);

  // ----- Update before data write Callback function  -----
  /**
          @brief Attach a function that will be called just before transmitting data.
          In single device or master mode the function is called just before sending data over serial,
          In slave mode the function is called just before sending data over i2c to master. Because the attached function is inside a ISR (interrupt service routine) it should as short and fast as possible.

          About ISRs: ISRs are special kinds of functions that have some unique limitations most other functions do not have. An ISR cannot have any parameters, and they shouldn’t return anything. Generally, an ISR
          should be as short and fast as possible. If your sketch uses multiple ISRs, only one can run at a time, other interrupts will be executed after the current one finishes in an order that depends on the priority they have.
          millis() relies on interrupts to count, so it will never increment inside an ISR. Since delay() requires interrupts to work, it will not work if called inside an ISR. micros() works initially but will start behaving
          erratically after 1-2 ms. delayMicroseconds() does not use any counter, so it will work as normal. Typically global variables are used to pass data between an ISR and the main program. To make sure variables shared between
          an ISR and the main program are updated correctly, declare them as volatile.
            For more information on interrupts, see Nick Gammon’s notes (http://gammon.com.au/interrupts).
    */
  void attachUpdate(void (*updateCallback)());

  // ----- Read  -----
  /**
           @brief Call this function every some milliseconds for reading serial input and execution of the command
    */
  void read();
  /**
          @brief Attach a function that will be called when a valid message was received;
    */
  void attachRead(void (*readCallback)(char *command, int *parameter, char *string_01));

  // ----- All-in-one -----
  /**
          @brief Call this function every some milliseconds for reading serial input, execution of the command
           and writing timed data; default messageId = 185273099
    */
  void tick();
  /**
          @brief Call this function every some milliseconds for reading serial input, execution of the command
           and writing timed data with messageID;
          @param messageId --> A unique message ID which echoes back to transmitter to indicate a response to a message.
    */
  void tick(unsigned long messageID);

private:
  void writeLocalData(unsigned long MessageID, bool send_eol);
  void writeSlaveData(bool send_eol);

  void writeLocalSymbols(unsigned long MessageID, bool send_eol);
  void writeSlaveSymbols(bool send_eol);

  void writeLocalDevices(unsigned long MessageID, bool send_eol);
  void writeSlaveDevices(bool send_eol);

  void scanI2CSlaves(char addressStart, char addressEnd);

  void wireSlaveTransmitToMaster();
  void wireSlaveReceive();

  void wireSlaveTransmitSingleSymbol();
  void wireSlaveTransmitSingleDataPoint();
  void wireSlaveTransmitSingleDevice();
  void wireSlaveTransmitStatusByte();

  bool slaveFound(const unsigned int index);
  void storeSlave(const unsigned int index, const boolean value);

  Stream *StreamRef;
  Signal *Signals;
  int _signalIndex = 0;

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

  void (*_readCallback)(char *command, int *parameter, char *string01);
  bool recvWithStartEndMarkers();
  void parseData();

  void (*_updateCallback)();

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