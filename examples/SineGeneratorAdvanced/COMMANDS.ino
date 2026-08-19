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
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalFirst = (byte)atoi(params[0]);
    EEPROM.updateByte(eepromaddress.signalFirst, signalFirst);
    // Pushes the bound's own state channel, so the control shows the new value at once -
    // the channel is otherwise only read when a host polls the catalog.
    Blaeck.writeCommandState(command);
  }
}

void onSetSignalLast(const char *command, const char *const *params, byte paramCount)
{
  if (paramCount >= 1 && params[0][0] != '\0')
  {
    signalLast = (byte)atoi(params[0]);
    EEPROM.updateByte(eepromaddress.signalLast, signalLast);
    Blaeck.writeCommandState(command);
  }
}

void onSignalActivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  // A button's parameters are optional: SIGNAL_ACTIVATE sends none and applies the
  // bounds SIGNAL_FIRST and SIGNAL_LAST last set, while SIGNAL_ACTIVATE_ALL declares a press
  // payload and the two numbers arrive here instead. Nothing has validated them - a button is
  // the one kind the library passes through unchecked - so they go to the same clamping the
  // stored bounds get.
  if (paramCount >= 2)
    ApplySignalRange(true, (byte)atoi(params[0]), (byte)atoi(params[1]));
  else
    ApplySignalRange(true, signalFirst, signalLast);
}

void onSignalDeactivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  if (paramCount >= 2)
    ApplySignalRange(false, (byte)atoi(params[0]), (byte)atoi(params[1]));
  else
    ApplySignalRange(false, signalFirst, signalLast);
}

// Applies the bounds it is given. Only the signals inside the range change, so
// activating 1-10 and then 15-20 leaves both ranges on - use the deactivate
// button to clear what you no longer want.
void ApplySignalRange(bool activate, byte lo, byte hi)
{
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
  // that. Push a one-line summary on a state channel as well, written only
  // on a button press.
  byte active = 0;
  for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
  {
    if (sine[i].isActivated)
      active++;
  }
  char text[48];
  snprintf(text, sizeof(text), "%u of %u signals active", (unsigned)active, (unsigned)MAXIMUM_SIGNALS);
  Blaeck.writeState(F("Status"), text);
}

// ---------------------------------------------------------------------------
// Help, one handler per topic.
//
// These were a strcmp chain inside onAnyCommand(). Registering each name instead
// lets the library do the matching it already does for every other command, so a
// help topic is declared the same way the command it describes is - and adding a
// command without its help now leaves an obvious hole rather than a silent one.
//
// They register with onCommand(), which makes them BLAECK_CMD_PLAIN: listed in the
// command catalog so a host can offer them, but carrying no control, because a
// paragraph of text is not something a dashboard can render.
//
// Nothing installs onAnyCommand() any more. With no catch-all, a command that
// matches nothing is answered as unknown instead of being acknowledged as accepted
// on the grounds that the catch-all saw it.
// ---------------------------------------------------------------------------

void onList(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  sinfo(), Serial.println(F("<LS> <STATUS> <SIGNAL_FIRST> <SIGNAL_LAST> <SIGNAL_ACTIVATE> <SIGNAL_DEACTIVATE> <SIGNAL_ACTIVATE_ALL>"));
  sinfo(), Serial.println(F("Enter <command?> for instructions, e.g. <STATUS?>"));
  // The BLAECK.* commands come from the library, not this sketch, so it lists them without
  // describing them - a copy of their documentation here would go stale on its own.
  sinfo(), Serial.println(F("From BlaeckSerial itself: <BLAECK.ACTIVATE> <BLAECK.DEACTIVATE> <BLAECK.WRITE_SYMBOLS> <BLAECK.WRITE_COMMANDS> <BLAECK.WRITE_DATA> <BLAECK.GET_DEVICES>"));
}

void onHelpList(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.println(F("Lists all available commands"));
}

void onHelpSignalFirst(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.print(F("<SIGNAL_FIRST, 1-"));
  Serial.print(MAXIMUM_SIGNALS);
  Serial.println(F(">"));
  shelp(), Serial.println(F("Sets the first signal of the range"));
}

void onHelpSignalLast(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.print(F("<SIGNAL_LAST, 1-"));
  Serial.print(MAXIMUM_SIGNALS);
  Serial.println(F(">"));
  shelp(), Serial.println(F("Sets the last signal of the range"));
}

void onHelpSignalActivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.println(F("Activates every signal in the current range"));
  shelp(), Serial.println(F("e.g. <SIGNAL_FIRST,1> <SIGNAL_LAST,10> <SIGNAL_ACTIVATE>"));
  shelp(), Serial.println(F("Or give the bounds directly: <SIGNAL_ACTIVATE,1,10>"));
  shelp(), Serial.println(F("Signals outside the range keep their state"));
}

void onHelpSignalDeactivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.println(F("Deactivates every signal in the current range"));
  shelp(), Serial.println(F("e.g. <SIGNAL_FIRST,1> <SIGNAL_LAST,25> <SIGNAL_DEACTIVATE> clears all"));
  shelp(), Serial.println(F("Or give the bounds directly: <SIGNAL_DEACTIVATE,1,25>"));
}

void onHelpSignalActivateAll(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.print(F("Activates every signal, 1-"));
  Serial.print(MAXIMUM_SIGNALS);
  Serial.println(F(", whatever the current range is"));
  shelp(), Serial.println(F("Same as <SIGNAL_ACTIVATE> with the bounds given: the two are one handler"));
}

void onHelpStatus(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  shelp(), Serial.println(F("Requests the current state"));
}





