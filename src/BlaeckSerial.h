/*
        File: BlaeckSerial.h
        Author: Sebastian Strobl

    Sends binary sensor data over a serial port, and receives commands written as
    <HelloWorld, 12, 47>. A device also describes its signals, commands, state and
    event channels, so a host can present them without being configured for it.

    The library is heavily inspired by Nick Dodd's
    AdvancedSerial Library https://github.com/Nick1787/AdvancedSerial/
    The message parser uses code from Robin2's Arduino forum thread
    "Serial Basic Input" https://forum.arduino.cc/index.php?topic=396450.0
*/

#ifndef BLAECKSERIAL_H
#define BLAECKSERIAL_H

#define BLAECKSERIAL_VERSION "7.0.0"
#define BLAECKSERIAL_VERSION_MAJOR 7
#define BLAECKSERIAL_VERSION_MINOR 0
#define BLAECKSERIAL_VERSION_PATCH 0
#define BLAECKSERIAL_NAME "BlaeckSerial"

#include "BlaeckCore.h"

// USB bulk packet size
// --------------------
// A buffered frame whose length is a multiple of this gets one byte of padding, so it
// never ends on a full USB packet (see _sendBuffered()). 64 covers both full and high speed.
// Lower it if a core uses a smaller endpoint.
#ifndef BLAECK_USB_PACKET_BYTES
  #define BLAECK_USB_PACKET_BYTES 64
#endif

// The core's names, at global scope where sketches use them.
using namespace BLAECK_CORE_NAMESPACE;

// Blaeck over a Serial port, or any other Stream.
class BlaeckSerial : public BlaeckCore
{
public:
  /*!
    @brief   Starts the library on a stream.

    Call it first, after opening the stream. A device then reports three kinds of
    thing: signals, values sampled and logged over time; state channels, current
    values that are shown but not logged; and event channels, for things that
    happen. The WaveformGenerator example uses all three.

    @param   Ref  The stream to talk over, such as Serial or Serial1.
    @return  A handle for setting table sizes and a debug stream. Each table has a
             default that suits the board, so the handle can be ignored.

    @code
      Serial.begin(115200);
      Blaeck.begin(&Serial)
          .withSignals(50)
          .withStateChannels(12)
          .withDebugStream(&Serial1);
    @endcode
  */
  BlaeckBeginRef begin(Stream *Ref);

  /*!
    @brief   Starts the library and sets the signal table size.

    The same as begin(Ref).withSignals(Size).

    @param   Ref   The stream to talk over.
    @param   Size  How many signals fit.
    @return  The same handle as begin(Stream *).

    @code
      Blaeck.begin(&Serial, 8);
    @endcode
  */
  BlaeckBeginRef begin(Stream *Ref, unsigned int Size);

protected:
  bool _transportReady() const override { return StreamRef != nullptr; }
  void _writeDirect(const byte *data, size_t len) override { StreamRef->write(data, len); }
  void _flushDirect() override { StreamRef->flush(); }
  void _sendBuffered() override;
  bool _receiveCommand() override;
  const char *_libraryName() const override { return BLAECKSERIAL_NAME; }
  const char *_libraryVersion() const override { return BLAECKSERIAL_VERSION; }

private:
  Stream *StreamRef = nullptr;
};

#endif //  BLAECKSERIAL_H
