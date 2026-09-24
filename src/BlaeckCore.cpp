/*
        File: BlaeckCore.cpp
        Author: Sebastian Strobl
*/

#include <Arduino.h>
#include "BlaeckCore.h"

namespace BLAECK_CORE_NAMESPACE
{

static const char *_defaultBoardName()
{
#if defined(ARDUINO_AVR_MEGA2560)
  return "Arduino Mega 2560";
#elif defined(ARDUINO_AVR_UNO)
  return "Arduino Uno";
#elif defined(ARDUINO_AVR_NANO)
  return "Arduino Nano";
#elif defined(ARDUINO_AVR_LEONARDO)
  return "Arduino Leonardo";
#elif defined(ARDUINO_AVR_MICRO)
  return "Arduino Micro";
#elif defined(ARDUINO_GIGA)
  return "Arduino GIGA R1";
#elif defined(ARDUINO_UNOWIFIR4)
  return "Arduino UNO R4 WiFi";
#elif defined(ARDUINO_MINIMA)
  return "Arduino UNO R4 Minima";
#elif defined(ARDUINO_SAMD_MKRZERO)
  return "Arduino MKR Zero";
#elif defined(ARDUINO_NANO_ESP32)
  return "Arduino Nano ESP32";
#elif defined(ARDUINO_BOARD)
  return ARDUINO_BOARD;
#else
  return "n/a";
#endif
}

BlaeckCore::BlaeckCore() : DeviceHWVersion(_defaultBoardName())
{
  validatePlatformSizes();
}

BlaeckCore::~BlaeckCore()
{
  // Free what the entries own before the table, which holds the pointers.
  _freeSignalOwned();
  delete[] Signals;
  Signals = nullptr;
  delete[] _commandHandlers;
  _commandHandlers = nullptr;
#if BLAECK_ENABLE_STATE_CHANNELS
  delete[] _stateChannels;
  _stateChannels = nullptr;
#endif
#if BLAECK_ENABLE_EVENTS
  delete[] _eventChannels;
  _eventChannels = nullptr;
  delete[] _eventTypes;
  _eventTypes = nullptr;
#endif
  _bufFree();
}

// Outside every BLAECK_ENABLE_* block: the symbol list and the schema hash need it too.
byte BlaeckCore::_dtypeCode(dataType t)
{
  switch (t)
  {
  case (Blaeck_bool):   return 0x0;
  case (Blaeck_byte):   return 0x1;
  case (Blaeck_short):  return 0x2;
  case (Blaeck_ushort): return 0x3;
  case (Blaeck_int):    return 0x4;
  case (Blaeck_uint):   return 0x5;
  case (Blaeck_long):   return 0x6;
  case (Blaeck_ulong):  return 0x7;
  case (Blaeck_float):  return 0x8;
  case (Blaeck_double): return 0x9;
  case (Blaeck_string): return 0xA;
  default:              return 0x8;
  }
}

// The type a numeric tag names. On AVR, BlaeckDouble maps to float, as double * does.
static dataType _tagType(BlaeckNumericTag tag)
{
#ifdef __AVR__
  return tag.t == Blaeck_double ? Blaeck_float : tag.t;
#else
  return tag.t;
#endif
}

void BlaeckCore::_flushCatalogs()
{
  if (!_mayWriteFrame())
    return;

  // Each writer clears its own dirty flag, so a catalog a host asked for isn't sent twice.
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_stateCatalogDirty)
    this->writeStateChannels(0);
#endif

#if BLAECK_ENABLE_EVENTS
if (_eventCatalogDirty)
    this->writeEventChannels(0);
#endif

  if (_commandCatalogDirty)
    this->writeCommands(0);

#if BLAECK_ENABLE_SIGNAL_META
  if (_signalConfigDirty)
    this->writeSignalConfig(0);
#endif
}

BlaeckBeginRef BlaeckCore::_beginCore()
{
  // Free first: _freeSignalOwned() needs _signalCapacity to still match the table.
  if (Signals != nullptr)
  {
    _freeSignalOwned();
    delete[] Signals;
    Signals = nullptr;
  }
  _signalCapacity = DEFAULT_SIGNALS;
  _signalIndex = 0;
  SignalCount = 0;
  _schemaHash = 0;
  _signalRegistrationFailed = false;
  _rejectedSignalCount = 0;
#if BLAECK_ENABLE_SIGNAL_META
  _rejectedSignalMetaCount = 0;
#endif

  // No table is allocated here; each is allocated by its first entry, so the begin() chain
  // can still change the sizes.
  return BlaeckBeginRef(this);
}

bool BlaeckCore::hasRejections() const
{
  if (_rejectedSignalCount > 0 || _rejectedCommandCount > 0)
    return true;
#if BLAECK_ENABLE_SIGNAL_META
  if (_rejectedSignalMetaCount > 0)
    return true;
#endif
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_rejectedStateChannelCount > 0)
    return true;
#endif
#if BLAECK_ENABLE_EVENTS
  if (_rejectedEventChannelCount > 0 || _rejectedEventTypeCount > 0)
    return true;
#endif
  return false;
}

void BlaeckCore::_printRejectionLine(Print *out, const __FlashStringHelper *what,
                                       const __FlashStringHelper *chainCall, uint16_t dropped,
                                       unsigned int capacity)
{
  out->print(F("  "));
  out->print(dropped);
  out->print(F(" "));
  out->print(what);
  out->print(F(" dropped, table holds "));
  out->print(capacity);
  out->print(F(" - begin(&Serial)."));
  out->print(chainCall);
  out->print(F("("));
  // The total asked for, which is the size that would have fitted everything.
  out->print(capacity + dropped);
  out->println(F(")"));
}

bool BlaeckCore::printRejections(Print *out)
{
  if (out == nullptr || !hasRejections())
    return false;

  out->println(F("BlaeckCore dropped what it had no room for:"));
  if (_rejectedSignalCount > 0)
    _printRejectionLine(out, F("signal(s)"), F("withSignals"), _rejectedSignalCount,
                        _signalCapacity);
  if (_rejectedCommandCount > 0)
    _printRejectionLine(out, F("command(s)"), F("withCommands"), _rejectedCommandCount,
                        _commandCapacity);
#if BLAECK_ENABLE_STATE_CHANNELS
  if (_rejectedStateChannelCount > 0)
    _printRejectionLine(out, F("state channel(s)"), F("withStateChannels"),
                        _rejectedStateChannelCount, _stateChannelCapacity);
#endif
#if BLAECK_ENABLE_EVENTS
  if (_rejectedEventChannelCount > 0)
    _printRejectionLine(out, F("event channel(s)"), F("withEventChannels"),
                        _rejectedEventChannelCount, _eventChannelCapacity);
  if (_rejectedEventTypeCount > 0)
    _printRejectionLine(out, F("event type(s)"), F("withEventTypes"),
                        _rejectedEventTypeCount, _eventTypeCapacity);
#endif
  // Also counts names that were too long or duplicated. The debug stream gave each reason.
  out->println(F("  (a name too long or already taken counts here too - "
                 "withDebugStream() names each one)"));
#if BLAECK_ENABLE_SIGNAL_META
  // Out of heap, not out of table, so there is no setting to suggest. The signals themselves
  // are fine.
  if (_rejectedSignalMetaCount > 0)
  {
    out->print(F("  "));
    out->print(_rejectedSignalMetaCount);
    out->println(F(" signal description(s) dropped, out of heap - the signals themselves "
                   "are unaffected"));
  }
#endif
  return true;
}

void BlaeckCore::_setTableCapacity(TableId table, unsigned int count)
{
  // The table this size is for, and whether it already exists.
  const void *existing = nullptr;
  const __FlashStringHelper *chainCall = nullptr;
  switch (table)
  {
  case TABLE_SIGNALS:
    existing = Signals;
    chainCall = F("withSignals");
    break;
#if BLAECK_ENABLE_STATE_CHANNELS
  case TABLE_STATE_CHANNELS:
    existing = _stateChannels;
    chainCall = F("withStateChannels");
    break;
#endif
#if BLAECK_ENABLE_EVENTS
  case TABLE_EVENT_CHANNELS:
    existing = _eventChannels;
    chainCall = F("withEventChannels");
    break;
  case TABLE_EVENT_TYPES:
    existing = _eventTypes;
    chainCall = F("withEventTypes");
    break;
#endif
  case TABLE_COMMANDS:
    existing = _commandHandlers;
    chainCall = F("withCommands");
    break;
  default:
    return;
  }

  // A table's size is fixed once it exists.
  if (existing != nullptr)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Too late for BLAECK."));
      _debugStream->print(chainCall);
      _debugStream->println(F("(): that table already exists. Move the call up, "
                              "before the first entry is added to it."));
    }
    return;
  }

  // Capped at MAX_TABLE_ENTRIES, with a warning.
  if (count > MAX_TABLE_ENTRIES && _debugStream != nullptr)
  {
    _debugStream->print(F("BLAECK."));
    _debugStream->print(chainCall);
    _debugStream->print(F("("));
    _debugStream->print(count);
    _debugStream->print(F("): clamped to "));
    _debugStream->print(MAX_TABLE_ENTRIES);
    _debugStream->println(F(", which is the most this table can hold."));
  }

  switch (table)
  {
  case TABLE_SIGNALS:
    // Also capped for signals, because _signalIndex is an int, 16-bit on AVR.
    _signalCapacity = (count > MAX_TABLE_ENTRIES) ? MAX_TABLE_ENTRIES : (uint16_t)count;
    break;
#if BLAECK_ENABLE_STATE_CHANNELS
  case TABLE_STATE_CHANNELS:
    _stateChannelCapacity = (count > MAX_TABLE_ENTRIES) ? MAX_TABLE_ENTRIES : (uint16_t)count;
    break;
#endif
#if BLAECK_ENABLE_EVENTS
  case TABLE_EVENT_CHANNELS:
    _eventChannelCapacity = (count > MAX_TABLE_ENTRIES) ? MAX_TABLE_ENTRIES : (uint16_t)count;
    break;
  case TABLE_EVENT_TYPES:
    _eventTypeCapacity = (count > MAX_TABLE_ENTRIES) ? MAX_TABLE_ENTRIES : (uint16_t)count;
    break;
#endif
  case TABLE_COMMANDS:
    _commandCapacity = (count > MAX_TABLE_ENTRIES) ? MAX_TABLE_ENTRIES : (uint16_t)count;
    break;
  default:
    break;
  }
}

void BlaeckCore::_warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
                                  const char *droppedName)
{
  if (_debugStream == nullptr)
    return;
  _debugStream->print(F("Dropped '"));
  _debugStream->print(droppedName != nullptr ? droppedName : "");
  _debugStream->print(F("': table full at "));
  _debugStream->print(capacity);
  _debugStream->print(F(". Room for more: BLAECK.begin(&Serial)."));
  _debugStream->print(table);
  _debugStream->print(F("("));
  _debugStream->print(capacity + 1);
  _debugStream->println(F(") or higher."));
}

void BlaeckCore::_warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
                                  const __FlashStringHelper *droppedName)
{
  if (_debugStream == nullptr)
    return;
  _debugStream->print(F("Dropped '"));
  if (droppedName != nullptr)
    _debugStream->print(droppedName);
  _debugStream->print(F("': table full at "));
  _debugStream->print(capacity);
  _debugStream->print(F(". Room for more: BLAECK.begin(&Serial)."));
  _debugStream->print(table);
  _debugStream->print(F("("));
  _debugStream->print(capacity + 1);
  _debugStream->println(F(") or higher."));
}

// Each table is allocated once, by its first entry, and never grows or is freed, so the
// heap doesn't fragment. If there isn't enough RAM the pointer stays null, which callers
// treat as a full table.
bool BlaeckCore::_ensureSignalTable()
{
  if (Signals != nullptr)
    return true;
  if (_signalCapacity == 0)
    return false;
  Signals = new (std::nothrow) Signal[_signalCapacity];
  if (Signals == nullptr)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("No RAM for the signal table ("));
      _debugStream->print(_signalCapacity);
      _debugStream->println(F(" signals). Every signal will be dropped."));
    }
    return false;
  }
  return true;
}

bool BlaeckCore::_ensureCommandTable()
{
  if (_commandHandlers != nullptr)
    return true;
  if (_commandCapacity == 0)
    return false;
  _commandHandlers = new (std::nothrow) CommandHandlerEntry[_commandCapacity]();
  if (_commandHandlers == nullptr && _debugStream != nullptr)
  {
    _debugStream->print(F("No RAM for the command table ("));
    _debugStream->print(_commandCapacity);
    _debugStream->println(F(" commands). Every command will be dropped."));
  }
  return _commandHandlers != nullptr;
}

#if BLAECK_ENABLE_STATE_CHANNELS
bool BlaeckCore::_ensureStateChannelTable()
{
  if (_stateChannels != nullptr)
    return true;
  if (_stateChannelCapacity == 0)
    return false;
  _stateChannels = new (std::nothrow) StateChannelEntry[_stateChannelCapacity]();
  if (_stateChannels == nullptr && _debugStream != nullptr)
  {
    _debugStream->print(F("No RAM for the state channel table ("));
    _debugStream->print(_stateChannelCapacity);
    _debugStream->println(F(" channels). Every channel will be dropped."));
  }
  return _stateChannels != nullptr;
}
#endif

#if BLAECK_ENABLE_EVENTS
bool BlaeckCore::_ensureEventChannelTable()
{
  if (_eventChannels != nullptr)
    return true;
  if (_eventChannelCapacity == 0)
    return false;
  _eventChannels = new (std::nothrow) EventChannelEntry[_eventChannelCapacity]();
  if (_eventChannels == nullptr && _debugStream != nullptr)
  {
    _debugStream->print(F("No RAM for the event channel table ("));
    _debugStream->print(_eventChannelCapacity);
    _debugStream->println(F(" channels). Every event channel will be dropped."));
  }
  return _eventChannels != nullptr;
}

bool BlaeckCore::_ensureEventTypeTable()
{
  if (_eventTypes != nullptr)
    return true;
  if (_eventTypeCapacity == 0)
    return false;
  _eventTypes = new (std::nothrow) EventTypeEntry[_eventTypeCapacity]();
  if (_eventTypes == nullptr && _debugStream != nullptr)
  {
    _debugStream->print(F("No RAM for the event type pool ("));
    _debugStream->print(_eventTypeCapacity);
    _debugStream->println(F(" types). Every event type will be dropped."));
  }
  return _eventTypes != nullptr;
}
#endif

int BlaeckCore::_registerSignal(const char *signalName, dataType type, void *address)
{
  return _registerSignalCommon(signalName, nullptr, type, address);
}

int BlaeckCore::_registerSignal(const __FlashStringHelper *signalName, dataType type, void *address)
{
  return _registerSignalCommon(nullptr, signalName, type, address);
}

int BlaeckCore::_registerSignalCommon(const char *ram, const __FlashStringHelper *flash,
                                        dataType type, void *address)
{
  if (!_ensureSignalTable() || static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    if (flash != nullptr)
      _warnTableFull(F("withSignals"), _signalCapacity, flash);
    else
      _warnTableFull(F("withSignals"), _signalCapacity, ram);
    _signalRegistrationFailed = true;
    _rejectedSignalCount++;
    // -1 makes a handle that ignores every call.
    return -1;
  }
  _setSignalName(_signalIndex, ram, flash);
  Signals[_signalIndex].DataType = type;
  Signals[_signalIndex].Address = address;
  // Bit-fields can't have initializers, so set them here. The slot may be reused after
  // deleteSignals().
  Signals[_signalIndex].Updated = 0;
  Signals[_signalIndex].HasSuffix = 0;
  Signals[_signalIndex].NameSuffix = 0;
#if BLAECK_ENABLE_SIGNAL_META
  // A reused slot may still hold a metadata record from before; free it.
  if (Signals[_signalIndex].Meta != nullptr)
  {
    delete Signals[_signalIndex].Meta;
    Signals[_signalIndex].Meta = nullptr;
  }
#endif
  int16_t added = (int16_t)_signalIndex;
  _signalIndex++;
  SignalCount = _signalIndex;
  _schemaHash = _computeSchemaHash();
  return added;
}

BlaeckBoolSignalRef BlaeckCore::addSignal(const char *signalName, bool *value)
{
  return BlaeckBoolSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_bool, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, byte *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_byte, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_short, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, unsigned short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ushort, value));
}

// int and unsigned int are registered by their real width: 2 bytes on AVR, 4 elsewhere.
// The same #ifdef appears on every int registration below.
BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, int *value)
{
#ifdef __AVR__
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_int, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
#endif
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, unsigned int *value)
{
#ifdef __AVR__
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_uint, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
#endif
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, unsigned long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, float *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const char *signalName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_double, value));
#endif
}

BlaeckTextSignalRef BlaeckCore::addSignal(const char *signalName, const char *value)
{
  // Address is void * for every type; a string is only read.
  return BlaeckTextSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_string, const_cast<char *>(value)));
}

BlaeckBoolSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, bool *value)
{
  return BlaeckBoolSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_bool, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, byte *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_byte, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_short, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, unsigned short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ushort, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, int *value)
{
#ifdef __AVR__
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_int, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
#endif
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, unsigned int *value)
{
#ifdef __AVR__
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_uint, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
#endif
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, unsigned long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, float *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
}

BlaeckNumericSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_double, value));
#endif
}

BlaeckTextSignalRef BlaeckCore::addSignal(const __FlashStringHelper *signalName, const char *value)
{
  return BlaeckTextSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_string, const_cast<char *>(value)));
}

void BlaeckCore::deleteSignals()
{
  // Free the name copies and metadata records too, not only rewind the index.
  _freeSignalOwned();
  _signalIndex = 0;
  SignalCount = _signalIndex;
  _schemaHash = 0;
  _signalRegistrationFailed = false;
  _rejectedSignalCount = 0;
#if BLAECK_ENABLE_SIGNAL_META
  _rejectedSignalMetaCount = 0;
#endif
}

uint16_t BlaeckCore::_computeSchemaHash()
{
  // CRC16-CCITT (init 0x0000, poly 0x1021) over the signal names and type codes, matching
  // Python's binascii.crc_hqx(data, 0). Names go through _emitSignalName(), as in the symbol list.
  _schemaHashAccum = 0x0000;
  for (int j = 0; j < _signalIndex; j++)
  {
    _signalNameFeedHash(Signals[j]);
    _schemaHashFeedByte(_dtypeCode(Signals[j].DataType));
  }
  return _schemaHashAccum;
}

void BlaeckCore::setSignalName(int signalIndex, const char *signalName)
{
  _setSignalName(signalIndex, signalName, nullptr);
  // A new name changes the schema, so the hash must change too.
  _schemaHash = _computeSchemaHash();
}

void BlaeckCore::_setSignalName(int signalIndex, const char *ram, const __FlashStringHelper *flash)
{
  if (Signals == nullptr || signalIndex < 0 || signalIndex >= (int)_signalCapacity)
    return;

  Signal &s = Signals[signalIndex];
  // Free the copy the slot held; a flash name owns nothing. Test the pointer before
  // NameInFlash, which means nothing in a slot that has never been named.
  if (s.SignalName != nullptr && !s.NameInFlash)
    free((void *)s.SignalName);
  s.SignalName = nullptr;
  s.NameInFlash = 0;

  if (flash != nullptr)
  {
    s.SignalName = reinterpret_cast<const char *>(flash);
    s.NameInFlash = 1;
    return;
  }
  if (ram == nullptr)
    return;

  // Copied, so the caller can reuse its buffer.
  size_t needed = strlen(ram) + 1;
  char *copy = (char *)malloc(needed);
  if (copy != nullptr)
  {
    memcpy(copy, ram, needed);
    s.SignalName = copy;
  }
    // Out of RAM: the signal stays, with an empty name.
}

void BlaeckCore::_freeSignalOwned()
{
  if (Signals == nullptr)
    return;
  for (unsigned int i = 0; i < _signalCapacity; i++)
  {
    // Pointer first, as in _setSignalName().
    if (Signals[i].SignalName != nullptr && !Signals[i].NameInFlash)
      free((void *)Signals[i].SignalName);
    Signals[i].SignalName = nullptr;
    Signals[i].NameInFlash = 0;
#if BLAECK_ENABLE_SIGNAL_META
    delete Signals[i].Meta;
    Signals[i].Meta = nullptr;
#endif
  }
}

#if BLAECK_ENABLE_SIGNAL_META
SignalMeta *BlaeckCore::_ensureSignalMeta(int16_t index)
{
  // A rejected signal's handle has nowhere to store anything.
  if (index < 0 || Signals == nullptr || static_cast<unsigned int>(index) >= _signalCapacity)
    return nullptr;
  Signal &s = Signals[index];
  if (s.Meta == nullptr)
  {
    // Works with either a throwing or a nothrow new: a failure gives null or doesn't return.
    s.Meta = new (std::nothrow) SignalMeta();
    if (s.Meta == nullptr)
      _rejectedSignalMetaCount++;
  }
  return s.Meta;
}
#endif

bool BlaeckCore::_signalNameEquals(const Signal &s, const char *name) const
{
  if (name == nullptr)
    return false;
  const char *q = name;
  if (s.SignalName != nullptr)
  {
    if (s.NameInFlash)
    {
      // pgm_read_byte rather than strcmp_P, which some cores lack.
      PGM_P p = reinterpret_cast<PGM_P>(s.SignalName);
      byte c;
      while ((c = pgm_read_byte(p++)) != 0)
      {
        if (*q++ != (char)c)
          return false;
      }
    }
    else
    {
      const char *p = s.SignalName;
      while (*p)
      {
        if (*q++ != *p++)
          return false;
      }
    }
  }
  // The suffix isn't stored, so compare against its digits.
  if (s.HasSuffix)
  {
    char digits[4];
    byte n = _signalSuffixDigits(s, digits);
    for (byte i = 0; i < n; i++)
    {
      if (*q++ != digits[i])
        return false;
    }
  }
  return *q == '\0';
}

// The suffix as decimal digits, without a terminator. Returns the count; out must hold three.
byte BlaeckCore::_signalSuffixDigits(const Signal &s, char *out)
{
  uint8_t v = s.NameSuffix;
  byte n = 0;
  if (v >= 100)
    out[n++] = (char)('0' + (v / 100));
  if (v >= 10)
    out[n++] = (char)('0' + ((v / 10) % 10));
  out[n++] = (char)('0' + (v % 10));
  return n;
}

// Walks a name (flash or RAM, plus any suffix) and sends each byte to the frame or the hash,
// so both always see the same bytes.
void BlaeckCore::_emitSignalName(const Signal &s, NameSink sink)
{
  if (s.SignalName != nullptr)
  {
    if (s.NameInFlash)
    {
      PGM_P p = reinterpret_cast<PGM_P>(s.SignalName);
      byte c;
      while ((c = pgm_read_byte(p++)) != 0)
        _emitNameByte(c, sink);
    }
    else
    {
      const char *p = s.SignalName;
      while (*p)
        _emitNameByte((byte)*p++, sink);
    }
  }
  if (s.HasSuffix)
  {
    char digits[4];
    byte n = _signalSuffixDigits(s, digits);
    for (byte i = 0; i < n; i++)
      _emitNameByte((byte)digits[i], sink);
  }
}

void BlaeckCore::_emitNameByte(byte c, NameSink sink)
{
  switch (sink)
  {
  case NAME_SINK_FRAME:
    _emitByte(c);
    break;
  default:
    _schemaHashFeedByte(c);
    break;
  }
}

void BlaeckCore::_signalNameFeedHash(const Signal &s)
{
  _emitSignalName(s, NAME_SINK_HASH);
}

void BlaeckCore::_emitSignalName0(const Signal &s)
{
  _emitSignalName(s, NAME_SINK_FRAME);
  // One terminator after the prefix and suffix together.
  _emitByte(0);
}

bool blaeck_detail::optionsAccepted(const __FlashStringHelper *optionsCsv, Print *debug,
                                    const char *name, bool nameInFlash)
{
  const bool empty = BlaeckCore::_flashCsvOptionCount(optionsCsv) == 0;
  if (!empty && !BlaeckCore::_flashCsvHasBlankField(optionsCsv))
    return true;

  if (debug != nullptr)
  {
    debug->print(empty ? F("withOptions ignored, needs at least one option: ")
                       : F("withOptions ignored, an option is blank: "));
    if (name != nullptr)
    {
      if (nameInFlash)
        debug->print(reinterpret_cast<const __FlashStringHelper *>(name));
      else
        debug->print(name);
    }
    debug->println(F(". Every value is rejected and a host has nothing to offer."));
  }
  return false;
}

bool blaeck_detail::stateGetterAccepted(const void *stateValue, dataType want, dataType have,
                                        const __FlashStringHelper *method, Print *debug,
                                        const char *name, bool nameInFlash)
{
  const bool taken = (stateValue != nullptr);
  if (!taken && want == have)
    return true;

  if (debug != nullptr)
  {
    debug->print(method);
    debug->print(taken ? F(" ignored, channel was declared with a variable: ")
                       : F(" ignored, the getter does not return what the channel carries: "));
    if (name != nullptr)
    {
      if (nameInFlash)
        debug->print(reinterpret_cast<const __FlashStringHelper *>(name));
      else
        debug->print(name);
    }
    debug->println(taken
                       ? F(". The getter would be read instead of the variable, so neither is trusted.")
                       : F(". Declare the channel with the type the getter returns."));
  }
  return false;
}

// Store a value in a signal, converted to the signal's declared type. There are three
// because no one C++ type holds all the others: a double on AVR is 4 bytes and can't hold a
// long. False if there is no such signal, or it holds text.
#define BLAECK_STORE_CASES(v)                                                                  \
  switch (Signals[signalIndex].DataType)                                                       \
  {                                                                                            \
  case (Blaeck_bool):   *((bool *)Signals[signalIndex].Address)           = ((v) != 0); break; \
  case (Blaeck_byte):   *((byte *)Signals[signalIndex].Address)           = (byte)(v); break;  \
  case (Blaeck_short):  *((short *)Signals[signalIndex].Address)          = (short)(v); break; \
  case (Blaeck_ushort): *((unsigned short *)Signals[signalIndex].Address) = (unsigned short)(v); break; \
  case (Blaeck_int):    *((int *)Signals[signalIndex].Address)            = (int)(v); break;   \
  case (Blaeck_uint):   *((unsigned int *)Signals[signalIndex].Address)   = (unsigned int)(v); break; \
  case (Blaeck_long):   *((long *)Signals[signalIndex].Address)           = (long)(v); break;  \
  case (Blaeck_ulong):  *((unsigned long *)Signals[signalIndex].Address)  = (unsigned long)(v); break; \
  case (Blaeck_float):  *((float *)Signals[signalIndex].Address)          = (float)(v); break; \
  case (Blaeck_double): *((double *)Signals[signalIndex].Address)         = (double)(v); break;\
  default: return false;                                                                       \
  }                                                                                            \
  return true;

bool BlaeckCore::_storeSigned(int signalIndex, long value)
{
  if (signalIndex < 0 || signalIndex >= _signalIndex)
    return false;
  BLAECK_STORE_CASES(value)
}

bool BlaeckCore::_storeUnsigned(int signalIndex, unsigned long value)
{
  if (signalIndex < 0 || signalIndex >= _signalIndex)
    return false;
  BLAECK_STORE_CASES(value)
}

bool BlaeckCore::_storeFloating(int signalIndex, double value)
{
  if (signalIndex < 0 || signalIndex >= _signalIndex)
    return false;
  BLAECK_STORE_CASES(value)
}

#undef BLAECK_STORE_CASES

void BlaeckCore::update(int signalIndex, bool value)
{
  if (_storeSigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, byte value)
{
  if (_storeUnsigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, short value)
{
  if (_storeSigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, unsigned short value)
{
  if (_storeUnsigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, int value)
{
  if (_storeSigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, unsigned int value)
{
  if (_storeUnsigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, long value)
{
  if (_storeSigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, unsigned long value)
{
  if (_storeUnsigned(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, float value)
{
  if (_storeFloating(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, double value)
{
  if (_storeFloating(signalIndex, value))
    Signals[signalIndex].Updated = true;
}

void BlaeckCore::update(int signalIndex, const char *value)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_string)
    {
      // Point at the caller's buffer; it isn't copied.
      Signals[signalIndex].Address = const_cast<char *>(value);
      Signals[signalIndex].Updated = true;
    }
  }
}

void BlaeckCore::update(const char *signalName, bool value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, byte value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, unsigned short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, unsigned int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, unsigned long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, float value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, double value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckCore::update(const char *signalName, const char *value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

int BlaeckCore::findSignalIndex(const char *signalName)
{
  for (int i = 0; i < _signalIndex; i++)
  {
    if (_signalNameEquals(Signals[i], signalName))
    {
      return i;
    }
  }
  return -1; // Not found
}

void BlaeckCore::read()
{
  this->writeRestarted();

  if (_receiveCommand())
  {
    // Parsed once, for both the built-ins and the registered handlers.
    _parseCommandTokens(_receiver.chars);
    // Before the truncation check, so even a cut-off built-in counts.
    if (strncmp(_parsedCommand, "BLAECK.", 7) == 0)
      _builtinCommandReceived();
    if (_debugStream != nullptr)
    {
      _debugStream->print("<");
      _debugStream->print(_receiver.chars);
      _debugStream->println(">");
    }

    // A command that didn't arrive whole must not run, built-in or not.
    if (_parsedTruncated)
    {
      _writeCommandAck(_receiver.chars, 1, BLAECK_ACK_TRUNCATED);
    }
    else
    {
      // Acknowledge before replying, so a host can tell the request arrived. The handler dispatch
      // below then doesn't acknowledge again.
      bool builtinMatched = true;
      const unsigned long msg_id = _parsedPrefixMsgId;
      _replying = true;

      if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_SYMBOLS)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeSymbols(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_SIGNAL_CONFIG)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeSignalConfig(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_DATA)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        // Marks the data frame as a reply to a request.
        _frameRequested = true;
        this->writeAllData(msg_id, getTimeStamp());
        _frameRequested = false;
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_GET_DEVICES)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeDevices(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_COMMANDS)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeCommands(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_STATE_CHANNELS)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeStateChannels(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_WRITE_EVENT_CHANNELS)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->writeEventChannels(msg_id);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_ACTIVATE)))
      {
        // strtoul, because atoi is 16-bit on AVR.
        unsigned long timedInterval_ms = 0;
        if (_parsedParamCount > 0 && _parsedParamPtrs[0] != nullptr)
          timedInterval_ms = strtoul(_parsedParamPtrs[0], nullptr, 10);
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->_setTimedDataState(true, timedInterval_ms);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_DEACTIVATE)))
      {
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
        this->_setTimedDataState(false, _timedInterval_ms);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_PAUSE_WRITES)))
      {
        // Check for the word first; strtoul would read it as 0, the default duration.
        bool forever = _parsedParamCount > 0 && _parsedParamPtrs[0] != nullptr &&
                       equalsFlash(_parsedParamPtrs[0], F(BLAECK_PAUSE_WRITES_FOREVER));

        unsigned long pause_ms = 0;
        if (!forever && _parsedParamCount > 0 && _parsedParamPtrs[0] != nullptr)
          pause_ms = strtoul(_parsedParamPtrs[0], nullptr, 10);
        // Acknowledge before pausing, or the ack itself would be held back.
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);

        if (forever)
          this->_setWritesPausedForever();
        else
          this->_setWritesPaused(pause_ms);
      }
      else if (equalsFlash(_parsedCommand, F(BLAECK_BUILTIN_RESUME_WRITES)))
      {
        _clearWritesPaused();
        _writeCommandAck(_receiver.chars, 0, BLAECK_ACK_OK);
      }
      else
      {
        builtinMatched = false;
      }
      // A handler may write state or events, which are for every host.
      _replying = false;

      _dispatchRegisteredHandlers(!builtinMatched);
    }
  }

  // Send any catalog a handler changed.
  _flushCatalogs();
}

void BlaeckCore::setBeforeWriteCallback(void (*callback)())
{
  _beforeWriteCallback = callback;
}

int BlaeckCore::_registerCommand(const char *command, BlaeckCommandHandler handler, uint8_t kind)
{
  if (command == nullptr || handler == nullptr || command[0] == '\0')
  {
    _rejectedCommandCount++;
    return -1;
  }
  if (strlen(command) >= MAX_COMMAND_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print("Command name too long for handler table: ");
      _debugStream->println(command);
    }
    _rejectedCommandCount++;
    return -1;
  }
  // '#' and '@' start a received command's prefix, so a name starting with one could never be
  // matched.
  if (command[0] == '#' || command[0] == '@')
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print("Command name starts with a reserved prefix character: ");
      _debugStream->println(command);
    }
    _rejectedCommandCount++;
    return -1;
  }
  // Names starting with BLAECK. are reserved for the built-ins.
  if (strncmp(command, "BLAECK.", 7) == 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print("Command name uses the reserved BLAECK. namespace: ");
      _debugStream->println(command);
    }
    _rejectedCommandCount++;
    return -1;
  }

  if (!_ensureCommandTable())
  {
    _warnTableFull(F("withCommands"), _commandCapacity, command);
    _rejectedCommandCount++;
    return -1;
  }

  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    if (_commandHandlers[i].inUse && strcmp(_commandHandlers[i].command, command) == 0)
    {
      _commandHandlers[i].handler = handler;
      _resetCommandMeta(i, kind);
      // The entry was reset, so the host's copy of the catalog is out of date.
      _commandCatalogDirty = true;
      return (int)i;
    }
  }

  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    if (!_commandHandlers[i].inUse)
    {
      strncpy(_commandHandlers[i].command, command, MAX_COMMAND_NAME_COUNT - 1);
      _commandHandlers[i].command[MAX_COMMAND_NAME_COUNT - 1] = '\0';
      _commandHandlers[i].handler = handler;
      _commandHandlers[i].inUse = true;
      _resetCommandMeta(i, kind);
      _commandCatalogDirty = true;
      return (int)i;
    }
  }

  _warnTableFull(F("withCommands"), _commandCapacity, command);
  _rejectedCommandCount++;
  return -1;
}

// Registering a name again replaces the command, so its metadata starts empty.
void BlaeckCore::_resetCommandMeta(uint16_t handlerIndex, uint8_t kind)
{
#if BLAECK_ENABLE_COMMAND_META
  CommandHandlerEntry &e = _commandHandlers[handlerIndex];
  e.kind = kind;
  e.meta_min = 0.0f;
  // A text command's default maximum length. Other kinds set their own range.
  e.meta_max = (kind == BLAECK_CMD_TEXT) ? (float)BLAECK_TEXT_MAX_LENGTH : 0.0f;
  e.meta_step = 0.0f;
  e.unit = nullptr;
  e.options = nullptr;
  e.stateSignal = nullptr;
  e.stateSource = BLAECK_STATE_SIGNAL;
  e.category = BLAECK_CAT_NONE;
  // Reset with the rest, so nothing carries over from an earlier registration.
  e.displayName = nullptr;
  e.deviceClass = nullptr;
  e.icon = nullptr;
  e.pressPayload = nullptr;
  e.mode = 0;
#else
  (void)handlerIndex;
  (void)kind;
#endif
}

void BlaeckCore::onCommand(const char *command, BlaeckCommandHandler handler)
{
  _registerCommand(command, handler, BLAECK_CMD_PLAIN);
}

void BlaeckCore::onAnyCommand(BlaeckAnyCommandHandler handler)
{
  _anyCommandHandler = handler;
}

void BlaeckCore::clearAllCommandHandlers()
{
#if BLAECK_ENABLE_STATE_CHANNELS
  // Free the channels these commands owned through withOwnState(). clearAllStateChannels()
  // leaves them alone, so this is the only place they are released.
  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    if (!_stateChannels[i].ownedByCommand)
      continue;

    // Each catalog is marked changed only if it held something.
    if (_stateChannels[i].inUse)
      _stateCatalogDirty = true;

    _stateChannels[i].inUse = false;
    _stateChannels[i].ownedByCommand = false;
    _stateChannels[i].icon = nullptr;
    _stateChannels[i].diagnostic = false;
    _setChannelName(_stateChannels[i].name, _stateChannels[i].nameInFlash, nullptr, nullptr);
  }
#endif

  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    if (_commandHandlers[i].inUse)
      _commandCatalogDirty = true;

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

void BlaeckCore::writeCommandState(const char *command)
{
#if !BLAECK_ENABLE_COMMAND_META || !BLAECK_ENABLE_STATE_CHANNELS
  (void)command;
#else
  if (command == nullptr)
    return;

  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.stateSource != BLAECK_STATE_CHANNEL || e.stateSignal == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // The channel exists unless the table was full, which was reported at registration.
    for (uint16_t c = 0; c < _stateChannelSlots(); c++)
    {
      if (!_stateChannels[c].inUse || !_stateChannels[c].ownedByCommand)
        continue;
      if (!_channelNameEqualsFlash(_stateChannels[c].name, _stateChannels[c].nameInFlash, e.stateSignal))
        continue;

      // Resolve the text as writeState(channelName) does: from a getter, a buffer, or a select's
      // index. An empty result would delete a retained value on the host.
      char optionBuf[BLAECK_STATE_MAX_OPTION_CHARS];
      _writeStateFrame(c, _channelText(_stateChannels[c], optionBuf, sizeof(optionBuf)));
      return;
    }
    return;
  }
#endif
}

#if BLAECK_ENABLE_COMMAND_META
// Adds a command's own channel. addStateChannel() refuses such names.
bool BlaeckCore::_addOwnedStateChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText,
                                        dataType valueType, const void *value)
{
#if !BLAECK_ENABLE_STATE_CHANNELS
  (void)channelName;
  (void)getStateText;
  (void)valueType;
  (void)value;
  return false;
#else
  if (channelName == nullptr)
    return false;

  // withOwnState() names are always F() literals, so the pointer is stored.
  if (pgm_read_byte(reinterpret_cast<PGM_P>(channelName)) == 0)
    return false;

  int existing = _findStateChannel(channelName);
  if (existing >= 0 && !_stateChannels[existing].ownedByCommand)
  {
    // The sketch already added this name. The command takes it over; say so, because the
    // addStateChannel() call no longer has any effect.
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel taken over by a command's own state; drop the addStateChannel() for: "));
      _debugStream->println(channelName);
    }
  }

  if (existing >= 0)
  {
    _stateChannels[existing].icon = nullptr;
    _stateChannels[existing].diagnostic = true;
    _stateChannels[existing].getStateText = getStateText;
    _stateChannels[existing].valueType = valueType;
    _stateChannels[existing].stateValue = value;
    _stateChannels[existing].ownedByCommand = true;
    return true;
  }

  if (!_ensureStateChannelTable())
  {
    _warnTableFull(F("withStateChannels"), _stateChannelCapacity, channelName);
    _rejectedStateChannelCount++;
    return false;
  }

  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    if (_stateChannels[i].inUse)
      continue;
    _setChannelName(_stateChannels[i].name, _stateChannels[i].nameInFlash, nullptr, channelName);
    _stateChannels[i].icon = nullptr;
    // Diagnostic, since the command's control already shows the value.
    _stateChannels[i].diagnostic = true;
    _stateChannels[i].getStateText = getStateText;
    _stateChannels[i].valueType = valueType;
    _stateChannels[i].stateValue = value;
    _stateChannels[i].truncationWarned = false;
    _stateChannels[i].ownedByCommand = true;
    _stateChannels[i].inUse = true;
    _stateCatalogDirty = true;
    return true;
  }

  _warnTableFull(F("withStateChannels"), _stateChannelCapacity, channelName);
  _rejectedStateChannelCount++;
  return false;
#endif
}

bool BlaeckCore::_declareOwnState(uint16_t handlerIndex, const __FlashStringHelper *channelName,
                                   BlaeckStateTextGetter getStateText)
{
  return _declareOwnState(handlerIndex, channelName, getStateText, Blaeck_string, nullptr);
}

bool BlaeckCore::_declareOwnState(uint16_t handlerIndex, const __FlashStringHelper *channelName,
                                   BlaeckStateTextGetter getStateText, dataType valueType,
                                   const void *value, bool selectIndex)
{
  // The value needs a source, a getter or a variable. Without one it would never be reported.
  if (channelName == nullptr || (getStateText == nullptr && value == nullptr))
    return false;

  if (!_addOwnedStateChannel(channelName, getStateText, valueType, value))
    return false;

  // A select's options go to its channel, so a host shows a list and an index can be turned
  // into a name.
#if BLAECK_ENABLE_STATE_CHANNELS
  const CommandHandlerEntry &cmd = _commandHandlers[handlerIndex];
  int ch = _findStateChannel(channelName);
  if (ch >= 0)
  {
    _stateChannels[ch].stateIsSelectIndex = selectIndex;
    // A host matches a switch's state against "1" and "0", so a getter's "ON" or "true" is
    // converted before it is sent.
    if (cmd.kind == BLAECK_CMD_SWITCH && getStateText != nullptr)
      _stateChannels[ch].stateIsSwitchBool = true;
    // A name from a getter or buffer is checked against the options.
    if (cmd.kind == BLAECK_CMD_SELECT && !selectIndex)
      _stateChannels[ch].stateIsSelectName = true;
    if (cmd.kind == BLAECK_CMD_SELECT && cmd.options != nullptr)
      _stateChannels[ch].options = cmd.options;
  }
#else
  // No channel was added, so there is nothing to give the options to.
  (void)handlerIndex;
  (void)selectIndex;
#endif

  return true;
}
#endif

BlaeckNumberCommandNeedsRange BlaeckCore::onNumberCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckNumberCommandNeedsRange(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_NUMBER));
}

BlaeckSwitchCommandRef BlaeckCore::onSwitchCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckSwitchCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_SWITCH));
}

BlaeckSelectCommandNeedsOptions BlaeckCore::onSelectCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckSelectCommandNeedsOptions(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_SELECT));
}

BlaeckButtonCommandRef BlaeckCore::onButtonCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckButtonCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_BUTTON));
}

BlaeckTextCommandRef BlaeckCore::onTextCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckTextCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_TEXT));
}

uint16_t BlaeckCore::_flashCsvOptionCount(const __FlashStringHelper *csv)
{
  if (csv == nullptr)
    return 0;
  PGM_P p = reinterpret_cast<PGM_P>(csv);
  uint16_t count = 1;
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

// True for a field that is empty or only spaces, which a host couldn't show or offer.
bool BlaeckCore::_flashCsvHasBlankField(const __FlashStringHelper *csv)
{
  if (csv == nullptr)
    return true;
  PGM_P p = reinterpret_cast<PGM_P>(csv);
  bool fieldHasContent = false;
  byte c;
  while ((c = pgm_read_byte(p++)) != 0)
  {
    if (c == ',')
    {
      if (!fieldHasContent)
        return true;
      fieldHasContent = false;
    }
    else if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
    {
      fieldHasContent = true;
    }
  }
  // The last field, and an empty string.
  return !fieldHasContent;
}

long BlaeckCore::getSelectOptionIndexOf(const char *command, const char *optionName) const
{
  if (command == nullptr || optionName == nullptr)
    return -1;

#if !BLAECK_ENABLE_COMMAND_META
  // No metadata, so no options to match.
  return -1;
#else
  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.kind != BLAECK_CMD_SELECT || e.options == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // The same match as an incoming command value.
    return _flashCsvIndexOf(e.options, optionName);
  }
  return -1;
#endif
}

bool BlaeckCore::getSelectOptionNameAt(const char *command, byte index, char *out, byte outSize) const
{
  if (out == nullptr || outSize == 0)
    return false;
  out[0] = '\0';
  if (command == nullptr)
    return false;

#if !BLAECK_ENABLE_COMMAND_META
  // No metadata, so no options to read.
  (void)index;
  return false;
#else
  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.kind != BLAECK_CMD_SELECT || e.options == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // Skip `index` commas, then copy up to the next one.
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
      // A shortened name would match no option, so fail instead.
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
long BlaeckCore::_flashCsvIndexOf(const __FlashStringHelper *csv, const char *value)
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
        // Case-sensitive, so options differing only in case stay distinct.
        if (*v == '\0' || (char)c != *v)
          matching = false;
        else
          v++;
      }
    }
  }
}
#endif

bool BlaeckCore::_receiveByte(Receiver &r, char rc)
{
  const char startMarker = '<';
  const char endMarker = '>';

  if (r.inProgress)
  {
    if (rc == startMarker)
    {
      // A second '<' abandons the unfinished command. Otherwise a command cut off mid-way would
      // swallow the next one.
      r.ndx = 0;
      r.overflowed = false;
    }
    else if (rc != endMarker)
    {
      r.chars[r.ndx] = rc;
      r.ndx++;
      if (r.ndx >= MAXIMUM_CHAR_COUNT)
      {
        // Buffer full: the rest of the command is dropped. Flag it now, because after parsing it
        // would look like a valid shorter command.
        r.ndx = MAXIMUM_CHAR_COUNT - 1;
        r.overflowed = true;
      }
    }
    else
    {
      r.chars[r.ndx] = '\0';
      r.inProgress = false;
      r.ndx = 0;
      return true;
    }
  }
  else if (rc == startMarker)
  {
    r.inProgress = true;
    r.overflowed = false;
  }
  return false;
}

bool BlaeckCore::_setChannelName(const char *&slot, bool &inFlash, const char *ram, const __FlashStringHelper *flash)
{
  // Free the copy the slot held; a flash name owns nothing.
  if (slot != nullptr && !inFlash)
    free((void *)slot);
  slot = nullptr;
  inFlash = false;

  if (flash != nullptr)
  {
    slot = reinterpret_cast<const char *>(flash);
    inFlash = true;
    return true;
  }
  if (ram == nullptr)
    return true;

  // Copied, so the caller's buffer is free the moment this returns.
  size_t needed = strlen(ram) + 1;
  char *copy = (char *)malloc(needed);
  if (copy != nullptr)
  {
    memcpy(copy, ram, needed);
    slot = copy;
    return true;
  }
  return false;
}

bool BlaeckCore::_channelNameEquals(const char *stored, bool inFlash, const char *candidate)
{
  if (stored == nullptr || candidate == nullptr)
    return false;
  if (!inFlash)
    return strcmp(stored, candidate) == 0;
  return equalsFlash(candidate, reinterpret_cast<const __FlashStringHelper *>(stored));
}

bool BlaeckCore::_channelNameEqualsFlash(const char *stored, bool inFlash, const __FlashStringHelper *candidate)
{
  if (stored == nullptr || candidate == nullptr)
    return false;
  if (!inFlash)
    return equalsFlash(stored, candidate);

  // Both in flash, so both are read with pgm_read_byte.
  PGM_P a = reinterpret_cast<PGM_P>(stored);
  PGM_P b = reinterpret_cast<PGM_P>(candidate);
  byte ca, cb;
  do
  {
    ca = pgm_read_byte(a++);
    cb = pgm_read_byte(b++);
    if (ca != cb)
      return false;
  } while (ca != 0);
  return true;
}

byte BlaeckCore::copyFlashName(const __FlashStringHelper *flash, char *out, byte outSize)
{
  if (out == nullptr || outSize == 0)
    return 0;

  byte len = 0;
  if (flash != nullptr)
  {
    PGM_P p = reinterpret_cast<PGM_P>(flash);
    byte c;
    while ((c = pgm_read_byte(p + len)) != 0 && len + 1 < outSize)
    {
      out[len] = (char)c;
      len++;
    }
  }
  // Always terminated, even for a null or overlong name.
  out[len] = '\0';
  return len;
}

char *BlaeckCore::toText(float value, byte decimals, char *out, byte outSize)
{
  if (out == nullptr || outSize == 0)
    return out;
  out[0] = '\0';

  // Said in words rather than digits, because no digits are right.
  const char *word = nullptr;
  if (isnan(value))
    word = "nan";
  else if (isinf(value))
    word = "inf";
  else if (value > 4294967040.0f || value < -4294967040.0f)
    word = "ovf"; // past what the unsigned long below can hold
  if (word != nullptr)
  {
    byte w = 0;
    while (word[w] != '\0' && w + 1 < outSize)
    {
      out[w] = word[w];
      w++;
    }
    out[w] = '\0';
    return out;
  }

  byte at = 0;
  if (value < 0.0f)
  {
    if (at + 1 < outSize)
      out[at++] = '-';
    value = -value;
  }

  // Round before splitting, so 9.999 at two decimals becomes 10.00, not 9.100.
  float rounding = 0.5f;
  for (byte i = 0; i < decimals; i++)
    rounding /= 10.0f;
  value += rounding;

  unsigned long whole = (unsigned long)value;
  float frac = value - (float)whole;

  // Digits come out lowest first, so they are written back to front.
  char digits[11];
  byte n = 0;
  do
  {
    digits[n++] = (char)('0' + (whole % 10UL));
    whole /= 10UL;
  } while (whole > 0UL && n < sizeof(digits));
  while (n > 0 && at + 1 < outSize)
    out[at++] = digits[--n];

  if (decimals > 0 && at + 1 < outSize)
    out[at++] = '.';
  for (byte i = 0; i < decimals && at + 1 < outSize; i++)
  {
    frac *= 10.0f;
    byte d = (byte)frac;
    out[at++] = (char)('0' + d);
    frac -= d;
  }
  out[at] = '\0';
  return out;
}

bool BlaeckCore::equalsFlash(const char *ram, const __FlashStringHelper *flash)
{
  if (ram == nullptr || flash == nullptr)
    return false;

  PGM_P p = reinterpret_cast<PGM_P>(flash);
  byte c;
  while ((c = pgm_read_byte(p++)) != 0)
  {
    if (*ram++ != (char)c)
      return false;
  }
  // Both ended together, or the RAM side is the longer of the two.
  return *ram == '\0';
}

void BlaeckCore::_parseCommandTokens(const char *raw)
{
  _parsedCommand[0] = '\0';
  _parsedParamCount = 0;
  _parsedPrefixMsgId = 0;
  _parsedPrefixLen = 0;
  // Characters were lost while receiving, so this is a fragment. Reset here, before the
  // empty-command check, so an empty command doesn't keep the previous verdict.
  _parsedTruncated = _receiver.overflowed;
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

  // Split on commas by hand, so empty fields between commas are kept.
  char *p = _parsedTokenBuffer;

  // The prefix: zero or more items before the command name, each starting with a sigil and
  // ending with ':'. Only '#' (message id) is understood. Anything else, including a malformed
  // item, stays part of the name, so the command doesn't match and is answered as unknown.
  while (*p == '#')
  {
    const char *scan = p + 1;
    uint32_t id = 0;
    byte digits = 0;
    while (*scan >= '0' && *scan <= '9' && digits < 5)
    {
      id = id * 10UL + (uint32_t)(*scan - '0');
      scan++;
      digits++;
    }
    // 0 means no id, so "#0:" is malformed.
    if (digits == 0 || *scan != ':' || id == 0 || id > 65535UL)
      break;
    _parsedPrefixMsgId = (uint16_t)id;
    p = (char *)scan + 1;
  }
  // The ack hashes the command after the prefix, as its sender wrote it.
  _parsedPrefixLen = (byte)(p - _parsedTokenBuffer);

  // The command name is everything before the first comma.
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
  strncpy(_parsedCommand, tokenStart, MAX_PARSED_COMMAND_COUNT - 1);
  _parsedCommand[MAX_PARSED_COMMAND_COUNT - 1] = '\0';

  if (!hasComma)
    return;

  // Parameters. An empty field (,,) gives a pointer to an empty string.
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

  // Out of parameter slots but more commas follow: the list was cut short.
  if (moreParams)
    _parsedTruncated = true;
}

void BlaeckCore::_dispatchRegisteredHandlers(bool sendAck)
{
  if (_parsedCommand[0] == '\0')
  {
    return;
  }

  // read() rejects a truncated command before this is reached.

  byte ackStatus = 1;                  // 0 = accepted, 1 = rejected
  byte ackReason = BLAECK_ACK_UNKNOWN; // reason reported when rejected
  bool matched = false;

  for (uint16_t i = 0; i < _commandSlots(); i++)
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
      // Handled by onAnyCommand(), so accepted.
      matched = true;
      ackStatus = 0;
      ackReason = BLAECK_ACK_OK;
    }
  }

  // Every command gets one ack. A matched built-in was acknowledged in read() and passes
  // sendAck false; an unknown BLAECK.* name arrives with sendAck true and is answered UNKNOWN.
  if (sendAck)
  {
    _writeCommandAck(_receiver.chars, ackStatus, ackReason);
  }
}

uint32_t BlaeckCore::_fnv1a32(const char *s)
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

void BlaeckCore::_writeCommandAck(const char *rawCommand, byte status, byte reasonCode)
{
  if (!_mayWriteFrame())
    return;

  // The name hash still identifies a command that didn't arrive whole, since the name comes
  // before the first comma.
  uint32_t nameHash = (_parsedCommand[0] == '\0') ? 0UL : _fnv1a32(_parsedCommand);

  // Hash what follows the prefix. The length check can't fail after the parse above, but guards
  // against reading past the end.
  const char *payload = rawCommand;
  if (payload != nullptr && _parsedPrefixLen > 0 && strlen(payload) >= _parsedPrefixLen)
    payload += _parsedPrefixLen;

  // The ack carries the message id from the command's prefix (0 if none), so a host can tell
  // which of two same-named commands it answers.
  uint32_t ackMsgId = (uint32_t)_parsedPrefixMsgId;

  if (!_frameOpen(0xA5, ackMsgId, false, AUDIENCE_REQUESTER))
    return;
  // Command hash (4 bytes, little-endian), name hash (4), status (1), reason (1).
  ulngCvt.val = _fnv1a32(payload);
  _emitBytes(ulngCvt.bval, 4);
  ulngCvt.val = nameHash;
  _emitBytes(ulngCvt.bval, 4);
  _emitByte(status);
  _emitByte(reasonCode);
  _frameClose();
}

#if BLAECK_ENABLE_STATE_CHANNELS
int BlaeckCore::_registerStateChannel(const char *channelName, const __FlashStringHelper *flashName,
                                         dataType valueType, const void *value)
{
  // Only a RAM name is copied, so only it can be too long.
  char probe[2];
  bool emptyFlash = flashName != nullptr && copyFlashName(flashName, probe, sizeof(probe)) == 0;
  if ((channelName == nullptr && flashName == nullptr) || emptyFlash ||
      (channelName != nullptr && channelName[0] == '\0'))
  {
    _rejectedStateChannelCount++;
    return -1;
  }

  // A command's own channel takes its value only from the command.
  int owned = flashName != nullptr ? _findStateChannel(flashName) : _findStateChannel(channelName);
  if (owned >= 0 && _stateChannels[owned].ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel belongs to a command's own state and cannot be redeclared: "));
      _debugStream->println(channelName);
    }
    _rejectedStateChannelCount++;
    return -1;
  }

  if (channelName != nullptr && strlen(channelName) >= MAX_STATE_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for state channel table: "));
      _debugStream->println(channelName);
    }
    _rejectedStateChannelCount++;
    return -1;
  }

  // Declaring an existing name reuses its slot with the metadata cleared.
  int existing = flashName != nullptr ? _findStateChannel(flashName) : _findStateChannel(channelName);
  if (existing >= 0)
  {
    _stateChannels[existing].icon = nullptr;
    _stateChannels[existing].diagnostic = false;
    _stateChannels[existing].deviceClass = nullptr;
    _stateChannels[existing].options = nullptr;
    _stateChannels[existing].disabledByDefault = false;
    _stateChannels[existing].forceUpdate = false;
    _stateChannels[existing].getStateText = nullptr;
    _stateChannels[existing].unit = nullptr;
    _stateChannels[existing].metaFlags = 0;
    _stateChannels[existing].displayPrecision = 0;
    _stateChannels[existing].valueType = valueType;
    _stateChannels[existing].stateValue = value;
    _stateCatalogDirty = true;
    return existing;
  }

  if (!_ensureStateChannelTable())
  {
    if (flashName != nullptr)
      _warnTableFull(F("withStateChannels"), _stateChannelCapacity, flashName);
    else
      _warnTableFull(F("withStateChannels"), _stateChannelCapacity, channelName);
    _rejectedStateChannelCount++;
    return -1;
  }

  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    if (!_stateChannels[i].inUse)
    {
      if (!_setChannelName(_stateChannels[i].name, _stateChannels[i].nameInFlash, channelName, flashName))
      {
        if (_debugStream != nullptr)
        {
          _debugStream->print(F("State channel name allocation failed: "));
          _debugStream->println(channelName);
        }
        _rejectedStateChannelCount++;
        return -1;
      }
      _stateChannels[i].icon = nullptr;
      _stateChannels[i].diagnostic = false;
      _stateChannels[i].deviceClass = nullptr;
      _stateChannels[i].options = nullptr;
      _stateChannels[i].disabledByDefault = false;
      _stateChannels[i].forceUpdate = false;
      _stateChannels[i].getStateText = nullptr;
      _stateChannels[i].unit = nullptr;
      _stateChannels[i].metaFlags = 0;
      _stateChannels[i].displayPrecision = 0;
      _stateChannels[i].truncationWarned = false;
      _stateChannels[i].valueType = valueType;
      _stateChannels[i].stateValue = value;
      _stateChannels[i].inUse = true;
      _stateCatalogDirty = true;
      return (int)i;
    }
  }

  _warnTableFull(F("withStateChannels"), _stateChannelCapacity, channelName);
  _rejectedStateChannelCount++;
  return -1;
}

BlaeckTextStateRef BlaeckCore::addStateChannel(const char *channelName, BlaeckTextTag)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr));
}

BlaeckBoolStateRef BlaeckCore::addStateChannel(const char *channelName, BlaeckBoolTag)
{
  return BlaeckBoolStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_bool, nullptr));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, BlaeckNumericTag type)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, _tagType(type), nullptr));
}

BlaeckTextStateRef BlaeckCore::addStateChannel(const char *channelName, const char *value)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_string, value));
}

BlaeckBoolStateRef BlaeckCore::addStateChannel(const char *channelName, bool *value)
{
  return BlaeckBoolStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_bool, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, byte *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_byte, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_short, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, unsigned short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_ushort, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, int *value)
{
#ifdef __AVR__
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_int, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_long, value));
#endif
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, unsigned int *value)
{
#ifdef __AVR__
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_uint, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_ulong, value));
#endif
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_long, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, unsigned long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_ulong, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, float *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_float, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *channelName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision. Registering it as a
  double would have the value writer copy 8 bytes out of a 4-byte union member, sending
  four bytes of stale memory as the top half of the number.*/
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_float, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, nullptr, Blaeck_double, value));
#endif
}

bool BlaeckCore::_flashStringEqualsName(const __FlashStringHelper *flashName, const char *name)
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

void BlaeckCore::clearAllStateChannels()
{
  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    // Channels a command owns stay; clearAllCommandHandlers() removes them with their commands.
    if (_stateChannels[i].ownedByCommand)
      continue;

    // Only a slot that held something changes the catalog.
    if (_stateChannels[i].inUse)
      _stateCatalogDirty = true;

    _stateChannels[i].inUse = false;
    _stateChannels[i].icon = nullptr;
    _stateChannels[i].diagnostic = false;
    _setChannelName(_stateChannels[i].name, _stateChannels[i].nameInFlash, nullptr, nullptr);
  }
}

int BlaeckCore::_findStateChannel(const __FlashStringHelper *channelName) const
{
  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    if (_stateChannels[i].inUse &&
        _channelNameEqualsFlash(_stateChannels[i].name, _stateChannels[i].nameInFlash, channelName))
      return i;
  }
  return -1;
}

int BlaeckCore::_findStateChannel(const char *channelName) const
{
  if (channelName == nullptr || channelName[0] == '\0')
    return -1;

  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    if (_stateChannels[i].inUse && _channelNameEquals(_stateChannels[i].name, _stateChannels[i].nameInFlash, channelName))
      return (int)i;
  }
  return -1;
}

void BlaeckCore::writeState(const char *channelName, const char *text)
{
  int channelIndex = _stateChannelForPush(channelName, true);
  if (channelIndex < 0)
    return;

  // A text channel points at the sketch's own buffer. Don't repoint it at the caller's text,
  // which may not outlive the call.
  if (_stateChannels[channelIndex].stateValue != nullptr)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel reads its own buffer; use writeState(channelName) for: "));
      _debugStream->println(channelName);
    }
    return;
  }

  _writeStateFrame(channelIndex, text);
}

// Sends the channel's current value, read from its variable or getter. The only way to push
// a channel that has a variable.
void BlaeckCore::writeState(const char *channelName)
{
  if (!_mayWriteFrame())
    return;

  int channelIndex = _findStateChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel not declared with addStateChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    return;
  }

  if (_stateChannels[channelIndex].ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel belongs to a command's own state; use writeCommandState() for: "));
      _debugStream->println(channelName);
    }
    return;
  }

  const StateChannelEntry &e = _stateChannels[channelIndex];
  char optionBuf[BLAECK_STATE_MAX_OPTION_CHARS];
  const char *text = _channelText(e, optionBuf, sizeof(optionBuf));
  _writeStateFrame(channelIndex, text);
}

// Finds the channel for a writeState() push, or returns -1 with a note on the debug stream.
// Shared by the text and number forms. A channel with a getter is refused, since the getter
// would replace the pushed value; a channel with a variable takes the value into it.
int BlaeckCore::_stateChannelForPush(const char *channelName, bool wantText)
{
  if (!_transportReady())
    return -1;

  int channelIndex = _findStateChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel not declared with addStateChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    return -1;
  }

  const StateChannelEntry &e = _stateChannels[channelIndex];
  if (e.ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel belongs to a command's own state; use writeCommandState() for: "));
      _debugStream->println(channelName);
    }
    return -1;
  }

  const bool isText = (e.valueType == Blaeck_string);
  if (isText != wantText)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(isText ? F("State dropped, channel carries text; use writeState(channelName, text) for: ")
                                 : F("State dropped, channel carries a number; use writeState(channelName, value) for: "));
      _debugStream->println(channelName);
    }
    return -1;
  }

  const bool hasGetter = isText ? (e.getStateText != nullptr) : (e.getNumber != nullptr);
  if (hasGetter)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel works its value out for itself; use writeState(channelName) for: "));
      _debugStream->println(channelName);
    }
    return -1;
  }

  return channelIndex;
}

void BlaeckCore::_writeStateNumber(const char *channelName, long s, unsigned long u, double d)
{
  int channelIndex = _stateChannelForPush(channelName, false);
  if (channelIndex < 0)
    return;

  const StateChannelEntry &e = _stateChannels[channelIndex];
  byte pushed[8];
  byte len = _valueBytes(e.valueType, s, u, d, pushed);

  // Store into the channel's variable, so the value stays when the catalog is next read.
  if (e.stateValue != nullptr && len > 0)
    memcpy(const_cast<void *>(e.stateValue), pushed, len);

  _writeStateFrame(channelIndex, nullptr, pushed, len);
}

void BlaeckCore::writeState(const char *channelName, bool value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, byte value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, short value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, unsigned short value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, int value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, unsigned int value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, long value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, unsigned long value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, float value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::writeState(const char *channelName, double value)
{
  _writeStateNumber(channelName, (long)value, (unsigned long)value, (double)value);
}

void BlaeckCore::_writeStateFrame(int channelIndex, const char *text, const byte *pushed, byte pushedLen)
{
  if (!_mayWriteFrame())
    return;

  // Send changed catalogs first, so the index refers to the list the host has.
  _flushCatalogs();

  // The value comes from the channel's variable or getter, or, for a channel added with a tag,
  // from the bytes writeState() passed in.
  StateChannelEntry &e = _stateChannels[channelIndex];
  byte valueBytes[8];
  byte valueLen = pushedLen;
  if (pushed != nullptr)
    memcpy(valueBytes, pushed, pushedLen);
  else
    valueLen = _channelValueBytes(e, valueBytes);

  // No value, so send nothing: the frame can't express "none", and an empty text would delete
  // a retained value on the host. An empty string is still sent, since that is a deliberate
  // clear.
  if (valueLen == 0 && text == nullptr)
    return;

  if (text == nullptr)
    text = "";

  // Capped at 255 bytes, like a string signal.
  size_t rawLen = strlen(text);
  byte len = (rawLen > 255) ? (byte)255 : (byte)rawLen;
  if (rawLen > 255 && !e.truncationWarned)
  {
    e.truncationWarned = true;
    _debugChannel(F("State text truncated to 255 bytes on channel: "), e);
    if (_debugStream != nullptr)
      _debugStream->println();
  }

  if (!_frameOpen(0x95, 0))
    return;
  // Layout: State (0x95) in the protocol spec.
  _emitByte((byte)0);
  _emitByte((byte)0);
  _emitByte((byte)(channelIndex & 0xFF));
  _emitByte((byte)((channelIndex >> 8) & 0xFF));
  _emitByte(_dtypeCode(e.valueType));
  if (valueLen > 0)
  {
    _emitBytes(valueBytes, valueLen);
  }
  else
  {
    _emitByte(len);
    if (len > 0)
      _emitBytes((const byte *)text, len);
  }
  _frameClose();
}

void BlaeckCore::writeStateChannels()
{
  this->writeStateChannels(0);
}

void BlaeckCore::writeStateChannels(unsigned long msg_id)
{
  this->writeStateChannelsFrame(msg_id);
}

#if BLAECK_ENABLE_STATE_CHANNELS
void BlaeckCore::_debugChannel(const __FlashStringHelper *prefix, const StateChannelEntry &e) const
{
  if (_debugStream == nullptr)
    return;
  _debugStream->print(prefix);
  if (e.nameInFlash)
    _debugStream->print(reinterpret_cast<const __FlashStringHelper *>(e.name));
  else
    _debugStream->print(e.name);
}

const char *BlaeckCore::_checkedSelectName(const StateChannelEntry &e, const char *text) const
{
  // No value yet is fine; pass it on as none.
  if (text == nullptr || text[0] == '\0')
    return text;

#if BLAECK_ENABLE_COMMAND_META
  if (e.options == nullptr)
    return text;

  // The same match used for incoming command values.
  if (_flashCsvIndexOf(e.options, text) >= 0)
    return text;

  // A host ignores a name that isn't an option and keeps showing the old one, so warn.
  if (!e.stateWarned)
  {
    e.stateWarned = true;
    _debugChannel(F("Select state is not a declared option on channel: "), e);
    if (_debugStream != nullptr)
    {
      _debugStream->print(F(" returned \""));
      _debugStream->print(text);
      _debugStream->print(F("\", allowed ["));
      _debugStream->print(e.options);
      _debugStream->println(F("]. Nothing is reported and the control keeps its last value."));
    }
  }
  return nullptr;
#else
  return text;
#endif
}

const char *BlaeckCore::_channelText(const StateChannelEntry &e, char *buf, byte bufSize) const
{
  // Check valueType first: getStateText shares storage with getNumber, so on a numeric channel
  // it holds a getter of a different type.
  if (e.valueType == Blaeck_string && e.getStateText != nullptr)
  {
    const char *t = e.getStateText();
    if (e.stateIsSelectName)
      return _checkedSelectName(e, t);
    if (!e.stateIsSwitchBool)
      return t;

    const char *canonical = blaeck_detail::switchStateText(t);
    // Not a recognised on/off spelling. Report nothing, and warn once.
    if (canonical == nullptr && t != nullptr && t[0] != '\0' && !e.stateWarned)
    {
      e.stateWarned = true;
      _debugChannel(F("Switch state not recognised on channel: "), e);
      if (_debugStream != nullptr)
      {
        _debugStream->print(F(" returned \""));
        _debugStream->print(t);
        _debugStream->println(F("\". Expected 1/on/true/yes or 0/off/false/no. "
                                "Nothing is reported and the control stays unknown."));
      }
    }
    return canonical;
  }

  // Only text channels keep text here.
  if (e.valueType != Blaeck_string || e.stateValue == nullptr)
    return nullptr;

  if (!e.stateIsSelectIndex)
  {
    const char *t = (const char *)e.stateValue;
    return e.stateIsSelectName ? _checkedSelectName(e, t) : t;
  }
  if (e.options == nullptr || buf == nullptr || bufSize == 0)
    return nullptr;

  // Find the index'th option, as getSelectOptionNameAt() does.
  byte index = *((const byte *)e.stateValue);
  PGM_P p = reinterpret_cast<PGM_P>(e.options);
  byte seen = 0;
  unsigned int at = 0;
  while (seen < index)
  {
    byte c = pgm_read_byte(p + at);
    if (c == 0)
      return nullptr; // fewer options than the index asked for
    if (c == ',')
      seen++;
    at++;
  }
  byte len = 0;
  byte c;
  while ((c = pgm_read_byte(p + at + len)) != 0 && c != ',')
  {
    // Too long for the buffer. A shortened name would match no option, so report nothing.
    if ((unsigned int)len + 1 >= bufSize)
    {
      if (!e.stateWarned)
      {
        e.stateWarned = true;
        _debugChannel(F("Select option too long to report on channel: "), e);
        if (_debugStream != nullptr)
        {
          _debugStream->print(F(" needs more than "));
          _debugStream->print((unsigned int)bufSize - 1);
          _debugStream->println(F(" characters. Nothing is reported; raise "
                                  "BLAECK_STATE_MAX_OPTION_CHARS or shorten the option."));
        }
      }
      return nullptr;
    }
    buf[len] = (char)c;
    len++;
  }
  buf[len] = '\0';
  return len > 0 ? buf : nullptr;
}
#endif

#if BLAECK_ENABLE_STATE_CHANNELS
// A pushed number converted to the channel's type, as bytes. The caller passes the value cast
// three ways and the switch picks the one that fits; one switch keeps the flash cost down.
byte BlaeckCore::_valueBytes(dataType declared, long s, unsigned long u, double d, byte *out)
{
  switch (declared)
  {
  case (Blaeck_bool):   boolCvt.val   = (s != 0);                memcpy(out, boolCvt.bval, 1);   return 1;
  case (Blaeck_byte):   out[0]        = (byte)u;                                                 return 1;
  case (Blaeck_short):  shortCvt.val  = (short)s;                memcpy(out, shortCvt.bval, 2);  return 2;
  case (Blaeck_ushort): ushortCvt.val = (unsigned short)u;       memcpy(out, ushortCvt.bval, 2); return 2;
  case (Blaeck_int):    intCvt.val    = (int)s;                  memcpy(out, intCvt.bval, 2);    return 2;
  case (Blaeck_uint):   uintCvt.val   = (unsigned int)u;         memcpy(out, uintCvt.bval, 2);   return 2;
  case (Blaeck_long):   lngCvt.val    = s;                       memcpy(out, lngCvt.bval, 4);    return 4;
  case (Blaeck_ulong):  ulngCvt.val   = u;                       memcpy(out, ulngCvt.bval, 4);   return 4;
  case (Blaeck_float):  fltCvt.val    = (float)d;                memcpy(out, fltCvt.bval, 4);    return 4;
  case (Blaeck_double): dblCvt.val    = d;                       memcpy(out, dblCvt.bval, 8);    return 8;
  default:                                                                                       return 0;
  }
}

byte BlaeckCore::_channelValueBytes(const StateChannelEntry &e, byte *out)
{
  // A getter takes priority over a variable. withStateValue() ensured its type matches.
  if (e.getNumber != nullptr && e.valueType != Blaeck_string)
  {
    switch (e.valueType)
    {
    case (Blaeck_bool):   boolCvt.val   = ((BlaeckStateBoolGetter)e.getNumber)();   memcpy(out, boolCvt.bval, 1);   return 1;
    case (Blaeck_byte):   out[0]        = ((BlaeckStateByteGetter)e.getNumber)();                                   return 1;
    case (Blaeck_short):  shortCvt.val  = ((BlaeckStateShortGetter)e.getNumber)();  memcpy(out, shortCvt.bval, 2);  return 2;
    case (Blaeck_ushort): ushortCvt.val = ((BlaeckStateUShortGetter)e.getNumber)(); memcpy(out, ushortCvt.bval, 2); return 2;
    case (Blaeck_int):    intCvt.val    = ((BlaeckStateIntGetter)e.getNumber)();    memcpy(out, intCvt.bval, 2);    return 2;
    case (Blaeck_uint):   uintCvt.val   = ((BlaeckStateUIntGetter)e.getNumber)();   memcpy(out, uintCvt.bval, 2);   return 2;
    case (Blaeck_long):   lngCvt.val    = ((BlaeckStateLongGetter)e.getNumber)();   memcpy(out, lngCvt.bval, 4);    return 4;
    case (Blaeck_ulong):  ulngCvt.val   = ((BlaeckStateULongGetter)e.getNumber)();  memcpy(out, ulngCvt.bval, 4);   return 4;
    case (Blaeck_float):  fltCvt.val    = ((BlaeckStateFloatGetter)e.getNumber)();  memcpy(out, fltCvt.bval, 4);    return 4;
    case (Blaeck_double): dblCvt.val    = ((BlaeckStateDoubleGetter)e.getNumber)(); memcpy(out, dblCvt.bval, 8);    return 8;
    default:                                                                                                        return 0;
    }
  }

  if (e.stateValue == nullptr)
    return 0;

  switch (e.valueType)
  {
  case (Blaeck_bool):   boolCvt.val   = *((const bool *)e.stateValue);           memcpy(out, boolCvt.bval, 1);   return 1;
  case (Blaeck_byte):   out[0]        = *((const byte *)e.stateValue);                                           return 1;
  case (Blaeck_short):  shortCvt.val  = *((const short *)e.stateValue);          memcpy(out, shortCvt.bval, 2);  return 2;
  case (Blaeck_ushort): ushortCvt.val = *((const unsigned short *)e.stateValue); memcpy(out, ushortCvt.bval, 2); return 2;
  case (Blaeck_int):    intCvt.val    = *((const int *)e.stateValue);            memcpy(out, intCvt.bval, 2);    return 2;
  case (Blaeck_uint):   uintCvt.val   = *((const unsigned int *)e.stateValue);   memcpy(out, uintCvt.bval, 2);   return 2;
  case (Blaeck_long):   lngCvt.val    = *((const long *)e.stateValue);           memcpy(out, lngCvt.bval, 4);    return 4;
  case (Blaeck_ulong):  ulngCvt.val   = *((const unsigned long *)e.stateValue);  memcpy(out, ulngCvt.bval, 4);   return 4;
  case (Blaeck_float):  fltCvt.val    = *((const float *)e.stateValue);          memcpy(out, fltCvt.bval, 4);    return 4;
  case (Blaeck_double): dblCvt.val    = *((const double *)e.stateValue);         memcpy(out, dblCvt.bval, 8);    return 8;
  default:                                                                                                       return 0;
  }
}
#endif

uint16_t BlaeckCore::_stateChannelFlags(const StateChannelEntry &e, bool hasStateValue) const
{
  uint16_t flags = 0;
  if (e.icon != nullptr)
    flags |= BLAECK_SCH_HAS_ICON;
  if (e.diagnostic)
    flags |= BLAECK_SCH_DIAGNOSTIC;
  if (hasStateValue)
    flags |= BLAECK_SCH_HAS_STATE_VALUE;
  if (e.deviceClass != nullptr)
    flags |= BLAECK_SCH_HAS_DEVICE_CLASS;
  if (e.disabledByDefault)
    flags |= BLAECK_SCH_DISABLED_BY_DEFAULT;
  if (e.forceUpdate)
    flags |= BLAECK_SCH_FORCE_UPDATE;
  if (e.options != nullptr)
    flags |= BLAECK_SCH_HAS_OPTIONS;
  if (e.unit != nullptr)
    flags |= BLAECK_SCH_HAS_UNIT;
  // State class and display precision are kept in metaFlags already.
  flags |= (uint16_t)(e.metaFlags & (BLAECK_SCH_STATE_CLASS_MASK | BLAECK_SCH_HAS_DISPLAY_PRECISION));
  return flags;
}

void BlaeckCore::writeStateChannelsFrame(unsigned long msg_id)
{
  // The host is about to have the current list.
  _stateCatalogDirty = false;

  // Layout: State Channel List (0x90) in the protocol spec. Values come from each channel's
  // variable or getter as the frame is built.
  //
  // _channelText() can warn, and with buffered writes off a warning printed during the frame
  // would land inside it if the debug stream is the same port. Each warning prints only once,
  // so calling it first gets them out before the frame starts.
  if (_debugStream != nullptr && !_bufferedWrites)
  {
    for (uint16_t i = 0; i < _stateChannelSlots(); i++)
    {
      if (!_stateChannels[i].inUse)
        continue;
      char optionBuf[BLAECK_STATE_MAX_OPTION_CHARS];
      _channelText(_stateChannels[i], optionBuf, sizeof(optionBuf));
    }
  }

  if (!_frameOpen(0x90, msg_id))
    return;

  for (uint16_t i = 0; i < _stateChannelSlots(); i++)
  {
    StateChannelEntry &e = _stateChannels[i];
    if (!e.inUse)
      continue;

    // Asked once: calling the getter twice could give two different answers.
    char optionBuf[BLAECK_STATE_MAX_OPTION_CHARS];
    const char *stateText = _channelText(e, optionBuf, sizeof(optionBuf));
    byte valueBytes[8];
    byte valueLen = _channelValueBytes(e, valueBytes);

    uint16_t flags = _stateChannelFlags(e, stateText != nullptr || valueLen > 0);

    _emitByte((byte)0);
    _emitByte((byte)0);
    if (e.nameInFlash)
      _emitFlashStr0(reinterpret_cast<const __FlashStringHelper *>(e.name));
    else
      _emitStr0(e.name);
    _emitByte((byte)(flags & 0xFF));
    _emitByte((byte)((flags >> 8) & 0xFF));
    _emitByte(_dtypeCode(e.valueType));

    if (flags & BLAECK_SCH_HAS_ICON)
      _emitFlashStr0(e.icon);
    // Text ends with a terminator; a number has its type's fixed width.
    if (flags & BLAECK_SCH_HAS_STATE_VALUE)
    {
      if (valueLen > 0)
        _emitBytes(valueBytes, valueLen);
      else
        _emitStr0(stateText);
    }
    if (flags & BLAECK_SCH_HAS_DEVICE_CLASS)
      _emitFlashStr0(e.deviceClass);
    if (flags & BLAECK_SCH_HAS_OPTIONS)
      _emitFlashStr0(e.options);
    if (flags & BLAECK_SCH_HAS_UNIT)
      _emitFlashStr0(e.unit);
    if (flags & BLAECK_SCH_HAS_DISPLAY_PRECISION)
      _emitByte(e.displayPrecision);
  }

  _frameClose();
}
#else
// BLAECK_ENABLE_STATE_CHANNELS=0: the API compiles but stores nothing, and the catalog
// answers empty.
BlaeckTextStateRef BlaeckCore::addStateChannel(const char *, BlaeckTextTag) { return BlaeckTextStateRef(this, -1); }
BlaeckBoolStateRef BlaeckCore::addStateChannel(const char *, BlaeckBoolTag) { return BlaeckBoolStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, BlaeckNumericTag) { return BlaeckNumericStateRef(this, -1); }
BlaeckTextStateRef BlaeckCore::addStateChannel(const char *, const char *) { return BlaeckTextStateRef(this, -1); }
BlaeckBoolStateRef BlaeckCore::addStateChannel(const char *, bool *) { return BlaeckBoolStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, byte *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, short *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, unsigned short *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, int *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, unsigned int *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, long *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, unsigned long *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, float *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckCore::addStateChannel(const char *, double *) { return BlaeckNumericStateRef(this, -1); }
void BlaeckCore::clearAllStateChannels() {}
// Used by the F() addStateChannel() overloads, which exist either way.
int BlaeckCore::_registerStateChannel(const char *, const __FlashStringHelper *, dataType, const void *) { return -1; }
void BlaeckCore::writeStateChannels() { this->writeStateChannels(0); }
void BlaeckCore::writeStateChannels(unsigned long msg_id) { this->_writeEmptyFrame(0x90, msg_id); }
void BlaeckCore::writeState(const char *, const char *) {}
void BlaeckCore::writeState(const char *) {}
void BlaeckCore::writeState(const char *, bool) {}
void BlaeckCore::writeState(const char *, byte) {}
void BlaeckCore::writeState(const char *, short) {}
void BlaeckCore::writeState(const char *, unsigned short) {}
void BlaeckCore::writeState(const char *, int) {}
void BlaeckCore::writeState(const char *, unsigned int) {}
void BlaeckCore::writeState(const char *, long) {}
void BlaeckCore::writeState(const char *, unsigned long) {}
void BlaeckCore::writeState(const char *, float) {}
void BlaeckCore::writeState(const char *, double) {}
#endif

#if BLAECK_ENABLE_EVENTS
int BlaeckCore::_registerEventChannel(const char *channelName, const __FlashStringHelper *flashName, const __FlashStringHelper *eventTypes)
{
  char probe[2];
  bool emptyFlash = flashName != nullptr && copyFlashName(flashName, probe, sizeof(probe)) == 0;
  if ((channelName == nullptr && flashName == nullptr) || emptyFlash ||
      (channelName != nullptr && channelName[0] == '\0'))
  {
    _rejectedEventChannelCount++;
    return -1;
  }

  // A channel needs at least one event type, and none may be blank.
  if (eventTypes == nullptr || _flashCsvOptionCount(eventTypes) == 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event channel needs at least one event type: "));
      _debugStream->println(channelName);
    }
    _rejectedEventChannelCount++;
    return -1;
  }

  if (_flashCsvHasBlankField(eventTypes))
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event channel has a blank event type: "));
      _debugStream->println(channelName);
    }
    _rejectedEventChannelCount++;
    return -1;
  }

  if (channelName != nullptr && strlen(channelName) >= MAX_EVENT_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for event channel table: "));
      _debugStream->println(channelName);
    }
    _rejectedEventChannelCount++;
    return -1;
  }

  // Declaring an existing name reuses its slot with the metadata cleared. Its event types are
  // kept, so their indices don't change.
  int existing = flashName != nullptr ? _findEventChannel(flashName) : _findEventChannel(channelName);
  if (existing >= 0)
  {
    _eventChannels[existing].icon = nullptr;
    _eventChannels[existing].deviceClass = nullptr;
    _eventChannels[existing].diagnostic = false;
    _eventChannels[existing].disabledByDefault = false;
    _eventCatalogDirty = true;
    return existing;
  }

  if (!_ensureEventChannelTable())
  {
    if (flashName != nullptr)
      _warnTableFull(F("withEventChannels"), _eventChannelCapacity, flashName);
    else
      _warnTableFull(F("withEventChannels"), _eventChannelCapacity, channelName);
    _rejectedEventChannelCount++;
    return -1;
  }

  for (uint16_t i = 0; i < _eventChannelSlots(); i++)
  {
    if (!_eventChannels[i].inUse)
    {
      if (!_setChannelName(_eventChannels[i].name, _eventChannels[i].nameInFlash, channelName, flashName))
      {
        if (_debugStream != nullptr)
        {
          _debugStream->print(F("Event channel name allocation failed: "));
          _debugStream->println(channelName);
        }
        _rejectedEventChannelCount++;
        return -1;
      }
      _eventChannels[i].icon = nullptr;
      _eventChannels[i].deviceClass = nullptr;
      _eventChannels[i].diagnostic = false;
      _eventChannels[i].disabledByDefault = false;
      _eventChannels[i].inUse = true;
      _addEventTypesCsv(i, eventTypes);
      _eventCatalogDirty = true;
      return (int)i;
    }
  }

  _warnTableFull(F("withEventChannels"), _eventChannelCapacity, channelName);
  _rejectedEventChannelCount++;
  return -1;
}

BlaeckEventChannelRef BlaeckCore::addEventChannel(const char *channelName, const __FlashStringHelper *eventTypes)
{
  return BlaeckEventChannelRef(this, (int16_t)_registerEventChannel(channelName, nullptr, eventTypes));
}

void BlaeckCore::_addEventTypesCsv(uint16_t channelIndex, const __FlashStringHelper *eventTypes)
{
  // One entry per field, all pointing at the same string, in order.
  uint16_t fieldCount = _flashCsvOptionCount(eventTypes);
  for (uint16_t f = 0; f < fieldCount; f++)
  {
    if (!_ensureEventTypeTable() || _eventTypeCount >= _eventTypeSlots())
    {
      if (_eventChannels[channelIndex].nameInFlash)
        _warnTableFull(F("withEventTypes"), _eventTypeCapacity,
                       reinterpret_cast<const __FlashStringHelper *>(_eventChannels[channelIndex].name));
      else
        _warnTableFull(F("withEventTypes"), _eventTypeCapacity,
                       _eventChannels[channelIndex].name);
      // The remaining fields are dropped too.
      _rejectedEventTypeCount += (uint16_t)(fieldCount - f);
      break;
    }
    _eventTypes[_eventTypeCount].channelIndex = channelIndex;
    _eventTypes[_eventTypeCount].text = eventTypes;
    _eventTypes[_eventTypeCount].field = f;
    _eventTypeCount++;
  }
}

void BlaeckCore::_eventTypeExtent(const EventTypeEntry &e, unsigned int &start, unsigned int &len)
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

  // Skip `field` commas, then measure to the next comma or the end.
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

void BlaeckCore::_emitEventType0(const EventTypeEntry &e)
{
  unsigned int start, len;
  _eventTypeExtent(e, start, len);
  PGM_P p = reinterpret_cast<PGM_P>(e.text) + start;
  for (unsigned int i = 0; i < len; i++)
    _emitByte(pgm_read_byte(p++));
  _emitByte(0);
}

bool BlaeckCore::_eventTypeEquals(const EventTypeEntry &e, const __FlashStringHelper *eventType)
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
  // Equal only if eventType ends where the field does.
  return pgm_read_byte(b + len) == 0;
}

bool BlaeckCore::addEventType(const char *channelName, const __FlashStringHelper *eventType)
{
  if (eventType == nullptr)
    return false;

  // A blank type is refused, as in addEventChannel().
  if (_flashCsvHasBlankField(eventType))
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Blank event type dropped on channel: "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    _rejectedEventTypeCount++;
    return false;
  }

  int channelIndex = _findEventChannel(channelName);
  if (channelIndex < 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Event type dropped, channel not declared with addEventChannel(): "));
      _debugStream->println(channelName != nullptr ? channelName : "");
    }
    _rejectedEventTypeCount++;
    return false;
  }

  // A duplicate could never be reported: writeEvent() would always find the first.
  if (_findEventType((byte)channelIndex, eventType) >= 0)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Duplicate event type ignored on channel: "));
      _debugStream->println(channelName);
    }
    return false;
  }

  if (!_ensureEventTypeTable() || _eventTypeCount >= _eventTypeSlots())
  {
    _warnTableFull(F("withEventTypes"), _eventTypeCapacity, channelName);
    _rejectedEventTypeCount++;
    return false;
  }

  _eventTypes[_eventTypeCount].channelIndex = (byte)channelIndex;
  _eventTypes[_eventTypeCount].text = eventType;
  _eventTypes[_eventTypeCount].field = WHOLE_STRING;
  _eventTypeCount++;
  // A new type changes the catalog.
  _eventCatalogDirty = true;
  return true;
}

void BlaeckCore::clearAllEventChannels()
{
  for (uint16_t i = 0; i < _eventChannelSlots(); i++)
  {
    if (_eventChannels[i].inUse)
      _eventCatalogDirty = true;

    _eventChannels[i].inUse = false;
    _eventChannels[i].icon = nullptr;
    _eventChannels[i].diagnostic = false;
    _setChannelName(_eventChannels[i].name, _eventChannels[i].nameInFlash, nullptr, nullptr);
  }
  // Resetting the count is enough; entries beyond it are never read.
  _eventTypeCount = 0;
}

int BlaeckCore::_findEventChannel(const __FlashStringHelper *channelName) const
{
  for (uint16_t i = 0; i < _eventChannelSlots(); i++)
  {
    if (_eventChannels[i].inUse &&
        _channelNameEqualsFlash(_eventChannels[i].name, _eventChannels[i].nameInFlash, channelName))
      return i;
  }
  return -1;
}

int BlaeckCore::_findEventChannel(const char *channelName) const
{
  if (channelName == nullptr || channelName[0] == '\0')
    return -1;

  for (uint16_t i = 0; i < _eventChannelSlots(); i++)
  {
    if (_eventChannels[i].inUse && _channelNameEquals(_eventChannels[i].name, _eventChannels[i].nameInFlash, channelName))
      return (int)i;
  }
  return -1;
}

int BlaeckCore::_findEventType(uint16_t channelIndex, const __FlashStringHelper *eventType) const
{
  if (eventType == nullptr)
    return -1;

  // The index is the position among this channel's entries. Compare the text, not the pointer:
  // identical F() literals may sit at different addresses.
  uint16_t index = 0;
  for (uint16_t i = 0; i < _eventTypeCount; i++)
  {
    if (_eventTypes[i].channelIndex != channelIndex)
      continue;

    if (_eventTypeEquals(_eventTypes[i], eventType))
      return (int)index;

    index++;
  }
  return -1;
}

// Compares two flash strings a byte at a time. avr-libc has no flash-to-flash strcmp.
bool BlaeckCore::_flashStringEquals(const __FlashStringHelper *a, const __FlashStringHelper *b)
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

void BlaeckCore::writeEventChannels()
{
  this->writeEventChannels(0);
}

void BlaeckCore::writeEventChannels(unsigned long msg_id)
{
  this->writeEventChannelsFrame(msg_id);
}

void BlaeckCore::writeEventChannelsFrame(unsigned long msg_id)
{
  _eventCatalogDirty = false;

  // Layout: Event Channel List (0x80) in the protocol spec.
  if (!_frameOpen(0x80, msg_id))
    return;

  for (uint16_t i = 0; i < _eventChannelSlots(); i++)
  {
    EventChannelEntry &e = _eventChannels[i];
    if (!e.inUse)
      continue;

    uint16_t flags = 0;
    if (e.icon != nullptr)
      flags |= 0x0001;
    if (e.diagnostic)
      flags |= 0x0002;
    if (e.deviceClass != nullptr)
      flags |= 0x0004;
    if (e.disabledByDefault)
      flags |= 0x0008;

    _emitByte((byte)0);
    _emitByte((byte)0);
    if (e.nameInFlash)
      _emitFlashStr0(reinterpret_cast<const __FlashStringHelper *>(e.name));
    else
      _emitStr0(e.name);
    _emitByte((byte)(flags & 0xFF));
    _emitByte((byte)((flags >> 8) & 0xFF));

    if (flags & 0x0001)
      _emitFlashStr0(e.icon);
    if (flags & 0x0004)
      _emitFlashStr0(e.deviceClass);

    uint16_t typeCount = 0;
    for (uint16_t t = 0; t < _eventTypeCount; t++)
    {
      if (_eventTypes[t].channelIndex == i)
        typeCount++;
    }
    _emitByte((byte)(typeCount & 0xFF));
    _emitByte((byte)((typeCount >> 8) & 0xFF));

    for (uint16_t t = 0; t < _eventTypeCount; t++)
    {
      if (_eventTypes[t].channelIndex == i)
        _emitEventType0(_eventTypes[t]);
    }
  }

  _frameClose();
}

void BlaeckCore::writeEvent(const char *channelName, const __FlashStringHelper *eventType)
{
  // Layout: Event (0x85) in the protocol spec. The indices refer to the event channel list.
  if (!_mayWriteFrame())
    return;

  // Send changed catalogs first. An event sent against an old list can't be corrected later.
  _flushCatalogs();

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

  if (!_frameOpen(0x85, 0))
    return;
  _emitByte((byte)0);
  _emitByte((byte)0);
  _emitByte((byte)(channelIndex & 0xFF));
  _emitByte((byte)((channelIndex >> 8) & 0xFF));
  _emitByte((byte)(eventIndex & 0xFF));
  _emitByte((byte)((eventIndex >> 8) & 0xFF));
  _frameClose();
}
#else
// BLAECK_ENABLE_EVENTS=0: the API compiles but stores nothing, and the catalog answers empty.
BlaeckEventChannelRef BlaeckCore::addEventChannel(const char *, const __FlashStringHelper *) { return BlaeckEventChannelRef(this, -1); }
bool BlaeckCore::addEventType(const char *, const __FlashStringHelper *) { return false; }
void BlaeckCore::clearAllEventChannels() {}
// Used by the F() addEventChannel() overload, which exists either way.
int BlaeckCore::_registerEventChannel(const char *, const __FlashStringHelper *, const __FlashStringHelper *) { return -1; }
void BlaeckCore::writeEventChannels() { this->writeEventChannels(0); }
void BlaeckCore::writeEventChannels(unsigned long msg_id) { this->_writeEmptyFrame(0x80, msg_id); }
void BlaeckCore::writeEvent(const char *, const __FlashStringHelper *) {}
#endif

#if BLAECK_ENABLE_COMMAND_META
// Whether withRange() was called. Without it, min and max are both 0.
static inline bool _rangeDeclared(const blaeck_detail::CommandHandlerEntry &e)
{
  return e.meta_max > e.meta_min;
}

// Whether a step was declared: a positive step. 0 means none on purpose, and a negative or
// NaN step can't be used.
static inline bool _stepDeclared(const blaeck_detail::CommandHandlerEntry &e)
{
  return e.meta_step > 0.0f;
}

// Whether an options list was set. withOptions() already refused empty ones.
static inline bool _optionsDeclared(const blaeck_detail::CommandHandlerEntry &e)
{
  return e.options != nullptr;
}

// Warns about a number command without withRange(), when the catalog goes out. A host would
// otherwise use its own range or drop the control. BLAECK_NODISCARD makes this rare: it
// needs the handle to be dropped.
static void _warnCommandWithoutRange(Print *dbg, const blaeck_detail::CommandHandlerEntry &e)
{
  if (dbg == nullptr)
    return;
  dbg->print(F("No withRange() on number command: "));
  dbg->print(e.command);
  dbg->println(F(". Any value is accepted, and a host has no limits to build a control from."));
}

// The same for a select without options, which accepts nothing at all.
static void _warnCommandWithoutOptions(Print *dbg, const blaeck_detail::CommandHandlerEntry &e)
{
  if (dbg == nullptr)
    return;
  dbg->print(F("No withOptions() on select command: "));
  dbg->print(e.command);
  dbg->println(F(". Every value is rejected and a host has nothing to offer."));
}

byte BlaeckCore::_validateTypedCommand(uint16_t handlerIndex)
{
  const CommandHandlerEntry &e = _commandHandlers[handlerIndex];

  // Plain commands and buttons have no value to check.
  if (e.kind == BLAECK_CMD_PLAIN || e.kind == BLAECK_CMD_BUTTON)
    return BLAECK_ACK_OK;

  // A typed command without its value is rejected rather than passed to the handler.
  if (_parsedParamCount < 1 || _parsedParamPtrs[0] == nullptr)
    return BLAECK_ACK_MISSING_VALUE;

  const char *v = _parsedParamPtrs[0];

  // An empty value is valid only for text, where it clears the field.
  if (v[0] == '\0' && e.kind != BLAECK_CMD_TEXT)
    return BLAECK_ACK_MISSING_VALUE;

  if (e.kind == BLAECK_CMD_NUMBER)
  {
    // The whole string must be a number: atof() would read "abc" as 0. NaN is refused here
    // because every comparison with it is false; infinity is caught by the range check.
    char *endp = nullptr;
    float f = (float)strtod(v, &endp);
    if (endp == v || *endp != '\0' || isnan(f))
    {
      if (_debugStream != nullptr)
      {
        _debugStream->print(F("Command rejected (not a number): "));
        _debugStream->print(e.command);
        _debugStream->print('=');
        _debugStream->println(v);
      }
      return BLAECK_ACK_OUT_OF_RANGE;
    }

    // Check the range only if one was declared.
    if (_rangeDeclared(e) && (f < e.meta_min || f > e.meta_max))
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
    uint16_t count = _flashCsvOptionCount(e.options);

    // An option name (exact) or an index.
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

    // Always hand the handler the index, whichever form was sent.
    snprintf(_selectIndexScratch, sizeof(_selectIndexScratch), "%ld", idx);
    _parsedParamPtrs[0] = _selectIndexScratch;
  }
  else if (e.kind == BLAECK_CMD_TEXT)
  {
    // Decode in place so the handler gets plain UTF-8. The ack hashes the command as received,
    // so it still matches what the host sent.
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

void BlaeckCore::_percentDecodeInPlace(char *s)
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

void BlaeckCore::_setTimedDataState(bool timedActivated, unsigned long timedInterval_ms)
{
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    _timedSetPoint_ms = timedInterval_ms;
    _timedInterval_ms = timedInterval_ms;
    _timedFirstTime = true;
  }
}

void BlaeckCore::writeSymbols()
{
  this->writeSymbols(0);
}
void BlaeckCore::writeSymbols(unsigned long msg_id)
{
  this->writeSymbolsFrame(msg_id);
}

#if BLAECK_ENABLE_SIGNAL_META
void BlaeckCore::writeSignalConfig()
{
  this->writeSignalConfig(0);
}
void BlaeckCore::writeSignalConfig(unsigned long msg_id)
{
  _signalConfigDirty = false;
  this->writeSignalConfigFrame(msg_id);
}
#else
// BLAECK_ENABLE_SIGNAL_META=0: the catalog answers empty.
void BlaeckCore::writeSignalConfig() { this->writeSignalConfig(0); }
void BlaeckCore::writeSignalConfig(unsigned long msg_id) { this->_writeEmptyFrame(0xF0, msg_id); }
#endif

#if BLAECK_ENABLE_COMMAND_META
void BlaeckCore::writeCommands()
{
  this->writeCommands(0);
}
void BlaeckCore::writeCommands(unsigned long msg_id)
{
  _commandCatalogDirty = false;
  this->writeCommandsFrame(msg_id);
}
#else
// BLAECK_ENABLE_COMMAND_META=0: commands work, and the catalog answers empty.
void BlaeckCore::writeCommands() { this->writeCommands(0); }
// Clear the dirty flag, or _flushCatalogs() would resend the empty catalog on every read().
void BlaeckCore::writeCommands(unsigned long msg_id) { _commandCatalogDirty = false; this->_writeEmptyFrame(0xA0, msg_id); }
#endif

// A catalog with no entries.
void BlaeckCore::_writeEmptyFrame(byte msgKey, unsigned long msg_id)
{
  if (!_frameOpen(msgKey, msg_id))
    return;
  _frameClose();
}

void BlaeckCore::write(const char *signalName, bool value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, byte value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, short value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, unsigned short value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, int value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, unsigned int value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, long value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, unsigned long value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, float value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, double value)
{
  this->write(signalName, value, getTimeStamp());
}


void BlaeckCore::write(const char *signalName, bool value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, byte value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, short value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, unsigned short value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, int value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, unsigned int value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, long value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, unsigned long value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, float value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(const char *signalName, double value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}

void BlaeckCore::write(int signalIndex, bool value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, byte value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, short value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, unsigned short value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, int value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, unsigned int value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, long value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, unsigned long value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, float value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, double value)
{
  this->write(signalIndex, value, getTimeStamp());
}











void BlaeckCore::write(int signalIndex, bool value, unsigned long long timestamp)
{
  if (_storeSigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, byte value, unsigned long long timestamp)
{
  if (_storeUnsigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, short value, unsigned long long timestamp)
{
  if (_storeSigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, unsigned short value, unsigned long long timestamp)
{
  if (_storeUnsigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, int value, unsigned long long timestamp)
{
  if (_storeSigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, unsigned int value, unsigned long long timestamp)
{
  if (_storeUnsigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, long value, unsigned long long timestamp)
{
  if (_storeSigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, unsigned long value, unsigned long long timestamp)
{
  if (_storeUnsigned(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, float value, unsigned long long timestamp)
{
  if (_storeFloating(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}
void BlaeckCore::write(int signalIndex, double value, unsigned long long timestamp)
{
  if (_storeFloating(signalIndex, value))
    this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
}

void BlaeckCore::write(const char *signalName, const char *value)
{
  this->write(signalName, value, getTimeStamp());
}
void BlaeckCore::write(const char *signalName, const char *value, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, timestamp);
  }
}
void BlaeckCore::write(int signalIndex, const char *value)
{
  this->write(signalIndex, value, getTimeStamp());
}
void BlaeckCore::write(int signalIndex, const char *value, unsigned long long timestamp)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    if (Signals[signalIndex].DataType == Blaeck_string)
    {
      // Point at the caller's buffer, as addSignal(const char *) does.
      Signals[signalIndex].Address = const_cast<char *>(value);
      this->writeDataFrame(0, signalIndex, signalIndex, false, timestamp);
    }
  }
}

void BlaeckCore::writeAllData()
{
  this->writeAllData(getTimeStamp());
}

void BlaeckCore::writeAllData(unsigned long long timestamp)
{
  this->writeAllData(0, timestamp);
}

void BlaeckCore::writeAllData(unsigned long msg_id, unsigned long long timestamp)
{
  this->writeData(msg_id, 0, _signalIndex - 1, false, timestamp);
}

void BlaeckCore::writeUpdatedData()
{
  this->writeUpdatedData(getTimeStamp());
}

void BlaeckCore::writeUpdatedData(unsigned long long timestamp)
{
  this->writeData(0, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckCore::writeData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
{
  if (_signalIndex == 0)
    return;

  if (_beforeWriteCallback != NULL)
    _beforeWriteCallback();
  this->writeDataFrame(msg_id, signalIndex_start, signalIndex_end, onlyUpdated, timestamp);
}

void BlaeckCore::timedWriteAllData()
{
  this->timedWriteAllData(getTimeStamp());
}

// Timed frames answer no request, so their message id is 0.
void BlaeckCore::timedWriteAllData(unsigned long long timestamp)
{
  this->timedWriteData(0, 0, _signalIndex - 1, false, timestamp);
}

void BlaeckCore::timedWriteUpdatedData()
{
  this->timedWriteUpdatedData(getTimeStamp());
}

void BlaeckCore::timedWriteUpdatedData(unsigned long long timestamp)
{
  this->timedWriteData(0, 0, _signalIndex - 1, true, timestamp);
}

void BlaeckCore::timedWriteData(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
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

// ----- Buffered writes -----

void BlaeckCore::_bufAllocate()
{
  _bufFree();
  // Sized for the signals added so far; _bufEnsure() grows it if a frame needs more.
  int signalsHeld = _signalIndex > 0 ? _signalIndex : 1;
  _frameBufSize = 60 + signalsHeld * 10;
  // The symbol list can be larger with long names.
  int b0b3_est = 60 + signalsHeld * 30;
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

bool BlaeckCore::_bufEnsure(size_t addLen)
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

void BlaeckCore::_bufFree()
{
  delete[] _frameBuf;
  _frameBuf = nullptr;
  _frameBufSize = 0;
  _framePos = 0;
}

void BlaeckCore::setBufferedWrites(bool enabled)
{
  _bufferedWrites = enabled;
  // Turning it on allocates nothing; the buffer is built by the first buffered frame.
  if (!enabled)
    _bufFree();
}

bool BlaeckCore::_frameOpen(byte msgKey, unsigned long msgId, bool withCrc, Audience audience)
{
  if (!_mayWriteFrame())
    return false;

  _frameAudience = _replying ? AUDIENCE_REQUESTER : audience;

  _frameDirect = !_bufReady();
  if (!_frameDirect)
    _bufReset();
  _frameCrcOn = false;

  _emitStr("<BLAECK:");
  if (withCrc)
  {
    // The CRC covers the key through the status payload, not the start marker.
    _crc.setPolynome(0x04C11DB7);
    _crc.setInitial(0xFFFFFFFF);
    _crc.setXorOut(0xFFFFFFFF);
    _crc.setReverseIn(true);
    _crc.setReverseOut(true);
    _crc.restart();
    _frameCrcOn = true;
  }
  _emitByte(msgKey);
  _emitByte(':');
  ulngCvt.val = msgId;
  _emitBytes(ulngCvt.bval, 4);
  _emitByte(':');
  return true;
}

bool BlaeckCore::_frameClose()
{
  _frameCrcOn = false;
  _emitStr("/BLAECK>\r\n");
  if (!_frameDirect)
    return _bufSend();
  _flushDirect();
  return true;
}

void BlaeckCore::_emitDevice(const char *name, const char *hw, const char *fw)
{
  // Two bytes, always 0.
  _emitByte((byte)0);
  _emitByte((byte)0);
  _emitStr0(name);
  _emitStr0(hw);
  _emitStr0(fw);
  _emitStr0(_libraryVersion());
  _emitStr0(_libraryName());
}

// ----- Frame writers -----

void BlaeckCore::writeRestarted()
{
  this->writeRestarted(0);
}

void BlaeckCore::writeRestarted(unsigned long msg_id)
{
  // Checked before the flag is set, so a read() before begin() doesn't use up the notice.
  if (!_mayWriteFrame())
    return;

  if (!_writeRestartedAlreadyDone)
  {
    _writeRestartedAlreadyDone = true;

    if (!_frameOpen(0xC0, msg_id))
      return;
    _emitDevice(_deviceName(), DeviceHWVersion, DeviceFWVersion);
    _frameClose();

    // Send every catalog after the notice, so a host that stayed connected sees what this run
    // declares. The state catalog matters most, since its values are back at their defaults.
    // This runs from read(), so setup() has finished declaring by then.
#if BLAECK_ENABLE_STATE_CHANNELS
    this->writeStateChannels(msg_id);
#endif

#if BLAECK_ENABLE_EVENTS
    this->writeEventChannels(msg_id);
#endif

    this->writeCommands(msg_id);

    this->writeSignalConfig(msg_id);
  }
}

void BlaeckCore::writeDevices()
{
  this->writeDevices(0);
}

void BlaeckCore::writeDevices(unsigned long msg_id)
{
  this->writeDevicesFrame(msg_id);
}

void BlaeckCore::writeDevicesFrame(unsigned long msg_id)
{
  if (!_frameOpen(0xB3, msg_id))
    return;
  _emitDevice(_deviceName(), DeviceHWVersion, DeviceFWVersion);
  _frameClose();
}

void BlaeckCore::writeDataFrame(unsigned long msg_id, int signalIndex_start, int signalIndex_end, bool onlyUpdated, unsigned long long timestamp)
{
  if (!_mayWriteFrame())
    return;

  if (onlyUpdated && !hasUpdatedSignals())
    return; // No updated signals

  // Clamp the range.
  if (signalIndex_start < 0)
    signalIndex_start = 0;
  if (signalIndex_end >= _signalIndex)
    signalIndex_end = _signalIndex - 1;
  if (signalIndex_start > signalIndex_end)
    return; // No valid range

  if (!_frameOpen(0xD2, msg_id, true, AUDIENCE_SUBSCRIBERS))
    return;

  bool restartFlagSnapshot = _sendRestartFlag;
  _emitByte(_frameFlags(restartFlagSnapshot));
  _emitByte(':');

  _emitByte((byte)(_schemaHash & 0xFF));
  _emitByte((byte)((_schemaHash >> 8) & 0xFF));
  _emitByte(':');

  _emitByte((byte)_timestampMode);
  if (_timestampMode != BLAECK_NO_TIMESTAMP)
  {
    // Always sent when a timestamp mode is set, even without a clock (it is then 0). Leaving
    // it out would shift every later byte.
    ullCvt.val = timestamp;
    _emitBytes(ullCvt.bval, 8);
  }
  _emitByte(':');

  for (int i = signalIndex_start; i <= signalIndex_end; i++)
  {
    if (onlyUpdated && !Signals[i].Updated)
      continue;

    intCvt.val = i;
    _emitBytes(intCvt.bval, 2);

    Signal signal = Signals[i];
    switch (signal.DataType)
    {
    case (Blaeck_bool):   boolCvt.val  = *((bool *)signal.Address);           _emitBytes(boolCvt.bval, 1);  break;
    case (Blaeck_byte):   _emitByte(*((byte *)signal.Address));                                              break;
    case (Blaeck_short):  shortCvt.val = *((short *)signal.Address);          _emitBytes(shortCvt.bval, 2); break;
    case (Blaeck_ushort): ushortCvt.val = *((unsigned short *)signal.Address); _emitBytes(ushortCvt.bval, 2); break;
    case (Blaeck_int):    intCvt.val   = *((int *)signal.Address);            _emitBytes(intCvt.bval, 2);   break;
    case (Blaeck_uint):   uintCvt.val  = *((unsigned int *)signal.Address);   _emitBytes(uintCvt.bval, 2);  break;
    case (Blaeck_long):   lngCvt.val   = *((long *)signal.Address);           _emitBytes(lngCvt.bval, 4);   break;
    case (Blaeck_ulong):  ulngCvt.val  = *((unsigned long *)signal.Address);  _emitBytes(ulngCvt.bval, 4);  break;
    case (Blaeck_float):  fltCvt.val   = *((float *)signal.Address);          _emitBytes(fltCvt.bval, 4);   break;
    case (Blaeck_double): dblCvt.val   = *((double *)signal.Address);         _emitBytes(dblCvt.bval, 8);   break;
    case (Blaeck_string):
    {
      const char *str = (const char *)signal.Address;
      size_t rawLen = (str != nullptr) ? strlen(str) : 0;
      byte len = (rawLen > 255) ? 255 : (byte)rawLen;
      _emitByte(len);
      if (len > 0)
        _emitBytes((byte *)str, len);
    }
    break;
    }

    if (onlyUpdated)
      Signals[i].Updated = false;
  }

  byte statusByte = 0;
  byte statusPayload[4] = {0, 0, 0, 0};
  _emitByte(statusByte);
  _emitBytes(statusPayload, 4);

  uint32_t crc_value = _frameCrcEnd();
  _emitBytes((byte *)&crc_value, 4);

  if (_frameClose())
    _sendRestartFlag = false;
}

void BlaeckCore::writeSymbolsFrame(unsigned long msg_id)
{
  if (!_frameOpen(0xB0, msg_id))
    return;

  for (int i = 0; i < _signalIndex; i++)
  {
    _emitByte((byte)0);
    _emitByte((byte)0);

    // A reference, to avoid copying the entry.
    const Signal &signal = Signals[i];

    _emitSignalName0(signal);
    _emitByte(_dtypeCode(signal.DataType));
  }
  _frameClose();
}

#if BLAECK_ENABLE_SIGNAL_META
void BlaeckCore::writeSignalConfigFrame(unsigned long msg_id)
{
  // Layout: Signal Config (0xF0) in the protocol spec. Only signals that declare something are
  // included.
  if (!_frameOpen(0xF0, msg_id))
    return;

  for (int i = 0; i < _signalIndex; i++)
  {
    const SignalMeta *m = Signals[i].Meta;
    // No record, or one that declares nothing.
    if (m == nullptr || m->MetaFlags == 0)
      continue;

    uint16_t symbolId = (uint16_t)i;
    _emitByte((byte)(symbolId & 0xFF));
    _emitByte((byte)((symbolId >> 8) & 0xFF));
    _emitByte((byte)(m->MetaFlags & 0xFF));
    _emitByte((byte)((m->MetaFlags >> 8) & 0xFF));

    if (m->MetaFlags & BLAECK_SIG_HAS_UNIT)
      _emitFlashStr0(m->Unit);
    if (m->MetaFlags & BLAECK_SIG_HAS_DEVICE_CLASS)
      _emitFlashStr0(m->DeviceClass);
    if (m->MetaFlags & BLAECK_SIG_HAS_ICON)
      _emitFlashStr0(m->Icon);
    if (m->MetaFlags & BLAECK_SIG_HAS_DISPLAY_PRECISION)
      _emitByte(m->DisplayPrecision);
    if (m->MetaFlags & BLAECK_SIG_HAS_OPTIONS)
      _emitFlashStr0(m->Options);
    if (m->MetaFlags & BLAECK_SIG_HAS_DISPLAY_NAME)
      _emitFlashStr0(m->DisplayName);
  }

  _frameClose();
}
#endif

#if BLAECK_ENABLE_COMMAND_META
void BlaeckCore::writeCommandsFrame(unsigned long msg_id)
{
  // Layout: Command List (0xA0) in the protocol spec. Every command is listed, plain ones too.
  //
  // Warn about missing ranges and options before the frame opens: with buffered writes off, a
  // warning printed during the frame would land inside it if the debug stream is the same port.
  if (_debugStream != nullptr)
  {
    for (uint16_t i = 0; i < _commandSlots(); i++)
    {
      const CommandHandlerEntry &e = _commandHandlers[i];
      if (!e.inUse)
        continue;
      if (e.kind == BLAECK_CMD_NUMBER && !_rangeDeclared(e))
        _warnCommandWithoutRange(_debugStream, e);
      else if (e.kind == BLAECK_CMD_SELECT && !_optionsDeclared(e))
        _warnCommandWithoutOptions(_debugStream, e);
    }
  }

  if (!_frameOpen(0xA0, msg_id))
    return;

  for (uint16_t i = 0; i < _commandSlots(); i++)
  {
    CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse)
      continue;

    uint32_t flags = 0;
    // Send a range only if one was declared; 0 to 0 would allow only zero.
    if (e.kind == BLAECK_CMD_NUMBER && _rangeDeclared(e))
      flags |= 0x0001;
    if (e.unit != nullptr)
      flags |= 0x0002;
    if (e.kind == BLAECK_CMD_SELECT && _optionsDeclared(e))
      flags |= 0x0004;
    if (e.stateSignal != nullptr)
      flags |= 0x0008;
    if (e.kind == BLAECK_CMD_TEXT)
      flags |= 0x0010;
    // Entity category in bits 5-6.
    flags |= (uint32_t)((e.category & 0x03) << 5);
    if (e.disabledByDefault)
      flags |= 0x4000;
    // The step has its own bit, so "no step" differs from a step of 0.
    if (e.kind == BLAECK_CMD_NUMBER && _stepDeclared(e))
      flags |= 0x0080;
    if (e.displayName != nullptr)
      flags |= 0x0100;
    // Input mode in bits 9-10: box or slider on a number, password on text. 0 is the default.
    if (e.kind == BLAECK_CMD_NUMBER || e.kind == BLAECK_CMD_TEXT)
      flags |= (uint32_t)((e.mode & 0x03) << 9);
    if (e.deviceClass != nullptr)
      flags |= 0x0800;
    if (e.icon != nullptr)
      flags |= 0x1000;
    // Buttons only.
    if (e.kind == BLAECK_CMD_BUTTON && e.pressPayload != nullptr)
      flags |= 0x2000;

    // The longest command the device can receive, so a host knows how much room is left for
    // parameters.
    uint16_t payloadMax = (uint16_t)(MAXIMUM_CHAR_COUNT - 1);
    _emitByte((byte)0);
    _emitByte((byte)0);
    _emitByte((byte)(payloadMax & 0xFF));
    _emitByte((byte)((payloadMax >> 8) & 0xFF));
    _emitStr0(e.command);
    _emitByte(e.kind);
    _emitByte((byte)(flags & 0xFF));
    _emitByte((byte)((flags >> 8) & 0xFF));
    _emitByte((byte)((flags >> 16) & 0xFF));
    _emitByte((byte)((flags >> 24) & 0xFF));

    if (flags & 0x0001)
    {
      fltCvt.val = e.meta_min;
      _emitBytes(fltCvt.bval, 4);
      fltCvt.val = e.meta_max;
      _emitBytes(fltCvt.bval, 4);
    }
    if (flags & 0x0002)
      _emitFlashStr0(e.unit);
    if (flags & 0x0004)
      _emitFlashStr0(e.options);
    if (flags & 0x0008)
    {
      _emitFlashStr0(e.stateSignal);
      _emitByte(e.stateSource);
    }
    if (flags & 0x0010)
    {
      uint16_t maxLen = (uint16_t)e.meta_max;
      _emitByte((byte)(maxLen & 0xFF));
      _emitByte((byte)((maxLen >> 8) & 0xFF));
    }
    if (flags & 0x0080)
    {
      fltCvt.val = e.meta_step;
      _emitBytes(fltCvt.bval, 4);
    }
    if (flags & 0x0100)
      _emitFlashStr0(e.displayName);
    if (flags & 0x0800)
      _emitFlashStr0(e.deviceClass);
    if (flags & 0x1000)
      _emitFlashStr0(e.icon);
    if (flags & 0x2000)
      _emitFlashStr0(e.pressPayload);
  }

  _frameClose();
}
#endif

void BlaeckCore::tickUpdated()
{
  this->tick(0, true);
}

void BlaeckCore::tick()
{
  this->tick(0, false);
}

void BlaeckCore::tick(unsigned long msg_id, bool onlyUpdated)
{
  this->read();
  this->timedWriteData(msg_id, 0, _signalIndex - 1, onlyUpdated, getTimeStamp());
}

void BlaeckCore::markSignalUpdated(int signalIndex)
{
  if (signalIndex >= 0 && signalIndex < _signalIndex)
  {
    Signals[signalIndex].Updated = true;
  }
}

void BlaeckCore::markSignalUpdated(const char *signalName)
{
  for (int i = 0; i < _signalIndex; i++)
  {
    if (_signalNameEquals(Signals[i], signalName))
    {
      Signals[i].Updated = true;
      break;
    }
  }
}

void BlaeckCore::markAllSignalsUpdated()
{
  for (int i = 0; i < _signalIndex; i++)
  {
    Signals[i].Updated = true;
  }
}

void BlaeckCore::clearAllUpdateFlags()
{
  for (int i = 0; i < _signalIndex; i++)
  {
    Signals[i].Updated = false;
  }
}

bool BlaeckCore::hasUpdatedSignals()
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

void BlaeckCore::setTimestampMode(BlaeckTimestampMode mode)
{
  _timestampMode = mode;

  // Restart micros() rollover tracking.
  _prevMicros = 0;
  _overflowCount = 0;

  // Install the clock for built-in modes.
  switch (mode)
  {
  case BLAECK_MICROS:
    _timestampCallback = _microsWrapper;
    break;
  case BLAECK_UNIX:
    // BLAECK_UNIX needs the sketch's clock; keep one that is already set.
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

void BlaeckCore::setTimestampCallback(unsigned long long (*callback)())
{
  _timestampCallback = callback;
}

bool BlaeckCore::hasValidTimestampCallback() const
{
  return (_timestampMode != BLAECK_NO_TIMESTAMP && _timestampCallback != nullptr);
}

unsigned long long BlaeckCore::getTimeStamp()
{
  unsigned long long timestamp = 0;

  if (_timestampMode != BLAECK_NO_TIMESTAMP && hasValidTimestampCallback())
  {
    if (_timestampMode == BLAECK_MICROS)
    {
      // Extend micros() past its rollover, which comes about every 71 minutes.
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
      // The callback returns microseconds since the Unix epoch.
      timestamp = _timestampCallback();
    }
  }

  return timestamp;
}

void BlaeckCore::validatePlatformSizes()
{
#ifdef __AVR__
  // AVR (8-bit)
  static_assert(sizeof(int) == 2, "BlaeckCore: Expected 2-byte int on AVR");
  static_assert(sizeof(unsigned int) == 2, "BlaeckCore: Expected 2-byte unsigned int on AVR");
  static_assert(sizeof(double) == 4, "BlaeckCore: Expected 4-byte double on AVR");
  static_assert(sizeof(double) == sizeof(float), "BlaeckCore: double should equal float on AVR");
#else
  // 32-bit boards
  static_assert(sizeof(int) == 4, "BlaeckCore: Expected 4-byte int on 32-bit platforms");
  static_assert(sizeof(unsigned int) == 4, "BlaeckCore: Expected 4-byte unsigned int on 32-bit platforms");
  static_assert(sizeof(double) == 8, "BlaeckCore: Expected 8-byte double on 32-bit platforms");
  static_assert(sizeof(double) != sizeof(float), "BlaeckCore: double should differ from float on 32-bit platforms");
  static_assert(sizeof(int) == sizeof(long), "BlaeckCore: int/long size mismatch breaks type remapping");
  static_assert(sizeof(unsigned int) == sizeof(unsigned long), "BlaeckCore: uint/ulong size mismatch breaks type remapping");
#endif

  // The same on every board
  static_assert(sizeof(bool) == 1, "BlaeckCore: Expected 1-byte bool");
  static_assert(sizeof(byte) == 1, "BlaeckCore: Expected 1-byte byte");
  static_assert(sizeof(short) == 2, "BlaeckCore: Expected 2-byte short");
  static_assert(sizeof(unsigned short) == 2, "BlaeckCore: Expected 2-byte unsigned short");
  static_assert(sizeof(long) == 4, "BlaeckCore: Expected 4-byte long");
  static_assert(sizeof(unsigned long) == 4, "BlaeckCore: Expected 4-byte unsigned long");
  static_assert(sizeof(float) == 4, "BlaeckCore: Expected 4-byte float");
}

// ----- F() overloads -----
// Most copy the name into a buffer and call the const char * overload. addStateChannel() and
// addEventChannel() keep the flash pointer instead. These sit outside the BLAECK_ENABLE_*
// blocks, so they call the real function or its stub, whichever was compiled.

BlaeckTextStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, BlaeckTextTag)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_string, nullptr));
}

BlaeckBoolStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, BlaeckBoolTag)
{
  return BlaeckBoolStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_bool, nullptr));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, BlaeckNumericTag type)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, _tagType(type), nullptr));
}

BlaeckTextStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, const char *value)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_string, value));
}

BlaeckBoolStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, bool *value)
{
  return BlaeckBoolStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_bool, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, byte *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_byte, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_short, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, unsigned short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_ushort, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, int *value)
{
#ifdef __AVR__
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_int, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_long, value));
#endif
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, unsigned int *value)
{
#ifdef __AVR__
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_uint, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_ulong, value));
#endif
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_long, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, unsigned long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_ulong, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, float *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_float, value));
}

BlaeckNumericStateRef BlaeckCore::addStateChannel(const __FlashStringHelper *channelName, double *value)
{
#ifdef __AVR__
  // A double is 4 bytes on AVR, so it is declared as float.
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_float, value));
#else
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(nullptr, channelName, Blaeck_double, value));
#endif
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, const char *text)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, text);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, bool value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, byte value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, short value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, unsigned short value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, int value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, unsigned int value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, long value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, unsigned long value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, float value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName, double value)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n, value);
}

void BlaeckCore::writeState(const __FlashStringHelper *channelName)
{
  char n[MAX_STATE_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeState(n);
}

BlaeckEventChannelRef BlaeckCore::addEventChannel(const __FlashStringHelper *channelName, const __FlashStringHelper *eventTypes)
{
  return BlaeckEventChannelRef(this, (int16_t)_registerEventChannel(nullptr, channelName, eventTypes));
}

bool BlaeckCore::addEventType(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType)
{
  // The buffer only needs to last for the lookup.
  char n[MAX_EVENT_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  return addEventType(n, eventType);
}

void BlaeckCore::writeEvent(const __FlashStringHelper *channelName, const __FlashStringHelper *eventType)
{
  char n[MAX_EVENT_NAME_COUNT];
  copyFlashName(channelName, n, sizeof(n));
  writeEvent(n, eventType);
}

} // namespace BLAECK_CORE_NAMESPACE
