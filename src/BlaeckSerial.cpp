/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include <Arduino.h>
#include "BlaeckSerial.h"

BlaeckBeginRef BlaeckSerial::begin(Stream *Ref)
{
  StreamRef = Ref;
  return _beginCore();
}

BlaeckBeginRef BlaeckSerial::begin(Stream *Ref, unsigned int size)
{
  return begin(Ref).withSignals(size);
}

void BlaeckSerial::_sendBuffered()
{
  // A USB bulk transfer only completes on a short packet. A frame that fills its last packet
  // exactly sits in the host until a later frame arrives, so on a native-USB board such as
  // the Giga, data arrives in bursts. One extra '\n' after the footer makes the last packet
  // short; hosts ignore it. flush() can't help, because Stream has no way to send the
  // zero-length packet that would also end the transfer.
  bool padded = false;
  if (_framePos > 0 && (_framePos % BLAECK_USB_PACKET_BYTES) == 0 && _bufEnsure(1))
  {
    // Not _emitByte(): a failure there would drop the whole frame.
    _frameBuf[_framePos++] = '\n';
    padded = true;
  }

  StreamRef->write(_frameBuf, _framePos);

  // The buffer couldn't grow, so send the padding as a separate write.
  if (!padded && _framePos > 0 && (_framePos % BLAECK_USB_PACKET_BYTES) == 0)
    StreamRef->write('\n');

  StreamRef->flush();
}

bool BlaeckSerial::_receiveCommand()
{
  if (StreamRef == nullptr)
    return false;

  // One command at a time: what follows it stays in the stream for the next read().
  while (StreamRef->available() > 0)
  {
    if (_receiveByte(_receiver, (char)StreamRef->read()))
      return true;
  }
  return false;
}
