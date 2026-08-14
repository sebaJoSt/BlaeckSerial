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
  // Names and metadata first: the entries own them, and freeing the table would lose
  // the pointers.
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

BlaeckBeginRef BlaeckSerial::begin(Stream *Ref)
{
  StreamRef = (Stream *)Ref;
  // Torn down before the capacity changes: _freeSignalOwned() walks the table by
  // _signalCapacity, so that member has to still describe the table that exists.
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

  // No table is built here. Each one is allocated by the first entry added to
  // it, so a sketch pays for the tables it uses and the chain this returns can
  // still change their sizes after begin() has returned.
  return BlaeckBeginRef(this);
}

BlaeckBeginRef BlaeckSerial::begin(Stream *Ref, unsigned int size)
{
  return begin(Ref).withSignals(size);
}

bool BlaeckSerial::hasRejections() const
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

void BlaeckSerial::_printRejectionLine(Stream *out, const __FlashStringHelper *what,
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
  // What was asked for in total. Enough to hold this run, which is the number the sketch
  // wants; a bigger one only matters if what it declares can grow.
  out->print(capacity + dropped);
  out->println(F(")"));
}

bool BlaeckSerial::printRejections(Stream *out)
{
  if (out == nullptr || !hasRejections())
    return false;

  out->println(F("BlaeckSerial dropped what it had no room for:"));
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
  // A name too long or a duplicate is counted here too, and no capacity would cure those.
  // The debug stream is where the individual reason was given.
  out->println(F("  (a name too long or already taken counts here too - "
                 "withDebugStream() names each one)"));
#if BLAECK_ENABLE_SIGNAL_META
  // Not a table that ran out but the heap, so no chain call is suggested: the signals
  // are all there and sending fine, only what they say about themselves is missing.
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

void BlaeckSerial::_setTableCapacity(TableId table, unsigned int count)
{
  if (count == 0)
    return;

  // The table this capacity would size, and whether it is already built.
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

  // A table is sized once, when its first entry builds it. Asking afterwards
  // would hand back a capacity the table does not have, so say so instead.
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

  switch (table)
  {
  case TABLE_SIGNALS:
    _signalCapacity = count;
    break;
#if BLAECK_ENABLE_STATE_CHANNELS
  case TABLE_STATE_CHANNELS:
    _stateChannelCapacity = (count > 255) ? 255 : (byte)count;
    break;
#endif
#if BLAECK_ENABLE_EVENTS
  case TABLE_EVENT_CHANNELS:
    _eventChannelCapacity = (count > 255) ? 255 : (byte)count;
    break;
  case TABLE_EVENT_TYPES:
    _eventTypeCapacity = (count > 255) ? 255 : (byte)count;
    break;
#endif
  case TABLE_COMMANDS:
    _commandCapacity = (count > 255) ? 255 : (byte)count;
    break;
  default:
    break;
  }
}

void BlaeckSerial::_warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
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

void BlaeckSerial::_warnTableFull(const __FlashStringHelper *table, unsigned int capacity,
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

// Each table is built by its first entry and never grows: one block per table,
// allocated during setup and never freed, so nothing can fragment the heap
// later. A board too small to hold the table leaves the pointer null, which
// every caller reads as "full" - the same path a full table takes, reported by
// the same hasRejected*() flag.
bool BlaeckSerial::_ensureSignalTable()
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

bool BlaeckSerial::_ensureCommandTable()
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
bool BlaeckSerial::_ensureStateChannelTable()
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
bool BlaeckSerial::_ensureEventChannelTable()
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

bool BlaeckSerial::_ensureEventTypeTable()
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

int BlaeckSerial::_registerSignal(const char *signalName, dataType type, void *address)
{
  return _registerSignalCommon(signalName, nullptr, type, address);
}

int BlaeckSerial::_registerSignal(const __FlashStringHelper *signalName, dataType type, void *address)
{
  return _registerSignalCommon(nullptr, signalName, type, address);
}

int BlaeckSerial::_registerSignalCommon(const char *ram, const __FlashStringHelper *flash,
                                        dataType type, void *address)
{
  if (!_ensureSignalTable() || static_cast<unsigned int>(_signalIndex) >= _signalCapacity)
  {
    if (Signals != nullptr)
    {
      if (flash != nullptr)
        _warnTableFull(F("withSignals"), _signalCapacity, flash);
      else
        _warnTableFull(F("withSignals"), _signalCapacity, ram);
    }
    _signalRegistrationFailed = true;
    _rejectedSignalCount++;
    // -1 gives a dead handle: the chain that follows compiles and runs and stores nothing.
    return -1;
  }
  _setSignalName(_signalIndex, ram, flash);
  Signals[_signalIndex].DataType = type;
  Signals[_signalIndex].Address = address;
  // No initializer on a bit-field, so this is where a fresh signal - or a slot being
  // written a second time after deleteSignals() - is told it holds nothing new yet.
  Signals[_signalIndex].Updated = 0;
  Signals[_signalIndex].HasSuffix = 0;
  Signals[_signalIndex].NameSuffix = 0;
#if BLAECK_ENABLE_SIGNAL_META
  // deleteSignals() only rewinds the index, so a slot can be written twice. Cleared on
  // registration rather than on deletion, which covers both - and the record the slot
  // may still hold from its last life is given back rather than leaked.
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

BlaeckBoolSignalRef BlaeckSerial::addSignal(const char *signalName, bool *value)
{
  return BlaeckBoolSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_bool, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, byte *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_byte, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_short, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, unsigned short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ushort, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, int *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_int, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, unsigned int *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_uint, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, unsigned long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, float *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const char *signalName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_double, value));
#endif
}

BlaeckTextSignalRef BlaeckSerial::addSignal(const char *signalName, const char *value)
{
  // Address is void* for every datatype; a string address is only ever read from.
  return BlaeckTextSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_string, const_cast<char *>(value)));
}

BlaeckBoolSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, bool *value)
{
  return BlaeckBoolSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_bool, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, byte *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_byte, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_short, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, unsigned short *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ushort, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, int *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_int, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, unsigned int *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_uint, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_long, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, unsigned long *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_ulong, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, float *value)
{
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
}

BlaeckNumericSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, double *value)
{
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_float, value));
#else
  return BlaeckNumericSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_double, value));
#endif
}

BlaeckTextSignalRef BlaeckSerial::addSignal(const __FlashStringHelper *signalName, const char *value)
{
  return BlaeckTextSignalRef(this, (int16_t)_registerSignal(signalName, Blaeck_string, const_cast<char *>(value)));
}

void BlaeckSerial::deleteSignals()
{
  // The slots are given back as well as rewound: a name copy and a metadata record are
  // heap the entries own, and holding them until the next sketch happens to reuse the
  // slot would keep memory that nothing can reach.
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

uint16_t BlaeckSerial::_computeSchemaHash()
{
  // CRC16-CCITT (init=0x0000, poly=0x1021) over signal names + datatype codes.
  // Must match Python: binascii.crc_hqx(data, 0) & 0xFFFF
  // Runs through the same accumulator the frame writers feed, so a name in flash and a
  // name in RAM cannot hash differently: both arrive here as the same bytes.
  uint16_t saved = _schemaHashAccum;
  _schemaHashAccum = 0x0000;
  for (int j = 0; j < _signalIndex; j++)
  {
    _signalNameFeedHash(Signals[j]);
    _schemaHashFeedByte((byte)Signals[j].DataType);
  }
  uint16_t crc = _schemaHashAccum;
  _schemaHashAccum = saved;
  return crc & 0xFFFF;
}

void BlaeckSerial::setSignalName(int signalIndex, const char *signalName)
{
  _setSignalName(signalIndex, signalName, nullptr);
  // A renamed signal is a changed schema. This used to leave the hash as registration
  // computed it, so a host was told nothing had moved and went on using the catalog it
  // already had - under the old names.
  _schemaHash = _computeSchemaHash();
}

void BlaeckSerial::_setSignalName(int signalIndex, const char *ram, const __FlashStringHelper *flash)
{
  if (Signals == nullptr || signalIndex < 0 || signalIndex >= (int)_signalCapacity)
    return;

  Signal &s = Signals[signalIndex];
  // Whatever the slot held: a copy is freed, a flash name owns nothing. deleteSignals()
  // only rewinds the index, so a slot is written twice whenever signals are re-declared.
  // The pointer is tested first so a slot that has never been named short-circuits before
  // NameInFlash is read: a bit-field takes no initializer, so on a fresh table that bit
  // means nothing until a name has been set.
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

  // The name is copied, so the caller's buffer is free the moment this returns - build one
  // with snprintf and reuse it for the next signal.
  size_t needed = strlen(ram) + 1;
  char *copy = (char *)malloc(needed);
  if (copy != nullptr)
  {
    memcpy(copy, ram, needed);
    s.SignalName = copy;
  }
  // Out of RAM leaves the name empty rather than the signal missing: the slot, its
  // datatype and its address are all still good, and _signalName* reads null as "".
}

void BlaeckSerial::_freeSignalOwned()
{
  if (Signals == nullptr)
    return;
  for (unsigned int i = 0; i < _signalCapacity; i++)
  {
    // Pointer first, so an unnamed slot short-circuits before the bit is read - see
    // _setSignalName. This walks the whole capacity, most of which may never have held
    // a signal at all.
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
SignalMeta *BlaeckSerial::_ensureSignalMeta(int16_t index)
{
  // A dead handle - the table was full when the signal was added - has nowhere to store
  // anything, which is what lets a chain be written without checking it first.
  if (index < 0 || Signals == nullptr || static_cast<unsigned int>(index) >= _signalCapacity)
    return nullptr;
  Signal &s = Signals[index];
  if (s.Meta == nullptr)
  {
    // Whichever new the core provides: a throwing one gives a record or does not
    // return, a nothrow one gives null, and null is handled either way.
    s.Meta = new (std::nothrow) SignalMeta();
    if (s.Meta == nullptr)
      _rejectedSignalMetaCount++;
  }
  return s.Meta;
}
#endif

bool BlaeckSerial::_signalNameEquals(const Signal &s, const char *name) const
{
  if (name == nullptr)
    return false;
  const char *q = name;
  if (s.SignalName != nullptr)
  {
    if (s.NameInFlash)
    {
      // Compared byte by byte through pgm_read_byte rather than strcmp_P, which not every
      // core provides; on a core where flash is directly addressable pgm_read_byte is a
      // plain read, so this costs nothing there.
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
  // The digits the name ends in are never stored, so they are matched as they would be
  // written rather than compared against anything.
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

// The suffix as decimal text, without a terminator. Returns how many digits were written;
// out must hold three. One place produces them, so a name cannot be matched one way and
// hashed or sent another.
byte BlaeckSerial::_signalSuffixDigits(const Signal &s, char *out)
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

// One walk over a name, feeding each byte wherever it is wanted. The prefix lives in
// flash or in RAM and may be followed by digits that are not stored at all, and every
// writer needs all of those cases - so the walking happens once here rather than three
// times, and a name cannot be sent one way and hashed another.
void BlaeckSerial::_emitSignalName(const Signal &s, NameSink sink)
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

void BlaeckSerial::_emitNameByte(byte c, NameSink sink)
{
  switch (sink)
  {
  case NAME_SINK_BUFFER:
    _bufByte(c);
    break;
  case NAME_SINK_STREAM:
    StreamRef->write(c);
    break;
  default:
    _schemaHashFeedByte(c);
    break;
  }
}

void BlaeckSerial::_signalNameFeedHash(const Signal &s)
{
  _emitSignalName(s, NAME_SINK_HASH);
}

void BlaeckSerial::_bufSignalName0(const Signal &s)
{
  _emitSignalName(s, NAME_SINK_BUFFER);
  // The terminator comes last, after whatever the prefix and the digits contributed - a
  // name with a suffix is one string on the wire, not two.
  _bufByte(0);
}

void BlaeckSerial::_printSignalName(const Signal &s)
{
  _emitSignalName(s, NAME_SINK_STREAM);
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

void BlaeckSerial::update(const char *signalName, bool value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, byte value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, unsigned short value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, unsigned int value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, unsigned long value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, float value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

void BlaeckSerial::update(const char *signalName, double value)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    update(index, value);
  }
}

int BlaeckSerial::findSignalIndex(const char *signalName)
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

void BlaeckSerial::read()
{
  this->writeRestarted();

  if (recvWithStartEndMarkers() == true)
  {
    // Parsed once, here: the built-in commands below and the registered handlers that
    // follow all read the same parse, rather than each running its own over the same bytes.
    _parseCommandTokens(receivedChars);
    if (_debugStream != nullptr)
    {
      _debugStream->print("<");
      _debugStream->print(receivedChars);
      _debugStream->println(">");
    }

    if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_SYMBOLS) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeSymbols(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_SIGNAL_CONFIG) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeSignalConfig(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_DATA) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeAllData(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_GET_DEVICES) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeDevices(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_COMMANDS) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeCommands(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_STATE_CHANNELS) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeStateChannels(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_WRITE_EVENT_CHANNELS) == 0)
    {
      unsigned long msg_id = _parsedMsgId();

      this->writeEventChannels(msg_id);
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_ACTIVATE) == 0)
    {
      if (_fixedInterval_ms == BLAECK_INTERVAL_CLIENT)
      {
        unsigned long timedInterval_ms = _parsedMsgId();
        this->_setTimedDataState(true, timedInterval_ms);
      }
    }
    else if (strcmp(_parsedCommand, BLAECK_BUILTIN_DEACTIVATE) == 0)
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

int BlaeckSerial::_registerCommand(const char *command, BlaeckCommandHandler handler, uint8_t kind)
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

  if (!_ensureCommandTable())
  {
    _rejectedCommandCount++;
    return -1;
  }

  for (byte i = 0; i < _commandSlots(); i++)
  {
    if (_commandHandlers[i].inUse && strcmp(_commandHandlers[i].command, command) == 0)
    {
      _commandHandlers[i].handler = handler;
      _resetCommandMeta(i, kind);
      return (int)i;
    }
  }

  for (byte i = 0; i < _commandSlots(); i++)
  {
    if (!_commandHandlers[i].inUse)
    {
      strncpy(_commandHandlers[i].command, command, MAX_COMMAND_NAME_COUNT - 1);
      _commandHandlers[i].command[MAX_COMMAND_NAME_COUNT - 1] = '\0';
      _commandHandlers[i].handler = handler;
      _commandHandlers[i].inUse = true;
      _resetCommandMeta(i, kind);
      return (int)i;
    }
  }

  _warnTableFull(F("withCommands"), _commandCapacity, command);
  _rejectedCommandCount++;
  return -1;
}

// Registering the same name twice replaces the command outright, so the metadata starts empty
// rather than inheriting whatever the previous declaration said.
void BlaeckSerial::_resetCommandMeta(byte handlerIndex, uint8_t kind)
{
#if BLAECK_ENABLE_COMMAND_META
  CommandHandlerEntry &e = _commandHandlers[handlerIndex];
  e.kind = kind;
  e.meta_min = 0.0f;
  // A text command that never states a limit advertises 255; every other kind reads this as a
  // range maximum and states its own.
  e.meta_max = (kind == BLAECK_CMD_TEXT) ? 255.0f : 0.0f;
  e.meta_step = 0.0f;
  e.unit = nullptr;
  e.options = nullptr;
  e.stateSignal = nullptr;
  e.stateSource = BLAECK_STATE_SIGNAL;
  e.category = BLAECK_CAT_NONE;
#else
  (void)handlerIndex;
  (void)kind;
#endif
}

void BlaeckSerial::onCommand(const char *command, BlaeckCommandHandler handler)
{
  _registerCommand(command, handler, BLAECK_CMD_PLAIN);
}

void BlaeckSerial::onAnyCommand(BlaeckAnyCommandHandler handler)
{
  _anyCommandHandler = handler;
}

void BlaeckSerial::clearAllCommandHandlers()
{
  for (byte i = 0; i < _commandSlots(); i++)
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
#if !BLAECK_ENABLE_COMMAND_META || !BLAECK_ENABLE_STATE_CHANNELS
  (void)command;
  (void)messageID;
#else
  if (command == nullptr)
    return;

  for (byte i = 0; i < _commandSlots(); i++)
  {
    const CommandHandlerEntry &e = _commandHandlers[i];
    if (!e.inUse || e.stateSource != BLAECK_STATE_CHANNEL || e.stateSignal == nullptr)
      continue;
    if (strcmp(e.command, command) != 0)
      continue;

    // The channel was declared from this same name at registration, so it exists unless the
    // table was full - in which case there is nothing to publish to and the warning was
    // already given there.
    for (byte c = 0; c < _stateChannelSlots(); c++)
    {
      if (!_stateChannels[c].inUse || !_stateChannels[c].ownedByCommand)
        continue;
      if (!_flashStringEqualsName(e.stateSignal, _stateChannels[c].name))
        continue;

      BlaeckStateTextGetter getter = _stateChannels[c].getStateText;
      _writeStateFrame(c, getter != nullptr ? getter() : nullptr, messageID);
      return;
    }
    return;
  }
#endif
}

#if BLAECK_ENABLE_COMMAND_META
// Copies a flash name into `out` and declares the channel as owned by a command. Kept apart
// from addStateChannel() so that one can refuse an owned name outright, with no exception
// for "unless the caller is me".
bool BlaeckSerial::_addOwnedStateChannel(const __FlashStringHelper *channelName, BlaeckStateTextGetter getStateText,
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

  char name[MAX_STATE_NAME_COUNT];
  PGM_P p = reinterpret_cast<PGM_P>(channelName);
  byte len = 0;
  byte c;
  while ((c = pgm_read_byte(p + len)) != 0 && len + 1 < MAX_STATE_NAME_COUNT)
  {
    name[len] = (char)c;
    len++;
  }
  name[len] = '\0';
  if (len == 0)
    return false;

  int existing = _findStateChannel(name);
  if (existing >= 0 && !_stateChannels[existing].ownedByCommand)
  {
    // The sketch declared this name itself. The command takes it, because its state has to
    // come from one place - but say so, since the addStateChannel() line is now dead.
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel taken over by a command's own state; drop the addStateChannel() for: "));
      _debugStream->println(name);
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
    _rejectedStateChannelCount++;
    return false;
  }

  for (byte i = 0; i < _stateChannelSlots(); i++)
  {
    if (_stateChannels[i].inUse)
      continue;
    strncpy(_stateChannels[i].name, name, MAX_STATE_NAME_COUNT - 1);
    _stateChannels[i].name[MAX_STATE_NAME_COUNT - 1] = '\0';
    _stateChannels[i].icon = nullptr;
    // Diagnostic on principle: a host that announces a sensor for it anyway should file it
    // away, since the control already shows this value.
    _stateChannels[i].diagnostic = true;
    _stateChannels[i].getStateText = getStateText;
    _stateChannels[i].valueType = valueType;
    _stateChannels[i].stateValue = value;
    _stateChannels[i].truncationWarned = false;
    _stateChannels[i].ownedByCommand = true;
    _stateChannels[i].inUse = true;
    return true;
  }

  _warnTableFull(F("withStateChannels"), _stateChannelCapacity, name);
  _rejectedStateChannelCount++;
  return false;
#endif
}

bool BlaeckSerial::_declareOwnState(byte handlerIndex, const __FlashStringHelper *channelName,
                                   BlaeckStateTextGetter getStateText)
{
  return _declareOwnState(handlerIndex, channelName, getStateText, Blaeck_string, nullptr);
}

bool BlaeckSerial::_declareOwnState(byte handlerIndex, const __FlashStringHelper *channelName,
                                   BlaeckStateTextGetter getStateText, dataType valueType,
                                   const void *value)
{
  // One or the other has to supply the value: a getter that builds text, or a variable the
  // library reads. Neither means the control would sit at unknown forever, so refuse it here
  // rather than announce a topic nothing publishes to.
  if (channelName == nullptr || (getStateText == nullptr && value == nullptr))
    return false;

  if (!_addOwnedStateChannel(channelName, getStateText, valueType, value))
    return false;

  // Announce once, here. A host connecting later reads the value from the channel catalog,
  // but one already connected when the board reset has no reason to re-read a catalog it
  // already holds - and the sketch's variables are back at their startup values. Dropped
  // harmlessly when no host holds the catalog yet, which is the cold-start case the poll
  // covers.
  writeCommandState(_commandHandlers[handlerIndex].command);
  return true;
}
#endif

BlaeckNumberCommandRef BlaeckSerial::onNumberCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckNumberCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_NUMBER));
}

BlaeckSwitchCommandRef BlaeckSerial::onSwitchCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckSwitchCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_SWITCH));
}

BlaeckSelectCommandRef BlaeckSerial::onSelectCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckSelectCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_SELECT));
}

BlaeckButtonCommandRef BlaeckSerial::onButtonCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckButtonCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_BUTTON));
}

BlaeckTextCommandRef BlaeckSerial::onTextCommand(const char *command, BlaeckCommandHandler handler)
{
  return BlaeckTextCommandRef(this, (int16_t)_registerCommand(command, handler, BLAECK_CMD_TEXT));
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
  for (byte i = 0; i < _commandSlots(); i++)
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

// The four little-endian bytes a built-in BLAECK.* command carries, as one number. A field
// the frame did not carry reads as 0, which is what the fixed parameter array it replaced
// held for an absent token. The value goes through int exactly as that array did, so a
// field outside 0-255 lands on the wire the same way it always has rather than being
// quietly masked into a different number.
unsigned long BlaeckSerial::_parsedMsgId() const
{
  unsigned long v = 0;
  for (byte i = 0; i < 4; i++)
  {
    if (i < _parsedParamCount && _parsedParamPtrs[i] != nullptr)
      v |= ((unsigned long)(int)atoi(_parsedParamPtrs[i])) << (8 * i);
  }
  return v;
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
  strncpy(_parsedCommand, tokenStart, MAX_PARSED_COMMAND_COUNT - 1);
  _parsedCommand[MAX_PARSED_COMMAND_COUNT - 1] = '\0';

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

  for (byte i = 0; i < _commandSlots(); i++)
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

  if (_bufReady())
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

#if BLAECK_ENABLE_STATE_CHANNELS
void BlaeckSerial::writeState(const char *channelName, const char *text)
{
  this->writeState(channelName, text, _stateMsgId++);
}

int BlaeckSerial::_registerStateChannel(const char *channelName, dataType valueType,
                                         const void *value)
{
  if (channelName == nullptr || channelName[0] == '\0')
  {
    _rejectedStateChannelCount++;
    return -1;
  }

  // A channel a command owns is that command's alone: its value comes from the getter it was
  // registered with, and nowhere else.
  int owned = _findStateChannel(channelName);
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

  if (strlen(channelName) >= MAX_STATE_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for state channel table: "));
      _debugStream->println(channelName);
    }
    _rejectedStateChannelCount++;
    return -1;
  }

  // Re-declaring a channel updates it rather than consuming a slot, so the metadata starts
  // empty: what the previous declaration said must not survive a chain that says less.
  int existing = _findStateChannel(channelName);
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
    return existing;
  }

  if (!_ensureStateChannelTable())
  {
    _rejectedStateChannelCount++;
    return -1;
  }

  for (byte i = 0; i < _stateChannelSlots(); i++)
  {
    if (!_stateChannels[i].inUse)
    {
      strncpy(_stateChannels[i].name, channelName, MAX_STATE_NAME_COUNT - 1);
      _stateChannels[i].name[MAX_STATE_NAME_COUNT - 1] = '\0';
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
      return (int)i;
    }
  }

  _warnTableFull(F("withStateChannels"), _stateChannelCapacity, channelName);
  _rejectedStateChannelCount++;
  return -1;
}

BlaeckTextStateRef BlaeckSerial::addStateChannel(const char *channelName)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(channelName));
}

BlaeckTextStateRef BlaeckSerial::addStateChannel(const char *channelName, const char *value)
{
  return BlaeckTextStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_string, value));
}

BlaeckBoolStateRef BlaeckSerial::addStateChannel(const char *channelName, bool *value)
{
  return BlaeckBoolStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_bool, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, byte *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_byte, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_short, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, unsigned short *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_ushort, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, int *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_int, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, unsigned int *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_uint, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_long, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, unsigned long *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_ulong, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, float *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_float, value));
}

BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *channelName, double *value)
{
  return BlaeckNumericStateRef(this, (int16_t)_registerStateChannel(channelName, Blaeck_double, value));
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

void BlaeckSerial::clearAllStateChannels()
{
  for (byte i = 0; i < _stateChannelSlots(); i++)
  {
    _stateChannels[i].inUse = false;
    _stateChannels[i].icon = nullptr;
    _stateChannels[i].diagnostic = false;
    _stateChannels[i].name[0] = '\0';
  }
}

int BlaeckSerial::_findStateChannel(const char *channelName) const
{
  if (channelName == nullptr || channelName[0] == '\0')
    return -1;

  for (byte i = 0; i < _stateChannelSlots(); i++)
  {
    if (_stateChannels[i].inUse && strcmp(_stateChannels[i].name, channelName) == 0)
      return (int)i;
  }
  return -1;
}

void BlaeckSerial::writeState(const char *channelName, const char *text, unsigned long messageID)
{
  // 0x95 "State" frame: the current value of a declared channel, device -> host.
  //   channelIndex(1)  valueType(1)  value
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

  // A channel a command owns reports what its getter says and nothing else. A line pushed
  // here would show until the next catalog poll and then be silently replaced, which is worse
  // than refusing it. Use writeCommandState() to publish the getter's value.
  if (_stateChannels[channelIndex].ownedByCommand)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel belongs to a command's own state; use writeCommandState() for: "));
      _debugStream->println(channelName);
    }
    return;
  }

  // Text on a channel declared as a number would go out under a numeric valueType and be read
  // as four bytes of a float - a plausible wrong number with nothing to show it went wrong.
  // Refuse it here; writeState(channelName) reports the variable instead.
  if (_stateChannels[channelIndex].valueType != Blaeck_string)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State dropped, channel carries a number; use writeState(channelName) for: "));
      _debugStream->println(channelName);
    }
    return;
  }

  _writeStateFrame(channelIndex, text, messageID);
}

// Reports whatever the channel currently holds: the variable a typed channel points at, or
// the text a getter builds. The one form that needs no value from the caller, and the only
// way to push a numeric channel.
void BlaeckSerial::writeState(const char *channelName)
{
  this->writeState(channelName, _stateMsgId++);
}

void BlaeckSerial::writeState(const char *channelName, unsigned long messageID)
{
  if (StreamRef == nullptr)
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
  const char *text = (e.getStateText != nullptr) ? e.getStateText() : nullptr;
  _writeStateFrame(channelIndex, text, messageID);
}

// The 0x95 frame itself. Split out because writeCommandState() has to reach it for a channel
// writeState() deliberately refuses - the guard is about who may choose the text, not about
// how it is sent.
void BlaeckSerial::_writeStateFrame(int channelIndex, const char *text, unsigned long messageID)
{
  if (StreamRef == nullptr)
    return;

  // A typed channel reports its variable; text is what a string channel was handed, or what
  // its getter returned. One or the other, never both - which is what valueType records.
  StateChannelEntry &e = _stateChannels[channelIndex];
  byte valueBytes[8];
  byte valueLen = _channelValueBytes(e, valueBytes);

  if (text == nullptr)
    text = "";

  // Capped at 255, the same as a string signal and as Home Assistant's own limit on a state.
  // The cap also bounds how long one push holds the link: a frame goes out whole, so an
  // unbounded value would delay every data frame queued behind it.
  size_t rawLen = strlen(text);
  byte len = (rawLen > 255) ? (byte)255 : (byte)rawLen;
  if (rawLen > 255 && !e.truncationWarned)
  {
    e.truncationWarned = true;
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("State text truncated to 255 bytes on channel: "));
      _debugStream->println(e.name);
    }
  }

  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0x95, messageID);
    // Channel index, the datatype, then the value: fixed width for a number, a 1-byte length
    // followed by that many UTF-8 bytes for a string - the same rule a data frame follows.
    _bufByte((byte)channelIndex);
    _bufByte(_dtypeCode(e.valueType));
    if (valueLen > 0)
    {
      _bufBytes(valueBytes, valueLen);
    }
    else
    {
      _bufByte(len);
      if (len > 0)
        _bufBytes((const byte *)text, len);
    }
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

    StreamRef->write((byte)channelIndex);
    StreamRef->write(_dtypeCode(e.valueType));
    if (valueLen > 0)
    {
      StreamRef->write(valueBytes, valueLen);
    }
    else
    {
      StreamRef->write(len);
      if (len > 0)
        StreamRef->write((const uint8_t *)text, len);
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeStateChannels()
{
  this->writeStateChannels(1);
}

void BlaeckSerial::writeStateChannels(unsigned long msg_id)
{
  this->writeStateChannelsFrame(msg_id);
}

byte BlaeckSerial::_dtypeCode(dataType t)
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

#if BLAECK_ENABLE_STATE_CHANNELS
byte BlaeckSerial::_channelValueBytes(const StateChannelEntry &e, byte *out)
{
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

uint16_t BlaeckSerial::_stateChannelFlags(const StateChannelEntry &e, bool hasStateValue) const
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
  // State class and display precision live in the entry's own word already, because neither
  // can be inferred from a member: state class 0 and precision 0 are both real values.
  flags |= (uint16_t)(e.metaFlags & (BLAECK_SCH_STATE_CLASS_MASK | BLAECK_SCH_HAS_DISPLAY_PRECISION));
  return flags;
}

void BlaeckSerial::writeStateChannelsFrame(unsigned long msg_id)
{
  // 0x90 "State Channel List" frame. Per declared channel entry:
  //   msConfig(1) slaveID(1) name\0 flags(2, LE uint16) valueType(1)
  //   [icon\0]                 if flags.hasIcon
  //   [stateValue]             if flags.hasStateValue
  //   [deviceClass\0]          if flags.hasDeviceClass
  //   [options\0]              if flags.hasOptions
  //   [unit\0]                 if flags.hasUnit
  //   [displayPrecision(1)]    if flags.hasDisplayPrecision
  // flags bits: 0=hasIcon 1=isDiagnostic 2=hasStateValue 3=hasDeviceClass 4=disabledByDefault
  //             5=forceUpdate 6=hasOptions 7=hasUnit 8-10=stateClass 11=hasDisplayPrecision.
  //             Bits 12-15 reserved - two bytes rather than one, so the catalog has room to
  //             grow without taking a new message key.
  // Optional fields follow in bit order, as they do in 0xF0.
  // valueType is unconditional: a channel has a type whether or not it has a value to report
  // yet, so tying the type to the presence of a value would leave a host guessing. stateValue
  // is a NUL-terminated string for type 0x0A and the fixed width its type implies otherwise.
  // Unit, state class and display precision are what make a host treat the channel as a number
  // rather than as text, so a text channel leaves all three unset.
  // stateText is fetched from the channel's getter as the frame is built, so the catalog
  // reports each channel's value as of that moment and there is no stored copy to go
  // stale. A channel that registered no getter, or whose getter returns nullptr, carries
  // no value.
  // The two leading bytes are the entry's device identity, msConfig and slaveID: zero from
  // a single-device library, rewritten by an aggregator relaying several boards. Not
  // padding - without them a catalog could name only one device.
  // Declared up-front so the host can announce one text entity per channel
  // before any 0x95 push arrives, the same way 0xA0 announces commands.
  if (StreamRef == nullptr)
    return;

  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0x90, msg_id);

    for (byte i = 0; i < _stateChannelSlots(); i++)
    {
      StateChannelEntry &e = _stateChannels[i];
      if (!e.inUse)
        continue;

      // Fetched once, before the flag is decided: the getter may return nullptr, and
      // calling it twice could hand the two uses different text.
      const char *stateText = (e.getStateText != nullptr) ? e.getStateText() : nullptr;
      byte valueBytes[8];
      byte valueLen = _channelValueBytes(e, valueBytes);

      uint16_t flags = _stateChannelFlags(e, stateText != nullptr || valueLen > 0);

      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufStr0(e.name);
      _bufByte((byte)(flags & 0xFF));
      _bufByte((byte)((flags >> 8) & 0xFF));
      _bufByte(_dtypeCode(e.valueType));

      if (flags & BLAECK_SCH_HAS_ICON)
        _bufFlashStr0(e.icon);
      // A string is NUL-terminated here like every other string in this frame; a number is the
      // fixed width its type implies, which is why it needs no terminator of its own.
      if (flags & BLAECK_SCH_HAS_STATE_VALUE)
      {
        if (valueLen > 0)
          _bufBytes(valueBytes, valueLen);
        else
          _bufStr0(stateText);
      }
      if (flags & BLAECK_SCH_HAS_DEVICE_CLASS)
        _bufFlashStr0(e.deviceClass);
      if (flags & BLAECK_SCH_HAS_OPTIONS)
        _bufFlashStr0(e.options);
      if (flags & BLAECK_SCH_HAS_UNIT)
        _bufFlashStr0(e.unit);
      if (flags & BLAECK_SCH_HAS_DISPLAY_PRECISION)
        _bufByte(e.displayPrecision);
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

    for (byte i = 0; i < _stateChannelSlots(); i++)
    {
      StateChannelEntry &e = _stateChannels[i];
      if (!e.inUse)
        continue;

      // Fetched before any of this entry's bytes go out: in this unbuffered path the frame
      // is streamed as it is built, so the getter runs mid-transmission and a slow one
      // stalls a half-sent frame.
      const char *stateText = (e.getStateText != nullptr) ? e.getStateText() : nullptr;
      byte valueBytes[8];
      byte valueLen = _channelValueBytes(e, valueBytes);

      uint16_t flags = _stateChannelFlags(e, stateText != nullptr || valueLen > 0);

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(e.name);
      StreamRef->write((byte)0);
      StreamRef->write((byte)(flags & 0xFF));
      StreamRef->write((byte)((flags >> 8) & 0xFF));
      StreamRef->write(_dtypeCode(e.valueType));

      if (flags & BLAECK_SCH_HAS_ICON)
      {
        StreamRef->print(e.icon);
        StreamRef->write((byte)0);
      }
      // A string is NUL-terminated here like every other string in this frame; a number is the
      // fixed width its type implies, which is why it needs no terminator of its own.
      if (flags & BLAECK_SCH_HAS_STATE_VALUE)
      {
        if (valueLen > 0)
        {
          StreamRef->write(valueBytes, valueLen);
        }
        else
        {
          StreamRef->print(stateText);
          StreamRef->write((byte)0);
        }
      }
      if (flags & BLAECK_SCH_HAS_DEVICE_CLASS)
      {
        StreamRef->print(e.deviceClass);
        StreamRef->write((byte)0);
      }
      if (flags & BLAECK_SCH_HAS_OPTIONS)
      {
        StreamRef->print(e.options);
        StreamRef->write((byte)0);
      }
      if (flags & BLAECK_SCH_HAS_UNIT)
      {
        StreamRef->print(e.unit);
        StreamRef->write((byte)0);
      }
      if (flags & BLAECK_SCH_HAS_DISPLAY_PRECISION)
        StreamRef->write(e.displayPrecision);
    }

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}
#else
// BLAECK_ENABLE_STATE_CHANNELS=0: the API stays so sketches still build, but nothing
// is stored. The catalog still answers, with an empty list (see _writeEmptyFrame).
// The handle's modifiers compile and store nothing, so a sketch declaring channels needs no
// #ifdef. Nothing is counted as rejected: the feature is off, not failing.
BlaeckTextStateRef BlaeckSerial::addStateChannel(const char *) { return BlaeckTextStateRef(this, -1); }
BlaeckTextStateRef BlaeckSerial::addStateChannel(const char *, const char *) { return BlaeckTextStateRef(this, -1); }
BlaeckBoolStateRef BlaeckSerial::addStateChannel(const char *, bool *) { return BlaeckBoolStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, byte *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, short *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, unsigned short *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, int *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, unsigned int *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, long *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, unsigned long *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, float *) { return BlaeckNumericStateRef(this, -1); }
BlaeckNumericStateRef BlaeckSerial::addStateChannel(const char *, double *) { return BlaeckNumericStateRef(this, -1); }
void BlaeckSerial::clearAllStateChannels() {}
void BlaeckSerial::writeStateChannels() { this->writeStateChannels(1); }
void BlaeckSerial::writeStateChannels(unsigned long msg_id) { this->_writeEmptyFrame(0x90, msg_id); }
void BlaeckSerial::writeState(const char *, const char *) {}
void BlaeckSerial::writeState(const char *) {}
void BlaeckSerial::writeState(const char *, unsigned long) {}
void BlaeckSerial::writeState(const char *, const char *, unsigned long) {}
#endif

#if BLAECK_ENABLE_EVENTS
int BlaeckSerial::_registerEventChannel(const char *channelName, const __FlashStringHelper *eventTypes)
{
  if (channelName == nullptr || channelName[0] == '\0')
  {
    _rejectedEventChannelCount++;
    return -1;
  }

  // A channel with no types can neither emit - writeEvent() resolves against this list - nor be
  // announced, since a host has nothing to declare the entity with. Refused rather than stored.
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

  if (strlen(channelName) >= MAX_EVENT_NAME_COUNT)
  {
    if (_debugStream != nullptr)
    {
      _debugStream->print(F("Channel name too long for event channel table: "));
      _debugStream->println(channelName);
    }
    _rejectedEventChannelCount++;
    return -1;
  }

  // Re-declaring a channel updates it rather than consuming a slot, so the metadata starts
  // empty. Its already-declared event types keep their indices: those are wire positions other
  // frames refer to, and clearing them would renumber events a host has already been told about.
  int existing = _findEventChannel(channelName);
  if (existing >= 0)
  {
    _eventChannels[existing].icon = nullptr;
    _eventChannels[existing].deviceClass = nullptr;
    _eventChannels[existing].diagnostic = false;
    _eventChannels[existing].disabledByDefault = false;
    return existing;
  }

  if (!_ensureEventChannelTable())
  {
    _rejectedEventChannelCount++;
    return -1;
  }

  for (byte i = 0; i < _eventChannelSlots(); i++)
  {
    if (!_eventChannels[i].inUse)
    {
      strncpy(_eventChannels[i].name, channelName, MAX_EVENT_NAME_COUNT - 1);
      _eventChannels[i].name[MAX_EVENT_NAME_COUNT - 1] = '\0';
      _eventChannels[i].icon = nullptr;
      _eventChannels[i].deviceClass = nullptr;
      _eventChannels[i].diagnostic = false;
      _eventChannels[i].disabledByDefault = false;
      _eventChannels[i].inUse = true;
      _addEventTypesCsv(i, eventTypes);
      return (int)i;
    }
  }

  _warnTableFull(F("withEventChannels"), _eventChannelCapacity, channelName);
  _rejectedEventChannelCount++;
  return -1;
}

BlaeckEventChannelRef BlaeckSerial::addEventChannel(const char *channelName, const __FlashStringHelper *eventTypes)
{
  return BlaeckEventChannelRef(this, (int16_t)_registerEventChannel(channelName, eventTypes));
}

void BlaeckSerial::_addEventTypesCsv(byte channelIndex, const __FlashStringHelper *eventTypes)
{
  // One pool entry per field, all pointing at the same flash string. Appended in
  // order, so a field's position is its wire index - the same rule call order gives
  // addEventType().
  byte fieldCount = _flashCsvOptionCount(eventTypes);
  for (byte f = 0; f < fieldCount; f++)
  {
    if (!_ensureEventTypeTable() || _eventTypeCount >= _eventTypeSlots())
    {
      _warnTableFull(F("withEventTypes"), _eventTypeCapacity,
                     _eventChannels[channelIndex].name);
      // Every remaining field is lost too, and each is a type a host will never hear about.
      _rejectedEventTypeCount += (uint16_t)(fieldCount - f);
      break;
    }
    _eventTypes[_eventTypeCount].channelIndex = channelIndex;
    _eventTypes[_eventTypeCount].text = eventTypes;
    _eventTypes[_eventTypeCount].field = f;
    _eventTypeCount++;
  }
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
    _rejectedEventTypeCount++;
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
  return true;
}

void BlaeckSerial::clearAllEventChannels()
{
  for (byte i = 0; i < _eventChannelSlots(); i++)
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

  for (byte i = 0; i < _eventChannelSlots(); i++)
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
  //   msConfig(1) slaveID(1) name\0 flags(2, LE uint16)
  //   [icon\0]                 if flags.hasIcon
  //   [deviceClass\0]          if flags.hasDeviceClass
  //   count(1) type\0 x count
  // flags bits: 0=hasIcon 1=isDiagnostic 2=hasDeviceClass 3=disabledByDefault.
  //             Bits 4-15 reserved - two bytes, matching 0x90, so the catalog has room to
  //             grow without taking a new message key.
  // The two leading bytes are the entry's device identity, msConfig and slaveID: zero from
  // a single-device library, rewritten by an aggregator relaying several boards. Not
  // padding - without them a catalog could name only one device.
  // Declared up-front so the host can announce one event entity per channel,
  // including its list of types, before any 0x85 event arrives. The count is
  // what lets a host reject an out-of-range index without parsing the run.
  if (StreamRef == nullptr)
    return;

  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0x80, msg_id);

    for (byte i = 0; i < _eventChannelSlots(); i++)
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

      _bufByte((byte)0);
      _bufByte((byte)0);
      _bufStr0(e.name);
      _bufByte((byte)(flags & 0xFF));
      _bufByte((byte)((flags >> 8) & 0xFF));

      if (flags & 0x0001)
        _bufFlashStr0(e.icon);
      if (flags & 0x0004)
        _bufFlashStr0(e.deviceClass);

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

    for (byte i = 0; i < _eventChannelSlots(); i++)
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

      StreamRef->write((byte)0);
      StreamRef->write((byte)0);
      StreamRef->print(e.name);
      StreamRef->write((byte)0);
      StreamRef->write((byte)(flags & 0xFF));
      StreamRef->write((byte)((flags >> 8) & 0xFF));

      if (flags & 0x0001)
      {
        StreamRef->print(e.icon);
        StreamRef->write((byte)0);
      }
      if (flags & 0x0004)
      {
        StreamRef->print(e.deviceClass);
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

  if (_bufReady())
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
BlaeckEventChannelRef BlaeckSerial::addEventChannel(const char *, const __FlashStringHelper *) { return BlaeckEventChannelRef(this, -1); }
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

  if (_bufReady())
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

void BlaeckSerial::write(const char *signalName, bool value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, byte value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, short value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, unsigned short value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, int value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, unsigned int value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, long value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, unsigned long value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, float value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, double value)
{
  this->write(signalName, value, 1);
}

void BlaeckSerial::write(const char *signalName, bool value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, byte value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, short value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, unsigned short value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, int value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, unsigned int value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, long value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, unsigned long value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, float value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, double value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}

void BlaeckSerial::write(const char *signalName, bool value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, byte value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, short value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, unsigned short value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, int value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, unsigned int value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, long value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, unsigned long value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, float value, unsigned long messageID, unsigned long long timestamp)
{
  int index = findSignalIndex(signalName);
  if (index >= 0)
  {
    this->write(index, value, messageID, timestamp);
  }
}
void BlaeckSerial::write(const char *signalName, double value, unsigned long messageID, unsigned long long timestamp)
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

void BlaeckSerial::write(const char *signalName, const char *value)
{
  this->write(signalName, value, 1);
}
void BlaeckSerial::write(const char *signalName, const char *value, unsigned long messageID)
{
  this->write(signalName, value, messageID, getTimeStamp());
}
void BlaeckSerial::write(const char *signalName, const char *value, unsigned long messageID, unsigned long long timestamp)
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
      // A string value lives in a user-owned buffer; repoint Address like addSignal(const char*).
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
  // Sized from the signals actually added, not from the capacity the table was
  // given: the buffer is built at the first write, by which time every add has
  // run, and _bufEnsure() grows it if a frame still turns out larger.
  int signalsHeld = _signalIndex > 0 ? _signalIndex : 1;
  _frameBufSize = 60 + signalsHeld * 10;
  // B0/B3 can also be large with long names; ensure minimum
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
  // Turning it on allocates nothing: the first buffered frame builds the buffer,
  // and by then it can be sized from the signals the sketch actually added.
  if (!enabled)
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

void BlaeckSerial::_bufDevice(const char *name, const char *hw, const char *fw)
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

    if (_bufReady())
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
  if (_bufReady())
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

  if (_bufReady())
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
  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0xB0, msg_id);

    for (int i = 0; i < _signalIndex; i++)
    {
      _bufByte((byte)0);
      _bufByte((byte)0);

      // A reference, not a copy: the entry is nine bytes, and there is no reason to move
      // them once per signal per frame.
      const Signal &signal = Signals[i];

      _signalNameFeedHash(signal);
      _bufSignalName0(signal);

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

      const Signal &signal = Signals[i];

      _signalNameFeedHash(signal);
      _printSignalName(signal);
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
  //   [options\0]              if flags bit 10
  // flags bits: 0=hasUnit 1=hasDeviceClass 2=hasIcon 3-5=stateClass
  //             6=isDiagnostic 7=disabledByDefault 8=forceUpdate
  //             9=hasDisplayPrecision 10=hasOptions. Bits 11-15 reserved.
  // stateClass takes three bits because Home Assistant defines five values counting
  // none: measurement, total, total_increasing and measurement_angle.
  // Optional fields follow in bit order, which is why precision precedes options.
  // Signals that declare nothing are skipped entirely, so a frame with no
  // entries is the ordinary case and not an error. The signal is named by its
  // index in the 0xB0 Symbol List, which already says which device it belongs
  // to - so unlike the other catalogs this frame carries no device fields.
  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0xF0, msg_id);

    for (int i = 0; i < _signalIndex; i++)
    {
      const SignalMeta *m = Signals[i].Meta;
      // No record, or one that ended up saying nothing - diagnostic(false) alone builds
      // one - is the ordinary case, and the frame carries no entry for it.
      if (m == nullptr || m->MetaFlags == 0)
        continue;

      uint16_t symbolId = (uint16_t)i;
      _bufByte((byte)(symbolId & 0xFF));
      _bufByte((byte)((symbolId >> 8) & 0xFF));
      _bufByte((byte)(m->MetaFlags & 0xFF));
      _bufByte((byte)((m->MetaFlags >> 8) & 0xFF));

      if (m->MetaFlags & BLAECK_SIG_HAS_UNIT)
        _bufFlashStr0(m->Unit);
      if (m->MetaFlags & BLAECK_SIG_HAS_DEVICE_CLASS)
        _bufFlashStr0(m->DeviceClass);
      if (m->MetaFlags & BLAECK_SIG_HAS_ICON)
        _bufFlashStr0(m->Icon);
      if (m->MetaFlags & BLAECK_SIG_HAS_DISPLAY_PRECISION)
        _bufByte(m->DisplayPrecision);
      if (m->MetaFlags & BLAECK_SIG_HAS_OPTIONS)
        _bufFlashStr0(m->Options);
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
      const SignalMeta *m = Signals[i].Meta;
      if (m == nullptr || m->MetaFlags == 0)
        continue;

      uint16_t symbolId = (uint16_t)i;
      StreamRef->write((byte)(symbolId & 0xFF));
      StreamRef->write((byte)((symbolId >> 8) & 0xFF));
      StreamRef->write((byte)(m->MetaFlags & 0xFF));
      StreamRef->write((byte)((m->MetaFlags >> 8) & 0xFF));

      if (m->MetaFlags & BLAECK_SIG_HAS_UNIT)
      {
        StreamRef->print(m->Unit);
        StreamRef->print('\0');
      }
      if (m->MetaFlags & BLAECK_SIG_HAS_DEVICE_CLASS)
      {
        StreamRef->print(m->DeviceClass);
        StreamRef->print('\0');
      }
      if (m->MetaFlags & BLAECK_SIG_HAS_ICON)
      {
        StreamRef->print(m->Icon);
        StreamRef->print('\0');
      }
      if (m->MetaFlags & BLAECK_SIG_HAS_DISPLAY_PRECISION)
        StreamRef->write(m->DisplayPrecision);
      if (m->MetaFlags & BLAECK_SIG_HAS_OPTIONS)
      {
        StreamRef->print(m->Options);
        StreamRef->print('\0');
      }
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
  //   msConfig(1) slaveID(1) payloadMax(2, LE uint16) name\0 kind(1) flags(2, LE uint16)
  //   [min(4) max(4) step(4)]  if flags.hasRange   (LE float)
  //   [unit\0]                 if flags.hasUnit
  //   [optionsCsv\0]           if flags.hasOptions
  //   [stateSignal\0 src(1)]   if flags.hasStateSignal
  //   [maxLen(2)]              if flags.isText     (LE uint16)
  // flags bits: 0=hasRange 1=hasUnit 2=hasOptions 3=hasStateSignal 4=isText
  //             5-6=entity category. Bits 7-15 reserved - two bytes rather than one,
  //             so the catalog has room to grow without taking a new message key.
  // src says what stateSignal names: 0 an addSignal() signal, 1 an
  // addStateChannel() channel (BlaeckStateSource). It rides with the name rather
  // than taking a flags bit, so the last free bit (0x80) stays available.
  // The two leading bytes are the entry's device identity, msConfig and slaveID: zero from
  // a single-device library, rewritten by an aggregator relaying several boards. Not
  // padding - without them a catalog could name only one device.
  // All in-use entries are emitted, including plain onCommand() entries
  // (kind=BLAECK_CMD_PLAIN, flags=0, no trailing metadata). Plain entries carry
  // no Home Assistant entity, but are listed so a host can build a full command
  // palette / autocomplete of every command the device accepts.
  if (_bufReady())
  {
    _bufReset();
    _bufHeader(0xA0, msg_id);

    for (byte i = 0; i < _commandSlots(); i++)
    {
      CommandHandlerEntry &e = _commandHandlers[i];
      if (!e.inUse)
        continue;

      uint16_t flags = 0;
      if (e.kind == BLAECK_CMD_NUMBER)
        flags |= 0x0001;
      if (e.unit != nullptr)
        flags |= 0x0002;
      if (e.kind == BLAECK_CMD_SELECT && e.options != nullptr)
        flags |= 0x0004;
      if (e.stateSignal != nullptr)
        flags |= 0x0008;
      if (e.kind == BLAECK_CMD_TEXT)
        flags |= 0x0010;
      // Entity category in bits 5-6, so it needs no trailing payload.
      flags |= (uint16_t)((e.category & 0x03) << 5);

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
      _bufByte((byte)(flags & 0xFF));
      _bufByte((byte)((flags >> 8) & 0xFF));

      if (flags & 0x0001)
      {
        fltCvt.val = e.meta_min;
        _bufBytes(fltCvt.bval, 4);
        fltCvt.val = e.meta_max;
        _bufBytes(fltCvt.bval, 4);
        fltCvt.val = e.meta_step;
        _bufBytes(fltCvt.bval, 4);
      }
      if (flags & 0x0002)
        _bufFlashStr0(e.unit);
      if (flags & 0x0004)
        _bufFlashStr0(e.options);
      if (flags & 0x0008)
      {
        _bufFlashStr0(e.stateSignal);
        _bufByte(e.stateSource);
      }
      if (flags & 0x0010)
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

    for (byte i = 0; i < _commandSlots(); i++)
    {
      CommandHandlerEntry &e = _commandHandlers[i];
      if (!e.inUse)
        continue;

      uint16_t flags = 0;
      if (e.kind == BLAECK_CMD_NUMBER)
        flags |= 0x0001;
      if (e.unit != nullptr)
        flags |= 0x0002;
      if (e.kind == BLAECK_CMD_SELECT && e.options != nullptr)
        flags |= 0x0004;
      if (e.stateSignal != nullptr)
        flags |= 0x0008;
      if (e.kind == BLAECK_CMD_TEXT)
        flags |= 0x0010;
      // Entity category in bits 5-6, so it needs no trailing payload.
      flags |= (uint16_t)((e.category & 0x03) << 5);

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
      StreamRef->write((byte)(flags & 0xFF));
      StreamRef->write((byte)((flags >> 8) & 0xFF));

      if (flags & 0x0001)
      {
        fltCvt.val = e.meta_min;
        StreamRef->write(fltCvt.bval, 4);
        fltCvt.val = e.meta_max;
        StreamRef->write(fltCvt.bval, 4);
        fltCvt.val = e.meta_step;
        StreamRef->write(fltCvt.bval, 4);
      }
      if (flags & 0x0002)
      {
        StreamRef->print(e.unit);
        StreamRef->write((byte)0);
      }
      if (flags & 0x0004)
      {
        StreamRef->print(e.options);
        StreamRef->write((byte)0);
      }
      if (flags & 0x0008)
      {
        StreamRef->print(e.stateSignal);
        StreamRef->write((byte)0);
        StreamRef->write(e.stateSource);
      }
      if (flags & 0x0010)
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

void BlaeckSerial::markSignalUpdated(const char *signalName)
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
