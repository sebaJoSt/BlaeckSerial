// Command syntax (onCommand API):
//   <COMMAND,PARAM01,PARAM02,...,PARAM10>
//   <- AVR: up to 48 chars, non-AVR: up to 96 chars ->
//   Parameters are string tokens; convert with atoi/atol/atof as needed.
//   Empty parameters are preserved positionally and default to empty string / 0.
//   To check if a parameter was provided: params[i][0] == '\0' means empty.

void onSignalActivate(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  int firstsignalno = (paramCount >= 1 && params[0][0] != '\0') ? atoi(params[0]) : 0;
  int secondsignalno = (paramCount >= 2 && params[1][0] != '\0') ? atoi(params[1]) : 0;

  if (firstsignalno == 0 && secondsignalno == 0)
  {
    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
      sine[i].isActivated = true;
  }
  if (firstsignalno == 900)
  {
    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
      sine[i].isActivated = false;
  }
  if (firstsignalno == 901)
  {
    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
    {
      sine[i].isActivated = false;
      if (i % 2 == 1)
        sine[i].isActivated = true;
    }
  }
  if (firstsignalno == 902)
  {
    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
    {
      sine[i].isActivated = false;
      if (i % 2 == 0)
        sine[i].isActivated = true;
    }
  }
  if (firstsignalno >= 1 && firstsignalno <= MAXIMUM_SIGNALS && secondsignalno >= 1 && secondsignalno <= MAXIMUM_SIGNALS)
  {
    byte minimum = firstsignalno;
    byte maximum = secondsignalno;
    if (firstsignalno > secondsignalno)
    {
      maximum = firstsignalno;
      minimum = secondsignalno;
    }

    for (byte i = minimum; i <= maximum; i++)
    {
      sine[i].isActivated = true;
    }
  }
  if (firstsignalno >= 1 && firstsignalno <= MAXIMUM_SIGNALS && secondsignalno == 0)
  {
    sine[firstsignalno].isActivated = true;
  }

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

  bool isActivated[MAXIMUM_SIGNALS + 1];
  for (byte i = 0; i <= MAXIMUM_SIGNALS; i++)
  {
    isActivated[i] = sine[i].isActivated;
  }
  EEPROM.updateBlock<bool>(eepromaddress.signalActivated, isActivated, MAXIMUM_SIGNALS + 1);
  UpdateLoggingSignals();
}

void onMasterSlaveMode(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  int parameter0 = (paramCount >= 1) ? atoi(params[0]) : 0;
  int parameter1 = (paramCount >= 2) ? atoi(params[1]) : 0;

  if (parameter0 < 0 || parameter0 > 2)
  {
    serror(), Serial.println(F("Only 0 (Single), 1 (Master) or 2 (SLAVE) allowed!"));
  }
  else
  {

    if (parameter0 == 0 || parameter0 == 1)
    {
      masterSlaveMode = parameter0;
      EEPROM.update(eepromaddress.masterSlaveMode, masterSlaveMode);
      Serial.print(F("Changed to new mode: "));
      if (masterSlaveMode == 0)
      {
        Serial.println(F(" Single Mode"));
      }
      if (masterSlaveMode == 1)
      {
        Serial.println(F(" Master Mode"));
      }
    }

    if (parameter0 == 2)
    {
      if (parameter1 >= 1 && parameter1 <= 127)
      {
        masterSlaveMode = parameter0;
        slaveID = parameter1;
        EEPROM.updateByte(eepromaddress.masterSlaveMode, masterSlaveMode);
        EEPROM.updateByte(eepromaddress.slaveID, slaveID);
        Serial.print(F("Changed to new mode: "));
        Serial.print(F(" Slave Mode (ID: "));
        Serial.print(slaveID);
        Serial.println(F(")"));
      }
      else
      {
        serror(), Serial.println(F("slaveID must be between 1 and 127"));
      }
    }

    Serial.println(F("RESTART MICROCONTROLLER FOR CHANGES TO TAKE EFFECT"));
  }
}

void onStatus(const char *command, const char *const *params, byte paramCount)
{
  (void)command;
  (void)params;
  (void)paramCount;
  PrintInfo(false);
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
    sinfo(), Serial.println(F("<LS> <STATUS> <SIGNAL_ACTIVATE> <MASTER_SLAVE_MODE>"));
    sinfo(), Serial.println(F("<BLAECK.ACTIVATE> <BLAECK.DEACTIVATE> <BLAECK.WRITE_SYMBOLS> <BLAECK.WRITE_DATA> <BLAECK.GET_DEVICES>"));
    sinfo(), Serial.println(F("Enter <command?> for instructions, e.g. <STATUS?>"));
  }
  else if (strcmp(command, "SIGNAL_ACTIVATE?") == 0)
  {
    shelp(), Serial.println(F("<SIGNAL_ACTIVATE, first signal, last signal>"));
    shelp(), Serial.println(F("Use this command to activate the used signals"));
    shelp(), Serial.print(F("first signal: 1-"));
    Serial.print(MAXIMUM_SIGNALS);
    Serial.println(F(", 900, 901, 902"));
    shelp(), Serial.print(F("last signal: 1-"));
    Serial.println(MAXIMUM_SIGNALS);
    shelp(), Serial.println(F("e.g. <SIGNAL_ACTIVATE, 1, 10> activates the first 10 signals"));
    shelp(), Serial.println(F("e.g. <SIGNAL_ACTIVATE> activates all signals"));
    shelp(), Serial.println(F("e.g. <SIGNAL_ACTIVATE, 900> deactivates all signals"));
    shelp(), Serial.println(F("e.g. <SIGNAL_ACTIVATE, 901> activates all odd numbered signals"));
    shelp(), Serial.println(F("e.g. <SIGNAL_ACTIVATE, 902> activates all even numbered signals"));
  }
  else if (strcmp(command, "MASTER_SLAVE_MODE?") == 0)
  {
    shelp(), Serial.println(F("<MASTER_SLAVE_MODE, mode, slaveID>"));
    shelp(), Serial.println(F("mode:"));
    shelp(), Serial.println(F("0 (Single Mode)"));
    shelp(), Serial.println(F("1 (Master Mode"));
    shelp(), Serial.println(F("2 (Slave Mode)"));
    shelp(), Serial.println(F("slaveID:"));
    shelp(), Serial.println(F("Only updated when mode is set to Slave Mode)"));
    shelp(), Serial.println(F("1 ... 127"));
    shelp(), Serial.println(F("e.g. <MASTER_SLAVE_MODE, 2, 1> changes to Slave Mode with Slave ID: 1"));
    shelp(), Serial.println(F("RESTART MICROCONTROLLER FOR CHANGES TO TAKE EFFECT"));
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
  else if (strcmp(command, "BLAECK.WRITE_DATA?") == 0)
  {
    shelp(), Serial.println(F("Writes the logging data in Blaeck format"));
  }
}
