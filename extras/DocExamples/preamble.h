// Globals the @code blocks in BlaeckSerial.h are written against - the snippets an
// editor shows on hover, not the sketches under examples/.
//
// Only one file includes this, and it does not exist until it is generated:
// extras/scripts/checkdocs.py --extract writes DocExamples.ino beside it, and CI
// compiles the result. Nothing in src/ or examples/ includes this, and no sketch of
// yours should. DocExamples.ino is generated and gitignored - do not edit or commit it.
//
// This folder cannot be renamed: arduino-cli ties a sketch folder to its .ino, and
// the #include resolves against that sketch.
//
// Before adding a name, read extras/API-STYLE.md. It explains why these are shared
// and when a new one is warranted; repeating it here would be a second copy to drift.

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
