// Command syntax (onCommand API):
//   <COMMAND,PARAM01,PARAM02,...,PARAM10>
//   <- AVR: up to 48 chars, non-AVR: up to 96 chars ->
//   Parameters are string tokens; convert with atoi/atol/atof as needed.
//   Empty parameters are preserved positionally and default to empty string / 0.
//   To check if a parameter was provided: params[i][0] == '\0' means empty.

// The library has already checked that the value is within [1, MAXIMUM_SIGNALS]
// before these run, so an out-of-range bound never reaches the sketch.
void onSetSignalFirst(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalFirst = (byte)atoi(params[0]);
    BlaeckSerial.write("Signal_First", signalFirst);
  }
}

void onSetSignalLast(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalLast = (byte)atoi(params[0]);
    BlaeckSerial.write("Signal_Last", signalLast);
  }
}

void onSignalActivateRange(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  ApplySignalRange(true);
}

void onSignalDeactivateRange(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  ApplySignalRange(false);
}

// Applies the current bounds. Only the signals inside the range change, so
// activating 1-10 and then 15-20 leaves both ranges on - use the deactivate
// button to clear what you no longer want.
void ApplySignalRange(bool activate)
{
  byte lo = signalFirst;
  byte hi = signalLast;
  if (lo > hi)
  {
    byte tmp = lo;
    lo = hi;
    hi = tmp;
  }
  if (lo < 1)
    lo = 1;
  if (hi > MAXIMUM_SIGNALS)
    hi = MAXIMUM_SIGNALS;

  for (byte i = lo; i <= hi; i++)
  {
    sine[i].isActivated = activate;
  }

  sinfo(), Serial.print(activate ? F("Activated") : F("Deactivated"));
  Serial.print(F(" signals "));
  Serial.print(lo);
  Serial.print(F(" - "));
  Serial.println(hi);

  sinfo(), Serial.print(F("Activated signals ("));
  byte active_count = 0;
  for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
    {
      active_count += 1;
    }
  }
  Serial.print(active_count);
  Serial.print(F("): "));

  active_count = 0;
  for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
  {

    if (sine[i].isActivated)
    {
      if (active_count == 0)
        Serial.print(i);
      if (active_count > 0)
      {
        Serial.print(F(", "));
        Serial.print(i);
      }
      active_count += 1;
    }
  }
  if (active_count == 0)
    Serial.print(F("none"));
  Serial.println();

  PersistActivatedSignals();
  UpdateLoggingSignals();
}

void PersistActivatedSignals()
{
  bool isActivated[MAXIMUM_SIGNALS + 1];
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    isActivated[i] = sine[i].isActivated;
  }
  EEPROM.updateBlock<bool>(eepromaddress.signalActivated, isActivated, MAXIMUM_SIGNALS + 1);
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;

  // The terminal gets the full multi-line report, as before.
  PrintInfo(false);

  // A Blaeck host skips anything that is not a frame, so it would see none of
  // that. Push a one-line summary on a message channel as well, written only
  // on a button press.
  byte active = 0;
  for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
      active++;
  }
  char text[48];
  snprintf(text, sizeof(text), "%u of %u signals active", (unsigned)active, (unsigned)MAXIMUM_SIGNALS);
  BlaeckSerial.writeMessage("Status", text);
}

// Catch-all handler for help commands (?) and LS
void onHelpOrList(const char *command, const char *const *params, byte paramCount)
{
  (void)params;
  (void)paramCount;

  if (strcmp(command, "LS?") == 0)
  {
    shelp(), Serial.println(F("Lists all available commands"));
  }
  else if (strcmp(command, "LS") == 0)
  {
    sinfo(), Serial.println(F("<LS> <STATUS> <SIGNAL_FIRST> <SIGNAL_LAST> <SIGNAL_ACTIVATE_RANGE> <SIGNAL_DEACTIVATE_RANGE>"));
    sinfo(), Serial.println(F("<BLAECK.ACTIVATE> <BLAECK.DEACTIVATE> <BLAECK.WRITE_SYMBOLS> <BLAECK.WRITE_COMMANDS> <BLAECK.WRITE_DATA> <BLAECK.GET_DEVICES>"));
    sinfo(), Serial.println(F("Enter <command?> for instructions, e.g. <STATUS?>"));
  }
  else if (strcmp(command, "SIGNAL_FIRST?") == 0)
  {
    shelp(), Serial.print(F("<SIGNAL_FIRST, 1-"));
    Serial.print(MAXIMUM_SIGNALS);
    Serial.println(F(">"));
    shelp(), Serial.println(F("Sets the first signal of the range"));
  }
  else if (strcmp(command, "SIGNAL_LAST?") == 0)
  {
    shelp(), Serial.print(F("<SIGNAL_LAST, 1-"));
    Serial.print(MAXIMUM_SIGNALS);
    Serial.println(F(">"));
    shelp(), Serial.println(F("Sets the last signal of the range"));
  }
  else if (strcmp(command, "SIGNAL_ACTIVATE_RANGE?") == 0)
  {
    shelp(), Serial.println(F("Activates every signal in the current range"));
    shelp(), Serial.println(F("e.g. <SIGNAL_FIRST,1> <SIGNAL_LAST,10> <SIGNAL_ACTIVATE_RANGE>"));
    shelp(), Serial.println(F("Signals outside the range keep their state"));
  }
  else if (strcmp(command, "SIGNAL_DEACTIVATE_RANGE?") == 0)
  {
    shelp(), Serial.println(F("Deactivates every signal in the current range"));
    shelp(), Serial.println(F("e.g. <SIGNAL_FIRST,1> <SIGNAL_LAST,25> <SIGNAL_DEACTIVATE_RANGE> clears all"));
  }
  else if (strcmp(command, "STATUS?") == 0)
  {
    shelp(), Serial.println(F("Requests the current state"));
  }
  else if (strcmp(command, "BLAECK.ACTIVATE?") == 0)
  {
    shelp(), Serial.println(F("<BLAECK.ACTIVATE,first,second,third,fourth byte>"));
    shelp(), Serial.println(F("Activates Logging in Blaeck format"));
    shelp(), Serial.println(F("e.g. <BLAECK.ACTIVATE,96,234> logs every 60000 ms"));
    shelp(), Serial.println(F("Interval: 0 to 4294967295 ms (little-endian 4-byte value)"));
  }
  else if (strcmp(command, "BLAECK.DEACTIVATE?") == 0)
  {
    shelp(), Serial.println(F("Deactivates Logging in Blaeck format"));
  }
  else if (strcmp(command, "BLAECK.WRITE_SYMBOLS?") == 0)
  {
    shelp(), Serial.println(F("Writes the symbols in Blaeck format"));
  }
  else if (strcmp(command, "BLAECK.WRITE_COMMANDS?") == 0)
  {
    shelp(), Serial.println(F("Writes the command list in Blaeck format"));
  }
  else if (strcmp(command, "BLAECK.WRITE_DATA?") == 0)
  {
    shelp(), Serial.println(F("Writes the logging data in Blaeck format"));
  }
}
