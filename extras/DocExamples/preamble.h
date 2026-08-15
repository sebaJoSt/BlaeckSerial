// The cast of characters every @code example in BlaeckSerial.h may use.
//
// An example has to compile, or it is instructions that do not build - but it also
// has to read well in a hover, and a snippet that declares two globals before it can
// say anything reads badly. So the names below are shared: an example uses them
// without introducing them, the way the reference examples in the Arduino docs assume
// a pin is already set up.
//
// Add a name here when an example needs one that is genuinely new. Reach for an
// existing one first - examples that all speak of Temperature and Frequency teach the
// library faster than examples that each invent their own vocabulary.

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

// --- Handlers, so a command example has something to point at ---
inline void onSetFreq(const char *command, const char *const *params, byte paramCount) {}
inline void onSetWave(const char *command, const char *const *params, byte paramCount) {}
inline void onSetEnable(const char *command, const char *const *params, byte paramCount) {}
inline void onStatus(const char *command, const char *const *params, byte paramCount) {}
