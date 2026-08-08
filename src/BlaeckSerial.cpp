/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include <Arduino.h>
#include "BlaeckSerial.h"

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
  begin(Ref, size, nullptr);
}
void BlaeckSerial::begin(Stream *Ref, unsigned int size, Stream *DebugRef)
{
  StreamRef = (Stream *)Ref;
  _debugStream = DebugRef;
  _signalCapacity = size;
  if (Signals != nullptr)
  {
    delete[] Signals;
    Signals = nullptr;
  }
  Signals = new (std::nothrow) Signal[size];
  _signalIndex = 0;
  SignalCount = 0;
  _schemaHash = 0;
  _signalOverflowOccurred = false;
  _signalOverflowCount = 0;

  if (_bufferedWrites)
    _bufAllocate();
}

void BlaeckSerial::addSignal(String signalName, bool *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, byte *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, short *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, unsigned short *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, int *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
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

void BlaeckSerial::addSignal(String signalName, unsigned int *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
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

void BlaeckSerial::addSignal(String signalName, long *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, unsigned long *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, float *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::addSignal(String signalName, double *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
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

void BlaeckSerial::addSignal(String signalName, char *value)
{
  if (static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    return;
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = Blaeck_string;
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

void BlaeckSerial::setSignalName(int signalIndex, String signalName)
{
  if (signalIndex < 0 || signalIndex >= (int)_signalCapacity)
    return;

  Signals[signalIndex].SignalName = "";
  size_t needed = signalName.length() + 1;
  Signals[signalIndex].SignalName.reserve(needed);

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
  this->writeRestarted();

  if (recvWithStartEndMarkers() == true)
  {
    parseData();
    if (_debugStream != nullptr)
    {
      _debugStream->print("<");
      _debugStream->print(receivedChars);
      _debugStream->println(">");
    }

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
#if BLAECK_ENABLE_COMMAND_META
    else if (strcmp(COMMAND, "BLAECK.WRITE_COMMANDS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeCommands(msg_id);
    }
#endif
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
      if (!_commandCallbackDeprecationWarned && _debugStream != nullptr)
      {
        _debugStream->println("WARNING: setCommandCallback(...) is deprecated; use onCommand(...) / onAnyCommand(...)");
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
  if (_commandCallback != NULL && !_commandCallbackDeprecationWarned && _debugStream != nullptr)
  {
    _debugStream->println("WARNING: setCommandCallback(...) is deprecated; use onCommand(...) / onAnyCommand(...)");
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
    if (_debugStream != nullptr)
    {
      _debugStream->print("Command name too long for handler table: ");
      _debugStream->println(command);
    }
    return false;
  }

  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    if (_commandHandlers[i].inUse && strcmp(_commandHandlers[i].command, command) == 0)
    {
      _commandHandlers[i].handler = handler;
#if BLAECK_ENABLE_COMMAND_META
      _commandHandlers[i].kind = BLAECK_CMD_PLAIN;
      _commandHandlers[i].unit = nullptr;
      _commandHandlers[i].options = nullptr;
      _commandHandlers[i].stateSignal = nullptr;
#endif
      return true;
    }
  }

  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    if (!_commandHandlers[i].inUse)
    {
      strncpy(_commandHandlers[i].command, command, MAX_COMMAND_NAME_COUNT - 1);
      _commandHandlers[i].command[MAX_COMMAND_NAME_COUNT - 1] = '\0';
      _commandHandlers[i].handler = handler;
      _commandHandlers[i].inUse = true;
#if BLAECK_ENABLE_COMMAND_META
      _commandHandlers[i].kind = BLAECK_CMD_PLAIN;
      _commandHandlers[i].unit = nullptr;
      _commandHandlers[i].options = nullptr;
      _commandHandlers[i].stateSignal = nullptr;
#endif
      return true;
    }
  }

  if (_debugStream != nullptr)
  {
    _debugStream->print("Command handler table full for: ");
    _debugStream->println(command);
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
#if BLAECK_ENABLE_COMMAND_META
    _commandHandlers[i].kind = BLAECK_CMD_PLAIN;
    _commandHandlers[i].unit = nullptr;
    _commandHandlers[i].options = nullptr;
    _commandHandlers[i].stateSignal = nullptr;
#endif
  }
  _anyCommandHandler = nullptr;
}

bool BlaeckSerial::onNumberCommand(const char *command, BlaeckCommandHandler handler,
                                   const __FlashStringHelper *stateSignal,
                                   float min, float max, float step,
                                   const __FlashStringHelper *unit)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    _annotateCommand(command, BLAECK_CMD_NUMBER, stateSignal, min, max, step, unit, nullptr);
#else
  (void)stateSignal; (void)min; (void)max; (void)step; (void)unit;
#endif
  return ok;
}

bool BlaeckSerial::onSwitchCommand(const char *command, BlaeckCommandHandler handler,
                                   const __FlashStringHelper *stateSignal)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    _annotateCommand(command, BLAECK_CMD_SWITCH, stateSignal, 0.0f, 0.0f, 0.0f, nullptr, nullptr);
#else
  (void)stateSignal;
#endif
  return ok;
}

bool BlaeckSerial::onSelectCommand(const char *command, BlaeckCommandHandler handler,
                                   const __FlashStringHelper *stateSignal,
                                   const __FlashStringHelper *optionsCsv)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    _annotateCommand(command, BLAECK_CMD_SELECT, stateSignal, 0.0f, 0.0f, 0.0f, nullptr, optionsCsv);
#else
  (void)stateSignal; (void)optionsCsv;
#endif
  return ok;
}

bool BlaeckSerial::onButtonCommand(const char *command, BlaeckCommandHandler handler)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    _annotateCommand(command, BLAECK_CMD_BUTTON, nullptr, 0.0f, 0.0f, 0.0f, nullptr, nullptr);
#endif
  return ok;
}

bool BlaeckSerial::onTextCommand(const char *command, BlaeckCommandHandler handler,
                                 const __FlashStringHelper *stateSignal,
                                 unsigned int maxLength)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    // maxLength is stored in meta_max (reused as the text length limit).
    _annotateCommand(command, BLAECK_CMD_TEXT, stateSignal, 0.0f, (float)maxLength, 0.0f, nullptr, nullptr);
#else
  (void)stateSignal;
  (void)maxLength;
#endif
  return ok;
}

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::_annotateCommand(const char *command, uint8_t kind,
                                    const __FlashStringHelper *stateSignal,
                                    float mn, float mx, float st,
                                    const __FlashStringHelper *unit,
                                    const __FlashStringHelper *options)
{
  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    if (_commandHandlers[i].inUse && strcmp(_commandHandlers[i].command, command) == 0)
    {
      _commandHandlers[i].kind = kind;
      _commandHandlers[i].meta_min = mn;
      _commandHandlers[i].meta_max = mx;
      _commandHandlers[i].meta_step = st;
      _commandHandlers[i].unit = unit;
      _commandHandlers[i].options = options;
      _commandHandlers[i].stateSignal = stateSignal;
      return;
    }
  }
}

byte BlaeckSerial::_flashCsvOptionCount(const __FlashStringHelper *csv)
{
  if (csv == nullptr)
    return 0;
  PGM_P p = reinterpret_cast<PGM_P>(csv);
  byte count = 1;
  bool any = false;
  byte c;
  while ((c = pgm_read_byte(p++)) != 0)
  {
    any = true;
    if (c == ',')
      count++;
  }
  return any ? count : 0;
}

long BlaeckSerial::_flashCsvIndexOf(const __FlashStringHelper *csv, const char *value)
{
  if (csv == nullptr || value == nullptr || value[0] == '\0')
    return -1;

  PGM_P p = reinterpret_cast<PGM_P>(csv);
  long index = 0;
  const char *v = value;
  bool matching = true; // current token still matches value so far

  byte c;
  while (true)
  {
    c = pgm_read_byte(p++);
    if (c == ',' || c == '\0')
    {
      // End of a token: match if value was fully consumed too.
      if (matching && *v == '\0')
        return index;
      if (c == '\0')
        return -1;
      // Advance to next token
      index++;
      v = value;
      matching = true;
    }
    else
    {
      if (matching)
      {
        char a = (char)c;
        char b = *v;
        // Case-insensitive compare
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (b == '\0' || a != b)
          matching = false;
        else
          v++;
      }
    }
  }
}
#endif

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

  // Manual comma-scanner that preserves empty fields between consecutive commas.
  char *p = tempChars;
  char *tokenStart;
  bool hasComma;

  STRING_01[0] = '\0';
  for (int i = 0; i < 10; i++)
    PARAMETER[i] = 0;

  // --- COMMAND (first token) ---
  tokenStart = p;
  while (*p != ',' && *p != '\0')
    p++;
  hasComma = (*p == ',');
  if (hasComma)
  {
    *p = '\0';
    p++;
  }
  if (tokenStart[0] != '\0')
  {
    strncpy(COMMAND, tokenStart, sizeof(COMMAND) - 1);
    COMMAND[sizeof(COMMAND) - 1] = '\0';
  }
  else
  {
    COMMAND[0] = '\0';
  }

  if (!hasComma)
    return;

  // --- STRING_01 / PARAMETER[0] ---
  tokenStart = p;
  while (*p != ',' && *p != '\0')
    p++;
  if (*p == ',')
  {
    *p = '\0';
    p++;
  }
  while (*tokenStart == ' ')
    tokenStart++;
  strncpy(STRING_01, tokenStart, 15);
  STRING_01[15] = '\0';
  PARAMETER[0] = atoi(tokenStart);

  // --- PARAMETER[1] through PARAMETER[9] ---
  for (int i = 1; i <= 9; i++)
  {
    if (*p == '\0')
      break;
    tokenStart = p;
    while (*p != ',' && *p != '\0')
      p++;
    if (*p == ',')
    {
      *p = '\0';
      p++;
    }
    while (*tokenStart == ' ')
      tokenStart++;
    PARAMETER[i] = atoi(tokenStart);
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

  // Manual comma-scanner that preserves empty fields between consecutive commas.
  char *p = _parsedTokenBuffer;

  // Extract command (first token before the first comma)
  char *tokenStart = p;
  while (*p != ',' && *p != '\0')
    p++;
  bool hasComma = (*p == ',');
  if (hasComma)
  {
    *p = '\0';
    p++;
  }
  while (*tokenStart == ' ')
    tokenStart++;
  if (tokenStart[0] == '\0')
  {
    return;
  }
  strncpy(_parsedCommand, tokenStart, MAX_COMMAND_NAME_COUNT - 1);
  _parsedCommand[MAX_COMMAND_NAME_COUNT - 1] = '\0';

  if (!hasComma)
    return;

  // Extract parameters — empty fields (,,) produce a pointer to '\0'
  bool moreParams = true;
  while (moreParams && _parsedParamCount < MAX_COMMAND_PARAM_COUNT)
  {
    tokenStart = p;
    while (*p != ',' && *p != '\0')
      p++;
    if (*p == ',')
    {
      *p = '\0';
      p++;
    }
    else
    {
      moreParams = false;
    }
    while (*tokenStart == ' ')
      tokenStart++;
    _parsedParamPtrs[_parsedParamCount] = tokenStart;
    _parsedParamCount++;
  }
}

void BlaeckSerial::_dispatchRegisteredHandlers(bool sendAck)
{
  _parseCommandTokens(receivedChars);
  if (_parsedCommand[0] == '\0')
  {
    return;
  }

  byte ackStatus = 1;                  // 0 = accepted, 1 = rejected
  byte ackReason = BLAECK_ACK_UNKNOWN; // reason reported when rejected
  bool matched = false;

  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    if (_commandHandlers[i].inUse &&
        _commandHandlers[i].handler != nullptr &&
        strcmp(_commandHandlers[i].command, _parsedCommand) == 0)
    {
      matched = true;
#if BLAECK_ENABLE_COMMAND_META
      ackReason = _validateTypedCommand(i);
#else
      ackReason = BLAECK_ACK_OK;
#endif
      if (ackReason == BLAECK_ACK_OK)
      {
        ackStatus = 0;
        _commandHandlers[i].handler(
            _parsedCommand,
            (const char *const *)_parsedParamPtrs,
            _parsedParamCount);
      }
      break;
    }
  }

  if (_anyCommandHandler != nullptr)
  {
    _anyCommandHandler(
        _parsedCommand,
        (const char *const *)_parsedParamPtrs,
        _parsedParamCount);

    if (!matched)
    {
      // Delivered to the catch-all handler: acknowledge as accepted.
      matched = true;
      ackStatus = 0;
      ackReason = BLAECK_ACK_OK;
    }
  }

  // Acknowledge every non-internal command back to the sender. BLAECK.* frames
  // are handled in read() and must not be acked here.
  if (sendAck && strncmp(_parsedCommand, "BLAECK.", 7) != 0)
  {
    _writeCommandAck(receivedChars, ackStatus, ackReason);
  }
}

uint32_t BlaeckSerial::_fnv1a32(const char *s)
{
  uint32_t h = 0x811C9DC5UL; // FNV offset basis
  if (s != nullptr)
  {
    while (*s != '\0')
    {
      h ^= (uint8_t)(*s++);
      h *= 0x01000193UL; // FNV prime
    }
  }
  return h;
}

void BlaeckSerial::_writeCommandAck(const char *rawCommand, byte status, byte reasonCode)
{
  if (StreamRef == nullptr)
    return;

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xF0, _commandAckMsgId++);
    // Payload: command hash (4 bytes, little-endian) + status (1) + reason (1).
    ulngCvt.val = _fnv1a32(rawCommand);
    _bufBytes(ulngCvt.bval, 4);
    _bufByte(status);
    _bufByte(reasonCode);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xF0;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = _commandAckMsgId++;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    // Payload: command hash (4 bytes, little-endian) + status (1) + reason (1).
    ulngCvt.val = _fnv1a32(rawCommand);
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(status);
    StreamRef->write(reasonCode);

    // No CRC32 tail: acks mirror the descriptive 0xE0 frame format.
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeMessage(const char *channelName, const char *text)
{
  this->writeMessage(channelName, text, _messageMsgId++);
}

void BlaeckSerial::writeMessage(const char *channelName, const char *text, unsigned long messageID)
{
  // 0x90 "Message" frame: a named free-text status/log channel, device -> host.
  //   name\0  length(2, LE uint16)  text[length]
  // No CRC (like the 0xE0/0xF0 frames). The host may surface it (e.g. an
  // auto-created Home Assistant text sensor per channel); it is never treated as
  // signal/telemetry data and is not stored.
  if (StreamRef == nullptr)
    return;

  if (channelName == nullptr)
    channelName = "";
  if (text == nullptr)
    text = "";

  // 2-byte length prefix caps a single message at 65535 bytes. Widen before
  // comparing: size_t is 16 bit on AVR, where `len > 65535` can never be true
  // and warns under -Wtype-limits.
  uint32_t rawLen = (uint32_t)strlen(text);
  uint16_t len = (rawLen > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)rawLen;

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0x90, messageID);
    // Channel name (NUL-terminated), then the UTF-8 text length-prefixed (LE uint16).
    _bufStr0(channelName);
    _bufByte((byte)(len & 0xFF));
    _bufByte((byte)((len >> 8) & 0xFF));
    _bufBytes((const byte *)text, len);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0x90;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = messageID;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    // Channel name (NUL-terminated), then the UTF-8 text length-prefixed (LE uint16).
    StreamRef->print(channelName);
    StreamRef->write((byte)0);
    StreamRef->write((byte)(len & 0xFF));
    StreamRef->write((byte)((len >> 8) & 0xFF));
    StreamRef->write((const uint8_t *)text, len);

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

#if BLAECK_ENABLE_COMMAND_META
byte BlaeckSerial::_validateTypedCommand(byte handlerIndex)
{
  const CommandHandlerEntry &e = _commandHandlers[handlerIndex];

  // Plain and button commands carry no value to validate.
  if (e.kind == BLAECK_CMD_PLAIN || e.kind == BLAECK_CMD_BUTTON)
    return BLAECK_ACK_OK;

  // No value supplied -> let the handler decide (e.g. query/toggle usage).
  if (_parsedParamCount < 1 || _parsedParamPtrs[0] == nullptr || _parsedParamPtrs[0][0] == '\0')
    return BLAECK_ACK_OK;

  const char *v = _parsedParamPtrs[0];

  if (e.kind == BLAECK_CMD_NUMBER)
  {
    float f = (float)atof(v);
    if (f < e.meta_min || f > e.meta_max)
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Command rejected (out of range): "));
        _debugStream->print(e.command);
        _debugStream->print('=');
        _debugStream->print(v);
        _debugStream->print(F(" allowed ["));
        _debugStream->print(e.meta_min);
        _debugStream->print(F(", "));
        _debugStream->print(e.meta_max);
        _debugStream->println(F("]"));
      }
      return BLAECK_ACK_OUT_OF_RANGE;
    }
  }
  else if (e.kind == BLAECK_CMD_SWITCH)
  {
    if (!(strcmp(v, "0") == 0 || strcmp(v, "1") == 0))
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Command rejected (switch expects 0/1): "));
        _debugStream->print(e.command);
        _debugStream->print('=');
        _debugStream->println(v);
      }
      return BLAECK_ACK_BAD_SWITCH;
    }
  }
  else if (e.kind == BLAECK_CMD_SELECT)
  {
    byte count = _flashCsvOptionCount(e.options);

    // Accept either an option name (case-insensitive) or a numeric index.
    long idx = _flashCsvIndexOf(e.options, v);
    if (idx < 0)
    {
      char *endp = nullptr;
      long n = strtol(v, &endp, 10);
      if (endp != v && *endp == '\0')
        idx = n;
    }

    if (idx < 0 || idx >= (long)count)
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Command rejected (bad select value): "));
        _debugStream->print(e.command);
        _debugStream->print('=');
        _debugStream->print(v);
        _debugStream->print(F(" allowed [0, "));
        _debugStream->print((int)count - 1);
        _debugStream->println(F("] or an option name"));
      }
      return BLAECK_ACK_BAD_SELECT;
    }

    // Normalize to the index string so index-based handlers work whether the
    // caller sent a name (e.g. HA select) or a raw index.
    snprintf(_selectIndexScratch, sizeof(_selectIndexScratch), "%ld", idx);
    _parsedParamPtrs[0] = _selectIndexScratch;
  }
  else if (e.kind == BLAECK_CMD_TEXT)
  {
    // Percent-decode in place (SELECT-style param normalization) so the handler
    // receives raw UTF-8. The 0xF0 ack still hashes the encoded receivedChars,
    // so it keeps matching the host's hash of what it sent.
    char *decoded = (char *)_parsedParamPtrs[0];
    _percentDecodeInPlace(decoded);

    unsigned int maxLen = (unsigned int)e.meta_max;
    if (maxLen > 0 && strlen(decoded) > maxLen)
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Command rejected (text too long): "));
        _debugStream->print(e.command);
        _debugStream->print(F(" len="));
        _debugStream->print((unsigned int)strlen(decoded));
        _debugStream->print(F(" max="));
        _debugStream->println(maxLen);
      }
      return BLAECK_ACK_TOO_LONG;
    }
  }

  return BLAECK_ACK_OK;
}

void BlaeckSerial::_percentDecodeInPlace(char *s)
{
  if (s == nullptr)
    return;

  const char *src = s;
  char *dst = s;
  while (*src != '\0')
  {
    if (*src == '%' && src[1] != '\0' && src[2] != '\0')
    {
      char hi = src[1];
      char lo = src[2];
      int hiVal = (hi >= '0' && hi <= '9') ? hi - '0'
                  : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                  : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                                             : -1;
      int loVal = (lo >= '0' && lo <= '9') ? lo - '0'
                  : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                  : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                                             : -1;
      if (hiVal >= 0 && loVal >= 0)
      {
        *dst++ = (char)((hiVal << 4) | loVal);
        src += 3;
        continue;
      }
    }
    *dst++ = *src++;
  }
  *dst = '\0';
}
#endif

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
  else if (_debugStream != nullptr)
  {
    _debugStream->print("Invalid interval mode: ");
    _debugStream->println(interval_ms);
  }
}

void BlaeckSerial::writeSymbols()
{
  this->writeSymbols(1);
}
void BlaeckSerial::writeSymbols(unsigned long msg_id)
{
  this->writeSymbolsFrame(msg_id);
}

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::writeCommands()
{
  this->writeCommands(1);
}
void BlaeckSerial::writeCommands(unsigned long msg_id)
{
  this->writeCommandsFrame(msg_id);
}
#endif

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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
    }
#else
    // On 32-bit platforms, int is mapped to Blaeck_long (4 bytes)
    if (Signals[signalIndex].DataType == Blaeck_long)
    {
      *((int *)Signals[signalIndex].Address) = value;
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
    }
#else
    // On 32-bit platforms, unsigned int is mapped to Blaeck_ulong (4 bytes)
    if (Signals[signalIndex].DataType == Blaeck_ulong)
    {
      *((unsigned int *)Signals[signalIndex].Address) = value;
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
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
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
    }
#else
    if (Signals[signalIndex].DataType == Blaeck_double)
    {
      *((double *)Signals[signalIndex].Address) = value;
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
    }
#endif
  }
}

void BlaeckSerial::write(String signalName, char *value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, char *value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, char *value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(int signalIndex, char *value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, char *value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(int signalIndex, char *value, unsigned long messageID, unsigned long long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_string)
    {
      // String values live in a user-owned buffer; repoint Address like addSignal(char*).
      Signals[signalIndex].Address = value;
      this->writeDataFrame(messageID, signalIndex, signalIndex, false, timestamp);
    }
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
  if (_signalIndex == 0)
    return;

  if (_beforeWriteCallback != NULL)
    _beforeWriteCallback();
  this->writeDataFrame(msg_id, signalIndex_start, signalIndex_end, onlyUpdated, timestamp);
}

void BlaeckSerial::timedWriteAllData()
{
  unsigned long id = (_fixedInterval_ms >= 0) ? 185273100 : 185273099;
  this->timedWriteAllData(id);
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
  unsigned long id = (_fixedInterval_ms >= 0) ? 185273100 : 185273099;
  this->timedWriteUpdatedData(id);
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
      if (_timedInterval_ms > 0)
      {
        while (_timedSetPoint_ms <= _timedElapsedTime_ms)
          _timedSetPoint_ms += _timedInterval_ms;
      }
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

void BlaeckSerial::_bufDevice(const String &name,
                              const String &hw, const String &fw)
{
  // Leading 2 bytes preserved for wire-format compatibility (always 0).
  _bufByte((byte)0);
  _bufByte((byte)0);
  _bufStr0(name);
  _bufStr0(hw);
  _bufStr0(fw);
  _bufStr0(BLAECKSERIAL_VERSION);
  _bufStr0(BLAECKSERIAL_NAME);
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
      _bufDevice(DeviceName, DeviceHWVersion, DeviceFWVersion);
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

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(DeviceName);
      StreamRef->print('\0');
      StreamRef->print(DeviceHWVersion);
      StreamRef->print('\0');
      StreamRef->print(DeviceFWVersion);
      StreamRef->print('\0');
      StreamRef->print(BLAECKSERIAL_VERSION);
      StreamRef->print('\0');
      StreamRef->print(BLAECKSERIAL_NAME);
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
  this->writeDevicesFrame(msg_id);
}

void BlaeckSerial::writeDevicesFrame(unsigned long msg_id)
{
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xB3, msg_id);
    _bufDevice(DeviceName, DeviceHWVersion, DeviceFWVersion);
      _bufFooter();
      _bufSend();

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
    StreamRef->write((byte)0);
    StreamRef->write((byte)0);
    StreamRef->print(DeviceName);
    StreamRef->print('\0');
    StreamRef->print(DeviceHWVersion);
    StreamRef->print('\0');
    StreamRef->print(DeviceFWVersion);
    StreamRef->print('\0');
    StreamRef->print(BLAECKSERIAL_VERSION);
    StreamRef->print('\0');
    StreamRef->print(BLAECKSERIAL_NAME);
    StreamRef->print('\0');

      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();

  }
}

void BlaeckSerial::writeDataFrame(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
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

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufStr("<BLAECK:");
    int crcStart = _framePos;

    _bufByte(0xD2); _bufByte(':');

    ulngCvt.val = msg_id;
    _bufBytes(ulngCvt.bval, 4); _bufByte(':');

    bool restartFlagSnapshot = _sendRestartFlag;
    _bufByte(restartFlagSnapshot ? 1 : 0);
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
      case (Blaeck_string):
      {
        const char *str = (const char *)signal.Address;
        size_t rawLen = (str != nullptr) ? strlen(str) : 0;
        byte len = (rawLen > 255) ? 255 : (byte)rawLen;
        _bufByte(len);
        if (len > 0)
          _bufBytes((byte *)str, len);
      }
      break;
      }

      if (onlyUpdated)
        Signals[i].Updated = false;
    }

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
    if (!_bufOverflow)
      _sendRestartFlag = false;
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
      case (Blaeck_string):
      {
        const char *str = (const char *)signal.Address;
        size_t rawLen = (str != nullptr) ? strlen(str) : 0;
        byte len = (rawLen > 255) ? 255 : (byte)rawLen;
        StreamRef->write(len);
        _crc.add(len);
        if (len > 0)
        {
          StreamRef->write((const uint8_t *)str, len);
          _crc.add((uint8_t *)str, len);
        }
      }
      break;
      }

      if (onlyUpdated)
        Signals[i].Updated = false;
    }

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

void BlaeckSerial::writeSymbolsFrame(unsigned long msg_id)
{
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xB0, msg_id);

    for (int i = 0; i < _signalIndex; i++)
    {
      _bufByte((byte)0);
      _bufByte((byte)0);

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
      case (Blaeck_string): dtCode = 0xA; break;
      default:              dtCode = 0x8; break;
      }
      _bufByte(dtCode);
      _schemaHashFeedByte(dtCode);
    }
      _bufFooter();
      _bufSend();

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
      StreamRef->write((byte)0);
      StreamRef->write((byte)0);

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
      case (Blaeck_string): dtCode = 0xA; break;
      default:              dtCode = 0x8; break;
      }
      StreamRef->write(dtCode);
      _schemaHashFeedByte(dtCode);
    }
      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();

  }
}

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::writeCommandsFrame(unsigned long msg_id)
{
  // 0xE0 "Command List" frame. Per discovered command entry:
  //   reserved(1) reserved(1) name\0 kind(1) flags(1)
  //   [min(4) max(4) step(4)]  if flags.hasRange   (LE float)
  //   [unit\0]                 if flags.hasUnit
  //   [optionsCsv\0]           if flags.hasOptions
  //   [stateSignal\0]          if flags.hasStateSignal
  //   [maxLen(2)]              if flags.isText     (LE uint16)
  // flags bits: 0=hasRange 1=hasUnit 2=hasOptions 3=hasStateSignal 4=isText
  // All in-use entries are emitted, including plain onCommand() entries
  // (kind=BLAECK_CMD_PLAIN, flags=0, no trailing metadata). Plain entries carry
  // no Home Assistant entity, but are listed so a host can build a full command
  // palette / autocomplete of every command the device accepts.
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xE0, msg_id);

    for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
    {
      CommandHandlerEntry &e = _commandHandlers[i];
      if (!e.inUse)
        continue;

      byte flags = 0;
      if (e.kind == BLAECK_CMD_NUMBER)
        flags |= 0x01;
      if (e.unit != nullptr)
        flags |= 0x02;
      if (e.kind == BLAECK_CMD_SELECT && e.options != nullptr)
        flags |= 0x04;
      if (e.stateSignal != nullptr)
        flags |= 0x08;
      if (e.kind == BLAECK_CMD_TEXT)
        flags |= 0x10;

      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufStr0(e.command);
      _bufByte(e.kind);
      _bufByte(flags);

      if (flags & 0x01)
      {
        fltCvt.val = e.meta_min;
        _bufBytes(fltCvt.bval, 4);
        fltCvt.val = e.meta_max;
        _bufBytes(fltCvt.bval, 4);
        fltCvt.val = e.meta_step;
        _bufBytes(fltCvt.bval, 4);
      }
      if (flags & 0x02)
        _bufFlashStr0(e.unit);
      if (flags & 0x04)
        _bufFlashStr0(e.options);
      if (flags & 0x08)
        _bufFlashStr0(e.stateSignal);
      if (flags & 0x10)
      {
        uint16_t maxLen = (uint16_t)e.meta_max;
        _bufByte((byte)(maxLen & 0xFF));
        _bufByte((byte)((maxLen >> 8) & 0xFF));
      }
    }

      _bufFooter();
      _bufSend();

  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xE0;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
    {
      CommandHandlerEntry &e = _commandHandlers[i];
      if (!e.inUse)
        continue;

      byte flags = 0;
      if (e.kind == BLAECK_CMD_NUMBER)
        flags |= 0x01;
      if (e.unit != nullptr)
        flags |= 0x02;
      if (e.kind == BLAECK_CMD_SELECT && e.options != nullptr)
        flags |= 0x04;
      if (e.stateSignal != nullptr)
        flags |= 0x08;
      if (e.kind == BLAECK_CMD_TEXT)
        flags |= 0x10;

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(e.command);
      StreamRef->write((byte)0);
      StreamRef->write(e.kind);
      StreamRef->write(flags);

      if (flags & 0x01)
      {
        fltCvt.val = e.meta_min;
        StreamRef->write(fltCvt.bval, 4);
        fltCvt.val = e.meta_max;
        StreamRef->write(fltCvt.bval, 4);
        fltCvt.val = e.meta_step;
        StreamRef->write(fltCvt.bval, 4);
      }
      if (flags & 0x02)
      {
        StreamRef->print(e.unit);
        StreamRef->write((byte)0);
      }
      if (flags & 0x04)
      {
        StreamRef->print(e.options);
        StreamRef->write((byte)0);
      }
      if (flags & 0x08)
      {
        StreamRef->print(e.stateSignal);
        StreamRef->write((byte)0);
      }
      if (flags & 0x10)
      {
        uint16_t maxLen = (uint16_t)e.meta_max;
        StreamRef->write((byte)(maxLen & 0xFF));
        StreamRef->write((byte)((maxLen >> 8) & 0xFF));
      }
    }

      StreamRef->write("/BLAECK>");
      StreamRef->write("\r\n");
      StreamRef->flush();

  }
}
#endif

void BlaeckSerial::tickUpdated()
{
  unsigned long id = (_fixedInterval_ms >= 0) ? 185273100 : 185273099;
  this->tickUpdated(id);
}

void BlaeckSerial::tickUpdated(unsigned long msg_id)
{
  this->tick(msg_id, true);
}

void BlaeckSerial::tick()
{
  unsigned long id = (_fixedInterval_ms >= 0) ? 185273100 : 185273099;
  this->tick(id);
}

void BlaeckSerial::tick(unsigned long msg_id)
{
  this->tick(msg_id, false);
}

void BlaeckSerial::tick(unsigned long msg_id, bool onlyUpdated)
{
  this->read();
  this->timedWriteData(msg_id, 0, _signalIndex - 1, onlyUpdated, getTimeStamp());
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

// Compiled here, so it reports the settings BlaeckSerial.cpp itself saw.
unsigned long BlaeckSerial::configFingerprint() const
{
  return BLAECK_CONFIG_FINGERPRINT;
}

// sketchFingerprint defaults to BLAECK_CONFIG_FINGERPRINT at the CALL SITE,
// so a caller in another translation unit passes that unit's own value.
bool BlaeckSerial::configMatchesLibrary(unsigned long sketchFingerprint) const
{
  return sketchFingerprint == BLAECK_CONFIG_FINGERPRINT;
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
