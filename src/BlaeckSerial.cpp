/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include <Arduino.h>
#include "BlaeckSerial.h"

// static initializer for the static member.
BlaeckSerial *BlaeckSerial::_pSingletonInstance = 0;

BlaeckSerial::BlaeckSerial()
{
  validatePlatformSizes();
}

BlaeckSerial::~BlaeckSerial()
{
  delete Signals;
}

void BlaeckSerial::begin(Stream *Ref, unsigned int size)
{
  StreamRef = (Stream *)Ref;
  Signals = new Signal[size];
  // Assign the static singleton used in the static handlers.
  BlaeckSerial::_pSingletonInstance = this;
}
void BlaeckSerial::beginMaster(Stream *Ref, unsigned int size, uint32_t WireClockFrequency)
{
  _masterSlaveConfig = Master;
  Wire.setClock(WireClockFrequency);
  Wire.begin();

  begin(Ref, size);
}
void BlaeckSerial::beginSlave(Stream *Ref, unsigned int size, byte slaveID)
{
  _masterSlaveConfig = Slave;
  _slaveID = slaveID;
  if (_slaveID > 127)
    _slaveID = 127;
  String s = "S";
  _slaveSymbolPrefix = s + _slaveID + "_";

  Wire.onReceive(OnSendHandler);
  Wire.onRequest(OnReceiveHandler);
  Wire.begin(_slaveID);

  begin(Ref, size);
}

void BlaeckSerial::addSignal(String signalName, bool *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, byte *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, short *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, unsigned short *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, int *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
#ifdef __AVR__
  Signals[_signalIndex].DataType = Blaeck_int; // 2 bytes
#else
  Signals[_signalIndex].DataType = Blaeck_long; // Treat as 4-byte long
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, unsigned int *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
#ifdef __AVR__
  Signals[_signalIndex].DataType = Blaeck_uint; // 2 bytes
#else
  Signals[_signalIndex].DataType = Blaeck_ulong; // Treat as 4-byte unsigned long
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, long *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, unsigned long *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, float *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::addSignal(String signalName, double *value, bool prefixSlaveID)
{
  Signals[_signalIndex].SignalName = signalName;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
  {
    Signals[_signalIndex].SignalName = _slaveSymbolPrefix + signalName;
  }
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  Signals[_signalIndex].DataType = Blaeck_float;
#else
  Signals[_signalIndex].DataType = Blaeck_double;
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
}

void BlaeckSerial::deleteSignals()
{
  _signalIndex = 0;
  SignalCount = _signalIndex;
}

void BlaeckSerial::update(int signalIndex, bool value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_bool)
    {
      *((bool *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, byte value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_byte)
    {
      *((byte *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, short value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_short)
    {
      *((short *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, unsigned short value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_ushort)
    {
      *((unsigned short *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, int value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    if (Signals[signalIndex].DataType == Blaeck_int)
    {
      *((int *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
#else
    if (Signals[signalIndex].DataType == Blaeck_long)
    {
      *((int *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
#endif
  }
}

void BlaeckSerial::update(int signalIndex, unsigned int value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    if (Signals[signalIndex].DataType == Blaeck_uint)
    {
      *((unsigned int *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
#else
    if (Signals[signalIndex].DataType == Blaeck_ulong)
    {
      *((unsigned int *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
#endif
  }
}

void BlaeckSerial::update(int signalIndex, long value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_long)
    {
      *((long *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, unsigned long value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_ulong)
    {
      *((unsigned long *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, float value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_float)
    {
      *((float *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckSerial::update(int signalIndex, double value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    // On AVR, double is same as float
    if (Signals[signalIndex].DataType == Blaeck_float)
    {
      *((float *)Signals[signalIndex].Address) = (float)value;
      Signals[signalIndex].Updated = true;
    }
#else
    if (Signals[signalIndex].DataType == Blaeck_double)
    {
      *((double *)Signals[signalIndex].Address) = value;
      Signals[signalIndex].Updated = true;
    }
#endif
  }
}

void BlaeckSerial::update(String signalName, bool value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, byte value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, unsigned short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, unsigned int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, unsigned long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, float value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(String signalName, double value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

int BlaeckSerial::findSignalIndex(String signalName)
{
  for (int i = 0; i < _signalIndex; i++)
  {
    if (Signals[i].SignalName == signalName)
    {
      return i;
    }
  }
  return -1; // Not found
}

void BlaeckSerial::read()
{

  if (recvWithStartEndMarkers() == true)
  {
    parseData();
    StreamRef->print("<");
    StreamRef->print(receivedChars);
    StreamRef->println(">");

    if (strcmp(COMMAND, "BLAECK.WRITE_SYMBOLS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeSymbols(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DATA") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeAllData(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.GET_DEVICES") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDevices(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.ACTIVATE") == 0)
    {
      unsigned long timedInterval_ms = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);
      this->setTimedData(true, timedInterval_ms);
    }
    else if (strcmp(COMMAND, "BLAECK.DEACTIVATE") == 0)
    {
      this->setTimedData(false, _timedInterval_ms);
    }

    if (_commandCallback != NULL)
      _commandCallback(COMMAND, PARAMETER, STRING_01);
  }
}

void BlaeckSerial::setCommandCallback(void (*callback)(char *command, int *parameter, char *string_01))
{
  _commandCallback = callback;
}

void BlaeckSerial::setBeforeWriteCallback(void (*callback)())
{
  _beforeWriteCallback = callback;
}

bool BlaeckSerial::recvWithStartEndMarkers()
{
  bool newData = false;
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (StreamRef->available() > 0 && newData == false)
  {
    rc = StreamRef->read();
    if (recvInProgress == true)
    {
      if (rc != endMarker)
      {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= MAXIMUM_CHAR_COUNT)
        {
          ndx = MAXIMUM_CHAR_COUNT - 1;
        }
      }
      else
      {
        // terminate the string
        receivedChars[ndx] = '\0';
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }
    else if (rc == startMarker)
    {
      recvInProgress = true;
    }
  }

  return newData;
}

void BlaeckSerial::parseData()
{
  // split the data into its parts
  char tempChars[sizeof(receivedChars)];
  strcpy(tempChars, receivedChars);
  char *strtokIndx;
  strtokIndx = strtok(tempChars, ",");
  strcpy(COMMAND, strtokIndx);

  strtokIndx = strtok(NULL, ",");
  if (strtokIndx != NULL)
  {
    // PARAMETER 1 is stored in PARAMETER_01 & STRING_01 (if PARAMETER 1 is a string)

    // Only copy first 15 chars
    strncpy(STRING_01, strtokIndx, 15);
    // 16th Char = Null Terminator
    STRING_01[15] = '\0';
    PARAMETER[0] = atoi(strtokIndx);
  }
  else
  {
    STRING_01[0] = '\0';
    PARAMETER[0] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[1] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[1] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[2] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[2] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[3] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[3] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[4] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[4] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[5] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[5] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[6] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[6] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[7] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[7] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[8] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[8] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[9] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[9] = 0;
  }
}

void BlaeckSerial::setTimedData(bool timedActivated, unsigned long timedInterval_ms)
{
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    _timedSetPoint_ms = timedInterval_ms;
    _timedInterval_ms = timedInterval_ms;
    _timedFirstTime = true;
  }
}

void BlaeckSerial::writeSymbols()
{
  this->writeSymbols(1);
}
void BlaeckSerial::writeSymbols(unsigned long msg_id)
{
  if (_masterSlaveConfig == Single || _masterSlaveConfig == Slave)
  {
    this->writeLocalSymbols(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    this->writeLocalSymbols(msg_id, false);
    this->writeSlaveSymbols(true);
  }
}

void BlaeckSerial::write(String signalName, bool value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, byte value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, short value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, unsigned short value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, int value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, unsigned int value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, long value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, unsigned long value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, float value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, double value)
{
  this->write(signalName, value, 1);
}

void BlaeckSerial::write(String signalName, bool value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, byte value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, short value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, unsigned short value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, int value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, unsigned int value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, long value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, unsigned long value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, float value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, double value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(String signalName, bool value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, byte value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, short value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned short value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, int value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned int value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, long value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned long value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, float value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, double value, unsigned long messageID, unsigned long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}

void BlaeckSerial::write(int signalIndex, bool value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, byte value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, short value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, unsigned short value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, int value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, unsigned int value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, long value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, unsigned long value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, float value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, double value)
{
  this->write(signalIndex, value, 1);
}

void BlaeckSerial::write(int signalIndex, bool value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, byte value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, short value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, unsigned short value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, int value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, unsigned int value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, long value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, unsigned long value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, float value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, double value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(int signalIndex, bool value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_bool)
    {
      *((bool *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, byte value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_byte)
    {
      *((byte *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, short value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_short)
    {
      *((short *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, unsigned short value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_ushort)
    {
      *((unsigned short *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, int value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    // On AVR, int stays as Blaeck_int (2 bytes)
    if (Signals[signalIndex].DataType == Blaeck_int)
    {
      *((int *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
#else
    // On 32-bit platforms, int is mapped to Blaeck_long (4 bytes)
    if (Signals[signalIndex].DataType == Blaeck_long)
    {
      *((int *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
#endif
  }
}
void BlaeckSerial::write(int signalIndex, unsigned int value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    // On AVR, unsigned int stays as Blaeck_uint (2 bytes)
    if (Signals[signalIndex].DataType == Blaeck_uint)
    {
      *((unsigned int *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
#else
    // On 32-bit platforms, unsigned int is mapped to Blaeck_ulong (4 bytes)
    if (Signals[signalIndex].DataType == Blaeck_ulong)
    {
      *((unsigned int *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
#endif
  }
}
void BlaeckSerial::write(int signalIndex, long value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_long)
    {
      *((long *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, unsigned long value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_ulong)
    {
      *((unsigned long *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, float value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_float)
    {
      *((float *)Signals[signalIndex].Address) = value;
      this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
    }
  }
}
void BlaeckSerial::write(int signalIndex, double value, unsigned long messageID, unsigned long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
#ifdef __AVR__
    // On AVR, double is same as float
    if (Signals[signalIndex].DataType == Blaeck_float)
    {
      *((float *)Signals[signalIndex].Address) = (float)value;
    }
#else
    if (Signals[signalIndex].DataType == Blaeck_double)
    {
      *((double *)Signals[signalIndex].Address) = value;
    }
#endif
    this->writeLocalData(messageID, signalIndex, signalIndex, true, false, timestamp);
  }
}

void BlaeckSerial::writeAllData()
{
  this->writeAllData(1);
}

void BlaeckSerial::writeAllData(unsigned long msg_id)
{
  this->writeAllData(msg_id, getTimeStamp());
}

void BlaeckSerial::writeAllData(unsigned long msg_id, unsigned long timestamp)
{

  this->writeData(msg_id, 0, _signalIndex - 1, false, timestamp);
}

void BlaeckSerial::writeUpdatedData()
{
  this->writeUpdatedData(1);
}

void BlaeckSerial::writeUpdatedData(unsigned long msg_id)
{
  this->writeUpdatedData(msg_id, getTimeStamp());
}

void BlaeckSerial::writeUpdatedData(unsigned long messageID, unsigned long timestamp)
{
  this->writeData(messageID, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckSerial::writeData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long timestamp)
{
  if (_masterSlaveConfig == Single)
  {
    if (_beforeWriteCallback != NULL)
      _beforeWriteCallback();
    this->writeLocalData(msg_id, 0, _signalIndex - 1, true, onlyUpdated, timestamp);
  }
  else if (_masterSlaveConfig == Slave)
  {
    // _beforeWriteCallback is called in BlaeckSerial::wireSlaveReceive()
    this->writeLocalData(msg_id, 0, _signalIndex - 1, true, onlyUpdated, timestamp);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    if (_beforeWriteCallback != NULL)
      _beforeWriteCallback();
    this->writeLocalData(msg_id, 0, _signalIndex - 1, false, onlyUpdated, timestamp);
    this->writeSlaveData(true, onlyUpdated);
  }
}

void BlaeckSerial::timedWriteAllData()
{
  this->timedWriteAllData(185273099);
}

void BlaeckSerial::timedWriteAllData(unsigned long msg_id)
{
  this->timedWriteAllData(msg_id, getTimeStamp());
}

void BlaeckSerial::timedWriteAllData(unsigned long msg_id, unsigned long timestamp)
{
  this->timedWriteData(msg_id, 0, _signalIndex - 1, false, timestamp);
}

void BlaeckSerial::timedWriteUpdatedData()
{
  this->timedWriteUpdatedData(185273099);
}

void BlaeckSerial::timedWriteUpdatedData(unsigned long msg_id)
{
  this->timedWriteUpdatedData(185273099, getTimeStamp());
}

void BlaeckSerial::timedWriteUpdatedData(unsigned long msg_id, unsigned long timestamp)
{
  this->timedWriteData(msg_id, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckSerial::timedWriteData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long timestamp)
{

  if (_timedFirstTime == true)
    _timedFirstTimeDone_ms = millis();
  unsigned long _timedElapsedTime_ms = (millis() - _timedFirstTimeDone_ms);

  if (((_timedElapsedTime_ms >= _timedSetPoint_ms) || _timedFirstTime == true) && _timedActivated == true)
  {
    if (_timedFirstTime == false)
      _timedSetPoint_ms += _timedInterval_ms;
    _timedFirstTime = false;
    this->writeData(msg_id, signalIndex_start, signalIndex_end, onlyUpdated, timestamp);
  }
}

void BlaeckSerial::writeRestarted()
{
  this->writeRestarted(1);
}

void BlaeckSerial::writeRestarted(unsigned long msg_id)
{
  if (!_writeRestartedAlreadyDone)
  {
    _writeRestartedAlreadyDone = true;
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xC0;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    StreamRef->write(_masterSlaveConfig);
    StreamRef->write(_slaveID);
    StreamRef->print(DeviceName);
    StreamRef->print('\0');
    StreamRef->print(DeviceHWVersion);
    StreamRef->print('\0');
    StreamRef->print(DeviceFWVersion);
    StreamRef->print('\0');
    StreamRef->print(LIBRARY_VERSION);
    StreamRef->print('\0');
    StreamRef->print(LIBRARY_NAME);
    StreamRef->print('\0');

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeDevices()
{
  this->writeDevices(1);
}

void BlaeckSerial::writeDevices(unsigned long msg_id)
{
  if (_masterSlaveConfig == Single || _masterSlaveConfig == Slave)
  {
    this->writeLocalDevices(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    scanI2CSlaves(0, 127);

    this->writeLocalDevices(msg_id, false);
    this->writeSlaveDevices(true);
  }
}

void BlaeckSerial::writeLocalDevices(unsigned long msg_id, bool send_eol)
{
  StreamRef->write("<BLAECK:");
  byte msg_key = 0xB3;
  StreamRef->write(msg_key);
  StreamRef->write(":");
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  StreamRef->write(":");
  StreamRef->write(_masterSlaveConfig);
  StreamRef->write(_slaveID);
  StreamRef->print(DeviceName);
  StreamRef->print('\0');
  StreamRef->print(DeviceHWVersion);
  StreamRef->print('\0');
  StreamRef->print(DeviceFWVersion);
  StreamRef->print('\0');
  StreamRef->print(LIBRARY_VERSION);
  StreamRef->print('\0');
  StreamRef->print(LIBRARY_NAME);
  StreamRef->print('\0');

  if (send_eol)
  {
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeSlaveDevices(bool send_eol)
{

  // Cycle through slaves
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 4; retries++)
      {
        Wire.beginTransmission(slaveindex);
        Wire.write(3);
        transmissionIsSuccess = Wire.endTransmission();
        // 0: success
        if (transmissionIsSuccess == 0)
        {
          break;
        }
      }

      if (transmissionIsSuccess == 0)
      {
        StreamRef->write(2);          // Slave config
        StreamRef->write(slaveindex); // Slave ID

        bool eolist_found = false;

        for (int i = 0; i < 1000; i++)
        {
          // request 32 bytes from slave device
          Wire.requestFrom(slaveindex, 32);

          bool eosignal_found = false;

          int charsToRead = 32;

          for (int symbolchar = 0; symbolchar <= charsToRead - 1; symbolchar++)
          {
            // Slave may send less than requested
            // DeviceInfo + \0
            //  receive a byte as character
            char c = Wire.read();
            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
            }
            //'\n'
            if (c == char(0x0A))
              eolist_found = true;
            if (eosignal_found != true && eolist_found != true)
              StreamRef->print(c);
          }
          if (eolist_found)
            break;
        }
      }
    }
  }
  if (send_eol)
  {
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeLocalData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool send_eol, bool onlyUpdated, unsigned long timestamp)
{
  if (onlyUpdated && !hasUpdatedSignals())
    return; // No updated signals

  // Bounds checking
  if (signalIndex_start < 0)
    signalIndex_start = 0;
  if (signalIndex_end >= _signalIndex)
    signalIndex_end = _signalIndex - 1;
  if (signalIndex_start > signalIndex_end)
    return; // No valid range

  _crc.setPolynome(0x04C11DB7);
  _crc.setInitial(0xFFFFFFFF);
  _crc.setXorOut(0xFFFFFFFF);
  _crc.setReverseIn(true);
  _crc.setReverseOut(true);
  _crc.restart();

  StreamRef->write("<BLAECK:");

  // Message Key
  byte msg_key = 0xD1;
  StreamRef->write(msg_key);
  _crc.add(msg_key);

  StreamRef->write(":");
  _crc.add(':');

  // Message Id
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  _crc.add(ulngCvt.bval, 4);

  StreamRef->write(":");
  _crc.add(':');

  // Restart flag
  byte restart_flag = _sendRestartFlag ? 1 : 0;
  StreamRef->write(restart_flag);
  _crc.add(restart_flag);
  _sendRestartFlag = false; // Clear the flag after first transmission

  StreamRef->write(":");
  _crc.add(':');

  // Timestamp mode
  byte timestamp_mode = (byte)_timestampMode;
  StreamRef->write(timestamp_mode);
  _crc.add(timestamp_mode);

  // Add timestamp data if mode is not NO_TIMESTAMP
  if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
  {
    ulngCvt.val = timestamp;
    StreamRef->write(ulngCvt.bval, 4);
    _crc.add(ulngCvt.bval, 4);
  }

  StreamRef->write(":");
  _crc.add(':');

  for (int i = signalIndex_start; i <= signalIndex_end; i++)
  {
    // Skip if onlyUpdated is true and signal is not updated
    if (onlyUpdated && !Signals[i].Updated)
      continue;

    intCvt.val = i;
    StreamRef->write(intCvt.bval, 2);
    _crc.add(intCvt.bval, 2);

    Signal signal = Signals[i];
    switch (signal.DataType)
    {
    case (Blaeck_bool):
    {
      boolCvt.val = *((bool *)signal.Address);
      StreamRef->write(boolCvt.bval, 1);
      _crc.add(boolCvt.bval, 1);
    }
    break;
    case (Blaeck_byte):
    {
      StreamRef->write(*((byte *)signal.Address));
      _crc.add(*((byte *)signal.Address));
    }
    break;
    case (Blaeck_short):
    {
      shortCvt.val = *((short *)signal.Address);
      StreamRef->write(shortCvt.bval, 2);
      _crc.add(shortCvt.bval, 2);
    }
    break;
    case (Blaeck_ushort):
    {
      ushortCvt.val = *((unsigned short *)signal.Address);
      StreamRef->write(ushortCvt.bval, 2);
      _crc.add(ushortCvt.bval, 2);
    }
    break;
    case (Blaeck_int):
    {
      intCvt.val = *((int *)signal.Address);
      StreamRef->write(intCvt.bval, 2);
      _crc.add(intCvt.bval, 2);
    }
    break;
    case (Blaeck_uint):
    {
      uintCvt.val = *((unsigned int *)signal.Address);
      StreamRef->write(uintCvt.bval, 2);
      _crc.add(uintCvt.bval, 2);
    }
    break;
    case (Blaeck_long):
    {
      lngCvt.val = *((long *)signal.Address);
      StreamRef->write(lngCvt.bval, 4);
      _crc.add(lngCvt.bval, 4);
    }
    break;
    case (Blaeck_ulong):
    {
      ulngCvt.val = *((unsigned long *)signal.Address);
      StreamRef->write(ulngCvt.bval, 4);
      _crc.add(ulngCvt.bval, 4);
    }
    break;
    case (Blaeck_float):
    {
      fltCvt.val = *((float *)signal.Address);
      StreamRef->write(fltCvt.bval, 4);
      _crc.add(fltCvt.bval, 4);
    }
    break;
    case (Blaeck_double):
    {
      dblCvt.val = *((double *)signal.Address);
      StreamRef->write(dblCvt.bval, 8);
      _crc.add(dblCvt.bval, 8);
    }
    break;
    }

    // Clear the updated flag after transmission if we're only sending updated signals (onlyUpdated)
    if (onlyUpdated)
    {
      Signals[i].Updated = false;
    }
  }

  if (send_eol)
  {
    // StatusByte 0: Normal transmission
    // StatusByte + CRC First Byte + CRC Second Byte + CRC Third Byte + CRC Fourth Byte
    StreamRef->write((byte)0);

    uint32_t crc_value = _crc.calc();
    StreamRef->write((byte *)&crc_value, 4);

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeSlaveData(bool send_eol, bool onlyUpdated)
{
  int signalCount = 0;
  bool slaveCRCErrorOccured = false;
  int slaveIDWithCRCError;
  int slaveSignalKeyWithCRCError;

  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    // Cycle through slaves
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 40; retries++)
      {
        Wire.beginTransmission(slaveindex);
        if (!onlyUpdated)
        {
          Wire.write(1);
        }
        else
        {
          Wire.write(4);
        }

        transmissionIsSuccess = Wire.endTransmission();
        // 0: success
        if (transmissionIsSuccess == 0)
          break;
      }

      if (transmissionIsSuccess == 0)
      {
        bool eolist_found = false;
        for (int slaveSignal = 0; slaveSignal < 1000; slaveSignal++)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          // try again
          if (receivedBytes < 2)
            continue;

          // slave may send less than requested
          for (int symbolchar = 0; symbolchar <= 31; symbolchar++)
          {
            // first receive number of bytes to expect
            char bytecount = Wire.read();
            char c;

            if (bytecount > 0 && bytecount < 127)
            {
              _crcWireCalc.restart();
              _crcWireCalc.add(bytecount);

              // First 2 bytes are always the signal index from slave
              uint8_t indexLow = Wire.read();
              uint8_t indexHigh = Wire.read();
              uint16_t slaveSignalIndex = ((uint16_t)indexHigh << 8) | ((uint16_t)indexLow);

              _crcWireCalc.add(indexLow);
              _crcWireCalc.add(indexHigh);

              StreamRef->write(lowByte(_signalIndex + slaveSignalIndex));
              StreamRef->write(highByte(_signalIndex + slaveSignalIndex));

              intCvt.val = _signalIndex + slaveSignalIndex;
              _crc.add(intCvt.bval, 2);

              // Read the remaining data bytes (bytecount - 2 since we already read the index)
              int remainingDataBytes = bytecount - 2;

              for (int i = 0; i < remainingDataBytes; i++)
              {
                // then read the data bytes
                c = Wire.read();
                StreamRef->write(c);
                _crc.add(c);
                _crcWireCalc.add(c);
              }

              // After reading all data, read the CRC bytes
              uint8_t crcWireTransmittedByte0 = Wire.read();
              uint8_t crcWireTransmittedByte1 = Wire.read();

              uint16_t crcWireTransmitted = ((uint16_t)crcWireTransmittedByte1 << 8) | ((uint16_t)crcWireTransmittedByte0);
              uint16_t crcWireCalculated = _crcWireCalc.calc();

              if (crcWireTransmitted != crcWireCalculated && slaveCRCErrorOccured == false)
              {
                // only first CRCError is sent
                slaveCRCErrorOccured = true;
                slaveIDWithCRCError = slaveindex;
                slaveSignalKeyWithCRCError = _signalIndex + slaveSignalIndex;
                break;
              }

              signalCount += 1;
            }
            else
            {
              if (bytecount == 127)
              {
                eolist_found = true;
                break;
              }
            }
          }
          if (eolist_found)
            break;
        }
      }
    }
  }

  if (send_eol)
  {
    if (slaveCRCErrorOccured)
    {
      // StatusByte 1: CRC Error at I2C transmission from Slave to Master
      // StatusByte + 0 + SignalKey First Byte + SignalKey Second Byte + SlaveID
      StreamRef->write(1);
      StreamRef->write((byte)0);
      intCvt.val = slaveSignalKeyWithCRCError;
      StreamRef->write(intCvt.bval, 2);
      StreamRef->write(slaveIDWithCRCError);
    }
    else
    {
      // StatusByte 0: Normal transmission, no wire CRC errors occured
      // StatusByte + CRC First Byte + CRC Second Byte + CRC Third Byte + CRC Fourth Byte
      StreamRef->write((byte)0);

      uint32_t crc_value = _crc.calc();
      StreamRef->write((byte *)&crc_value, 4);
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeLocalSymbols(unsigned long msg_id, bool send_eol)
{
  StreamRef->write("<BLAECK:");
  byte msg_key = 0xB0;
  StreamRef->write(msg_key);
  StreamRef->write(":");
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  StreamRef->write(":");

  for (int i = 0; i < _signalIndex; i++)
  {
    StreamRef->write(_masterSlaveConfig);
    StreamRef->write(_slaveID);

    Signal signal = Signals[i];

    StreamRef->print(signal.SignalName);
    StreamRef->print('\0');

    switch (signal.DataType)
    {
    case (Blaeck_bool):
    {
      StreamRef->write((byte)0x0);
      break;
    }
    case (Blaeck_byte):
    {
      StreamRef->write(0x1);
      break;
    }
    case (Blaeck_short):
    {
      StreamRef->write(0x2);
      break;
    }
    case (Blaeck_ushort):
    {
      StreamRef->write(0x3);
      break;
    }
    case (Blaeck_int):
    {
      StreamRef->write(0x4);
      break;
    }
    case (Blaeck_uint):
    {
      StreamRef->write(0x5);
      break;
    }
    case (Blaeck_long):
    {
      StreamRef->write(0x6);
      break;
    }
    case (Blaeck_ulong):
    {
      StreamRef->write(0x7);
      break;
    }
    case (Blaeck_float):
    {
      StreamRef->write(0x8);
      break;
    }
    case (Blaeck_double):
    {
      StreamRef->write(0x9);
      break;
    }
    }
  }
  if (send_eol)
  {
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeSlaveSymbols(bool send_eol)
{

  int signalCount = 0;

  // Cycle through slaves
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    if (slaveFound(slaveindex))
    {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 4; retries++)
      {
        Wire.beginTransmission(slaveindex);
        Wire.write(0);
        transmissionIsSuccess = Wire.endTransmission();
        // 0: success
        if (transmissionIsSuccess == 0)
          break;
      }

      if (transmissionIsSuccess == 0)
      {

        bool eolist_found = false;

        for (int i = 0; i < 1000; i++)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          if (receivedBytes < 2)
            continue; // try again

          bool eosignal_found = false;

          int charsToRead = 32;

          for (int symbolchar = 0; symbolchar <= charsToRead - 1; symbolchar++)
          {
            // Slave may send less than requested
            // SignalName + \0 + DataType
            //  receive a byte as character
            char c = Wire.read();

            //'\0'
            if (c != char(0x00) && symbolchar == 0)
            {
              StreamRef->write(2);          // Slave config
              StreamRef->write(slaveindex); // Slave ID
            }
            if (c == char(0x00) && symbolchar == 0)
            {
              continue;
            }

            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
              signalCount += 1;
            }

            //'\n'
            if (c == char(0x0A))
              eolist_found = true;

            if (eosignal_found != true && eolist_found != true)
              StreamRef->print(c);
          }
          if (eolist_found)
            break;
        }
      }
    }
  }
  if (send_eol)
  {
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::scanI2CSlaves(char addressStart, char addressEnd)
{
  // Cycle through slaves
  for (int slaveindex = addressStart; slaveindex <= addressEnd; slaveindex++)
  {
    byte transmissionIsSuccess = false;

    for (byte retries = 0; retries < 4; retries++)
    {
      Wire.beginTransmission(slaveindex);
      Wire.write(2);
      transmissionIsSuccess = Wire.endTransmission();
      // 0: success
      if (transmissionIsSuccess == 0)
        break;
    }

    if (transmissionIsSuccess == 0)
    {
      storeSlave(slaveindex, false);

      Wire.requestFrom(slaveindex, 1);

      // Expecting response 0xAA from slave -> Slave found
      // Receive a byte as character
      char c = Wire.read();

      if (c == char(0xAA))
      {
        storeSlave(slaveindex, true);
      }
      else
      {
        storeSlave(slaveindex, false);
      }
    }
  }
}

void BlaeckSerial::tickUpdated()
{
  this->tickUpdated(185273099);
}

void BlaeckSerial::tickUpdated(unsigned long msg_id)
{
  this->tickUpdated(msg_id, getTimeStamp());
}

void BlaeckSerial::tickUpdated(unsigned long msg_id, unsigned long timestamp)
{
  this->tick(msg_id, true, timestamp);
}

void BlaeckSerial::tick()
{
  this->tick(185273099);
}

void BlaeckSerial::tick(unsigned long msg_id)
{
  this->tick(msg_id, getTimeStamp());
}

void BlaeckSerial::tick(unsigned long msg_id, unsigned long timestamp)
{
  this->tick(msg_id, false, timestamp);
}

void BlaeckSerial::tick(unsigned long msg_id, bool onlyUpdated, unsigned long timestamp)
{
  this->read();
  this->writeRestarted(msg_id);
  this->timedWriteData(msg_id, 0, _signalIndex - 1, onlyUpdated, timestamp);
}

void BlaeckSerial::wireSlaveTransmitStatusByte()
{
  Wire.write(0xAA);
}

void BlaeckSerial::wireSlaveTransmitSingleDevice()
{
  char little_s_string[32] = "";

  if (_wireDeviceIndex == 0)
  {
    DeviceName.toCharArray(little_s_string, 32);
    Wire.print(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 1)
  {
    DeviceHWVersion.toCharArray(little_s_string, 32);
    Wire.print(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 2)
  {
    DeviceFWVersion.toCharArray(little_s_string, 32);
    Wire.print(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 3)
  {
    LIBRARY_VERSION.toCharArray(little_s_string, 32);
    Wire.print(little_s_string);
    Wire.write('\0');
  }
  else if (_wireDeviceIndex == 4)
  {
    LIBRARY_NAME.toCharArray(little_s_string, 32);
    Wire.print(little_s_string);
    Wire.write('\0');
  }

  Wire.write(0x0D);

  _wireDeviceIndex += 1;
  if (_wireDeviceIndex > 4)
  {
    _wireDeviceIndex = 0;
    Wire.write(0x0A);
  }
}

void BlaeckSerial::wireSlaveTransmitSingleSymbol()
{
  Signal signal = Signals[_wireSignalIndex];

  char little_s_string[32] = "";
  signal.SignalName.toCharArray(little_s_string, 32);

  Wire.print(little_s_string);
  Wire.write('\0');

  switch (signal.DataType)
  {
  case (Blaeck_bool):
  {
    Wire.write(0x0);
    break;
  }
  case (Blaeck_byte):
  {
    Wire.write(0x1);
    break;
  }
  case (Blaeck_short):
  {
    Wire.write(0x2);
    break;
  }
  case (Blaeck_ushort):
  {
    Wire.write(0x3);
    break;
  }
  case (Blaeck_int):
  {
    Wire.write(0x4);
    break;
  }
  case (Blaeck_uint):
  {
    Wire.write(0x5);
    break;
  }
  case (Blaeck_long):
  {
    Wire.write(0x6);
    break;
  }
  case (Blaeck_ulong):
  {
    Wire.write(0x7);
    break;
  }
  case (Blaeck_float):
  {
    Wire.write(0x8);
    break;
  }
  case (Blaeck_double):
  {
    Wire.write(0x9);
    break;
  }
  }

  Wire.write(0x0D);

  _wireSignalIndex += 1;
  if (_wireSignalIndex >= _signalIndex)
  {
    _wireSignalIndex = 0;
    Wire.write(0x0A);
  }
}

void BlaeckSerial::wireSlaveTransmitSingleDataPoint(bool onlyUpdated)
{
  _crcWire.restart();

  Signal signal = Signals[_wireSignalIndex];

  if (!onlyUpdated || (onlyUpdated && Signals[_wireSignalIndex].Updated))
  {
    switch (signal.DataType)
    {
    case (Blaeck_bool):
    {
      // Send: [index_size + data_size][index_bytes][data_bytes]
      Wire.write(3); // 2 bytes index + 1 byte data
      _crcWire.add(3);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      boolCvt.val = *((bool *)signal.Address);
      Wire.write(boolCvt.bval, 1);
      _crcWire.add(boolCvt.bval, 1);
    }
    break;
    case (Blaeck_byte):
    {
      Wire.write(3);
      _crcWire.add(3);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      Wire.write(*((byte *)signal.Address));
      _crcWire.add(*((byte *)signal.Address));
    }
    break;
    case (Blaeck_short):
    {
      Wire.write(4);
      _crcWire.add(4);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      shortCvt.val = *((short *)signal.Address);
      Wire.write(shortCvt.bval, 2);
      _crcWire.add(shortCvt.bval, 2);
    }
    break;
    case (Blaeck_ushort):
    {
      Wire.write(4);
      _crcWire.add(4);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      ushortCvt.val = *((unsigned short *)signal.Address);
      Wire.write(ushortCvt.bval, 2);
      _crcWire.add(ushortCvt.bval, 2);
    }
    break;
    case (Blaeck_int):
    {
      Wire.write(4);
      _crcWire.add(4);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      intCvt.val = *((int *)signal.Address);
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
    }
    break;
    case (Blaeck_uint):
    {
      Wire.write(4);
      _crcWire.add(4);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      uintCvt.val = *((unsigned int *)signal.Address);
      Wire.write(uintCvt.bval, 2);
      _crcWire.add(uintCvt.bval, 2);
    }
    break;
    case (Blaeck_long):
    {
      Wire.write(6);
      _crcWire.add(6);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      lngCvt.val = *((long *)signal.Address);
      Wire.write(lngCvt.bval, 4);
      _crcWire.add(lngCvt.bval, 4);
    }
    break;
    case (Blaeck_ulong):
    {
      Wire.write(6);
      _crcWire.add(6);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      ulngCvt.val = *((unsigned long *)signal.Address);
      Wire.write(ulngCvt.bval, 4);
      _crcWire.add(ulngCvt.bval, 4);
    }
    break;
    case (Blaeck_float):
    {
      Wire.write(6);
      _crcWire.add(6);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      fltCvt.val = *((float *)signal.Address);
      Wire.write(fltCvt.bval, 4);
      _crcWire.add(fltCvt.bval, 4);
    }
    break;
    case (Blaeck_double):
    {
      Wire.write(10);
      _crcWire.add(10);
      intCvt.val = _wireSignalIndex;
      Wire.write(intCvt.bval, 2);
      _crcWire.add(intCvt.bval, 2);
      dblCvt.val = *((double *)signal.Address);
      Wire.write(dblCvt.bval, 8);
      _crcWire.add(dblCvt.bval, 8);
    }
    break;
    }
  }

  // Clear the updated flag after transmission if we're only sending updated signals (onlyUpdated)
  if (onlyUpdated)
  {
    Signals[_wireSignalIndex].Updated = false;
  }

  _wireSignalIndex += 1;

  if (_signalIndex == 0)
  {
    Wire.write(0);
  }
  else
  {
    uint16_t crc_value = _crcWire.calc();
    Wire.write((byte *)&crc_value, 2);
  }

  if (_wireSignalIndex >= _signalIndex)
  {
    _wireSignalIndex = 0;
    Wire.write(0x7F);
  }
}

void BlaeckSerial::wireSlaveReceive()
{
  _wireMode = Wire.read();
  _wireSignalIndex = 0;
  _wireDeviceIndex = 0;

  if (_masterSlaveConfig == Slave && (_wireMode == 1 || _wireMode == 4))
  {
    if (_beforeWriteCallback != NULL)
      _beforeWriteCallback();
  };
}

void BlaeckSerial::wireSlaveTransmitToMaster()
{
  if (_wireMode == 0)
    this->wireSlaveTransmitSingleSymbol();
  if (_wireMode == 1)
    this->wireSlaveTransmitSingleDataPoint(false);
  if (_wireMode == 4)
    this->wireSlaveTransmitSingleDataPoint(true);
  if (_wireMode == 2)
    this->wireSlaveTransmitStatusByte();
  if (_wireMode == 3)
    this->wireSlaveTransmitSingleDevice();
}

bool BlaeckSerial::slaveFound(const unsigned int index)
{
  if (index > 127)
    return false;
  return (boolean)bitRead(_slaveFound[index / 8], index % 8);
}

void BlaeckSerial::storeSlave(const unsigned int index, const boolean value)
{
  if (index > 127)
    return;
  bitWrite(_slaveFound[index / 8], index % 8, value);
}

void BlaeckSerial::markSignalUpdated(int signalIndex)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    Signals[signalIndex].Updated = true;
  }
}

void BlaeckSerial::markSignalUpdated(String signalName)
{
  for (int i = 0; i < _signalIndex; i++)
  {
    if (Signals[i].SignalName == signalName)
    {
      Signals[i].Updated = true;
      break;
    }
  }
}

void BlaeckSerial::markAllSignalsUpdated()
{
  for (int i = 0; i < _signalIndex; i++)
  {
    Signals[i].Updated = true;
  }
}

void BlaeckSerial::clearAllUpdateFlags()
{
  for (int i = 0; i < _signalIndex; i++)
  {
    Signals[i].Updated = false;
  }
}

bool BlaeckSerial::hasUpdatedSignals()
{
  for (int i = 0; i < _signalIndex; i++)
  {
    if (Signals[i].Updated)
    {
      return true;
    }
  }
  return false;
}

void BlaeckSerial::setTimestampMode(BlaeckTimestampMode mode)
{
  _timestampMode = mode;

  // Set default callbacks for built-in modes
  switch (mode)
  {
  case BLAECK_MICROS:
    _timestampCallback = micros;
    break;
  case BLAECK_RTC:
    // User must provide RTC callback - don't override if already set
    if (_timestampCallback == micros)
    {
      _timestampCallback = nullptr;
    }
    break;
  case BLAECK_NO_TIMESTAMP:
  default:
    _timestampCallback = nullptr;
    break;
  }
}

void BlaeckSerial::setTimestampCallback(unsigned long (*callback)())
{
  _timestampCallback = callback;
}

bool BlaeckSerial::hasValidTimestampCallback() const
{
  return (_timestampMode != BLAECK_NO_TIMESTAMP && _timestampCallback != nullptr);
}

unsigned long BlaeckSerial::getTimeStamp()
{
  unsigned long timestamp = 0;

  // Add timestamp data if mode is not NO_TIMESTAMP
  if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
  {
    timestamp = _timestampCallback();
  }

  return timestamp;
}

void BlaeckSerial::validatePlatformSizes()
{
#ifdef __AVR__
  // AVR (8-bit) platform checks
  static_assert(sizeof(int) == 2, "BlaeckSerial: Expected 2-byte int on AVR");
  static_assert(sizeof(unsigned int) == 2, "BlaeckSerial: Expected 2-byte unsigned int on AVR");
  static_assert(sizeof(double) == 4, "BlaeckSerial: Expected 4-byte double on AVR");
  static_assert(sizeof(double) == sizeof(float), "BlaeckSerial: double should equal float on AVR");
#else
  // 32-bit platform checks
  static_assert(sizeof(int) == 4, "BlaeckSerial: Expected 4-byte int on 32-bit platforms");
  static_assert(sizeof(unsigned int) == 4, "BlaeckSerial: Expected 4-byte unsigned int on 32-bit platforms");
  static_assert(sizeof(double) == 8, "BlaeckSerial: Expected 8-byte double on 32-bit platforms");
  static_assert(sizeof(double) != sizeof(float), "BlaeckSerial: double should differ from float on 32-bit platforms");
  static_assert(sizeof(int) == sizeof(long), "BlaeckSerial: int/long size mismatch breaks type remapping");
  static_assert(sizeof(unsigned int) == sizeof(unsigned long), "BlaeckSerial: uint/ulong size mismatch breaks type remapping");
#endif

  // Universal checks (should be same on ALL Arduino platforms)
  static_assert(sizeof(bool) == 1, "BlaeckSerial: Expected 1-byte bool");
  static_assert(sizeof(byte) == 1, "BlaeckSerial: Expected 1-byte byte");
  static_assert(sizeof(short) == 2, "BlaeckSerial: Expected 2-byte short");
  static_assert(sizeof(unsigned short) == 2, "BlaeckSerial: Expected 2-byte unsigned short");
  static_assert(sizeof(long) == 4, "BlaeckSerial: Expected 4-byte long");
  static_assert(sizeof(unsigned long) == 4, "BlaeckSerial: Expected 4-byte unsigned long");
  static_assert(sizeof(float) == 4, "BlaeckSerial: Expected 4-byte float");
}