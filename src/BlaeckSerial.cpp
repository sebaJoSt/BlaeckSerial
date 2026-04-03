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
  delete[] Signals;
  Signals = nullptr;
  _bufFree();
}

void BlaeckSerial::begin(Stream *Ref, unsigned int size)
{
  StreamRef = (Stream *)Ref;
  _signalCapacity = size;
  if (Signals != nullptr)
  {
    delete[] Signals;
    Signals = nullptr;
  }
  Signals = new Signal[size];
  _signalIndex = 0;
  SignalCount = 0;
  _schemaHash = 0;
  _signalOverflowOccurred = false;
  _signalOverflowCount = 0;
  // Assign the static singleton used in the static handlers.
  BlaeckSerial::_pSingletonInstance = this;

  if (_bufferedWrites)
    _bufAllocate();
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
  _slaveSymbolPrefix = "";
  _slaveSymbolPrefix.reserve(8); // "S127_" + null
  _slaveSymbolPrefix += "S";
  _slaveSymbolPrefix += _slaveID;
  _slaveSymbolPrefix += "_";

  Wire.onReceive(OnSendHandler);
  Wire.onRequest(OnReceiveHandler);
  Wire.begin(_slaveID);

  begin(Ref, size);
}

void BlaeckSerial::addSignal(String signalName, bool *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, byte *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, short *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, unsigned short *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, int *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
#ifdef __AVR__
  Signals[_signalIndex].DataType = Blaeck_int; // 2 bytes
#else
  Signals[_signalIndex].DataType = Blaeck_long; // Treat as 4-byte long
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, unsigned int *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
#ifdef __AVR__
  Signals[_signalIndex].DataType = Blaeck_uint; // 2 bytes
#else
  Signals[_signalIndex].DataType = Blaeck_ulong; // Treat as 4-byte unsigned long
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, long *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, unsigned long *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, float *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, double *value, bool prefixSlaveID)
{
  if (_signalIndex >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName, prefixSlaveID);
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
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::deleteSignals()
{
  _signalIndex = 0;
  SignalCount = _signalIndex;
  _schemaHash = 0;
  _signalOverflowOccurred = false;
  _signalOverflowCount = 0;
}

uint16_t BlaeckSerial::_computeSchemaHash()
{
  // CRC16-CCITT (init=0x0000, poly=0x1021) over signal names + datatype codes.
  // Must match Python: binascii.crc_hqx(data, 0) & 0xFFFF
  uint16_t crc = 0x0000;
  for (int j = 0; j < _signalIndex; j++)
  {
    const char *name = Signals[j].SignalName.c_str();
    while (*name)
    {
      byte b = (byte)*name++;
      crc ^= ((uint16_t)b << 8);
      for (byte k = 0; k < 8; k++)
      {
        if (crc & 0x8000)
          crc = (crc << 1) ^ 0x1021;
        else
          crc <<= 1;
      }
    }
    byte code = (byte)Signals[j].DataType;
    crc ^= ((uint16_t)code << 8);
    for (byte k = 0; k < 8; k++)
    {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }
  return crc & 0xFFFF;
}

void BlaeckSerial::setSignalName(int signalIndex, String signalName, bool prefixSlaveID)
{
  if (signalIndex < 0 || signalIndex >= (int)_signalCapacity)
    return;

  Signals[signalIndex].SignalName = "";
  size_t needed = signalName.length() + 1;
  if (_masterSlaveConfig == Slave && prefixSlaveID)
    needed += _slaveSymbolPrefix.length();
  Signals[signalIndex].SignalName.reserve(needed);

  if (_masterSlaveConfig == Slave && prefixSlaveID)
    Signals[signalIndex].SignalName += _slaveSymbolPrefix;
  Signals[signalIndex].SignalName += signalName;
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
      if (_fixedInterval_ms == BLAECK_INTERVAL_CLIENT)
      {
        unsigned long timedInterval_ms = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);
        this->_setTimedDataState(true, timedInterval_ms);
      }
    }
    else if (strcmp(COMMAND, "BLAECK.DEACTIVATE") == 0)
    {
      if (_fixedInterval_ms == BLAECK_INTERVAL_CLIENT)
      {
        this->_setTimedDataState(false, _timedInterval_ms);
      }
    }

    if (_commandCallback != NULL)
    {
      if (!_commandCallbackDeprecationWarned && StreamRef != nullptr)
      {
        StreamRef->println("WARNING: setCommandCallback(...) is deprecated; use onCommand(...) / onAnyCommand(...)");
        _commandCallbackDeprecationWarned = true;
      }
      _commandCallback(COMMAND, PARAMETER, STRING_01);
    }
    _dispatchRegisteredHandlers();
  }
}

void BlaeckSerial::setCommandCallback(void (*callback)(char *command, int *parameter, char *string_01))
{
  _commandCallback = callback;
  if (_commandCallback != NULL && !_commandCallbackDeprecationWarned && StreamRef != nullptr)
  {
    StreamRef->println("WARNING: setCommandCallback(...) is deprecated; use onCommand(...) / onAnyCommand(...)");
    _commandCallbackDeprecationWarned = true;
  }
}

void BlaeckSerial::setBeforeWriteCallback(void (*callback)())
{
  _beforeWriteCallback = callback;
}

bool BlaeckSerial::onCommand(const char *command, BlaeckCommandHandler handler)
{
  if (command == nullptr || handler == nullptr || command[0] == '\0')
  {
    return false;
  }
  if (strlen(command) >= MAX_COMMAND_NAME_COUNT)
  {
    if (StreamRef != nullptr)
    {
      StreamRef->print("Command name too long for handler table: ");
      StreamRef->println(command);
    }
    return false;
  }

  for (byte i = 0; i < _commandHandlerCapacity; i++)
  {
    if (_commandHandlers[i].inUse && strcmp(_commandHandlers[i].command, command) == 0)
    {
      _commandHandlers[i].handler = handler;
      return true;
    }
  }

  for (byte i = 0; i < _commandHandlerCapacity; i++)
  {
    if (!_commandHandlers[i].inUse)
    {
      strncpy(_commandHandlers[i].command, command, MAX_COMMAND_NAME_COUNT - 1);
      _commandHandlers[i].command[MAX_COMMAND_NAME_COUNT - 1] = '\0';
      _commandHandlers[i].handler = handler;
      _commandHandlers[i].inUse = true;
      return true;
    }
  }

  if (StreamRef != nullptr)
  {
    StreamRef->print("Command handler table full for: ");
    StreamRef->println(command);
  }
  return false;
}

void BlaeckSerial::onAnyCommand(BlaeckAnyCommandHandler handler)
{
  _anyCommandHandler = handler;
}

void BlaeckSerial::clearAllCommandHandlers()
{
  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    _commandHandlers[i].inUse = false;
    _commandHandlers[i].handler = nullptr;
    _commandHandlers[i].command[0] = '\0';
  }
  _anyCommandHandler = nullptr;
}

void BlaeckSerial::setCommandHandlerCapacity(byte capacity)
{
  if (capacity == 0)
  {
    capacity = 1;
  }
  if (capacity > MAX_COMMAND_HANDLERS)
  {
    capacity = MAX_COMMAND_HANDLERS;
  }
  _commandHandlerCapacity = capacity;
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
  strncpy(tempChars, receivedChars, sizeof(tempChars) - 1);
  tempChars[sizeof(tempChars) - 1] = '\0';
  char *strtokIndx;
  strtokIndx = strtok(tempChars, ",");
  if (strtokIndx != NULL)
  {
    strncpy(COMMAND, strtokIndx, sizeof(COMMAND) - 1);
    COMMAND[sizeof(COMMAND) - 1] = '\0';
  }
  else
  {
    COMMAND[0] = '\0';
  }

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

void BlaeckSerial::_parseCommandTokens(const char *raw)
{
  _parsedCommand[0] = '\0';
  _parsedParamCount = 0;
  for (byte i = 0; i < MAX_COMMAND_PARAM_COUNT; i++)
  {
    _parsedParamPtrs[i] = nullptr;
  }

  if (raw == nullptr || raw[0] == '\0')
  {
    return;
  }

  strncpy(_parsedTokenBuffer, raw, sizeof(_parsedTokenBuffer) - 1);
  _parsedTokenBuffer[sizeof(_parsedTokenBuffer) - 1] = '\0';

  char *token = strtok(_parsedTokenBuffer, ",");
  if (token == nullptr)
  {
    return;
  }

  while (*token == ' ')
    token++;
  strncpy(_parsedCommand, token, MAX_COMMAND_NAME_COUNT - 1);
  _parsedCommand[MAX_COMMAND_NAME_COUNT - 1] = '\0';

  while (_parsedParamCount < MAX_COMMAND_PARAM_COUNT)
  {
    token = strtok(NULL, ",");
    if (token == nullptr)
      break;
    while (*token == ' ')
      token++;
    _parsedParamPtrs[_parsedParamCount] = token;
    _parsedParamCount++;
  }
}

void BlaeckSerial::_dispatchRegisteredHandlers()
{
  _parseCommandTokens(receivedChars);
  if (_parsedCommand[0] == '\0')
  {
    return;
  }

  for (byte i = 0; i < _commandHandlerCapacity; i++)
  {
    if (_commandHandlers[i].inUse &&
        _commandHandlers[i].handler != nullptr &&
        strcmp(_commandHandlers[i].command, _parsedCommand) == 0)
    {
      _commandHandlers[i].handler(
          _parsedCommand,
          (const char *const *)_parsedParamPtrs,
          _parsedParamCount);
      break;
    }
  }

  if (_anyCommandHandler != nullptr)
  {
    _anyCommandHandler(
        _parsedCommand,
        (const char *const *)_parsedParamPtrs,
        _parsedParamCount);
  }
}

void BlaeckSerial::_setTimedDataState(bool timedActivated, unsigned long timedInterval_ms)
{
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    _timedSetPoint_ms = timedInterval_ms;
    _timedInterval_ms = timedInterval_ms;
    _timedFirstTime = true;
  }
}

void BlaeckSerial::setIntervalMs(long interval_ms)
{
  if (interval_ms >= 0)
  {
    _fixedInterval_ms = interval_ms;
    this->_setTimedDataState(true, (unsigned long)interval_ms);
  }
  else if (interval_ms == BLAECK_INTERVAL_OFF)
  {
    _fixedInterval_ms = BLAECK_INTERVAL_OFF;
    this->_setTimedDataState(false, _timedInterval_ms);
  }
  else if (interval_ms == BLAECK_INTERVAL_CLIENT)
  {
    _fixedInterval_ms = BLAECK_INTERVAL_CLIENT;
  }
  else if (StreamRef != nullptr)
  {
    StreamRef->print("Invalid interval mode: ");
    StreamRef->println(interval_ms);
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
    refreshI2CSlavesIfNeeded();

    // Recompute schema hash from local + slave signals
    _schemaHashAccum = 0x0000;
    this->writeLocalSymbols(msg_id, false);
    this->writeSlaveSymbols(true);
    _schemaHash = _schemaHashAccum & 0xFFFF;
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

void BlaeckSerial::write(String signalName, bool value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, byte value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, short value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned short value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, int value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned int value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, long value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, unsigned long value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, float value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(String signalName, double value, unsigned long messageID, unsigned long long timestamp)
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

void BlaeckSerial::write(int signalIndex, bool value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, byte value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, short value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, unsigned short value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, int value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, unsigned int value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, long value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, unsigned long value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, float value, unsigned long messageID, unsigned long long timestamp)
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
void BlaeckSerial::write(int signalIndex, double value, unsigned long messageID, unsigned long long timestamp)
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

void BlaeckSerial::writeAllData(unsigned long msg_id, unsigned long long timestamp)
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

void BlaeckSerial::writeUpdatedData(unsigned long messageID, unsigned long long timestamp)
{
  this->writeData(messageID, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckSerial::writeData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
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
    bool skipSlaves[128] = {false};
    byte skippedSlaveCount = 0;
    byte firstSkippedSlaveID = 0xFF;
    byte firstSkipReason = 0x00;

    refreshI2CSlavesIfNeeded();
    prepareMasterSlaveSkipMap(skipSlaves, skippedSlaveCount, firstSkippedSlaveID, firstSkipReason);

    if (_beforeWriteCallback != NULL)
      _beforeWriteCallback();
    this->writeLocalData(msg_id, 0, _signalIndex - 1, false, onlyUpdated, timestamp);
    this->writeSlaveData(true, onlyUpdated, skipSlaves, skippedSlaveCount, firstSkippedSlaveID, firstSkipReason);
  }
}

void BlaeckSerial::prepareMasterSlaveSkipMap(bool *skipSlaves, byte &skippedSlaveCount, byte &firstSkippedSlaveID, byte &firstSkipReason)
{
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    if (!slaveFound(slaveindex))
      continue;

    byte transmissionIsSuccess = false;
    for (byte retries = 0; retries < 4; retries++)
    {
      Wire.beginTransmission(slaveindex);
      Wire.write(2);
      transmissionIsSuccess = Wire.endTransmission();
      if (transmissionIsSuccess == 0)
        break;
    }
    if (transmissionIsSuccess != 0)
    {
      if (!skipSlaves[slaveindex])
      {
        skipSlaves[slaveindex] = true;
        skippedSlaveCount++;
        if (firstSkippedSlaveID == 0xFF)
        {
          firstSkippedSlaveID = (byte)slaveindex;
          firstSkipReason = 0x01; // Preflight no response
        }
      }
      continue;
    }

    const unsigned long timeout_ms = 20;
    unsigned long start_ms = millis();
    bool statusOk = false;

    while ((millis() - start_ms) < timeout_ms && !statusOk)
    {
      byte receivedBytes = Wire.requestFrom(slaveindex, 1);
      if (receivedBytes < 1)
        continue;

      for (int i = 0; i < receivedBytes && Wire.available(); i++)
      {
        int c = Wire.read();
        if (c == 0xAA)
        {
          statusOk = true;
          break;
        }
      }
    }

    if (!statusOk && !skipSlaves[slaveindex])
    {
      skipSlaves[slaveindex] = true;
      skippedSlaveCount++;
      if (firstSkippedSlaveID == 0xFF)
      {
        firstSkippedSlaveID = (byte)slaveindex;
        firstSkipReason = 0x01; // Preflight no response
      }
    }
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

void BlaeckSerial::timedWriteAllData(unsigned long msg_id, unsigned long long timestamp)
{
  this->timedWriteData(msg_id, 0, _signalIndex - 1, false, timestamp);
}

void BlaeckSerial::timedWriteUpdatedData()
{
  this->timedWriteUpdatedData(185273099);
}

void BlaeckSerial::timedWriteUpdatedData(unsigned long msg_id)
{
  this->timedWriteUpdatedData(msg_id, getTimeStamp());
}

void BlaeckSerial::timedWriteUpdatedData(unsigned long msg_id, unsigned long long timestamp)
{
  this->timedWriteData(msg_id, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckSerial::timedWriteData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
{

  if (_timedFirstTime == true)
    _timedFirstTimeDone_ms = millis();
  unsigned long _timedElapsedTime_ms = (millis() - _timedFirstTimeDone_ms);

  if (((_timedElapsedTime_ms >= _timedSetPoint_ms) || _timedFirstTime == true) && _timedActivated == true)
  {
    if (_timedFirstTime == false)
    {
      while (_timedSetPoint_ms <= _timedElapsedTime_ms)
        _timedSetPoint_ms += _timedInterval_ms;
    }
    _timedFirstTime = false;
    this->writeData(msg_id, signalIndex_start, signalIndex_end, onlyUpdated, timestamp);
  }
}

// ── Buffered writes ────────────────────────────────────────────────

void BlaeckSerial::_bufAllocate()
{
  _bufFree();
  // Max frame size: D2 is largest.
  // Header(22) + per-signal(10) + timestamp(9) + tail(9) + footer(10) + margin
  _frameBufSize = 60 + (int)_signalCapacity * 10;
  // B0/B3 can also be large with long names; ensure minimum
  int b0b3_est = 60 + (int)_signalCapacity * 30;
  if (b0b3_est > _frameBufSize)
    _frameBufSize = b0b3_est;
  _frameBuf = new (std::nothrow) byte[_frameBufSize];
  if (_frameBuf == nullptr)
  {
    _frameBufSize = 0;
  }
  _bufOverflow = false;
  _bufOverflowWarned = false;
}

bool BlaeckSerial::_bufEnsure(size_t addLen)
{
  if (_frameBuf == nullptr)
  {
    return false;
  }

  if (addLen > (SIZE_MAX - (size_t)_framePos))
  {
    return false;
  }

  size_t needed = (size_t)_framePos + addLen;
  if (needed <= (size_t)_frameBufSize)
  {
    return true;
  }

  size_t newSize = (size_t)_frameBufSize;
  while (newSize < needed)
  {
    if (newSize < 128)
    {
      newSize = 128;
    }
    else
    {
      if (newSize > (SIZE_MAX / 2))
      {
        return false;
      }
      newSize *= 2;
    }
  }

  if (newSize > (size_t)INT_MAX)
  {
    return false;
  }

  byte *newBuf = new (std::nothrow) byte[newSize];
  if (newBuf == nullptr)
  {
    return false;
  }

  memcpy(newBuf, _frameBuf, _framePos);
  delete[] _frameBuf;
  _frameBuf = newBuf;
  _frameBufSize = (int)newSize;
  return true;
}

void BlaeckSerial::_bufFree()
{
  delete[] _frameBuf;
  _frameBuf = nullptr;
  _frameBufSize = 0;
  _framePos = 0;
}

void BlaeckSerial::setBufferedWrites(bool enabled)
{
  _bufferedWrites = enabled;
  if (enabled && _frameBuf == nullptr && _signalCapacity > 0)
    _bufAllocate();
  else if (!enabled)
    _bufFree();
}

void BlaeckSerial::_bufHeader(byte msgKey, unsigned long msgId)
{
  _bufStr("<BLAECK:");
  _bufByte(msgKey);
  _bufByte(':');
  ulngCvt.val = msgId;
  _bufBytes(ulngCvt.bval, 4);
  _bufByte(':');
}

void BlaeckSerial::_bufDevice(byte msc, byte sid, const String &name,
                              const String &hw, const String &fw)
{
  _bufByte(msc);
  _bufByte(sid);
  _bufStr0(name);
  _bufStr0(hw);
  _bufStr0(fw);
  _bufStr0(LIBRARY_VERSION);
  _bufStr0(LIBRARY_NAME);
}

// ── Frame write functions ─────────────────────────────────────────

void BlaeckSerial::writeRestarted()
{
  this->writeRestarted(1);
}

void BlaeckSerial::writeRestarted(unsigned long msg_id)
{
  if (!_writeRestartedAlreadyDone)
  {
    _writeRestartedAlreadyDone = true;

    if (_bufferedWrites && _frameBuf)
    {
      _bufReset();
      _bufHeader(0xC0, msg_id);
      _bufDevice(_masterSlaveConfig, _slaveID, DeviceName, DeviceHWVersion, DeviceFWVersion);
      _bufFooter();
      _bufSend();
    }
    else
    {
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
    refreshI2CSlavesIfNeeded();

    this->writeLocalDevices(msg_id, false);
    this->writeSlaveDevices(true);
  }
}

void BlaeckSerial::refreshI2CSlavesIfNeeded()
{
  unsigned long now = millis();
  if (!_i2cScanInitialized || (now - _lastI2CScanMs) >= I2C_SCAN_INTERVAL_MS)
  {
    scanI2CSlaves(0, 127);
    _lastI2CScanMs = now;
    _i2cScanInitialized = true;
  }
}

void BlaeckSerial::writeLocalDevices(unsigned long msg_id, bool send_eol)
{
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xB3, msg_id);
    _bufDevice(_masterSlaveConfig, _slaveID, DeviceName, DeviceHWVersion, DeviceFWVersion);
    if (send_eol)
    {
      _bufFooter();
      _bufSend();
    }
  }
  else
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
        if (_bufferedWrites && _frameBuf)
        {
          _bufByte(2);          // Slave config
          _bufByte(slaveindex); // Slave ID
        }
        else
        {
          StreamRef->write(2);          // Slave config
          StreamRef->write(slaveindex); // Slave ID
        }

        bool eolist_found = false;
        const unsigned long timeout_ms = 50;
        unsigned long start_ms = millis();
        while ((millis() - start_ms) < timeout_ms && !eolist_found)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          if (receivedBytes < 1)
            continue;

          bool eosignal_found = false;

          for (int symbolchar = 0; symbolchar < receivedBytes && Wire.available(); symbolchar++)
          {
            // Slave may send less than requested
            // DeviceInfo + \0
            //  receive a byte as character
            char c = (char)Wire.read();
            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
            }
            //'\n'
            if (c == char(0x0A))
              eolist_found = true;
            if (eosignal_found != true && eolist_found != true)
            {
              if (_bufferedWrites && _frameBuf)
                _bufByte((byte)c);
              else
                StreamRef->print(c);
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
    if (_bufferedWrites && _frameBuf)
    {
      _bufFooter();
      _bufSend();
    }
    else
    {
      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();
    }
  }
}

void BlaeckSerial::writeLocalData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool send_eol, bool onlyUpdated, unsigned long long timestamp)
{
  if (onlyUpdated && !hasUpdatedSignals() && send_eol)
    return; // No updated signals

  // Bounds checking
  if (signalIndex_start < 0)
    signalIndex_start = 0;
  if (signalIndex_end >= _signalIndex)
    signalIndex_end = _signalIndex - 1;
  if (signalIndex_start > signalIndex_end && send_eol)
    return; // No valid range

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufStr("<BLAECK:");
    int crcStart = _framePos;

    _bufByte(0xD2); _bufByte(':');

    ulngCvt.val = msg_id;
    _bufBytes(ulngCvt.bval, 4); _bufByte(':');

    _bufByte(_sendRestartFlag ? 1 : 0);
    _sendRestartFlag = false;
    _bufByte(':');

    _bufByte((byte)(_schemaHash & 0xFF));
    _bufByte((byte)((_schemaHash >> 8) & 0xFF));
    _bufByte(':');

    _bufByte((byte)_timestampMode);
    if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
    {
      ullCvt.val = timestamp;
      _bufBytes(ullCvt.bval, 8);
    }
    _bufByte(':');

    for (int i = signalIndex_start; i <= signalIndex_end; i++)
    {
      if (onlyUpdated && !Signals[i].Updated)
        continue;

      intCvt.val = i;
      _bufBytes(intCvt.bval, 2);

      Signal signal = Signals[i];
      switch (signal.DataType)
      {
      case (Blaeck_bool):   boolCvt.val  = *((bool *)signal.Address);           _bufBytes(boolCvt.bval, 1);  break;
      case (Blaeck_byte):   _bufByte(*((byte *)signal.Address));                                              break;
      case (Blaeck_short):  shortCvt.val = *((short *)signal.Address);          _bufBytes(shortCvt.bval, 2); break;
      case (Blaeck_ushort): ushortCvt.val = *((unsigned short *)signal.Address); _bufBytes(ushortCvt.bval, 2); break;
      case (Blaeck_int):    intCvt.val   = *((int *)signal.Address);            _bufBytes(intCvt.bval, 2);   break;
      case (Blaeck_uint):   uintCvt.val  = *((unsigned int *)signal.Address);   _bufBytes(uintCvt.bval, 2);  break;
      case (Blaeck_long):   lngCvt.val   = *((long *)signal.Address);           _bufBytes(lngCvt.bval, 4);   break;
      case (Blaeck_ulong):  ulngCvt.val  = *((unsigned long *)signal.Address);  _bufBytes(ulngCvt.bval, 4);  break;
      case (Blaeck_float):  fltCvt.val   = *((float *)signal.Address);          _bufBytes(fltCvt.bval, 4);   break;
      case (Blaeck_double): dblCvt.val   = *((double *)signal.Address);         _bufBytes(dblCvt.bval, 8);   break;
      }

      if (onlyUpdated)
        Signals[i].Updated = false;
    }

    if (send_eol)
    {
      byte statusByte = 0;
      byte statusPayload[4] = {0, 0, 0, 0};
      _bufByte(statusByte);
      _bufBytes(statusPayload, 4);

      // CRC32 over content (crcStart..framePos-1)
      _crc.setPolynome(0x04C11DB7);
      _crc.setInitial(0xFFFFFFFF);
      _crc.setXorOut(0xFFFFFFFF);
      _crc.setReverseIn(true);
      _crc.setReverseOut(true);
      _crc.restart();
      _crc.add(_frameBuf + crcStart, _framePos - crcStart);
      uint32_t crc_value = _crc.calc();
      _bufBytes((byte *)&crc_value, 4);

      _bufFooter();
      _bufSend();
    }
  }
  else
  {
    _crc.setPolynome(0x04C11DB7);
    _crc.setInitial(0xFFFFFFFF);
    _crc.setXorOut(0xFFFFFFFF);
    _crc.setReverseIn(true);
    _crc.setReverseOut(true);
    _crc.restart();

    StreamRef->write("<BLAECK:");

    byte msg_key = 0xD2;
    StreamRef->write(msg_key);
    _crc.add(msg_key);

    StreamRef->write(":");
    _crc.add(':');

    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    _crc.add(ulngCvt.bval, 4);

    StreamRef->write(":");
    _crc.add(':');

    byte restart_flag = _sendRestartFlag ? 1 : 0;
    StreamRef->write(restart_flag);
    _crc.add(restart_flag);
    _sendRestartFlag = false;

    StreamRef->write(":");
    _crc.add(':');

    byte hash_lo = (byte)(_schemaHash & 0xFF);
    byte hash_hi = (byte)((_schemaHash >> 8) & 0xFF);
    StreamRef->write(hash_lo);
    StreamRef->write(hash_hi);
    _crc.add(hash_lo);
    _crc.add(hash_hi);

    StreamRef->write(":");
    _crc.add(':');

    byte timestamp_mode = (byte)_timestampMode;
    StreamRef->write(timestamp_mode);
    _crc.add(timestamp_mode);

    if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
    {
      ullCvt.val = timestamp;
      StreamRef->write(ullCvt.bval, 8);
      _crc.add(ullCvt.bval, 8);
    }

    StreamRef->write(":");
    _crc.add(':');

    for (int i = signalIndex_start; i <= signalIndex_end; i++)
    {
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

      if (onlyUpdated)
        Signals[i].Updated = false;
    }

    if (send_eol)
    {
      byte statusByte = 0;
      byte statusPayload[4] = {0, 0, 0, 0};
      StreamRef->write(statusByte);
      StreamRef->write(statusPayload, 4);
      _crc.add(statusByte);
      _crc.add(statusPayload, 4);

      uint32_t crc_value = _crc.calc();
      StreamRef->write((byte *)&crc_value, 4);

      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();
    }
  }
}

void BlaeckSerial::writeSlaveData(bool send_eol, bool onlyUpdated, bool *skipSlaves, byte &skippedSlaveCount, byte &firstSkippedSlaveID, byte &firstSkipReason)
{
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++)
  {
    // Cycle through slaves
    if (slaveFound(slaveindex))
    {
      if (skipSlaves[slaveindex])
        continue;

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
        bool slaveFailed = false;
        const unsigned long timeout_ms = 50;
        unsigned long start_ms = millis();
        while ((millis() - start_ms) < timeout_ms && !eolist_found)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          // try again
          if (receivedBytes < 2)
            continue;

          // slave may send less than requested
          while (Wire.available() > 0 && !eolist_found)
          {
            // first receive number of bytes to expect
            uint8_t bytecount = (uint8_t)Wire.read();

            if (bytecount > 0 && bytecount < 127)
            {
              // Need full payload: [2 bytes index] + [N-2 data bytes] + [2 bytes crc]
              if (Wire.available() < bytecount + 2)
              {
                // Incomplete chunk, skip and request next chunk to avoid partial parsing.
                slaveFailed = true;
                break;
              }

              _crcWireCalc.restart();
              _crcWireCalc.add(bytecount);

              // First 2 bytes are always the signal index from slave
              uint8_t indexLow = (uint8_t)Wire.read();
              uint8_t indexHigh = (uint8_t)Wire.read();
              uint16_t slaveSignalIndex = ((uint16_t)indexHigh << 8) | ((uint16_t)indexLow);

              _crcWireCalc.add(indexLow);
              _crcWireCalc.add(indexHigh);

              intCvt.val = _signalIndex + slaveSignalIndex;
              // Read the remaining data bytes (bytecount - 2 since we already read the index)
              int remainingDataBytes = bytecount - 2;
              if (remainingDataBytes > 8)
              {
                slaveFailed = true;
                break;
              }

              byte dataBuffer[8];

              for (int i = 0; i < remainingDataBytes; i++)
              {
                // then read the data bytes
                dataBuffer[i] = (byte)Wire.read();
                _crcWireCalc.add(dataBuffer[i]);
              }

              // After reading all data, read the CRC bytes
              uint8_t crcWireTransmittedByte0 = (uint8_t)Wire.read();
              uint8_t crcWireTransmittedByte1 = (uint8_t)Wire.read();

              uint16_t crcWireTransmitted = ((uint16_t)crcWireTransmittedByte1 << 8) | ((uint16_t)crcWireTransmittedByte0);
              uint16_t crcWireCalculated = _crcWireCalc.calc();

              if (crcWireTransmitted != crcWireCalculated)
              {
                slaveFailed = true;
                break;
              }

              // Write only after full slave chunk validation.
              if (_bufferedWrites && _frameBuf)
              {
                _bufBytes(intCvt.bval, 2);
                _bufBytes(dataBuffer, remainingDataBytes);
              }
              else
              {
                StreamRef->write(lowByte(_signalIndex + slaveSignalIndex));
                StreamRef->write(highByte(_signalIndex + slaveSignalIndex));
                _crc.add(intCvt.bval, 2);
                for (int i = 0; i < remainingDataBytes; i++)
                {
                  StreamRef->write(dataBuffer[i]);
                  _crc.add(dataBuffer[i]);
                }
              }
            }
            else
            {
              if (bytecount == 127)
              {
                eolist_found = true;
                break;
              }
            }
            if (slaveFailed)
              break;
          }
          if (eolist_found)
            break;
          if (slaveFailed)
            break;
        }

        if (!eolist_found)
          slaveFailed = true;

        if (slaveFailed && !skipSlaves[slaveindex])
        {
          skipSlaves[slaveindex] = true;
          skippedSlaveCount++;
          if (firstSkippedSlaveID == 0xFF)
          {
            firstSkippedSlaveID = (byte)slaveindex;
            firstSkipReason = 0x02; // Runtime timeout/short read/malformed/CRC mismatch
          }
        }
      }
      else if (!skipSlaves[slaveindex])
      {
        skipSlaves[slaveindex] = true;
        skippedSlaveCount++;
        if (firstSkippedSlaveID == 0xFF)
        {
          firstSkippedSlaveID = (byte)slaveindex;
          firstSkipReason = 0x01; // No response
        }
      }
    }
  }

  if (send_eol)
  {
    // D2 tail: StatusByte + StatusPayload(4) + CRC32(4)
    byte statusByte = 0;
    byte statusPayload[4] = {0, 0, 0, 0};
    if (skippedSlaveCount > 0)
    {
      // StatusByte 1: One or more slaves skipped/unavailable.
      // StatusPayload: [SkippedSlaveCount][FirstSkippedSlaveID][FirstSkipReason][Reserved]
      statusByte = 1;
      statusPayload[0] = skippedSlaveCount;
      statusPayload[1] = firstSkippedSlaveID;
      statusPayload[2] = firstSkipReason;
      statusPayload[3] = 0x00;
    }

    if (_bufferedWrites && _frameBuf)
    {
      _bufByte(statusByte);
      _bufBytes(statusPayload, 4);

      // CRC32 over content: buffer[8..framePos-1] (after "<BLAECK:")
      _crc.setPolynome(0x04C11DB7);
      _crc.setInitial(0xFFFFFFFF);
      _crc.setXorOut(0xFFFFFFFF);
      _crc.setReverseIn(true);
      _crc.setReverseOut(true);
      _crc.restart();
      _crc.add(_frameBuf + 8, _framePos - 8);
      uint32_t crc_value = _crc.calc();
      _bufBytes((byte *)&crc_value, 4);

      _bufFooter();
      _bufSend();
    }
    else
    {
      StreamRef->write(statusByte);
      StreamRef->write(statusPayload, 4);
      _crc.add(statusByte);
      _crc.add(statusPayload, 4);

      uint32_t crc_value = _crc.calc();
      StreamRef->write((byte *)&crc_value, 4);

      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();
    }
  }
}

void BlaeckSerial::writeLocalSymbols(unsigned long msg_id, bool send_eol)
{
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xB0, msg_id);

    for (int i = 0; i < _signalIndex; i++)
    {
      _bufByte(_masterSlaveConfig);
      _bufByte(_slaveID);

      Signal signal = Signals[i];

      const char *namePtr = signal.SignalName.c_str();
      while (*namePtr)
        _schemaHashFeedByte((byte)*namePtr++);

      _bufStr0(signal.SignalName);

      byte dtCode;
      switch (signal.DataType)
      {
      case (Blaeck_bool):   dtCode = 0x0; break;
      case (Blaeck_byte):   dtCode = 0x1; break;
      case (Blaeck_short):  dtCode = 0x2; break;
      case (Blaeck_ushort): dtCode = 0x3; break;
      case (Blaeck_int):    dtCode = 0x4; break;
      case (Blaeck_uint):   dtCode = 0x5; break;
      case (Blaeck_long):   dtCode = 0x6; break;
      case (Blaeck_ulong):  dtCode = 0x7; break;
      case (Blaeck_float):  dtCode = 0x8; break;
      case (Blaeck_double): dtCode = 0x9; break;
      default:              dtCode = 0x8; break;
      }
      _bufByte(dtCode);
      _schemaHashFeedByte(dtCode);
    }
    if (send_eol)
    {
      _bufFooter();
      _bufSend();
    }
  }
  else
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

      const char *namePtr = signal.SignalName.c_str();
      while (*namePtr)
        _schemaHashFeedByte((byte)*namePtr++);

      StreamRef->print(signal.SignalName);
      StreamRef->print('\0');

      byte dtCode;
      switch (signal.DataType)
      {
      case (Blaeck_bool):   dtCode = 0x0; break;
      case (Blaeck_byte):   dtCode = 0x1; break;
      case (Blaeck_short):  dtCode = 0x2; break;
      case (Blaeck_ushort): dtCode = 0x3; break;
      case (Blaeck_int):    dtCode = 0x4; break;
      case (Blaeck_uint):   dtCode = 0x5; break;
      case (Blaeck_long):   dtCode = 0x6; break;
      case (Blaeck_ulong):  dtCode = 0x7; break;
      case (Blaeck_float):  dtCode = 0x8; break;
      case (Blaeck_double): dtCode = 0x9; break;
      default:              dtCode = 0x8; break;
      }
      StreamRef->write(dtCode);
      _schemaHashFeedByte(dtCode);
    }
    if (send_eol)
    {
      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();
    }
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
        bool afterNull = false; // true = next non-control byte is datatype
        const unsigned long timeout_ms = 50;
        unsigned long start_ms = millis();
        while ((millis() - start_ms) < timeout_ms && !eolist_found)
        {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          if (receivedBytes < 2)
            continue; // try again

          bool eosignal_found = false;

          for (int symbolchar = 0; symbolchar < receivedBytes && Wire.available(); symbolchar++)
          {
            // Slave may send less than requested
            // SignalName + \0 + DataType
            //  receive a byte as character
            char c = (char)Wire.read();

            //'\0'
            if (c != char(0x00) && symbolchar == 0)
            {
              if (_bufferedWrites && _frameBuf)
              {
                _bufByte(2);          // Slave config
                _bufByte(slaveindex); // Slave ID
              }
              else
              {
                StreamRef->write(2);          // Slave config
                StreamRef->write(slaveindex); // Slave ID
              }
            }
            if (c == char(0x00) && symbolchar == 0)
            {
              continue;
            }

            //'\r'
            if (c == char(0x0D))
            {
              eosignal_found = true;
              afterNull = false;
              signalCount += 1;
            }

            //'\n'
            if (c == char(0x0A))
              eolist_found = true;

            if (eosignal_found != true && eolist_found != true)
            {
              if (_bufferedWrites && _frameBuf)
                _bufByte((byte)c);
              else
                StreamRef->print(c);

              // Feed into schema hash accumulator
              if (c == char(0x00))
              {
                afterNull = true; // next byte is datatype
              }
              else if (afterNull)
              {
                _schemaHashFeedByte((byte)c); // datatype code
                afterNull = false;
              }
              else
              {
                _schemaHashFeedByte((byte)c); // name character
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
    if (_bufferedWrites && _frameBuf)
    {
      _bufFooter();
      _bufSend();
    }
    else
    {
      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();
    }
  }
}

void BlaeckSerial::scanI2CSlaves(char addressStart, char addressEnd)
{
  // Cycle through slaves (add-only: once found, a slave stays registered
  // until reboot.  Temporary I2C failures are reported via StatusByte=1
  // in data frames, not as schema changes.)
  for (int slaveindex = addressStart; slaveindex <= addressEnd; slaveindex++)
  {
    if (slaveFound(slaveindex))
      continue; // already registered, skip

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
      byte receivedBytes = Wire.requestFrom(slaveindex, 1);
      if (receivedBytes < 1 || !Wire.available())
      {
        continue;
      }

      // Expecting response 0xAA from slave -> Slave found
      // Receive a byte as character
      char c = (char)Wire.read();

      if (c == char(0xAA))
      {
        storeSlave(slaveindex, true);
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
  this->tick(msg_id, true);
}

void BlaeckSerial::tick()
{
  this->tick(185273099);
}

void BlaeckSerial::tick(unsigned long msg_id)
{
  this->tick(msg_id, false);
}

void BlaeckSerial::tick(unsigned long msg_id, bool onlyUpdated)
{
  this->read();
  this->writeRestarted(msg_id);
  this->timedWriteData(msg_id, 0, _signalIndex - 1, onlyUpdated, getTimeStamp());
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
  if (Wire.available())
  {
    _wireMode = (byte)Wire.read();
  }
  else
  {
    _wireMode = 0;
  }
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

  // Reset overflow tracking
  _prevMicros = 0;
  _overflowCount = 0;

  // Set default callbacks for built-in modes
  switch (mode)
  {
  case BLAECK_MICROS:
    _timestampCallback = _microsWrapper;
    break;
  case BLAECK_UNIX:
    // User must provide Unix time callback - don't override if already set
    if (_timestampCallback == _microsWrapper)
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

void BlaeckSerial::setTimestampCallback(unsigned long long (*callback)())
{
  _timestampCallback = callback;
}

bool BlaeckSerial::hasValidTimestampCallback() const
{
  return (_timestampMode != BLAECK_NO_TIMESTAMP && _timestampCallback != nullptr);
}

unsigned long long BlaeckSerial::getTimeStamp()
{
  unsigned long long timestamp = 0;

  if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
  {
    if (_timestampMode == BLAECK_MICROS)
    {
      // Track micros() overflow: uint32 wraps every ~71 minutes
      unsigned long raw = (unsigned long)_timestampCallback();
      if (raw < _prevMicros)
      {
        _overflowCount++;
      }
      _prevMicros = raw;
      timestamp = (_overflowCount * 4294967296ULL) + raw;
    }
    else if (_timestampMode == BLAECK_UNIX)
    {
      // Callback returns microseconds since Unix epoch directly
      timestamp = _timestampCallback();
    }
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
