/*
        File: BlaeckSerial.h
        Author: Sebastian Strobl
*/


/*  Message Decoder

  
  * Incoming Messages
  Message:         <COMMAND,PARAMETER_01,PARAMETER_02,...,PARAMETER_10>
  StringMessage:   <COMMAND, STRING_01  ,PARAMETER_02,...,PARAMETER_10>
                   <---------max. 64 chars---------------------------->
  COMMAND:             String, not allowed chars: comma, <, >
  PARAMETER:           Int 16 Bit, max 10 parameters
  STRING_01:           max. 15 chars

	* Internal Messages:
    <BLAECK.WRITE_SYMBOLS, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>
    <BLAECK.WRITE_DATA, MessageID_firstByte, MessageID_secondByte, MessageID_thirdByte, MessageID_fourthByte>
    <BLAECK.ACTIVATE, intervalInSeconds>
          intervalInSeconds: from 1 to 32767 [s]
    <BLAECK.DEACTIVATE>

	
	
  * Outgoing Messages
   |    Header         ||         Data           ||   EOT    |
   <BLAECK:MSGKEY:MSGID:........................../BLAECK>\r\n
   012345678       9 10-131415  

   MSGKEY:   DATA#:    DATA:                           DESCRIPTION:
    B0        n        <SymbolID><SymbolName><DTYPE>   Up to n Items. Response to request for available symbols. (< and > just for illustration, not transmitted)
    B1        n        <SymbolID><DATA>                Up to n Items. Response to request for data. (< and > just for illustration, not transmitted)

                   TYPE:            DESCRIPTION:
   MSGKEY          byte             Message KEY, A unique key for the type of message being sent
   MSGID           ulong            Message ID,  A unique message ID which echoes back to transmitter to indicate a response to a message (0 to 4294967295)
   DATA           (varying)         Message Data, varying data types and length depending on message
   <BLAECK         char             Start of a transmission
   /BLAECK>\r\n    char             End of a transmission
   SymbolID        uint             Symbol ID number
   SymbolName      String0          Symbol Name - Null Terminated String
   DTYPE           byte             DataType  0=bool, 1=byte, 2=short, 3=ushort, 4=int, 5=uint, 6=long, 7=ulong, 8=float

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
	void addSignal(String SymbolName, short * value);
	void addSignal(String SymbolName, unsigned short * value);
    void addSignal(String SymbolName, int * value);
	void addSignal(String SymbolName, unsigned int * value);
	void addSignal(String SymbolName, long * value);
    void addSignal(String SymbolName, unsigned long * value);
	void addSignal(String SymbolName, float * value);
    void addSignal(String symbolname, double * value);
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


    bool _timedActivated = false;
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
      short val;
      byte bval[2];
    } ushortCvt;
		
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

};



#endif //  BLAECKSERIAL_H