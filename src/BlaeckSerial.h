/*
        File: BlaeckSerial.h
        Author: Sebastian Strobl
*/


/*  MESSAGE DECODER

  -INCOMING MESSAGES----------------------------------------------------
  Message:         <COMMAND,PARAMETER_01,PARAMETER_02,...,PARAMETER_10>
  StringMessage:   <COMMAND, STRING_01  ,PARAMETER_02,...,PARAMETER_10>
                   <---------max. 64 chars---------------------------->
  COMMAND:             String, not allowed chars: comma, <, >
  PARAMETER:           Int 16 Bit, max 10 parameters
  STRING_01:           max. 15 chars


    -BLAECK MESSAGES-----------------
    <BLAECK.WRITE_SYMBOLS, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>
    <BLAECK.WRITE_DATA, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>
    <BLAECK.ACTIVATE_TIMED, intervalInSeconds>
          intervalInSeconds: from 1 to 32767 [s]
        <BLAECK.DEACTIVATE_TIMED>

  -OUTGOING MESSAGES-----------------------------------------------------
   |--Header------------|-DATA--------------------|-EOT---------|
   #BLA:<MSGKEY>:<MSGID>:..........................ENDOFBLA<CRNL>

   MSGKEY:   DATA#:    DATA:                           DESCRIPTION:
    B0       N         <SymbolID><SymbolName><DTYPE>   Up to N Items. Response to request for available symbols.
    B1       N         <SymbolID><DATA>                Up to N Items. Response to request for data.

                   TYPE:            DESCRIPTION:
   <MSGKEY>        byte             Message KEY, A unique key for the type of message being sent
   <MSGID>         uint32/ulong     Message ID,  A unique message ID which echoes back to transmitter to indicate a response to a message (0 to 4294967295)
   <DATA>          (varying)        Message Data, varying data types and length depending on message
   ENDOFBLA <CRNL> char             'ENDOFBLA' + Carriage Return + New Line Character signifying the end of a transmission.
   <SymbolID>      uint             Symbol ID number
   <SymbolName>    String0          Symbol Name - Null Terminated String
   <DTYPE>         byte             DataType  0=Boolean, 1=Byte, 2=short,...

*/

#ifndef BLAECKSERIAL_H
#define BLAECKSERIAL_H

#include <Wire.h>
#include <Arduino.h>

typedef enum MasterSlaveConfig
{ Single,
  Master,
  Slave
};

typedef enum DataType
{ Blaeck_bool,
  Blaeck_byte,
  Blaeck_short,
  Blaeck_ushort,
  Blaeck_int,
  Blaeck_uint,
  Blaeck_long,
  Blaeck_ulong,
  Blaeck_float,
  Blaeck_double
};

struct Signal {
  String SymbolName;
  DataType DataType;
  void * Address;
};

class BlaeckSerial {
  public:
    // ----- Constructor -----
    BlaeckSerial();

    // ----- Destructor -----
    ~BlaeckSerial();

    // ----- Initialize -----
    void begin(HardwareSerial *Ref, unsigned int Size);
    void beginMaster(HardwareSerial *Ref, unsigned int Size, uint32_t WireClockFrequency);
    void beginSlave(HardwareSerial *Ref, unsigned int Size, byte SlaveID);

    // ----- Signals -----
    //add or delete signals
    void addSignal(String SymbolName, bool * value);
    void addSignal(String SymbolName, byte * value);
    void addSignal(String SymbolName, float * value);
    void addSignal(String SymbolName, double * value);
    void addSignal(String SymbolName, unsigned long * value);
    void addSignal(String SymbolName, int * value);
    void deleteSignals();

    // ----- Symbols -----
    void writeSymbols();
    void writeSymbols(unsigned long messageID);

    // ----- Data -----
    void writeData();
    void writeData(unsigned long messageID);

    // ----- Timed Data -----
    /**
         @brief Call this function every some milliseconds for writing timed data
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

    // ----- Read  -----
    /**
       @brief Call this function every some milliseconds for reading serial input
    */
    void read();
    /**
      @brief Attach a function that will be called when a valid message was received;
    */
    void attachRead(void (*readCallback)(char * command, int * parameter, char * string_01));


    // ----- All-in-one  -----
    /**
       @brief Call this function every some milliseconds for reading serial input
       and writing timed data;
    */
    void tick();


  private:
    void writeLocalData(unsigned long MessageID, bool send_eol);
    void writeSlaveData(bool send_eol);
    void writeLocalSymbols(unsigned long MessageID, bool send_eol);
    void writeSlaveSymbols(bool send_eol);
    void wireSlaveTransmitToMaster();
    void wireSlaveReceive();
    void wireSlaveTransmitSingleSymbol();
    void wireSlaveTransmitSingleDataPoint();


    HardwareSerial* SerialRef;
    Signal* Signals;
    unsigned int _signalIndex = 0;


    bool _timedActivated = true;
    bool _timedFirstTime = true;
    unsigned long _timedFirstTimeDone_ms = 0;
    unsigned long _timedSetPoint_ms = 0;
    unsigned long _timedInterval_ms = 1000;


    MasterSlaveConfig _masterSlaveConfig = Single;
    byte _slaveID;
    bool _slaveFound[128];
    String _slaveSymbolPrefix;


    byte _wireMode = 0;
    unsigned int _wireSignalIndex = 0;

    static const int MAXIMUM_CHAR_COUNT = 64;
    char receivedChars[MAXIMUM_CHAR_COUNT];
    char COMMAND[MAXIMUM_CHAR_COUNT] = {0};
    int PARAMETER[10];
    //STRING_01: Max. 15 chars allowed  + Null Terminator '\0' = 16
    //In case more than 15 chars are sent, the rest is cut off in function void parseData()
    char STRING_01[16];

    static BlaeckSerial* _pSingletonInstance;

    static void OnReceiveHandler() {
      if (_pSingletonInstance)
        _pSingletonInstance->wireSlaveTransmitToMaster();
    }

    static void OnSendHandler() {
      if (_pSingletonInstance)
        _pSingletonInstance->wireSlaveReceive();
    }

    void (*_readCallback)(char * command, int * parameter, char * string01);
    bool recvWithStartEndMarkers();
    void parseData();


    union {
      bool val;
      byte bval[1];
    } boolCvt;

    union {
      short val;
      byte bval[2];
    } shortCvt;

    union {
      int val;
      byte bval[2];
    } intCvt;

    union {
      unsigned int val;
      byte bval[2];
    } uintCvt;

    union {
      long val;
      byte bval[4];
    } lngCvt;

    union {
      unsigned long val;
      byte bval[4];
    } ulngCvt;

    union {
      float val;
      byte bval[4];
    } fltCvt;

    union {
      double val;
      byte bval[8];
    } dblCvt;

};



#endif //  BLAECKSERIAL_H