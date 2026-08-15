// Globals the @code blocks in BlaeckSerial.h are written against.
// Only one file includes this, and it does not exist until it is generated: DocCodeBlocks.ino
//
// Before adding a name, read extras/API-STYLE.md - it explains why these are shared
// and when a new one is warranted.

#pragma once

#include "Arduino.h"
#include <EEPROM.h>
#include "BlaeckSerial.h"

// The instance, named as every example sketch in this library names it.
BlaeckSerial Blaeck;

// --- Values a sketch might publish as signals ---
float Temperature = 0.0f;
float Frequency = 1.0f;
float Output = 0.0f;
unsigned long Uptime = 0;

// --- Values a control might carry as its own state ---
float Amplitude = 1.0f;
float Offset = 0.0f;
bool Enabled = true;
byte waveIndex = 0;
char DeviceLabel[33] = "wave-gen";

// --- For the write() example, which shows a signal that pulses ---
bool Pulse = false;
bool triggered = false;
unsigned long pulseSince = 0;

// --- Where a restored setting is kept, for the getSelectOptionIndexOf() example ---
const int addr = 0;

// Stands in for whatever a sketch actually reads, so an example can show a fresh
// value arriving without dragging a sensor library in with it.
inline float readSensor() { return 21.5f; }

// Reports a value as text, for the withOwnState() getter form.
inline const char *offsetText()
{
  static char buf[12];
  Blaeck.toText(Offset, 1, buf, sizeof(buf));
  return buf;
}

// --- A signal a switch command can mirror ---
bool ledState = false;

// Declared, not defined: a @code block on writeCommandState() shows this handler in
// full, and the extractor emits it at file scope. Blocks are emitted in header order,
// so one using the name earlier needs it declared here first.
void onSetOffset(const char *command, const char *const *params, byte paramCount);

// --- Handlers, so a command example has something to point at ---
inline void onSetFreq(const char *command, const char *const *params, byte paramCount) {}
inline void onSetWave(const char *command, const char *const *params, byte paramCount) {}
inline void onSetEnable(const char *command, const char *const *params, byte paramCount) {}
inline void onStatus(const char *command, const char *const *params, byte paramCount) {}
inline void onLED(const char *command, const char *const *params, byte paramCount) {}
inline void onReboot(const char *command, const char *const *params, byte paramCount) {}
inline void onSetLabel(const char *command, const char *const *params, byte paramCount) {}
