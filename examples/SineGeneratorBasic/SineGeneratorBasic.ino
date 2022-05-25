/*
  SineGeneratorBasic.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit 200 identical sine waves
  from the Arduino board to your PC.
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Signals
float sine;

void setup()
{
  // Initialize Serial port
  Serial.begin(115200);

  //Setup BlaeckSerial
  BlaeckSerial.begin(
    &Serial, //Serial reference
    200        //Maxmimal signal count used;
  );

  BlaeckSerial.DeviceName = "Basic Sine Number Generator";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  // Add signals to BlaeckSerial
  //  BlaeckSerial.addSignal("Sine", &sine);

  for (int i = 1; i <= 200; i++)
  {
    String signalName = "Sine_";
    BlaeckSerial.addSignal(signalName + i, &sine);
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
  sine = sin(millis() * 0.00005);
}
