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

  // Requesting more signals than the board has RAM for leaves Signals null.
  // Reported through the same flag addSignal uses, so hasSignalOverflow()
  // catches it even before the first addSignal call.
  if (Signals == nullptr)
    _signalOverflowOccurred = true;

  if (_bufferedWrites)
    _bufAllocate();
}

BlaeckSignalRef BlaeckSerial::_registerSignal(const String &signalName, dataType type, void *address)
{
  if (Signals == nullptr || static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    _signalOverflowOccurred = true;
    _signalOverflowCount++;
    // Dead handle: the chain that follows compiles and runs and stores nothing.
    return BlaeckSignalRef(this, -1);
  }
  setSignalName(_signalIndex, signalName);
  Signals[_signalIndex].DataType = type;
  Signals[_signalIndex].Address = address;
#if BLAECK_ENABLE_SIGNAL_META
  // deleteSignals() only rewinds the index, so a slot can be written twice.
  // Cleared on registration rather than on deletion, which covers both.
  Signals[_signalIndex].Unit = nullptr;
  Signals[_signalIndex].DeviceClass = nullptr;
  Signals[_signalIndex].Icon = nullptr;
  Signals[_signalIndex].MetaFlags = 0;
  Signals[_signalIndex].DisplayPrecision = 0;
#endif
  int16_t added = (int16_t)_signalIndex;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
  return BlaeckSignalRef(this, added);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, bool *value)
{
  return _registerSignal(signalName, Blaeck_bool, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, byte *value)
{
  return _registerSignal(signalName, Blaeck_byte, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, short *value)
{
  return _registerSignal(signalName, Blaeck_short, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, unsigned short *value)
{
  return _registerSignal(signalName, Blaeck_ushort, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, int *value)
{
  return _registerSignal(signalName, Blaeck_int, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, unsigned int *value)
{
  return _registerSignal(signalName, Blaeck_uint, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, long *value)
{
  return _registerSignal(signalName, Blaeck_long, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, unsigned long *value)
{
  return _registerSignal(signalName, Blaeck_ulong, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, float *value)
{
  return _registerSignal(signalName, Blaeck_float, value);
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  return _registerSignal(signalName, Blaeck_float, value);
#else
  return _registerSignal(signalName, Blaeck_double, value);
#endif
}

BlaeckSignalRef BlaeckSerial::addSignal(String signalName, const char *value)
{
  // Address is void* for every datatype; a string address is only ever read from.
  return _registerSignal(signalName, Blaeck_string, const_cast<char *>(value));
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
  if (Signals == nullptr || signalIndex < 0 || signalIndex >= (int)_signalCapacity)
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
    else if (strcmp(COMMAND, "BLAECK.WRITE_SIGNAL_CONFIG") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeSignalConfig(msg_id);
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
    else if (strcmp(COMMAND, "BLAECK.WRITE_COMMANDS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeCommands(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_MESSAGE_CHANNELS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeMessageChannels(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_EVENT_CHANNELS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeEventChannels(msg_id);
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

    _dispatchRegisteredHandlers();
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
      _commandHandlers[i].stateSource = BLAECK_STATE_SIGNAL;
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
      _commandHandlers[i].stateSource = BLAECK_STATE_SIGNAL;
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
    _commandHandlers[i].stateSource = BLAECK_STATE_SIGNAL;
#endif
  }
  _anyCommandHandler = nullptr;
}

void BlaeckSerial::writeCommandState(const char *command)
{
  this->writeCommandState(command, 1);
}

void BlaeckSerial::writeCommandState(const char *command, unsigned long messageID)
{
#if !BLAECK_ENABLE_COMMAND_META || !BLAECK_ENABLE_MESSAGES
  (void)command;
  (void)messageID;
#else
  if (command == nullptr)
    return;

  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.stateSource != BLAECK_STATE_MESSAGE || e.stateSignal == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // The channel was declared from this same name at registration, so it exists unless the
    // table was full - in which case there is nothing to publish to and the warning was
    // already given there.
    for (byte c = 0; c < MAX_MESSAGE_CHANNELS; c++)
    {
      if (!_messageChannels[c].inUse || !_messageChannels[c].ownedByCommand)
        continue;
      if (!_flashStringEqualsName(e.stateSignal, _messageChannels[c].name))
        continue;

      BlaeckStateTextGetter getter = _messageChannels[c].getStateText;
      _writeMessageFrame(c, getter != nullptr ? getter() : nullptr, messageID);
      return;
    }
    return;
  }
#endif
}

#if BLAECK_ENABLE_COMMAND_META
// Copies a flash name into `out` and declares the channel as owned by a command. Kept apart
// from addMessageChannel() so that one can refuse an owned name outright, with no exception
// for "unless the caller is me".
bool BlaeckSerial::_addOwnedMessageChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText)
{
#if !BLAECK_ENABLE_MESSAGES
  (void)channelName;
  (void)getStateText;
  return false;
#else
  if (channelName == nullptr)
    return false;

  char name[MAX_MESSAGE_NAME_COUNT];
  PGM_P p = reinterpret_cast<PGM_P>(channelName);
  byte len = 0;
  byte c;
  while ((c = pgm_read_byte(p + len)) != 0 && len + 1 < MAX_MESSAGE_NAME_COUNT)
  {
    name[len] = (char)c;
    len++;
  }
  name[len] = '\0';
  if (len == 0)
    return false;

  int existing = _findMessageChannel(name);
  if (existing >= 0 && !_messageChannels[existing].ownedByCommand)
  {
    // The sketch declared this name itself. The command takes it, because its state has to
    // come from one place - but say so, since the addMessageChannel() line is now dead.
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel taken over by a command's own state; drop the addMessageChannel() for: "));
      _debugStream->println(name);
    }
  }

  if (existing >= 0)
  {
    _messageChannels[existing].icon = nullptr;
    _messageChannels[existing].diagnostic = true;
    _messageChannels[existing].getStateText = getStateText;
    _messageChannels[existing].ownedByCommand = true;
    return true;
  }

  for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
  {
    if (_messageChannels[i].inUse)
      continue;
    strncpy(_messageChannels[i].name, name, MAX_MESSAGE_NAME_COUNT - 1);
    _messageChannels[i].name[MAX_MESSAGE_NAME_COUNT - 1] = '\0';
    _messageChannels[i].icon = nullptr;
    // Diagnostic on principle: a host that announces a sensor for it anyway should file it
    // away, since the control already shows this value.
    _messageChannels[i].diagnostic = true;
    _messageChannels[i].getStateText = getStateText;
    _messageChannels[i].ownedByCommand = true;
    _messageChannels[i].inUse = true;
    return true;
  }

  if (_debugStream != nullptr)
  {
    _debugStream->print(F("Message channel table full for a command's own state: "));
    _debugStream->println(name);
  }
  return false;
#endif
}

void BlaeckSerial::_declareOwnState(const char *command, const BlaeckCommandState &state)
{
  if (state.source != BLAECK_STATE_MESSAGE || state.getStateText == nullptr)
    return;

  if (!_addOwnedMessageChannel(state.name, state.getStateText))
    return;

  // Announce once, here. A host connecting later reads the value from the channel catalog,
  // but one already connected when the board reset has no reason to re-read a catalog it
  // already holds - and the sketch's variables are back at their startup values. Dropped
  // harmlessly when no host holds the catalog yet, which is the cold-start case the poll
  // covers.
  writeCommandState(command);
}
#endif

bool BlaeckSerial::onNumberCommand(const char *command, BlaeckCommandHandler handler,
                                   BlaeckCommandState state,
                                   float min, float max, float step,
                                   const __FlashStringHelper *unit,
                                   BlaeckEntityCategory category)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
  {
    _annotateCommand(command, BLAECK_CMD_NUMBER, state.name, min, max, step, unit, nullptr, (uint8_t)category, state.source);
    _declareOwnState(command, state);
  }
#else
  (void)state; (void)min; (void)max; (void)step; (void)unit; (void)category;
#endif
  return ok;
}

bool BlaeckSerial::onSwitchCommand(const char *command, BlaeckCommandHandler handler,
                                   BlaeckCommandState state,
                                   BlaeckEntityCategory category)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
  {
    _annotateCommand(command, BLAECK_CMD_SWITCH, state.name, 0.0f, 0.0f, 0.0f, nullptr, nullptr, (uint8_t)category, state.source);
    _declareOwnState(command, state);
  }
#else
  (void)state; (void)category;
#endif
  return ok;
}

bool BlaeckSerial::onSelectCommand(const char *command, BlaeckCommandHandler handler,
                                   BlaeckCommandState state,
                                   const __FlashStringHelper *optionsCsv,
                                   BlaeckEntityCategory category)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
  {
    _annotateCommand(command, BLAECK_CMD_SELECT, state.name, 0.0f, 0.0f, 0.0f, nullptr, optionsCsv, (uint8_t)category, state.source);
    _declareOwnState(command, state);
  }
#else
  (void)state; (void)optionsCsv; (void)category;
#endif
  return ok;
}

bool BlaeckSerial::onButtonCommand(const char *command, BlaeckCommandHandler handler,
                                   BlaeckEntityCategory category)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
    // A button carries no state, so the source is irrelevant and stays at the default.
    _annotateCommand(command, BLAECK_CMD_BUTTON, nullptr, 0.0f, 0.0f, 0.0f, nullptr, nullptr, (uint8_t)category, (uint8_t)BLAECK_STATE_SIGNAL);
#else
  (void)category;
#endif
  return ok;
}

bool BlaeckSerial::onTextCommand(const char *command, BlaeckCommandHandler handler,
                                 BlaeckCommandState state,
                                 unsigned int maxLength,
                                 BlaeckEntityCategory category)
{
  bool ok = onCommand(command, handler);
#if BLAECK_ENABLE_COMMAND_META
  if (ok)
  {
    // maxLength is stored in meta_max (reused as the text length limit).
    _annotateCommand(command, BLAECK_CMD_TEXT, state.name, 0.0f, (float)maxLength, 0.0f, nullptr, nullptr, (uint8_t)category, state.source);
    _declareOwnState(command, state);
  }
#else
  (void)state;
  (void)maxLength;
  (void)category;
#endif
  return ok;
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

bool BlaeckSerial::getSelectOption(const char *command, byte index, char *out, byte outSize) const
{
  if (out == nullptr || outSize == 0)
    return false;
  out[0] = '\0';
  if (command == nullptr)
    return false;

#if !BLAECK_ENABLE_COMMAND_META
  // No metadata is stored, so there is no option list to read back. Reported as a
  // failure with an empty result, the same as an unknown command.
  (void)index;
  return false;
#else
  for (byte i = 0; i < MAX_COMMAND_HANDLERS; i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.kind != BLAECK_CMD_SELECT || e.options == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // Walk past `index` commas, then copy up to the next one. Same field-walk the 0x80
    // catalog does over an event type list, on the options CSV instead.
    PGM_P p = reinterpret_cast<PGM_P>(e.options);
    byte seen = 0;
    unsigned int at = 0;
    while (seen < index)
    {
      byte c = pgm_read_byte(p + at);
      if (c == 0)
        return false; // fewer options than the index asked for
      if (c == ',')
        seen++;
      at++;
    }

    byte len = 0;
    byte c;
    while ((c = pgm_read_byte(p + at + len)) != 0 && c != ',')
    {
      // Truncating would produce a name no host can match against the declared
      // options, so report failure rather than hand back half of one.
      if ((unsigned int)len + 1 >= outSize)
      {
        out[0] = '\0';
        return false;
      }
      out[len] = (char)c;
      len++;
    }
    out[len] = '\0';
    return len > 0;
  }
  return false;
#endif
}

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::_annotateCommand(const char *command, uint8_t kind,
                                    const __FlashStringHelper *stateSignal,
                                    float mn, float mx, float st,
                                    const __FlashStringHelper *unit,
                                    const __FlashStringHelper *options,
                                    uint8_t category,
                                    uint8_t stateSource)
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
      _commandHandlers[i].stateSource = stateSource;
      _commandHandlers[i].category = category;
      return;
    }
  }
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
          // Full: this character and every one after it overwrites the last slot and is lost.
          // Recorded here because it is the only point at which the loss is visible - by the
          // time the frame is parsed it looks like a complete, shorter command.
          ndx = MAXIMUM_CHAR_COUNT - 1;
          _receiveOverflowed = true;
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
      _receiveOverflowed = false;
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
  // Characters were dropped while the frame was being received, so whatever follows is a
  // fragment however complete it looks. Reset here with the rest of the parse state rather
  // than after the empty-frame check, or an empty frame would inherit the previous verdict.
  _parsedTruncated = _receiveOverflowed;
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
    _parsedParamPtrs[_parsedParamCount] = tokenStart;
    _parsedParamCount++;
  }

  // Out of parameter slots with commas still to come: the argument list was cut short.
  if (moreParams)
    _parsedTruncated = true;
}

void BlaeckSerial::_dispatchRegisteredHandlers(bool sendAck)
{
  _parseCommandTokens(receivedChars);
  if (_parsedCommand[0] == '\0')
  {
    return;
  }

  // Frame level, so it covers plain commands and the any-handler as well: what was parsed is
  // not what was sent, and nothing should act on the remains.
  if (_parsedTruncated)
  {
    if (sendAck && strncmp(_parsedCommand, "BLAECK.", 7) != 0)
      _writeCommandAck(receivedChars, 1, BLAECK_ACK_TRUNCATED);
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

  // The name hash covers _parsedCommand, which precedes the first comma and so survives a frame
  // the device could not take in whole. It is what still identifies the command when the byte
  // hash cannot, because the bytes are not the ones the sender wrote.
  uint32_t nameHash = (_parsedCommand[0] == '\0') ? 0UL : _fnv1a32(_parsedCommand);

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xA5, _commandAckMsgId++);
    // Payload: command hash (4 bytes, little-endian) + name hash (4) + status (1) + reason (1).
    ulngCvt.val = _fnv1a32(rawCommand);
    _bufBytes(ulngCvt.bval, 4);
    ulngCvt.val = nameHash;
    _bufBytes(ulngCvt.bval, 4);
    _bufByte(status);
    _bufByte(reasonCode);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xA5;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = _commandAckMsgId++;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    // Payload: command hash (4 bytes, little-endian) + name hash (4) + status (1) + reason (1).
    ulngCvt.val = _fnv1a32(rawCommand);
    StreamRef->write(ulngCvt.bval, 4);
    ulngCvt.val = nameHash;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(status);
    StreamRef->write(reasonCode);

    // No CRC32 tail: acks mirror the descriptive 0xA0 frame format.
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

#if BLAECK_ENABLE_MESSAGES
void BlaeckSerial::writeMessage(const char *channelName, const char *text)
{
  this->writeMessage(channelName, text, _messageMsgId++);
}

bool BlaeckSerial::addMessageChannel(const char *channelName)
{
  return this->addMessageChannel(channelName, nullptr, false);
}

bool BlaeckSerial::addMessageChannel(const char *channelName, const __FlashStringHelper *icon)
{
  return this->addMessageChannel(channelName, icon, false);
}

bool BlaeckSerial::addMessageChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic)
{
  return this->addMessageChannel(channelName, icon, diagnostic, nullptr);
}

bool BlaeckSerial::_flashStringEqualsName(const __FlashStringHelper *flashName, const char *name)
{
  if (flashName == nullptr || name == nullptr)
    return false;

  PGM_P p = reinterpret_cast<PGM_P>(flashName);
  unsigned int i = 0;
  for (;; i++)
  {
    byte a = pgm_read_byte(p + i);
    char b = name[i];
    if (a != (byte)b)
      return false;
    if (a == 0)
      return true;
  }
}

bool BlaeckSerial::addMessageChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic,
                                     BlaeckStateTextGetter getStateText)
{
  if (channelName == nullptr || channelName[0] == '\0')
    return false;

  // A channel a command owns is that command's alone: its value comes from the getter it was
  // registered with, and nowhere else.
  int owned = _findMessageChannel(channelName);
  if (owned >= 0 && _messageChannels[owned].ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel belongs to a command's own state and cannot be redeclared: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  if (strlen(channelName) >= MAX_MESSAGE_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for message channel table: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  // Re-declaring a channel updates its metadata rather than consuming a slot.
  int existing = _findMessageChannel(channelName);
  if (existing >= 0)
  {
    _messageChannels[existing].icon = icon;
    _messageChannels[existing].diagnostic = diagnostic;
    _messageChannels[existing].getStateText = getStateText;
    return true;
  }

  for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
  {
    if (!_messageChannels[i].inUse)
    {
      strncpy(_messageChannels[i].name, channelName, MAX_MESSAGE_NAME_COUNT - 1);
      _messageChannels[i].name[MAX_MESSAGE_NAME_COUNT - 1] = '\0';
      _messageChannels[i].icon = icon;
      _messageChannels[i].diagnostic = diagnostic;
      _messageChannels[i].getStateText = getStateText;
      _messageChannels[i].inUse = true;
      return true;
    }
  }

  if (_debugStream != nullptr)
  {
    _debugStream->print(F("Message channel table full for: "));
    _debugStream->println(channelName);
  }
  return false;
}

void BlaeckSerial::clearAllMessageChannels()
{
  for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
  {
    _messageChannels[i].inUse = false;
    _messageChannels[i].icon = nullptr;
    _messageChannels[i].diagnostic = false;
    _messageChannels[i].name[0] = '\0';
  }
}

int BlaeckSerial::_findMessageChannel(const char *channelName) const
{
  if (channelName == nullptr || channelName[0] == '\0')
    return -1;

  for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
  {
    if (_messageChannels[i].inUse && strcmp(_messageChannels[i].name, channelName) == 0)
      return (int)i;
  }
  return -1;
}

void BlaeckSerial::writeMessage(const char *channelName, const char *text, unsigned long messageID)
{
  // 0x95 "Message" frame: a free-text status/log line on a declared channel,
  // device -> host.
  //   channelIndex(1)  length(2, LE uint16)  text[length]
  // channelIndex is the channel's position in the 0x90 catalog, so that frame
  // must be received first. Channels are never removed, only cleared as a whole,
  // so the slot index and the catalog position cannot drift apart.
  // No CRC (like the 0xA0/0xA5 frames). The host may surface it (e.g. a Home
  // Assistant text sensor announced from the 0x90 channel catalog); it is never
  // treated as signal/telemetry data and is not stored.
  if (StreamRef == nullptr)
    return;

  // Only declared channels are sent: the host announces its entities from the
  // 0x90 catalog, so a line on an undeclared channel would have nowhere to go.
  int channelIndex = _findMessageChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Message dropped, channel not declared with addMessageChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    return;
  }

  // A channel a command owns reports what its getter says and nothing else. A line pushed
  // here would show until the next catalog poll and then be silently replaced, which is worse
  // than refusing it. Use writeCommandState() to publish the getter's value.
  if (_messageChannels[channelIndex].ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Message dropped, channel belongs to a command's own state; use writeCommandState() for: "));
      _debugStream->println(channelName);
    }
    return;
  }

  _writeMessageFrame(channelIndex, text, messageID);
}

// The 0x95 frame itself. Split out because writeCommandState() has to reach it for a channel
// writeMessage() deliberately refuses - the guard is about who may choose the text, not about
// how it is sent.
void BlaeckSerial::_writeMessageFrame(int channelIndex, const char *text, unsigned long messageID)
{
  if (StreamRef == nullptr)
    return;

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
    _bufHeader(0x95, messageID);
    // Channel index, then the UTF-8 text length-prefixed (LE uint16).
    _bufByte((byte)channelIndex);
    _bufByte((byte)(len & 0xFF));
    _bufByte((byte)((len >> 8) & 0xFF));
    _bufBytes((const byte *)text, len);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0x95;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = messageID;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    // Channel index, then the UTF-8 text length-prefixed (LE uint16).
    StreamRef->write((byte)channelIndex);
    StreamRef->write((byte)(len & 0xFF));
    StreamRef->write((byte)((len >> 8) & 0xFF));
    StreamRef->write((const uint8_t *)text, len);

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeMessageChannels()
{
  this->writeMessageChannels(1);
}

void BlaeckSerial::writeMessageChannels(unsigned long msg_id)
{
  this->writeMessageChannelsFrame(msg_id);
}

void BlaeckSerial::writeMessageChannelsFrame(unsigned long msg_id)
{
  // 0x90 "Message Channel List" frame. Per declared channel entry:
  //   reserved(1) reserved(1) name\0 flags(1)
  //   [icon\0]                 if flags.hasIcon
  //   [stateText\0]            if flags.hasStateText
  // flags bits: 0=hasIcon 1=isDiagnostic 2=hasStateText
  // stateText is fetched from the channel's getter as the frame is built, so the catalog
  // reports each channel's value as of that moment and there is no stored copy to go
  // stale. A channel that registered no getter, or whose getter returns nullptr, carries
  // no value.
  // The two leading bytes are always zero. They keep the entry byte-shape
  // identical to a 0xA0 command entry (where they carried the now-removed I2C
  // masterSlaveConfig/slaveID), so a host parses both frames the same way:
  // skip two, read the NUL-terminated name, then the flags.
  // Declared up-front so the host can announce one text entity per channel
  // before any 0x95 message arrives, the same way 0xA0 announces commands.
  if (StreamRef == nullptr)
    return;

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0x90, msg_id);

    for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
    {
      MessageChannelEntry &e = _messageChannels[i];
      if (!e.inUse)
        continue;

      // Fetched once, before the flag is decided: the getter may return nullptr, and
      // calling it twice could hand the two uses different text.
      const char *stateText = (e.getStateText != nullptr) ? e.getStateText() : nullptr;

      byte flags = 0;
      if (e.icon != nullptr)
        flags |= 0x01;
      if (e.diagnostic)
        flags |= 0x02;
      if (stateText != nullptr)
        flags |= 0x04;

      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufStr0(e.name);
      _bufByte(flags);

      if (flags & 0x01)
        _bufFlashStr0(e.icon);
      if (flags & 0x04)
        _bufStr0(stateText);
    }

    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0x90;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    for (byte i = 0; i < MAX_MESSAGE_CHANNELS; i++)
    {
      MessageChannelEntry &e = _messageChannels[i];
      if (!e.inUse)
        continue;

      // Fetched before any of this entry's bytes go out: in this unbuffered path the frame
      // is streamed as it is built, so the getter runs mid-transmission and a slow one
      // stalls a half-sent frame.
      const char *stateText = (e.getStateText != nullptr) ? e.getStateText() : nullptr;

      byte flags = 0;
      if (e.icon != nullptr)
        flags |= 0x01;
      if (e.diagnostic)
        flags |= 0x02;
      if (stateText != nullptr)
        flags |= 0x04;

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(e.name);
      StreamRef->write((byte)0);
      StreamRef->write(flags);

      if (flags & 0x01)
      {
        StreamRef->print(e.icon);
        StreamRef->write((byte)0);
      }
      if (flags & 0x04)
      {
        StreamRef->print(stateText);
        StreamRef->write((byte)0);
      }
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}
#else
// BLAECK_ENABLE_MESSAGES=0: the API stays so sketches still build, but nothing
// is stored. The catalog still answers, with an empty list (see _writeEmptyFrame).
bool BlaeckSerial::addMessageChannel(const char *) { return false; }
bool BlaeckSerial::addMessageChannel(const char *, const __FlashStringHelper *) { return false; }
bool BlaeckSerial::addMessageChannel(const char *, const __FlashStringHelper *, bool) { return false; }
bool BlaeckSerial::addMessageChannel(const char *, const __FlashStringHelper *, bool, BlaeckStateTextGetter) { return false; }
void BlaeckSerial::clearAllMessageChannels() {}
void BlaeckSerial::writeMessageChannels() { this->writeMessageChannels(1); }
void BlaeckSerial::writeMessageChannels(unsigned long msg_id) { this->_writeEmptyFrame(0x90, msg_id); }
void BlaeckSerial::writeMessage(const char *, const char *) {}
void BlaeckSerial::writeMessage(const char *, const char *, unsigned long) {}
#endif

#if BLAECK_ENABLE_EVENTS
bool BlaeckSerial::addEventChannel(const char *channelName)
{
  return this->addEventChannel(channelName, nullptr, false);
}

bool BlaeckSerial::addEventChannel(const char *channelName, const __FlashStringHelper *icon)
{
  return this->addEventChannel(channelName, icon, false);
}

bool BlaeckSerial::addEventChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic)
{
  if (channelName == nullptr || channelName[0] == '\0')
    return false;

  if (strlen(channelName) >= MAX_EVENT_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for event channel table: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  // Re-declaring a channel updates its metadata rather than consuming a slot.
  // Its already-declared event types keep their indices.
  int existing = _findEventChannel(channelName);
  if (existing >= 0)
  {
    _eventChannels[existing].icon = icon;
    _eventChannels[existing].diagnostic = diagnostic;
    return true;
  }

  for (byte i = 0; i < MAX_EVENT_CHANNELS; i++)
  {
    if (!_eventChannels[i].inUse)
    {
      strncpy(_eventChannels[i].name, channelName, MAX_EVENT_NAME_COUNT - 1);
      _eventChannels[i].name[MAX_EVENT_NAME_COUNT - 1] = '\0';
      _eventChannels[i].icon = icon;
      _eventChannels[i].diagnostic = diagnostic;
      _eventChannels[i].inUse = true;
      return true;
    }
  }

  if (_debugStream != nullptr)
  {
    _debugStream->print(F("Event channel table full for: "));
    _debugStream->println(channelName);
  }
  return false;
}

void BlaeckSerial::_eventTypeExtent(const EventTypeEntry &e, unsigned int &start, unsigned int &len)
{
  start = 0;
  len = 0;
  if (e.text == nullptr)
    return;

  PGM_P p = reinterpret_cast<PGM_P>(e.text);
  if (e.field == WHOLE_STRING)
  {
    while (pgm_read_byte(p + len) != 0)
      len++;
    return;
  }

  // Walk past `field` commas, then measure to the next comma or the terminator.
  byte seen = 0;
  unsigned int i = 0;
  while (seen < e.field)
  {
    byte c = pgm_read_byte(p + i);
    if (c == 0)
      return; // fewer fields than expected: empty extent
    if (c == ',')
      seen++;
    i++;
  }
  start = i;
  byte c;
  while ((c = pgm_read_byte(p + start + len)) != 0 && c != ',')
    len++;
}

void BlaeckSerial::_bufEventType0(const EventTypeEntry &e)
{
  unsigned int start, len;
  _eventTypeExtent(e, start, len);
  PGM_P p = reinterpret_cast<PGM_P>(e.text) + start;
  for (unsigned int i = 0; i < len; i++)
    _bufByte(pgm_read_byte(p++));
  _bufByte(0);
}

bool BlaeckSerial::_eventTypeEquals(const EventTypeEntry &e, const __FlashStringHelper *eventType)
{
  if (e.text == nullptr || eventType == nullptr)
    return false;

  unsigned int start, len;
  _eventTypeExtent(e, start, len);

  PGM_P a = reinterpret_cast<PGM_P>(e.text) + start;
  PGM_P b = reinterpret_cast<PGM_P>(eventType);
  for (unsigned int i = 0; i < len; i++)
  {
    byte bc = pgm_read_byte(b + i);
    if (bc == 0 || pgm_read_byte(a + i) != bc)
      return false;
  }
  // Equal only if eventType ends exactly where the extent does.
  return pgm_read_byte(b + len) == 0;
}

bool BlaeckSerial::addEventChannel(const char *channelName, const __FlashStringHelper *icon, bool diagnostic,
                                   const __FlashStringHelper *eventTypes)
{
  if (!this->addEventChannel(channelName, icon, diagnostic))
    return false;

  if (eventTypes == nullptr)
    return true;

  int channelIndex = _findEventChannel(channelName);
  if (channelIndex < 0)
    return false;

  // One pool entry per field, all pointing at the same flash string. Appended in
  // order, so a field's position is its wire index - the same rule call order gives
  // addEventType().
  byte fieldCount = _flashCsvOptionCount(eventTypes);
  for (byte f = 0; f < fieldCount; f++)
  {
    if (_eventTypeCount >= MAX_EVENT_TYPES)
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Event type pool full for channel: "));
        _debugStream->println(channelName);
      }
      break;
    }
    _eventTypes[_eventTypeCount].channelIndex = (byte)channelIndex;
    _eventTypes[_eventTypeCount].text = eventTypes;
    _eventTypes[_eventTypeCount].field = f;
    _eventTypeCount++;
  }
  return true;
}

bool BlaeckSerial::addEventType(const char *channelName, const __FlashStringHelper *eventType)
{
  if (eventType == nullptr)
    return false;

  int channelIndex = _findEventChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event type dropped, channel not declared with addEventChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    return false;
  }

  // A duplicate would be unreachable: writeEvent() resolves by text and would
  // always match the first one.
  if (_findEventType((byte)channelIndex, eventType) >= 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Duplicate event type ignored on channel: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  if (_eventTypeCount >= MAX_EVENT_TYPES)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event type pool full for channel: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  _eventTypes[_eventTypeCount].channelIndex = (byte)channelIndex;
  _eventTypes[_eventTypeCount].text = eventType;
  _eventTypes[_eventTypeCount].field = WHOLE_STRING;
  _eventTypeCount++;
  return true;
}

void BlaeckSerial::clearAllEventChannels()
{
  for (byte i = 0; i < MAX_EVENT_CHANNELS; i++)
  {
    _eventChannels[i].inUse = false;
    _eventChannels[i].icon = nullptr;
    _eventChannels[i].diagnostic = false;
    _eventChannels[i].name[0] = '\0';
  }
  // The count gates every read of the pool, so the entries need no cleanup.
  _eventTypeCount = 0;
}

int BlaeckSerial::_findEventChannel(const char *channelName) const
{
  if (channelName == nullptr || channelName[0] == '\0')
    return -1;

  for (byte i = 0; i < MAX_EVENT_CHANNELS; i++)
  {
    if (_eventChannels[i].inUse && strcmp(_eventChannels[i].name, channelName) == 0)
      return (int)i;
  }
  return -1;
}

int BlaeckSerial::_findEventType(byte channelIndex, const __FlashStringHelper *eventType) const
{
  if (eventType == nullptr)
    return -1;

  // Walks the pool in insertion order, counting only this channel's entries, so
  // the result is both the wire index and the position the 0x80 catalog emits.
  // Compares by text, not pointer: the compiler is free to keep two identical
  // F() literals at different addresses. Both operands live in flash, so
  // neither strcmp() nor strcmp_P() applies (the latter reads its first
  // argument from RAM) — read both sides with pgm_read_byte().
  byte index = 0;
  for (byte i = 0; i < _eventTypeCount; i++)
  {
    if (_eventTypes[i].channelIndex != channelIndex)
      continue;

    if (_eventTypeEquals(_eventTypes[i], eventType))
      return (int)index;

    index++;
  }
  return -1;
}

// Compares two PROGMEM strings. On AVR a flash pointer cannot be dereferenced
// directly, and avr-libc offers no plain flash-to-flash strcmp, so both sides
// are read a byte at a time. On flat-address cores (ESP32, SAMD) pgm_read_byte
// is an ordinary dereference, so this stays correct there too.
bool BlaeckSerial::_flashStringEquals(const __FlashStringHelper *a, const __FlashStringHelper *b)
{
  if (a == b)
    return true;
  if (a == nullptr || b == nullptr)
    return false;

  PGM_P pa = reinterpret_cast<PGM_P>(a);
  PGM_P pb = reinterpret_cast<PGM_P>(b);
  for (;;)
  {
    byte ca = pgm_read_byte(pa++);
    byte cb = pgm_read_byte(pb++);
    if (ca != cb)
      return false;
    if (ca == 0)
      return true;
  }
}

void BlaeckSerial::writeEventChannels()
{
  this->writeEventChannels(1);
}

void BlaeckSerial::writeEventChannels(unsigned long msg_id)
{
  this->writeEventChannelsFrame(msg_id);
}

void BlaeckSerial::writeEventChannelsFrame(unsigned long msg_id)
{
  // 0x80 "Event Channel List" frame. Per declared channel entry:
  //   reserved(1) reserved(1) name\0 flags(1)
  //   [icon\0]                 if flags.hasIcon
  //   count(1) type\0 x count
  // flags bits: 0=hasIcon 1=isDiagnostic
  // The two leading bytes are always zero, matching the 0x90 and 0xA0 entries,
  // so a host parses all three the same way: skip two, read the NUL-terminated
  // name, then the flags.
  // Declared up-front so the host can announce one event entity per channel,
  // including its list of types, before any 0x85 event arrives. The count is
  // what lets a host reject an out-of-range index without parsing the run.
  if (StreamRef == nullptr)
    return;

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0x80, msg_id);

    for (byte i = 0; i < MAX_EVENT_CHANNELS; i++)
    {
      EventChannelEntry &e = _eventChannels[i];
      if (!e.inUse)
        continue;

      byte flags = 0;
      if (e.icon != nullptr)
        flags |= 0x01;
      if (e.diagnostic)
        flags |= 0x02;

      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufStr0(e.name);
      _bufByte(flags);

      if (flags & 0x01)
        _bufFlashStr0(e.icon);

      byte typeCount = 0;
      for (byte t = 0; t < _eventTypeCount; t++)
      {
        if (_eventTypes[t].channelIndex == i)
          typeCount++;
      }
      _bufByte(typeCount);

      for (byte t = 0; t < _eventTypeCount; t++)
      {
        if (_eventTypes[t].channelIndex == i)
          _bufEventType0(_eventTypes[t]);
      }
    }

    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0x80;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    for (byte i = 0; i < MAX_EVENT_CHANNELS; i++)
    {
      EventChannelEntry &e = _eventChannels[i];
      if (!e.inUse)
        continue;

      byte flags = 0;
      if (e.icon != nullptr)
        flags |= 0x01;
      if (e.diagnostic)
        flags |= 0x02;

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(e.name);
      StreamRef->write((byte)0);
      StreamRef->write(flags);

      if (flags & 0x01)
      {
        StreamRef->print(e.icon);
        StreamRef->write((byte)0);
      }

      byte typeCount = 0;
      for (byte t = 0; t < _eventTypeCount; t++)
      {
        if (_eventTypes[t].channelIndex == i)
          typeCount++;
      }
      StreamRef->write(typeCount);

      for (byte t = 0; t < _eventTypeCount; t++)
      {
        if (_eventTypes[t].channelIndex == i)
        {
          unsigned int start, len;
          _eventTypeExtent(_eventTypes[t], start, len);
          PGM_P p = reinterpret_cast<PGM_P>(_eventTypes[t].text) + start;
          for (unsigned int c = 0; c < len; c++)
            StreamRef->write(pgm_read_byte(p++));
          StreamRef->write((byte)0);
        }
      }
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeEvent(const char *channelName, const __FlashStringHelper *eventType)
{
  this->writeEvent(channelName, eventType, _eventMsgId++);
}

void BlaeckSerial::writeEvent(const char *channelName, const __FlashStringHelper *eventType, unsigned long messageID)
{
  // 0x85 "Event" frame: one occurrence on a declared channel, device -> host.
  //   channelIndex(1)  eventIndex(1)
  // Both indices refer to the 0x80 catalog, so that frame must be received
  // first. Channels are never removed, only cleared as a whole, so the slot
  // index and the catalog position cannot drift apart.
  // No CRC (like the 0x95/0xA0/0xA5 frames). The event carries no text and no
  // timestamp: the host supplies its own receipt time.
  if (StreamRef == nullptr)
    return;

  int channelIndex = _findEventChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event dropped, channel not declared with addEventChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    return;
  }

  int eventIndex = _findEventType((byte)channelIndex, eventType);
  if (eventIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event dropped, type not declared with addEventType() on channel: "));
      _debugStream->println(channelName);
    }
    return;
  }

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0x85, messageID);
    _bufByte((byte)channelIndex);
    _bufByte((byte)eventIndex);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0x85;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = messageID;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    StreamRef->write((byte)channelIndex);
    StreamRef->write((byte)eventIndex);

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}
#else
// BLAECK_ENABLE_EVENTS=0: the API stays so sketches still build, but nothing
// is stored. The catalog still answers, with an empty list (see _writeEmptyFrame).
bool BlaeckSerial::addEventChannel(const char *) { return false; }
bool BlaeckSerial::addEventChannel(const char *, const __FlashStringHelper *) { return false; }
bool BlaeckSerial::addEventChannel(const char *, const __FlashStringHelper *, bool) { return false; }
bool BlaeckSerial::addEventChannel(const char *, const __FlashStringHelper *, bool, const __FlashStringHelper *) { return false; }
bool BlaeckSerial::addEventType(const char *, const __FlashStringHelper *) { return false; }
void BlaeckSerial::clearAllEventChannels() {}
void BlaeckSerial::writeEventChannels() { this->writeEventChannels(1); }
void BlaeckSerial::writeEventChannels(unsigned long msg_id) { this->_writeEmptyFrame(0x80, msg_id); }
void BlaeckSerial::writeEvent(const char *, const __FlashStringHelper *) {}
void BlaeckSerial::writeEvent(const char *, const __FlashStringHelper *, unsigned long) {}
#endif

#if BLAECK_ENABLE_COMMAND_META
byte BlaeckSerial::_validateTypedCommand(byte handlerIndex)
{
  const CommandHandlerEntry &e = _commandHandlers[handlerIndex];

  // Plain and button commands carry no value to validate.
  if (e.kind == BLAECK_CMD_PLAIN || e.kind == BLAECK_CMD_BUTTON)
    return BLAECK_ACK_OK;

  // Every typed command declared that it takes a value, so an absent one is a rejection rather
  // than something to pass on. Answering OK to a command the handler then ignores is the one
  // outcome a host cannot recover from.
  if (_parsedParamCount < 1 || _parsedParamPtrs[0] == nullptr)
    return BLAECK_ACK_MISSING_VALUE;

  const char *v = _parsedParamPtrs[0];

  // An empty parameter is a value only for text, where it clears the field. Elsewhere it would
  // read as 0 or as no option at all, so report it as the missing value it is.
  if (v[0] == '\0' && e.kind != BLAECK_CMD_TEXT)
    return BLAECK_ACK_MISSING_VALUE;

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
    // receives raw UTF-8. The 0xA5 ack still hashes the encoded receivedChars,
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

#if BLAECK_ENABLE_SIGNAL_META
void BlaeckSerial::writeSignalConfig()
{
  this->writeSignalConfig(1);
}
void BlaeckSerial::writeSignalConfig(unsigned long msg_id)
{
  this->writeSignalConfigFrame(msg_id);
}
#else
// BLAECK_ENABLE_SIGNAL_META=0: signals still stream, they just carry no
// presentation metadata. The catalog answers with an empty list so a polling
// host learns that immediately (see _writeEmptyFrame).
void BlaeckSerial::writeSignalConfig() { this->writeSignalConfig(1); }
void BlaeckSerial::writeSignalConfig(unsigned long msg_id) { this->_writeEmptyFrame(0xF0, msg_id); }
#endif

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::writeCommands()
{
  this->writeCommands(1);
}
void BlaeckSerial::writeCommands(unsigned long msg_id)
{
  this->writeCommandsFrame(msg_id);
}
#else
// BLAECK_ENABLE_COMMAND_META=0: commands still run, they just carry no
// discovery metadata. The catalog answers with an empty list so a polling
// host learns that immediately (see _writeEmptyFrame).
void BlaeckSerial::writeCommands() { this->writeCommands(1); }
void BlaeckSerial::writeCommands(unsigned long msg_id) { this->_writeEmptyFrame(0xA0, msg_id); }
#endif

// Header + footer with no payload. Every catalog frame shares this envelope,
// and an empty body is already the legal "nothing declared" case, so a host
// needs no special handling: it simply announces no entities.
void BlaeckSerial::_writeEmptyFrame(byte msgKey, unsigned long msg_id)
{
  if (StreamRef == nullptr)
    return;

  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(msgKey, msg_id);
    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    StreamRef->write(msgKey);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");
    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
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

void BlaeckSerial::write(String signalName, const char *value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(String signalName, const char *value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(String signalName, const char *value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(int signalIndex, const char *value)
{
  this->write(signalIndex, value, 1);
}
void BlaeckSerial::write(int signalIndex, const char *value, unsigned long messageID)
{
  this->write(signalIndex, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(int signalIndex, const char *value, unsigned long messageID, unsigned long long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_string)
    {
      // String values live in a user-owned buffer; repoint Address like addSignal(const char*).
      Signals[signalIndex].Address = const_cast<char *>(value);
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

#if BLAECK_ENABLE_SIGNAL_META
void BlaeckSerial::writeSignalConfigFrame(unsigned long msg_id)
{
  // 0xF0 "Signal Config" frame. Per signal that declares something:
  //   symbolId(2) flags(2)                          (LE uint16)
  //   [unit\0]                 if flags bit 0
  //   [deviceClass\0]          if flags bit 1
  //   [icon\0]                 if flags bit 2
  //   [displayPrecision(1)]    if flags bit 9
  // flags bits: 0=hasUnit 1=hasDeviceClass 2=hasIcon 3-5=stateClass
  //             6=isDiagnostic 7=disabledByDefault 8=forceUpdate
  //             9=hasDisplayPrecision
  // Signals that declare nothing are skipped entirely, so a frame with no
  // entries is the ordinary case and not an error. The signal is named by its
  // index in the 0xB0 Symbol List, which already says which device it belongs
  // to - so unlike the other catalogs this frame carries no device fields.
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xF0, msg_id);

    for (int i = 0; i < _signalIndex; i++)
    {
      Signal &s = Signals[i];
      if (s.MetaFlags == 0)
        continue;

      uint16_t symbolId = (uint16_t)i;
      _bufByte((byte)(symbolId & 0xFF));
      _bufByte((byte)((symbolId >> 8) & 0xFF));
      _bufByte((byte)(s.MetaFlags & 0xFF));
      _bufByte((byte)((s.MetaFlags >> 8) & 0xFF));

      if (s.MetaFlags & BLAECK_SIG_HAS_UNIT)
        _bufFlashStr0(s.Unit);
      if (s.MetaFlags & BLAECK_SIG_HAS_DEVICE_CLASS)
        _bufFlashStr0(s.DeviceClass);
      if (s.MetaFlags & BLAECK_SIG_HAS_ICON)
        _bufFlashStr0(s.Icon);
      if (s.MetaFlags & BLAECK_SIG_HAS_DISPLAY_PRECISION)
        _bufByte(s.DisplayPrecision);
    }

    _bufFooter();
    _bufSend();
  }
  else
  {
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xF0;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    for (int i = 0; i < _signalIndex; i++)
    {
      Signal &s = Signals[i];
      if (s.MetaFlags == 0)
        continue;

      uint16_t symbolId = (uint16_t)i;
      StreamRef->write((byte)(symbolId & 0xFF));
      StreamRef->write((byte)((symbolId >> 8) & 0xFF));
      StreamRef->write((byte)(s.MetaFlags & 0xFF));
      StreamRef->write((byte)((s.MetaFlags >> 8) & 0xFF));

      if (s.MetaFlags & BLAECK_SIG_HAS_UNIT)
      {
        StreamRef->print(s.Unit);
        StreamRef->print('\0');
      }
      if (s.MetaFlags & BLAECK_SIG_HAS_DEVICE_CLASS)
      {
        StreamRef->print(s.DeviceClass);
        StreamRef->print('\0');
      }
      if (s.MetaFlags & BLAECK_SIG_HAS_ICON)
      {
        StreamRef->print(s.Icon);
        StreamRef->print('\0');
      }
      if (s.MetaFlags & BLAECK_SIG_HAS_DISPLAY_PRECISION)
        StreamRef->write(s.DisplayPrecision);
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}
#endif

#if BLAECK_ENABLE_COMMAND_META
void BlaeckSerial::writeCommandsFrame(unsigned long msg_id)
{
  // 0xA0 "Command List" frame. Per discovered command entry:
  //   reserved(1) reserved(1) name\0 kind(1) flags(1)
  //   [min(4) max(4) step(4)]  if flags.hasRange   (LE float)
  //   [unit\0]                 if flags.hasUnit
  //   [optionsCsv\0]           if flags.hasOptions
  //   [stateSignal\0 src(1)]   if flags.hasStateSignal
  //   [maxLen(2)]              if flags.isText     (LE uint16)
  // flags bits: 0=hasRange 1=hasUnit 2=hasOptions 3=hasStateSignal 4=isText
  // src says what stateSignal names: 0 an addSignal() signal, 1 an
  // addMessageChannel() channel (BlaeckStateSource). It rides with the name rather
  // than taking a flags bit, so the last free bit (0x80) stays available.
  // All in-use entries are emitted, including plain onCommand() entries
  // (kind=BLAECK_CMD_PLAIN, flags=0, no trailing metadata). Plain entries carry
  // no Home Assistant entity, but are listed so a host can build a full command
  // palette / autocomplete of every command the device accepts.
  if (_bufferedWrites && _frameBuf)
  {
    _bufReset();
    _bufHeader(0xA0, msg_id);

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
      // Entity category in bits 5-6, so it needs no trailing payload.
      flags |= (byte)((e.category & 0x03) << 5);

      // How long a command this device can receive: characters between the delimiters, terminator
      // excluded. The same on every entry - one buffer serves them all - but carried here so each
      // entry keeps the shape every catalog frame uses. A host subtracts the name and its comma
      // for the room left for parameters; anything longer is dropped on arrival, which the sender
      // cannot otherwise know.
      uint16_t payloadMax = (uint16_t)(MAXIMUM_CHAR_COUNT - 1);
      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufByte((byte)(payloadMax & 0xFF));
      _bufByte((byte)((payloadMax >> 8) & 0xFF));
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
      {
        _bufFlashStr0(e.stateSignal);
        _bufByte(e.stateSource);
      }
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
    byte msg_key = 0xA0;
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
      // Entity category in bits 5-6, so it needs no trailing payload.
      flags |= (byte)((e.category & 0x03) << 5);

      // How long a command this device can receive: characters between the delimiters, terminator
      // excluded. The same on every entry - one buffer serves them all - but carried here so each
      // entry keeps the shape every catalog frame uses. A host subtracts the name and its comma
      // for the room left for parameters; anything longer is dropped on arrival, which the sender
      // cannot otherwise know.
      uint16_t payloadMax = (uint16_t)(MAXIMUM_CHAR_COUNT - 1);
      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->write((byte)(payloadMax & 0xFF));
      StreamRef->write((byte)((payloadMax >> 8) & 0xFF));
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
        StreamRef->write(e.stateSource);
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
