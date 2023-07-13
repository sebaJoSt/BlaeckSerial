void sinfo()
{
  Serial.print(F("INFO> "));
}
void shelp()
{
  Serial.print(F("HELP> "));
}
void swarning()
{
  Serial.print(F("WARNING> "));
}
void serror()
{
  Serial.print(F("ERROR> "));
}

void PrintInfo()
{
  PrintInfo(false);
}
void PrintInfo(bool IsStartUp)
{
  Serial.println(F("---------------------------------------------"));

  if (IsStartUp)
  {
    //--FIRMWARE INFO
    Serial.print(F("Welcome to the "));

    Serial.println(BlaeckSerial.DeviceName);
    sinfo(), Serial.print(F("Hardware Version: "));
    Serial.println(BlaeckSerial.DeviceHWVersion);
    sinfo(), Serial.print(F("Firmware Version: "));
    Serial.println(BlaeckSerial.DeviceFWVersion);
    sinfo(), Serial.print(F("BlaeckSerial Version: "));
    Serial.println(BlaeckSerial.BLAECKSERIAL_VERSION);
  }

  //--MASTER SLAVE MODE
  sinfo(), Serial.print(F("BlaeckSerial Mode: "));
  if (masterSlaveMode == 0)
    Serial.println(F("Single Mode"));
  if (masterSlaveMode == 1)
    Serial.println(F("Master Mode"));
  if (masterSlaveMode == 2)
  {
    Serial.print(F("Slave Mode ID:"));
    Serial.println(slaveID);
  }

  //--LOGGING INFO
  sinfo(), Serial.print(F("BlaeckSerial currently "));
  if (loggingActivated == false)
    Serial.print(F("NOT "));
  Serial.println(F("activated."));
  sinfo(), Serial.print(F("BlaeckSerial interval [ms]: "));
  Serial.println(loggingInterval);

  //--ACTIVATED SIGNALS INFO
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

  //--FREE RAM
  sinfo(), Serial.print(F("Free Ram: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));

  //--SIGNAL INFO -- ACTIVATED SIGNALS
  if (IsStartUp == false)
  {
    // Signal status - active signals
    for (byte i = 1; i <= MAXIMUM_SIGNALS; i++)
    {
      if (sine[i].isActivated)
      {
        sinfo(), Serial.print(F("Signal "));
        if (i < 10)
          Serial.print("0");
        Serial.print(i);
        Serial.print(F(" | "));
        Serial.println(sine[i].value);
      }
    }
  }

  if (IsStartUp)
  {
    //--COMMANDS INFO
    Serial.println();
    shelp(), Serial.println(F("Enter <LS> for list of available commands"));
    Serial.println();
  }
}
