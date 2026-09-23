/*
        File: BlaeckCoreLibrary.h

    The one core file that differs between Blaeck libraries: it names this library's
    namespace and settings. BlaeckCore.h and BlaeckCore.cpp are identical copies.
*/

#ifndef BLAECK_CORE_LIBRARY_H
#define BLAECK_CORE_LIBRARY_H

#define BLAECK_CORE_NAMESPACE blaeck_serial

// A sketch's own settings. See docs/configuration.md.
#if defined __has_include
  #if __has_include(<BlaeckSerialConfig.h>)
    #include <BlaeckSerialConfig.h>
  #endif
#endif

// Buffered writes
// ---------------
// On: each frame is built in RAM and sent with one write. The buffer is sized from the
// signals added and grows if a frame needs more.
// Off: bytes go to the stream as the frame is built, and no buffer is allocated.
//
// Off on AVR, where SRAM is scarce and the USB-serial bridges cope with small writes. On
// everywhere else, because the Uno R4 WiFi's USB bridge drops bytes when fed many small
// writes. setBufferedWrites() changes it at runtime.
#ifndef BLAECK_BUFFERED_WRITES_DEFAULT
  #if defined(__AVR__)
    #define BLAECK_BUFFERED_WRITES_DEFAULT false
  #else
    #define BLAECK_BUFFERED_WRITES_DEFAULT true
  #endif
#endif

#endif // BLAECK_CORE_LIBRARY_H
