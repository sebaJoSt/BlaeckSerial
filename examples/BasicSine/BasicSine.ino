/*
  BasicSine.ino
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Signals
float y;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  //Setup BlaeckSerial
  BlaeckSerial.begin(
    &Serial, //Serial reference
    200        //Maxmimal signal count used;
  );

  BlaeckSerial.DeviceName = "Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
//  BlaeckSerial.addSignal("Sine", &y);

  for (int i = 1; i <= 200; i++)
  {
    String signalName = "Sine_";
    BlaeckSerial.addSignal(signalName + i, &y);
  }

  /*Uncomment this function for initial settings
    first parameter: timedActivated
    second parameter: timedInterval_ms */
  // BlaeckSerial.setTimedData(true, 60000);
}

void loop()
{
  UpdateSineNumbers();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}

void UpdateSineNumbers()
{
  y = sin(millis() * 0.00005);
}